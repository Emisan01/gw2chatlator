// kb_chat.cpp — incoming chat, tabs, Events: Chat, chat codes and links.

#include "kb_shared.hpp"

namespace kb {

std::wstring LinkHost(const std::wstring& url) {  // "https://www.example.org/a?b" -> "www.example.org"
    size_t a = url.find(L"://");
    a = a == std::wstring::npos ? 0 : a + 3;
    const size_t b = url.find_first_of(L"/?#:", a);
    return url.substr(a, b == std::wstring::npos ? std::wstring::npos : b - a);
}

// The brake: at most 8 automatic translations per 3 s – a flood must not use up a contingent (only for a translator
// with one: LimitedContingent; your own LibreTranslate server is not braked). The cache saves before it has to.
bool AllowAutoTranslate() {
    static std::mutex s_rateMu;
    static uint64_t s_windowStart = 0;
    static int s_countInWindow = 0;
    const uint64_t now = GetTickCount64();
    std::lock_guard<std::mutex> lock(s_rateMu);
    if (now - s_windowStart > 3000) {
        s_windowStart = now;
        s_countInWindow = 0;
    }
    if (s_countInWindow < 8) {
        s_countInWindow++;
        return true;
    }
    return false;
}

// Trims oldest lines when exceeding total buffer quota (kMaxLines).
// Intelligent separation: Public channels (Map, Say, System) are pruned first so that
// private Whispers and Group channels (Guild, Party, Squad) are protected against flood.
static void TrimChatLines() {  // g_mu held
    while (g_lines.size() > kMaxLines) {
        // Priority 1: Drop oldest public flood line (Map, Say, System, etc.)
        auto it = std::find_if(g_lines.begin(), g_lines.end(), [](const ChatLine& x) {
            return GroupOfChannel(x.channel) == ChannelGroup::Public;
        });
        if (it != g_lines.end()) {
            g_lines.erase(it);
            continue;
        }
        // Priority 2: Drop oldest group line (Guild, Party, Squad)
        it = std::find_if(g_lines.begin(), g_lines.end(), [](const ChatLine& x) {
            return GroupOfChannel(x.channel) == ChannelGroup::Group;
        });
        if (it != g_lines.end()) {
            g_lines.erase(it);
            continue;
        }
        // Priority 3: All lines are Whispers: drop oldest line
        g_lines.pop_front();
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Game thread: a line arrives – copy it, queue its translation.
void AddLine(Channel ch, bool outgoing, const char* speaker, const char* text) {
    if (!text || !*text) return;
    ChatLine l;
    l.channel = ch;
    l.outgoing = outgoing;
    l.speaker = speaker ? SanitizeChatText(W(speaker)) : std::wstring();
    if (l.speaker.size() > 64) l.speaker.resize(64);
    l.text = SanitizeChatText(UnescapeChatText(W(text)));  // Events: Chat escapes brackets: "\[&...\]"
    if (l.text.empty()) return;
    if (l.text.size() > 600) l.text.resize(600);
    l.speakerFolded = CaseFold(Trim(l.speaker));
    l.speakerDisplay = l.speaker.empty() ? std::string() : UI((l.outgoing ? L"> " : L"") + l.speaker + L":");
    l.u8text = U8(l.text);
    l.textSegs = PieUI::ChatLinks::SegmentLine(l.u8text);
    const std::wstring rawText = l.text;
    uint64_t id;
    bool own, brake;
    bool shouldQueue = false;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        own = outgoing || (!g_ownName.empty() && l.speakerFolded == CaseFold(g_ownName));
        brake = LimitedContingent();
        l.own = own;
        l.receivedTime = GetTickCount64();
        l.mapId = g_mapId.load();
        l.id = id = g_nextId++;
        if (!own) {  // completions offer the names and words of the chat (never learned – invariant 10)
            if (!l.speaker.empty()) g_context.insert(g_context.begin(), l.speaker);
            for (const Span& s : WordSpans(l.text))
                if (s.length >= 4) g_context.insert(g_context.begin(), l.text.substr(s.start, s.length));
            if (g_context.size() > 80) g_context.resize(80);
        }

        // Smart-Sieve-Manager: spam filter, repetition cache, and occlusion culling
        if (!own) {
            if (IsGamerInterjection(rawText)) {
                // Gamer interjection / shorthand: skip translation
            } else {
                std::wstring cachedTrans, cachedBy;
                if (g_orchestrator.SieveMessage(rawText, TypingLang(), true, true, true, false, cachedTrans, cachedBy,
                                                 g_translator ? g_translator->Name() : std::wstring()) == ChatPriority::Immediate) {
                    l.translated = cachedTrans;
                    l.transTarget = TypingLang();
                    l.transBy = cachedBy;
                    l.u8translated = U8(l.translated);
                    l.u8transNote.clear();
                    l.transSegs = PieUI::ChatLinks::SegmentLine(l.u8translated);
                } else {
                    const bool windowVisible = g_show && (g_inMap || g_test.forceMap) && !g_mapOpen && !g_collapsed;
                    if (g_nx.translateChat && windowVisible) l.transPending = true;
                    if (!brake || AllowAutoTranslate()) shouldQueue = true;
                }
            }
        }

        g_lines.push_back(std::move(l));
        TrimChatLines();
        for (size_t i = 0; i < g_tabs.size(); ++i) {  // a tab not in front that shows this line: marked
            if (static_cast<int>(i) == g_activeTab) continue;
            const WhisperTab& t = g_tabs[i];
            if (t.custom ? (t.channels & ChannelBit(ch)) != 0
                         : ch == Channel::Whisper && t.nameFolded == g_lines.back().speakerFolded)
                g_tabs[i].unread = true;
        }
        g_scrollDown = true;
    }
    if (shouldQueue) g_worker.Post([id, t = rawText] { TranslateLine(id, t); });
}

// OnChatEvents: kb_chatsource.cpp (the guard between Events: Chat and us).

// A click on a name: that player's whisper tab (opened, or brought to the front); writing goes to them. g_mu held.
void OpenWhisperTab(const std::wstring& name) {
    const std::wstring who = Trim(name);
    if (who.empty()) return;
    const std::wstring whoFolded = CaseFold(who);
    int at = -1;
    for (size_t i = 0; i < g_tabs.size(); ++i)
        if (!g_tabs[i].custom && g_tabs[i].nameFolded == whoFolded) at = static_cast<int>(i);
    if (at < 0) {
        const size_t whispers = std::count_if(g_tabs.begin(), g_tabs.end(), [](const WhisperTab& t) { return !t.custom; });
        if (whispers >= kMaxWhisperTabs) {  // the oldest whisper tab goes
            const auto oldest = std::find_if(g_tabs.begin(), g_tabs.end(), [](const WhisperTab& t) { return !t.custom; });
            const int gone = static_cast<int>(oldest - g_tabs.begin());
            g_tabs.erase(oldest);
            if (g_activeTab > gone) --g_activeTab;
            else if (g_activeTab == gone) g_activeTab = -1;
        }
        WhisperTab t;
        t.SetName(who);
        t.uid = g_nextTabUid++;
        g_tabs.push_back(t);
        at = static_cast<int>(g_tabs.size()) - 1;
    }
    g_tabs[at].unread = false;
    if (g_tabs[at].sendLang.empty()) {  // first time: answer in the language this player writes in
        auto lang = g_playerLang.find(whoFolded);
        if (lang != g_playerLang.end() && PrimaryLang(lang->second) != TypingLang()) {
            g_tabs[at].sendLang = SendCodeFor(lang->second);
            SetStatus(TrF(L"{1} writes in {2} – your answers go out in it (Send: change it)",
                          {g_tabs[at].name, NativeName(g_tabs[at].sendLang)}), 8000);
        }
    }
    g_sendOverride = g_tabs[at].sendLang;
    strncpy_s(g_test.sendLang, U8(g_sendOverride).c_str(), _TRUNCATE);
    g_activeTab = g_selectTab = at;
    g_channel = Channel::Whisper;
    g_whisperTo = g_tabs[at].name;
    g_focusInput = true;
    ++g_inputGen;  // translated again for this player
    g_inputChanged = GetTickCount64();
}
// A tab of your own in front: its channel to write in and its send language (g_mu held).
void ActivateCustomTab(int i, bool focus) {  // focus: the input takes the keyboard (not under its settings)
    WhisperTab& t = g_tabs[i];
    t.unread = false;
    g_activeTab = g_selectTab = i;
    g_channel = t.write;
    g_whisperTo.clear();
    g_sendOverride = t.sendLang;
    if (focus) g_focusInput = true;
    ++g_inputGen;
    g_inputChanged = GetTickCount64();
}
void ActivateTab(int i) {  // g_mu held
    if (g_tabs[i].custom) ActivateCustomTab(i);
    else OpenWhisperTab(g_tabs[i].name);
}

void SaveCustomTabs() {  // [NexusTabs]: "name|channels|write|send" per tab of your own (g_mu held)
    int n = 0;
    for (const WhisperTab& t : g_tabs) {
        if (!t.custom) continue;
        ++n;
        g_cfg.SaveValue(L"NexusTabs", (L"Tab" + std::to_wstring(n)).c_str(),
                        AsciiEscape(t.name) + L"|" + std::to_wstring(t.channels) + L"|" +
                            std::to_wstring(static_cast<int>(t.write)) + L"|" + AsciiEscape(t.sendLang));
    }
    g_cfg.SaveValue(L"NexusTabs", L"Count", std::to_wstring(n));
}
void LoadCustomTabs() {  // g_mu held
    const int n = static_cast<int>(GetPrivateProfileIntW(L"NexusTabs", L"Count", 0, g_cfg.iniPath.c_str()));
    for (int i = 1; i <= std::min<int>(n, static_cast<int>(kMaxCustomTabs)); ++i) {
        wchar_t buf[512] = {};
        GetPrivateProfileStringW(L"NexusTabs", (L"Tab" + std::to_wstring(i)).c_str(), L"", buf, 512, g_cfg.iniPath.c_str());
        std::vector<std::wstring> f;
        std::wstring cur;
        for (const wchar_t* c = buf; *c; ++c) {
            if (*c == L'|') {
                f.push_back(cur);
                cur.clear();
            } else {
                cur += *c;
            }
        }
        f.push_back(cur);
        if (f.size() < 3 || f[0].empty()) continue;
        WhisperTab t;
        t.custom = true;
        t.SetName(AsciiUnescape(f[0]));
        t.channels = static_cast<uint32_t>(std::wcstoul(f[1].c_str(), nullptr, 10));
        t.write = static_cast<Channel>(std::clamp(_wtoi(f[2].c_str()), 0, static_cast<int>(Channel::System)));
        if (f.size() > 3) t.sendLang = AsciiUnescape(f[3]);
        t.uid = g_nextTabUid++;
        g_tabs.push_back(t);
    }
}
int NewCustomTab() {  // a copy of the chat: every channel, writing into the active one (g_mu held); -1 = too many
    if (std::count_if(g_tabs.begin(), g_tabs.end(), [](const WhisperTab& t) { return t.custom; }) >= static_cast<long>(kMaxCustomTabs))
        return -1;
    WhisperTab t;
    t.custom = true;
    t.uid = g_nextTabUid++;
    int number = 1;  // "Tab 2", "Tab 3" … – the first free number
    for (bool taken = true; taken; ++number) {
        taken = false;
        for (const WhisperTab& o : g_tabs)
            if (o.custom && o.name == TrF(L"Tab {1}", {std::to_wstring(number + 1)})) taken = true;
        if (!taken) t.SetName(TrF(L"Tab {1}", {std::to_wstring(number + 1)}));
    }
    const auto firstWhisper = std::find_if(g_tabs.begin(), g_tabs.end(), [](const WhisperTab& o) { return !o.custom; });
    const int at = static_cast<int>(firstWhisper - g_tabs.begin());  // your own tabs stand before the whisper tabs
    g_tabs.insert(firstWhisper, t);
    if (g_activeTab >= at) ++g_activeTab;
    SaveCustomTabs();
    ActivateCustomTab(at, false);  // its settings open next; the input's focus would close them
    return at;
}

void LeaveWhisperTab() {  // back to the whole chat, writing in the channel active in GW2 (g_mu held)
    g_activeTab = -1;
    g_selectTab = -1;
    g_channel = Channel::Unknown;
    g_whisperTo.clear();
    g_sendOverride.clear();
    ++g_inputGen;
    g_inputChanged = GetTickCount64();
}
std::atomic<bool> g_chatEventsPresent{false};
std::atomic<bool> g_chatEventsFile{false};  // Events_Chat.dll lies in the add-ons folder (maybe not loaded in Nexus)
std::atomic<bool> g_chatEventsInstalling{false};
std::thread g_chatEventsThread;

// A Nexus add-on in this process that says it is "Events: Chat" (its definition is only read). Every 3 s at most.
void RemoveChatEventsNow() {  // game thread
    const std::wstring dll = AddonsDir() + kChatEventsDllName;
    const bool gone = DeleteFileW(dll.c_str()) || GetFileAttributesW(dll.c_str()) == INVALID_FILE_ATTRIBUTES;
    if (gone) g_chatEventsFile = false;
    std::lock_guard<std::mutex> lock(g_mu);
    SetStatus(gone ? Tr(L"Events: Chat removed")
                   : Tr(L"Events: Chat is loaded – remove it in Nexus → Addons → Events: Chat (other add-ons may use it)"),
              10000);
}

bool ChatEventsPresent(bool now) {
    static ULONGLONG next = 0;
    if (!now && GetTickCount64() < next) return g_chatEventsPresent;
    next = GetTickCount64() + 3000;
    bool found = false;
    std::wstring version;  // Events: Chat's version (the technical details, the protocol)
    HMODULE mods[1024];
    DWORD needed = 0;
    if (K32EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &needed)) {
        const DWORD n = std::min<DWORD>(needed / sizeof(HMODULE), 1024);
        for (DWORD i = 0; i < n && !found; ++i) {
            if (mods[i] == g_self) continue;
            if (FARPROC get = GetProcAddress(mods[i], "GetAddonDef")) {  // another add-on's code: behind a guard
                uint32_t sig = 0;
                AddonVersion_t v{};
                if (ReadAddonDefGuarded(reinterpret_cast<void*>(get), &sig, &v) && sig == bridge::kChatEventsSignature) {
                    found = true;
                    version = std::to_wstring(v.Major) + L"." + std::to_wstring(v.Minor) + L"." + std::to_wstring(v.Build) +
                              L"." + std::to_wstring(v.Revision);
                }
            }
        }
    }
    ChatSourceSetVersion(version);
    g_chatEventsPresent = found;
    g_chatEventsFile = GetFileAttributesW((AddonsDir() + kChatEventsDllName).c_str()) != INVALID_FILE_ATTRIBUTES;
    return found;
}

void InstallChatEventsNow() {  // on your click: newest release, checksum checked, into the add-ons folder
    if (g_chatEventsInstalling.exchange(true)) return;
    if (g_chatEventsThread.joinable()) g_chatEventsThread.join();
    g_chatEventsThread = std::thread([dir = AddonsDir()] {
        ExtrasInstallResult r;
        if (!Shielded([&] { r = InstallChatEvents(dir, [] { return g_disabled.load(); }); })) r.code = ExtrasInstallCode::DownloadFailed;
        g_chatEventsInstalling = false;
        std::lock_guard<std::mutex> lock(g_mu);
        SetStatus(r.code == ExtrasInstallCode::Ok ? Tr(L"Events: Chat installed – Nexus loads it (restart GW2 if no chat arrives)")
                  : r.code == ExtrasInstallCode::NoAnswer ? TrF(L"GitHub did not answer ({1}) – please try again later", {r.detail})
                                                          : TrF(L"Loading {1} failed: {2}", {L"Events: Chat", r.detail}),
                  12000);
    });
}

ChatLinks g_links;

std::wstring ApiLang() {  // the GW2 API speaks en, de, fr, es, zh (g_mu held)
    if (!g_nx.namesLang.empty()) return g_nx.namesLang;  // like the game
    const std::wstring p = ToLowerAscii(PrimaryLang(TypingLang()));
    return p == L"de" || p == L"fr" || p == L"es" || p == L"zh" ? p : L"en";
}
bool g_openLinkAsk = false;
bool g_minimizeRequest = false, g_expandRequest = false;  // on the main window, never inside the chat area
// A chat line with its web links (a click asks first) and chat codes (shown by name, details on hover). A click on a
// chat code does nothing (2.3.1 – the click on chat codes crashed the game; copying the code was in the way too): it is
// swallowed, so it does not translate the line either.
bool RichLine(const std::vector<PieUI::ChatLinks::LinkSegment>& segs, ImVec4 color) {  // g_mu held
    bool clicked = false;
    const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    const float space = ImGui::CalcTextSize(" ").x;
    bool first = true;
    auto place = [&](const std::string& piece) {  // on this line, or the next when it does not fit any more
        if (!first) {
            ImGui::SameLine(0, space);
            if (ImGui::GetCursorScreenPos().x + ImGui::CalcTextSize(piece.c_str()).x > right) ImGui::NewLine();
        }
        first = false;
    };
    ImGui::BeginGroup();
    for (const auto& seg : segs) {
        switch (seg.kind) {
            case PieUI::ChatLinks::SegmentKind::Plain: {
                size_t pos = 0;
                while (pos < seg.display.size()) {
                    size_t end = seg.display.find(' ', pos);
                    if (end == std::string::npos) end = seg.display.size();
                    if (end > pos) {
                        const std::string word = seg.display.substr(pos, end - pos);
                        place(word);
                        LookText(word.c_str(), color, false);
                    }
                    pos = end;
                    while (pos < seg.display.size() && seg.display[pos] == ' ') ++pos;
                }
                break;
            }
            case PieUI::ChatLinks::SegmentKind::Url: {  // a web link: blue, underlined; a click asks first
                place(seg.display);
                LookText(seg.display.c_str(), Bright(ImVec4(0.45f, 0.7f, 1.f, 1.f)), false);
                const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
                ImGui::GetWindowDrawList()->AddLine(ImVec2(a.x, b.y - 1), ImVec2(b.x, b.y - 1), IM_COL32(115, 178, 255, 255));
                if (ImGui::IsItemHovered()) {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    ImGui::SetTooltip("%s", seg.url.c_str());
                }
                if (ImGui::IsItemClicked()) {
                    g_openLink = W(seg.url);
                    g_openLinkAsk = true;
                    clicked = true;
                }
                break;
            }
            case PieUI::ChatLinks::SegmentKind::Link: {
                const std::string lbl = g_links.Label(seg);
                place(lbl);
                g_links.Render(seg, lbl, [](const std::string& l, ImVec4 c) { LookText(l.c_str(), Bright(c), false); });
                break;
            }
            case PieUI::ChatLinks::SegmentKind::Ae2: {
                place(seg.display);
                LookText(seg.display.c_str(), Bright(ImVec4(0.62f, 0.78f, 1.f, 1.f)), false);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted(seg.display.c_str());
                    if (!seg.ae2Profession.empty()) {
                        ImGui::TextDisabled("%s", seg.ae2Profession.c_str());
                    }
                    ImGui::EndTooltip();
                }
                if (ImGui::IsItemClicked()) {
                    const std::string& code = !seg.ae2ChatLink.empty() ? seg.ae2ChatLink : seg.raw;
                    ImGui::SetClipboardText(code.c_str());
                    SetStatus(Tr(L"Build code copied to clipboard"), 2500);
                    clicked = true;
                }
                break;
            }
        }
    }
    ImGui::EndGroup();
    return clicked;
}

void SafeOpenUrl(const std::wstring& rawLink) {
    const std::wstring target = LinkTarget(rawLink);
    if ((target.rfind(L"https://", 0) == 0 || target.rfind(L"http://", 0) == 0) &&
        target.size() <= 2048 &&
        target.find(L' ') == std::wstring::npos &&
        target.find(L'"') == std::wstring::npos &&
        target.find(L'\'') == std::wstring::npos) {
        std::thread([target] {
            Shielded([&target] {
                CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
                ShellExecuteW(nullptr, L"open", target.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                CoUninitialize();
            });
        }).detach();
    }
}

}  // namespace kb
