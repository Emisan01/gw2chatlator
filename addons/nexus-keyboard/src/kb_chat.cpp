// kb_chat.cpp — incoming chat, tabs, Events: Chat, chat codes and links.

#include "kb_shared.hpp"

namespace kb {

std::wstring LinkHost(const std::wstring& url) {  // "https://www.example.org/a?b" -> "www.example.org"
    size_t a = url.find(L"://");
    a = a == std::wstring::npos ? 0 : a + 3;
    const size_t b = url.find_first_of(L"/?#:", a);
    return url.substr(a, b == std::wstring::npos ? std::wstring::npos : b - a);
}

// Rate limiter against chat flood / DoS attacks (max 8 auto-translations per 3s window)
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
    bool own;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        own = outgoing || (!g_ownName.empty() && l.speakerFolded == CaseFold(g_ownName));
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
        g_lines.push_back(std::move(l));
        while (g_lines.size() > kMaxLines) g_lines.pop_front();
        for (size_t i = 0; i < g_tabs.size(); ++i) {  // a tab not in front that shows this line: marked
            if (static_cast<int>(i) == g_activeTab) continue;
            const WhisperTab& t = g_tabs[i];
            if (t.custom ? (t.channels & ChannelBit(ch)) != 0
                         : ch == Channel::Whisper && t.nameFolded == g_lines.back().speakerFolded)
                g_tabs[i].unread = true;
        }
        g_scrollDown = true;
    }
    if (!own && AllowAutoTranslate()) g_worker.Post([id, t = rawText] { TranslateLine(id, t); });
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
    g_decoderFile = GetFileAttributesW((AddonsDir() + kDecoderRingDllName).c_str()) != INVALID_FILE_ATTRIBUTES;
    return found;
}

void InstallChatEventsNow() {  // on your click: newest release, checksum checked, into the add-ons folder
    if (g_chatEventsInstalling.exchange(true)) return;
    if (g_chatEventsThread.joinable()) g_chatEventsThread.join();
    g_chatEventsThread = std::thread([dir = AddonsDir()] {
        const ExtrasInstallResult r = InstallChatEvents(dir, [] { return g_disabled.load(); });
        g_chatEventsInstalling = false;
        std::lock_guard<std::mutex> lock(g_mu);
        SetStatus(r.code == ExtrasInstallCode::Ok ? Tr(L"Events: Chat installed – Nexus loads it (restart GW2 if no chat arrives)")
                  : r.code == ExtrasInstallCode::NoAnswer ? TrF(L"GitHub did not answer ({1}) – please try again later", {r.detail})
                                                          : TrF(L"Loading {1} failed: {2}", {L"Events: Chat", r.detail}),
                  12000);
    });
}
// Decoder Ring (optional): names, maps and details of chat codes – installed and removed only on your click.
std::atomic<bool> g_decoderFile{false};  // DecoderRing.dll lies in the add-ons folder (maybe not loaded in Nexus)
std::atomic<bool> g_decoderInstalling{false};
std::thread g_decoderThread;

void InstallDecoderRingNow() {  // on your click: newest release, checksum checked, into the add-ons folder
    if (g_decoderInstalling.exchange(true)) return;
    if (g_decoderThread.joinable()) g_decoderThread.join();
    g_decoderThread = std::thread([dir = AddonsDir()] {
        const ExtrasInstallResult r = InstallDecoderRing(dir, [] { return g_disabled.load(); });
        g_decoderInstalling = false;
        if (r.code == ExtrasInstallCode::Ok) g_decoderFile = true;
        std::lock_guard<std::mutex> lock(g_mu);
        SetStatus(r.code == ExtrasInstallCode::Ok ? Tr(L"Decoder Ring installed – Nexus loads it (restart GW2 if the names stay missing)")
                  : r.code == ExtrasInstallCode::NoAnswer ? TrF(L"GitHub did not answer ({1}) – please try again later", {r.detail})
                                                          : TrF(L"Loading {1} failed: {2}", {L"Decoder Ring", r.detail}),
                  12000);
    });
}

void RemoveDecoderRingNow() {  // game thread: only while Nexus does not hold it
    const std::wstring dll = AddonsDir() + kDecoderRingDllName;
    const bool gone = DeleteFileW(dll.c_str()) || GetFileAttributesW(dll.c_str()) == INVALID_FILE_ATTRIBUTES;
    if (gone) g_decoderFile = false;
    std::lock_guard<std::mutex> lock(g_mu);
    SetStatus(gone ? Tr(L"Decoder Ring removed")
                   : Tr(L"Decoder Ring is loaded – remove it in Nexus → Addons → Decoder Ring (other add-ons may use it)"),
              10000);
}

ChatLinks g_links;

std::wstring ApiLang() {  // the GW2 API speaks en, de, fr, es, zh (g_mu held)
    if (!g_nx.namesLang.empty()) return g_nx.namesLang;  // like the game
    const std::wstring p = ToLowerAscii(PrimaryLang(TypingLang()));
    return p == L"de" || p == L"fr" || p == L"es" || p == L"zh" ? p : L"en";
}
bool g_openLinkAsk = false;
bool g_minimizeRequest = false, g_expandRequest = false;  // on the main window, never inside the chat area
int g_redirect = 0;  // 0 none; 3 waiting for GW2's chat line; 4 it is open (1 and 2 were the waypoint's, gone in 2.3.0)
ULONGLONG g_redirectSince = 0;

// The Waypoint/POI click starts only while GW2's original can still stand in view at the bottom of its chat – never
// into nothing. Not its age (two minutes old and still the newest line), not a map change (it need not empty the
// chat): its place from the bottom – at most kWpNewerLines lines came after it (Events: Chat counts them; lines of
// channels GW2's tab hides count too: rather no macro than one into nothing). Whether GW2 emptied its chat only the
// screen tells: the very line must be seen there (FindChatLink), else no click. GW2's chat is never scrolled.
constexpr uint64_t kWpNewerLines = 8;

bool OriginalStillThere(uint64_t lineId) {  // g_mu held
    if (lineId == 0 || g_nextId - 1 - lineId > kWpNewerLines) return false;
    const auto it = std::lower_bound(g_lines.begin(), g_lines.end(), lineId,
                                     [](const ChatLine& l, uint64_t id) { return l.id < id; });  // ids rise
    return it != g_lines.end() && it->id == lineId;
}

void HandleChatLinkClick(const std::string& raw, ULONGLONG receivedTime, const std::string& where,
                         const ChatLineHint& line, uint64_t lineId) {  // g_mu held
    ImGui::SetClipboardText(raw.c_str());
    const bool waypoint = DecodeChatCode(W(raw)).kind == ChatCodeKind::Poi;
    // Waypoints and points of interest (and vistas): never GW2's chat line – open, it is typing mode and a click on a
    // link there does not fly the map – and never the map from us (GW2's map key opens it without a target). Only a
    // real click on the original flies the map: the Waypoint/POI click does it (`line.label` is set for a waypoint or a
    // point of interest Decoder Ring has named), else the code is copied and the status says why and where to click.
    (void)receivedTime;
    if (waypoint) {
        if (g_nx.waypointPoiClick && !line.label.empty()) {
            if (OriginalStillThere(lineId)) StartWaypointClick(line);
            else SetStatus(TrF(L"{1} is copied – too far up in GW2's chat to click it for you.", {line.label}), 12000);
        } else if (g_nx.waypointPoiClick && where.empty()) {
            SetStatus(Tr(L"Decoder Ring has not named this one yet – click again in a moment (the code is copied)."), 10000);
        } else {
            SetStatus(where.empty() ? Tr(L"The code is copied – click it in GW2's chat, the map flies there.")
                                    : TrF(L"{1} – the code is copied: click it in GW2's chat, the map flies there.", {W(where)}),
                      12000);
        }
        return;
    }
    g_minimizeRequest = true;  // any other link: GW2's chat line opens, you click the original there
    g_redirect = 3;
    g_redirectSince = GetTickCount64();
    g_suppressChatFocusUntil = GetTickCount64() + 2000;  // our own "come back when GW2's chat opens" waits
    if (g_api && g_api->GameBinds_InvokeAsync) g_api->GameBinds_InvokeAsync(GB_UiChatFocus, 20);
}

bool RichLine(const std::wstring& text, const std::vector<PieUI::ChatLinks::LinkSegment>& segs, ImVec4 color,
              ULONGLONG receivedTime, const std::wstring& speaker, uint64_t lineId) {  // g_mu held
    (void)text;
    std::wstring asShown;  // the line as GW2 shows it: chat codes as their "[Name]" (the waypoint click finds it)
    for (const auto& seg : segs)
        asShown += W(seg.kind == PieUI::ChatLinks::SegmentKind::Link ? g_links.Label(seg) : seg.display);
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
                if (g_links.Render(seg, lbl, [](const std::string& l, ImVec4 c) { LookText(l.c_str(), Bright(c), false); })) {
                    clicked = true;
                    const std::string where = g_links.Where(seg);  // empty: Decoder Ring does not know it
                    const int kind = g_links.PoiKind(seg);          // the Waypoint/POI click: only these two
                    const bool wpPoi = kind == DP_Waypoint || kind == DP_PointOfInterest;
                    HandleChatLinkClick(seg.raw, receivedTime, where,
                                        ChatLineHint{speaker, asShown, wpPoi ? W(lbl) : std::wstring()}, lineId);
                }
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
                    ImGui::TextDisabled("%s", ToUtf8(Tr(L"Click: open it in GW2's chat")).c_str());
                    ImGui::EndTooltip();
                }
                if (ImGui::IsItemClicked()) {
                    clicked = true;
                    const std::string toCopy = !seg.ae2ChatLink.empty() ? seg.ae2ChatLink : seg.raw;
                    HandleChatLinkClick(toCopy, receivedTime, std::string());
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
            CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
            ShellExecuteW(nullptr, L"open", target.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            CoUninitialize();
        }).detach();
    }
}

}  // namespace kb
