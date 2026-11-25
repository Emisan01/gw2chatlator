// kb_chat.cpp — incoming chat, tabs, Events: Chat, chat codes and links.

#include "kb_shared.hpp"
#include "core/soft_wrap.hpp"
#include "imgui/imgui_internal.h"  // ImGuiWindow: the chat scrolls in the frame it is drawn

#include <map>

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
static void BindWhisperTabs(const ChatLine& l);

void AddLine(Channel ch, bool outgoing, const char* speaker, const char* text, const char* account) {
    if (!text || !*text) return;
    TechLog('C', L"line in: " + ChannelLabel(ch) + (outgoing ? L", yours, " : L", ") + std::to_wstring(std::strlen(text)) + L" bytes");
    ChatLine l;
    l.channel = ch;
    l.outgoing = outgoing;
    l.speaker = speaker ? SanitizeChatText(W(speaker)) : std::wstring();
    if (l.speaker.size() > 64) l.speaker.resize(64);
    l.text = SanitizeChatText(UnescapeChatText(W(text)));  // Events: Chat escapes brackets: "\[&...\]"
    if (l.text.empty()) return;
    if (l.text.size() > 600) l.text.resize(600);
    l.speakerFolded = CaseFold(Trim(l.speaker));
    if (ch == Channel::Whisper && account && *account) {  // ":Name.1234" as GW2 writes it; kept for whispers only
        std::wstring a = Trim(SanitizeChatText(W(account)));
        if (!a.empty() && a[0] == L':') a.erase(0, 1);
        if (a.size() <= 64) l.accountFolded = CaseFold(a);
    }
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
        g_lastChatLineTime = l.receivedTime;
        if (own && g_whisperWatch.WentElsewhere(l.text, ch == Channel::Whisper, l.receivedTime)) {
            ++g_healthWhisperElsewhere;
            g_syncedChannel.clear();  // GW2's line is not where we thought: the next message sets it again
            SetStatus(TrF(L"That whisper landed in {1}, not with the player – check GW2's chat line.", {ChannelLabel(ch)}), 15000);
        }
        l.mapId = g_mapId.load();
        l.id = id = g_nextId++;
        if (!own) {  // completions offer the names and words of the chat (never learned – invariant 10)
            if (!l.speaker.empty()) g_context.insert(g_context.begin(), l.speaker);
            if (!l.speaker.empty()) g_chatNames.Add(l.speaker);  // a name in a line stays a name (memory only)
            for (const Span& s : WordSpans(l.text))
                if (s.length >= 4) g_context.insert(g_context.begin(), l.text.substr(s.start, s.length));
            if (g_context.size() > 80) g_context.resize(80);
        }

        // Smart-Sieve-Manager: spam filter, repetition cache, and occlusion culling
        if (!own) {
            if (!g_rawTranslate.load() && IsGamerInterjection(rawText)) {
                // Gamer interjection / shorthand: skip translation
            } else {
                std::wstring cachedTrans, cachedBy;
                if (g_orchestrator.SieveMessage(rawText, TypingLang(), true, true, true, false, cachedTrans, cachedBy,
                                                 TranslatorTag(g_translator, g_cfg.localQuality)) == ChatPriority::Immediate) {
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
        // Someone else's whisper tells their account: their tab is the player's from now on, whichever character.
        // Never from a line of your own – whose account that one carries is not ours to guess.
        if (ch == Channel::Whisper && !own) BindWhisperTabs(g_lines.back());
        for (size_t i = 0; i < g_tabs.size(); ++i) {  // a tab not in front that shows this line: marked
            if (static_cast<int>(i) == g_activeTab) continue;
            const WhisperTab& t = g_tabs[i];
            if (t.custom ? (t.channels & ChannelBit(ch)) != 0
                         : ch == Channel::Whisper && t.peer.Shows(g_lines.back().speakerFolded, g_lines.back().accountFolded))
                g_tabs[i].unread = true;
        }
    }
    if (shouldQueue) g_worker.Post([id, t = rawText] { TranslateLine(id, t); });
}

// OnChatEvents: kb_chatsource.cpp (the guard between Events: Chat and us).

// What you started in a tab and did not send stays in that tab: each tab shows its own text – a new one is empty – so
// you can write in several channels side by side and switch back and forth; the chat tab keeps its own as well.
// Memory only, never saved (g_mu held).
namespace {
constexpr uint32_t kChatDraft = 0xFFFFFFFFu, kNoDraft = 0xFFFFFFFEu;
std::map<uint32_t, std::string> s_drafts;
uint32_t s_draftOwner = kChatDraft;  // whose text the input shows

void ShowDraftOf(uint32_t uid) {
    if (uid == s_draftOwner) return;
    if (s_draftOwner != kNoDraft) s_drafts[s_draftOwner] = g_input;
    const std::string shown = Flat(g_input);
    const auto it = s_drafts.find(uid);
    const std::string next = it == s_drafts.end() ? std::string() : it->second;
    CopyUtf8(g_input, next);
    s_draftOwner = uid;
    if (g_inputActive) {  // the box keeps its own copy while it has the keyboard: replaced through its callback
        g_edit.active = true;
        g_edit.startByte = 0;
        g_edit.lenBytes = shown.size();
        g_edit.insert = Flat(next.c_str());
    }
    g_flatInput = Flat(g_input);
    g_wFlatInput = W(g_flatInput);
    g_preview.clear();  // another tab's translation
    g_back.clear();
    g_historyPos = -1;
    g_undo.active = false;
    g_pick = -1;
}
}  // namespace

void ForgetDraft(uint32_t uid) {
    s_drafts.erase(uid);
    if (s_draftOwner == uid) s_draftOwner = kNoDraft;  // the input showed that tab's text: kept nowhere
}

// A whisper from someone else: the tab of that player learns the account and follows them to the character they are
// on now (its name, and whom you write to). A second tab of the same player, opened under another character before
// the account was known, folds into it – the one in front stays, with what you were writing. g_mu held.
static void BindWhisperTabs(const ChatLine& l) {
    if (l.accountFolded.empty() || l.speakerFolded.empty()) return;
    std::vector<gct::WhisperPeer*> peers;
    for (WhisperTab& t : g_tabs) peers.push_back(t.custom ? nullptr : &t.peer);
    const gct::WhisperBinding b = gct::BindWhisper(peers, g_activeTab, l.speakerFolded, l.accountFolded);
    if (b.keep < 0) return;
    WhisperTab& keep = g_tabs[b.keep];
    if (keep.nameFolded != l.speakerFolded) keep.SetName(Trim(l.speaker));
    if (b.keep == g_activeTab) g_whisperTo = keep.name;
    TechLog('C', std::wstring(L"whisper tab: account known, ") + std::to_wstring(keep.peer.names.size()) + L" character(s)" +
                     (b.drop >= 0 ? L", a second tab of the player folded in" : L""));
    if (b.drop < 0) return;
    if (keep.sendLang.empty()) keep.sendLang = g_tabs[b.drop].sendLang;
    ForgetDraft(g_tabs[b.drop].uid);
    g_tabs.erase(g_tabs.begin() + b.drop);
    if (g_activeTab > b.drop) --g_activeTab;
    if (g_selectTab > b.drop) --g_selectTab;
    if (g_tabSettings == b.drop) g_tabSettings = -1;
    else if (g_tabSettings > b.drop) --g_tabSettings;
}

// A click on a name: that player's whisper tab (opened, or brought to the front); writing goes to them. g_mu held.
void OpenWhisperTab(const std::wstring& name) {
    const std::wstring who = Trim(name);
    if (who.empty()) return;
    const std::wstring whoFolded = CaseFold(who);
    std::wstring account;  // known from a whisper of theirs in the chat
    for (auto l = g_lines.rbegin(); l != g_lines.rend() && account.empty(); ++l)
        if (l->channel == Channel::Whisper && !l->own && l->speakerFolded == whoFolded) account = l->accountFolded;
    int at = -1;
    for (size_t i = 0; i < g_tabs.size() && at < 0; ++i)  // the player's tab, under whichever character it was opened
        if (!g_tabs[i].custom && (g_tabs[i].peer.Has(whoFolded) || (!account.empty() && g_tabs[i].peer.account == account)))
            at = static_cast<int>(i);
    if (at >= 0 && g_tabs[at].nameFolded != whoFolded && !account.empty() && g_tabs[at].peer.account == account) {
        g_tabs[at].peer.Add(whoFolded);
        g_tabs[at].SetName(who);  // the character clicked is whom you write to
    }
    if (at < 0) {
        const size_t whispers = std::count_if(g_tabs.begin(), g_tabs.end(), [](const WhisperTab& t) { return !t.custom; });
        if (whispers >= kMaxWhisperTabs) {  // the oldest whisper tab goes
            const auto oldest = std::find_if(g_tabs.begin(), g_tabs.end(), [](const WhisperTab& t) { return !t.custom; });
            const int gone = static_cast<int>(oldest - g_tabs.begin());
            ForgetDraft(oldest->uid);
            g_tabs.erase(oldest);
            if (g_activeTab > gone) --g_activeTab;
            else if (g_activeTab == gone) g_activeTab = -1;
        }
        WhisperTab t;
        t.SetName(who);
        t.peer.Add(whoFolded);
        t.peer.account = account;
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
                          {g_tabs[at].name, LanguageName(g_tabs[at].sendLang)}), 8000);
        }
    }
    g_sendOverride = g_tabs[at].sendLang;
    CopyUtf8(g_test.sendLang, U8(g_sendOverride));
    ShowDraftOf(g_tabs[at].uid);
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
    ShowDraftOf(t.uid);
    g_activeTab = g_selectTab = i;
    g_channel = t.write;
    g_guildSlot = 0;
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
    ShowDraftOf(kChatDraft);
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

// Nexus says when an add-on loads or goes (EV_ADDON_LOADED / EV_ADDON_UNLOADED, its signature as payload): Events:
// Chat's state at once instead of scanning every module every 3 s. The scan stays for the start (it may have loaded
// before us), once after it loaded (its version) and for a Nexus without these events.
std::atomic<bool> g_addonEventsOn{false};
std::atomic<bool> g_chatModuleLoaded{false};  // Nexus loaded Events: Chat: read its version once on the game thread
// Our install finished: for ten seconds (or until Nexus loads it) the row says Nexus is taking it in – after that, not
// loaded means Nexus waits for the user's Load.
std::atomic<ULONGLONG> g_chatInstalledAt{0};
static bool ReadSignatureGuarded(const void* payload, uint32_t* sig) {
    if (!payload) return false;
    __try {
        *sig = *static_cast<const volatile uint32_t*>(payload);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
void OnAddonLoaded(void* payload) {
    uint32_t sig = 0;
    if (!ReadSignatureGuarded(payload, &sig) || sig != bridge::kChatEventsSignature) return;
    TechLog('N', L"Nexus: Events: Chat loaded");
    g_chatEventsPresent = true;
    g_chatModuleLoaded = true;
}
void OnAddonUnloaded(void* payload) {  // no scan after it: the module may still be mapped for a moment
    uint32_t sig = 0;
    if (!ReadSignatureGuarded(payload, &sig) || sig != bridge::kChatEventsSignature) return;
    TechLog('N', L"Nexus: Events: Chat unloaded");
    g_chatEventsPresent = false;
    g_chatEventsLive = false;
}

bool ChatEventsPresent(bool now) {
    static ULONGLONG next = 0;
    static bool first = true;  // loaded before us: no event says so – the first call always scans
    if (first || g_chatModuleLoaded.exchange(false)) now = true;
    first = false;
    const ULONGLONG tick = GetTickCount64();
    if (const ULONGLONG at = g_chatInstalledAt; at && (g_chatEventsPresent || tick - at > 10000)) g_chatInstalledAt = 0;
    if (!now && tick < next) return g_chatEventsPresent;
    next = tick + 3000;
    if (!now && g_addonEventsOn) {  // Nexus tells the state: only whether the file lies there (installed, not loaded)
        g_chatEventsFile = GetFileAttributesW((AddonsDir() + kChatEventsDllName).c_str()) != INVALID_FILE_ATTRIBUTES;
        return g_chatEventsPresent;
    }
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
    // Removed or unloaded in Nexus: its lines stopped – the state says so (the first line of the session set "live" and
    // kept the dashboard on "ready" until the game ended, 2026-10-27).
    if (g_chatEventsPresent.exchange(found) && !found) g_chatEventsLive = false;
    g_chatEventsFile = GetFileAttributesW((AddonsDir() + kChatEventsDllName).c_str()) != INVALID_FILE_ATTRIBUTES;
    return found;
}

void InstallChatEventsNow() {  // on your click: newest release, checksum checked, into the add-ons folder
    if (g_chatEventsInstalling.exchange(true)) return;
    if (g_chatEventsThread.joinable()) g_chatEventsThread.join();
    g_chatEventsThread = std::thread([dir = AddonsDir()] {
        ExtrasInstallResult r;
        if (!Shielded([&] { r = InstallChatEvents(dir, [] { return !KeepGoing(); }); })) r.code = ExtrasInstallCode::DownloadFailed;
        g_chatEventsInstalling = false;
        if (r.code == ExtrasInstallCode::Ok) g_chatInstalledAt = GetTickCount64();
        std::lock_guard<std::mutex> lock(g_mu);
        SetStatus(r.code == ExtrasInstallCode::Ok ? Tr(L"Events: Chat installed – if no chat arrives, Nexus waits for your OK: Nexus → Addons → Events: Chat → Load (once)")
                  : r.code == ExtrasInstallCode::NoAnswer ? TrF(L"GitHub did not answer ({1}) – please try again later", {r.detail})
                                                          : TrF(L"Loading {1} failed: {2}", {L"Events: Chat", r.detail}),
                  12000);
    });
}

ChatLinks g_links;

bool g_openLinkAsk = false;
bool g_minimizeRequest = false, g_expandRequest = false;  // on the main window, never inside the chat area
// A chat line with its web links (a click asks first) and chat codes (shown by name, details on hover). A click on a
// chat code does nothing (2.3.1 – the click on chat codes crashed the game; copying the code was in the way too): it is
// swallowed, so it does not translate the line either.
bool RichLine(const std::vector<PieUI::ChatLinks::LinkSegment>& segs, ImVec4 color) {  // g_mu held
    g_links.SetOptions(ReadLang(), g_cfg.chatCodeNames);
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
                const bool leftClick = g_links.Render(seg, lbl, [](const std::string& l, ImVec4 c) { LookText(l.c_str(), Bright(c), false); });
                if (seg.linkType == PieUI::ChatLinks::LINK_MAP && seg.primaryId) {
                    if (leftClick) {  // GW2's own flight; the window shows if it cannot run
                        FlyToWaypoint(seg.primaryId);
                        clicked = true;
                    } else if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {  // only a look: no message, no click
                        OpenWaypointPreview(seg.primaryId);
                        clicked = true;
                    }
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

// ---- The chat's lines in the main window (moved from kb_window.cpp, 2026-11-17: the window file was one monolith).
namespace {
// Hover and clicks over every piece of a line's text (a message beside its name is drawn in two pieces).
struct LineHit {
    bool hovered = false, clickL = false, clickR = false, visible = false;
    void AddLast() {
        hovered |= ImGui::IsItemHovered();
        clickL |= ImGui::IsItemClicked(ImGuiMouseButton_Left);
        clickR |= ImGui::IsItemClicked(ImGuiMouseButton_Right);
        visible |= ImGui::IsItemVisible();
    }
};
// A window's scroll in this very frame, after its Begin: Begin placed the cursor for the old scroll, everything it derived
// from it moves along – as if Begin had known. (A scroll set the usual way counts from the next frame on.)
void ScrollNow(ImGuiWindow* w, float y) {
    const float d = y - w->Scroll.y;
    if (d == 0.0f) return;
    w->Scroll.y = y;
    w->DC.CursorPos.y -= d;
    w->DC.CursorPosPrevLine.y -= d;
    w->DC.CursorStartPos.y -= d;
    w->DC.CursorMaxPos.y -= d;
    w->DC.IdealMaxPos.y -= d;
    w->WorkRect.Min.y -= d;
    w->WorkRect.Max.y -= d;
    w->ContentRegionRect.Min.y -= d;
    w->ContentRegionRect.Max.y -= d;
}
}  // namespace

// The subtitle mode fades a line as GW2's own chat does (Emi, 2026-11-09): 10 s fully there, then 1.5 s down to
// nothing. While you write – a click in the window, the focus key – every line is back; sent, they fade again.
constexpr ULONGLONG kSubtitleShowMs = 10000, kSubtitleFadeMs = 1500;
static float LineFade(const ChatLine& l, bool fading, ULONGLONG now) {
    if (!fading || !l.receivedTime || now <= l.receivedTime + kSubtitleShowMs) return 1.0f;
    const ULONGLONG past = now - l.receivedTime - kSubtitleShowMs;
    return past >= kSubtitleFadeMs ? 0.0f : 1.0f - static_cast<float>(past) / static_cast<float>(kSubtitleFadeMs);
}

void RenderChat(ImGuiIO& io, std::unique_lock<std::mutex>& lock, const ImGuiStyle& st, bool& s_lineRightClick, uint64_t& s_lineMenuId, std::wstring& s_lineMenuText, bool& s_openLineMenu, bool& s_lineMenuOpen, const float& line, const float& lowerH) {
    if (Shown().chat) {
        static float scrollY = 0, scrollMax = 0, viewH = 1, dragOffset = 0;
        static bool atBottom = true;
        static int lastActiveTab = -999;
        const float h = std::max(line * 2, ImGui::GetContentRegionAvail().y - lowerH);
        const float sb = std::max(8.0f, st.ScrollbarSize * 0.8f);
        const ImVec2 track = ImGui::GetCursorScreenPos();
        float setScroll = -1;
        ImGui::InvisibleButton("##chatsb", ImVec2(sb, h));
        {
            const float grabH = scrollMax > 0 ? std::max(sb * 2, h * viewH / (viewH + scrollMax)) : h;
            const float grabY = track.y + (scrollMax > 0 ? (h - grabH) * scrollY / scrollMax : 0);
            if (ImGui::IsItemActivated()) {
                const float my = io.MousePos.y;
                dragOffset = my >= grabY && my <= grabY + grabH ? my - grabY : grabH * 0.5f;
            }
            if (ImGui::IsItemActive() && scrollMax > 0)
                setScroll = std::clamp((io.MousePos.y - dragOffset - track.y) / std::max(1.0f, h - grabH), 0.0f, 1.0f) * scrollMax;
            // On glass (subtitle mode) the bar shows only while the pointer is over the window, as GW2's own chat does.
            if (scrollMax > 0 && (!ProfileOn(2) || ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows))) {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(track, ImVec2(track.x + sb, track.y + h), ImGui::GetColorU32(ImGuiCol_ScrollbarBg), sb * 0.5f);
                const ImGuiCol gc = ImGui::IsItemActive() ? ImGuiCol_ScrollbarGrabActive
                                    : ImGui::IsItemHovered() ? ImGuiCol_ScrollbarGrabHovered : ImGuiCol_ScrollbarGrab;
                dl->AddRectFilled(ImVec2(track.x + 1, grabY), ImVec2(track.x + sb - 1, grabY + grabH), ImGui::GetColorU32(gc), sb * 0.5f);
            }
        }
        ImGui::SameLine(0, 4);
        ImVec4 chatBg = ImGui::GetStyleColorVec4(ImGuiCol_ChildBg);  // fades with the window, like everything but text
        chatBg.w = g_look.chatBg && !ProfileOn(2) ? chatBg.w * std::clamp(g_cfg.opacity, 0, 255) / 255.0f : 0.0f;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, chatBg);
        ImGui::BeginChild("chat", ImVec2(0, h), false, ImGuiWindowFlags_NoScrollbar);
        ImGui::PopStyleColor();
        ImGuiWindow* const cw = ImGui::GetCurrentWindow();
        static float s_rowsTop = 0, s_leftAt = 0, s_slide = 0;  // the first row's place, the scroll left last frame, the slide
        // The wheel, or anything else that scrolled up since last frame: no longer at the bottom. (Begin also cuts the
        // scroll back when the chat gets higher or shorter – then it stands at its end and stays there.)
        if (atBottom && cw->Scroll.y < s_leftAt - 1.0f && cw->Scroll.y < cw->ScrollMax.y - 4.0f) atBottom = false;
        const ImVec2 childP0 = ImGui::GetWindowPos();
        const ImVec2 childP1 = ImVec2(childP0.x + ImGui::GetWindowSize().x, childP0.y + ImGui::GetWindowSize().y);
        const bool chatHovered = ImGui::IsMouseHoveringRect(childP0, childP1);
        const float wheel = io.MouseWheel;
        if (chatHovered && wheel > 0.0f) {
            atBottom = false;
        }
        bool snapToBottom = false;  // another tab: its newest lines at once
        if (g_activeTab != lastActiveTab) {
            lastActiveTab = g_activeTab;
            atBottom = snapToBottom = true;
        }
        const WhisperTab* front = g_activeTab >= 0 && g_activeTab < static_cast<int>(g_tabs.size()) ? &g_tabs[g_activeTab] : nullptr;
        const gct::WhisperPeer* tabPeer = front && !front->custom ? &front->peer : nullptr;
        const uint32_t tabChannels = front && front->custom ? front->channels : 0xFFFFFFFFu;
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,  // the space between chat lines (Window tab: Line spacing) – also the info lines, so it shows at once
                            ImVec2(ImGui::GetStyle().ItemSpacing.x,
                                   ImGui::GetStyle().ItemSpacing.y * std::clamp(g_cfg.chatSpacing, 0, 100) / 100.0f - ImGui::GetFontSize() * 0.2f * (100 - std::clamp(g_cfg.chatSpacing, 0, 100)) / 100.0f));
        g_visibleLines.clear();
        const float rowWrap = ImGui::GetContentRegionAvail().x, rowFont = ImGui::GetFontSize();
        const float rowGap = ImGui::GetStyle().ItemSpacing.y;
        auto inThisTab = [&](const ChatLine& l) {
            if (tabPeer && (l.channel != Channel::Whisper || !tabPeer->Shows(l.speakerFolded, l.accountFolded))) return false;
            return (tabChannels & ChannelBit(l.channel)) != 0;  // a tab of your own: only its channels
        };
        // the original under a translation (your own line: what you typed), when chosen – part of the row's size
        auto typedOf = [&](const ChatLine& l) { return g_cfg.showOriginal && l.own ? OwnOriginal(l.text) : std::wstring(); };
        auto originalUnderOf = [&](const ChatLine& l, const std::wstring& ownTyped) {
            return g_cfg.showOriginal && ((!l.own && !l.translated.empty()) || !ownTyped.empty());
        };
        auto rowKnown = [&](const ChatLine& l) {  // measured for this width, font, spacing and text
            return l.rowH > 0 && l.rowW == rowWrap && l.rowFont == rowFont && l.rowGap == rowGap && l.rowLen == l.rowLenNow;
        };
        auto drawRow = [&](ChatLine& l) {
            const float y0 = ImGui::GetCursorPosY();
            const std::wstring ownTyped = typedOf(l);
            const bool originalUnder = originalUnderOf(l, ownTyped);
            ImGui::PushID(static_cast<int>(l.id));
            const float rowTop = ImGui::GetCursorScreenPos().y;
            const float rowX = ImGui::GetCursorPosX();
            if (!l.speaker.empty()) {  // the name in the channel colour – a click: whisper with that player in their tab
                LookText(l.speakerDisplay.c_str(), NameColorOf(l.channel), false);
                const bool canWhisper = !l.own || (l.outgoing && l.channel == Channel::Whisper);
                if (canWhisper && ImGui::IsItemHovered()) {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
                    ImGui::GetWindowDrawList()->AddLine(ImVec2(a.x, b.y - 1), ImVec2(b.x, b.y - 1),
                                                        ImGui::GetColorU32(NameColorOf(l.channel)));
                }
                if (canWhisper && ImGui::IsItemClicked()) OpenWhisperTab(l.speaker);
                ImGui::SameLine(0, ImGui::CalcTextSize(" ").x);
            }
            const std::wstring dispOrig = DisplayForOurFont(l.text, L"", LoadedFontCanDraw);
            const bool hidOrig = dispOrig != l.text;
            const std::wstring dispTrans =
                l.translated.empty() ? std::wstring() : DisplayForOurFont(l.translated, L"", LoadedFontCanDraw);
            const bool hidTrans = !l.translated.empty() && dispTrans != l.translated;
            const bool hideMain = l.translated.empty() ? hidOrig : hidTrans;
            const std::string u8hidMain = hideMain ? U8(l.translated.empty() ? dispOrig : dispTrans) : std::string();
            const std::string& u8shown = hideMain ? u8hidMain : (l.translated.empty() ? l.u8text : l.u8translated);
            const std::string u8origTip = hidOrig ? U8(dispOrig) : l.u8text;
            const auto& segs = l.translated.empty() ? l.textSegs : l.transSegs;
            bool clicked = false;
            LineHit hit;  // over every piece of the text
            if (hideMain || (segs.size() <= 1 && (segs.empty() || segs[0].kind == PieUI::ChatLinks::SegmentKind::Plain))) {
                if (!l.speaker.empty()) {
                    // Under its name one space in: one item, its wrapped lines start one space from the left, its first
                    // line padded past the name – so it measures and wraps like any line (a width change is exact).
                    // a space as it is drawn (ImGui::CalcTextSize rounds it up to a whole pixel: the padding fell short
                    // of a long name by its last letters, and the text covered the colon)
                    const float sp = std::max(1.0f, ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, 0.0f, " ").x);
                    const float hang = ImGui::GetCursorPosX() - rowX;  // the name and its space
                    const int pads = std::max(0, static_cast<int>(std::ceil((hang - sp) / sp)));
                    ImGui::SetCursorPosX(rowX + sp);
                    const float textX0 = ImGui::GetCursorScreenPos().x + pads * sp;
                    LookText((std::string(static_cast<size_t>(pads), ' ') + u8shown).c_str(), ColorOf(l.channel), true);
                    hit.AddLast();
                    const ImVec2 m = ImGui::GetIO().MousePos, a = ImGui::GetItemRectMin();
                    if (m.y < a.y + ImGui::GetTextLineHeight() && m.x < textX0)  // over the name: the name's, not the text's
                        hit.hovered = hit.clickL = hit.clickR = false;
                } else {
                    LookText(u8shown.c_str(), ColorOf(l.channel), true);
                    hit.AddLast();
                }
            } else {
                clicked = RichLine(segs, ColorOf(l.channel));
                hit.AddLast();
            }
            if (!l.translated.empty() && hit.hovered) {
                if (l.u8transNote.empty()) {
                    ImGui::SetTooltip("%s", u8origTip.c_str());
                } else {  // the original and what the translation check found
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted(u8origTip.c_str());
                    ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.35f, 1.0f), "%s", l.u8transNote.c_str());
                    ImGui::EndTooltip();
                }
            }
            if (!l.own && l.transPending && l.translated.empty()) {
                ImGui::SameLine(0, ImGui::CalcTextSize(" ").x);
                LookText("...", ImVec4(0.6f, 0.6f, 0.6f, 0.65f), false);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", UI(Tr(L"Translating …")).c_str());
            }
            // A click on the text of a line someone else wrote: just this line into your language.
            if (!l.own && hit.visible) {
                g_visibleLines.push_back(l.id);  // in view: "translate once" takes it
                if (g_nx.translateChat && !g_orchestrator.IsPaused() && l.translated.empty() && !l.transAttempted && !l.transPending && !IsGamerInterjection(l.text)) {
                    l.transPending = true;
                    g_worker.Post([id = l.id, t = l.text] { TranslateLine(id, t); });
                }
            }
            if (hit.clickR) s_lineRightClick = true;
            // A click on someone's text: into your reading language. A right-click: a small menu – the reading
            // language first, right under the pointer, any other language below; a click always translates.
            if (!clicked && !l.own && l.translated.empty() && hit.clickL && !ProfileOn(2)) {  // subtitles: a click writes
                l.transPending = true;
                g_worker.Post([id = l.id, t = l.text] { TranslateLine(id, t, true); });
            }
            if (!clicked && !l.own && hit.clickR) {
                s_lineMenuId = l.id;
                s_lineMenuText = l.text;
                s_openLineMenu = true;
            }
            if (!l.translated.empty() && !l.u8transNote.empty()) {  // a doubtful translation: marked, the reason on hover
                ImGui::SameLine(0, ImGui::CalcTextSize(" ").x);
                LookText("(?)", ImVec4(0.95f, 0.78f, 0.35f, 0.9f), false);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", l.u8transNote.c_str());
            }
            if (originalUnder) {
                const std::wstring& origW = ownTyped.empty() ? l.text : ownTyped;
                LookText(U8(DisplayForOurFont(origW, L"", LoadedFontCanDraw)).c_str(),
                         ImVec4(0.62f, 0.62f, 0.62f, 0.8f), true);
            }
            if (s_lineMenuOpen && l.id == s_lineMenuId) {  // the message whose menu is open: marked across its lines
                const float x0 = ImGui::GetWindowPos().x;
                ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(x0, rowTop),
                                                          ImVec2(x0 + ImGui::GetWindowWidth(), ImGui::GetCursorScreenPos().y),
                                                          ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.08f)));
            }
            ImGui::PopID();
            l.rowH = ImGui::GetCursorPosY() - y0;  // what it took, for the frames it is out of view
            l.rowW = rowWrap;
            l.rowFont = rowFont;
            l.rowGap = rowGap;
            l.rowLen = l.rowLenNow;
        };
        // The layout first, then the drawing – like a browser, whose scroll and layout reach the screen together. A row that
        // changed (a translation arrived, a new line, another width) is measured now by drawing it far above the window,
        // where nothing shows and nothing can be hit. So the height is right before the first row is drawn:
        // - at the bottom the chat stays at the bottom, in the same frame: no frame with the new text pushing the rows
        //   down and the next one pulling them up again (that was the jumping);
        // - scrolled up, the first row in view stays where it is (scroll anchoring), whatever changes above it.
        if (!cw->SkipItems) {
            float oldY = s_rowsTop, newY = s_rowsTop, shift = 0;
            bool anchored = false, anyRow = false;
            for (ChatLine& l : g_lines) {
                if (!inThisTab(l)) continue;
                anyRow = true;
                const std::wstring ownTyped = typedOf(l);
                l.rowLenNow = (l.translated.empty() ? l.text.size() : l.translated.size() + 1) +
                              (originalUnderOf(l, ownTyped) ? (ownTyped.empty() ? l.text.size() : ownTyped.size()) + 1 : 0);
                const float was = std::max(0.0f, l.rowH);
                if (!rowKnown(l)) {
                    const ImVec2 back = ImGui::GetCursorScreenPos();
                    ImGui::SetCursorScreenPos(ImVec2(back.x, std::floor(cw->Pos.y) - 8000.0f));
                    drawRow(l);
                    ImGui::SetCursorScreenPos(back);
                }
                if (!anchored && oldY + was > cw->Scroll.y) {  // the first row reaching into the view
                    anchored = true;
                    shift = newY - oldY;
                }
                oldY += was;
                newY += std::max(0.0f, l.rowH);
            }
            const float contentH = std::floor(newY - (anyRow ? rowGap : 0.0f));
            const float maxY = std::max(0.0f, contentH + cw->WindowPadding.y * 2.0f - cw->Size.y);
            const float now = cw->Scroll.y;
            float next;
            if (setScroll >= 0) {  // the scroll bar's handle in your hand
                next = std::min(setScroll, maxY);
                atBottom = setScroll >= maxY - 4;
            } else if (atBottom) {
                next = maxY;
                // A few lines more at the bottom slide in over a moment, like in a messenger; a long way (another tab, a
                // big jump) or a shorter chat at once.
                if (!snapToBottom && maxY > now && maxY - now <= cw->Size.y) {
                    if (std::fabs(s_slide - now) > 1.0f) s_slide = now;
                    s_slide += (maxY - s_slide) * (1.0f - std::exp(-io.DeltaTime * 18.0f));
                    if (maxY - s_slide >= 0.5f) next = std::floor(s_slide);
                }
            } else {
                next = std::clamp(now + shift, 0.0f, maxY);
            }
            if (next == maxY || !atBottom || setScroll >= 0) s_slide = next;
            ScrollNow(cw, std::floor(next));
            cw->ScrollMax.y = maxY;
        }
        RenderChatSourceNotice();  // reading Events: Chat is off or paused: why, and what you can do
        if (g_lines.empty() && InfoTexts()) {  // nothing arrived yet: what is ready, what is missing, where translation is switched on
            lock.unlock();
            RenderModules();
            lock.lock();
        }
        // Only what is in view is drawn: a measured line outside the view (plus a margin) is just a placeholder of its
        // height – the layout stays exactly the same.
        const float viewTop = ImGui::GetScrollY() - line * 4;
        const float viewBottom = ImGui::GetScrollY() + ImGui::GetWindowHeight() + line * 4;
        s_rowsTop = ImGui::GetCursorPosY();
        const bool fading = ProfileOn(2) && !SubtitleWriting();
        const ULONGLONG fadeNow = GetTickCount64();
        for (ChatLine& l : g_lines) {
            if (!inThisTab(l)) continue;
            const float y0 = ImGui::GetCursorPosY();
            const float fade = LineFade(l, fading, fadeNow);
            // out of view – or faded out on the glass: only its height, nothing to see or to click (no hand over a name
            // nobody sees)
            if (rowKnown(l) && (fade <= 0.0f || y0 + l.rowH < viewTop || y0 > viewBottom)) {
                ImGui::Dummy(ImVec2(1.0f, std::max(0.0f, l.rowH - rowGap)));
                continue;
            }
            if (fade < 1.0f) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * fade);
            drawRow(l);
            if (fade < 1.0f) ImGui::PopStyleVar();
        }
        ImGui::PopStyleVar();  // the chat's line spacing
        // A line's menu (right-click on someone's text): the reading language first, under the pointer; others below.
        if (s_openLineMenu) {
            ImGui::OpenPopup("##linemenu");
            s_openLineMenu = false;
        }
        if (ImGui::BeginPopup("##linemenu")) {
            ScopedPopupFont popupFont;
            std::wstring pick;
            std::wstring readName = ReadLang();
            for (const LangInfo& lg : Languages())
                if (PrimaryLang(lg.code) == ReadLang()) readName = LanguageName(lg.code);
            if (ImGui::MenuItem(UI(TrF(L"Translate into {1}", {readName})).c_str())) pick = ReadLang();
            if (ImGui::BeginMenu(UI(Tr(L"Into another language")).c_str())) {
                // every language of the active translator, by name, with a search field (step 2)
                const std::wstring got = PickLanguage("lineinto", OfferedLanguages(OfferNow()), g_cfg.writeLangs, L"");
                if (!got.empty()) {
                    pick = got;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndMenu();
            }
            // what happened when this line was translated: language, translator, protected parts, time, score
            std::string trace;
            for (const ChatLine& x : g_lines)
                if (x.id == s_lineMenuId) trace = x.u8trace;
            // the model's other versions of this line: a click takes one, the one shown takes its place
            for (ChatLine& x : g_lines) {
                if (x.id != s_lineMenuId || x.transVariants.empty()) continue;
                if (ImGui::BeginMenu(UI(Tr(L"Other versions")).c_str())) {
                    for (std::wstring& version : x.transVariants) {
                        if (!ImGui::MenuItem(U8(version).c_str())) continue;
                        std::swap(x.translated, version);
                        x.u8translated = U8(x.translated);
                        x.transSegs = PieUI::ChatLinks::SegmentLine(x.u8translated);
                        ImGui::CloseCurrentPopup();
                        break;
                    }
                    ImGui::EndMenu();
                }
                break;
            }
            // A word of this line as a GW2 term of your own (a short form the translator gets wrong, "comms"): never
            // translated from now on, and this line comes again with it. Gone again in Options → Technical.
            if (ImGui::BeginMenu(UI(Tr(L"A GW2 term – never translated")).c_str())) {
                std::vector<std::wstring> words;
                for (const Span& s : WordSpans(s_lineMenuText)) {
                    const std::wstring w = s_lineMenuText.substr(s.start, s.length);
                    if (w.size() < 2 || w.size() > 30 || std::all_of(w.begin(), w.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; })) continue;
                    if (std::find(words.begin(), words.end(), w) == words.end()) words.push_back(w);
                    if (words.size() >= 15) break;
                }
                for (const std::wstring& w : words) {
                    if (!ImGui::MenuItem(U8(w).c_str())) continue;
                    g_worker.Post([w, id = s_lineMenuId, t = s_lineMenuText, target = ReadLang()] {
                        if (g_spell) g_spell->AddTerm(w);
                        ForgetRepetitions();
                        TranslateLine(id, t, true, false, target);
                    });
                    SetStatus(TrF(L"Never translated from now on: {1}", {w}), 6000);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndMenu();
            }
            if (!trace.empty() && ImGui::BeginMenu(UI(Tr(L"What happened?")).c_str())) {
                ImGui::TextUnformatted(trace.c_str());
                ImGui::EndMenu();
            }
            if (!pick.empty()) {
                g_worker.Post([id = s_lineMenuId, t = s_lineMenuText, pick] { TranslateLine(id, t, true, false, pick); });
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        s_lineMenuOpen = ImGui::IsPopupOpen("##linemenu");
        scrollY = ImGui::GetScrollY();
        scrollMax = ImGui::GetScrollMaxY();
        viewH = ImGui::GetWindowHeight();
        if (!atBottom) atBottom = scrollY >= scrollMax - 4;  // scrolled back down: it follows again
        s_leftAt = scrollY;
        ImGui::EndChild();
    } else {  // no chat: the input stays the lowest line, the space above stays empty (see-through like the window)
        const float fill = ImGui::GetContentRegionAvail().y - lowerH;
        if (fill > 0) ImGui::Dummy(ImVec2(0, fill));
    }
}


}  // namespace kb
