// kb_state.cpp — shared state: settings of the window, languages, the translator, status.

#include "kb_shared.hpp"

namespace kb {

Layout g_layout;
NexusOptions g_nx;
TextLook g_look;
MyWords g_myWords;  // your own words and what they mean (<data>\my-words.txt), g_mu
uint32_t g_nextTabUid = 1;
std::map<std::wstring, std::wstring> g_playerLang;
std::wstring g_sendOverride;
std::vector<WhisperTab> g_tabs;
int g_activeTab = -1;  // -1: the whole chat
int g_selectTab = -2;  // a tab to select (-1: the chat; -2: none asked)
int g_tabSettings = -1, g_tabSettingsOpen = -1;  // a tab of your own whose settings are asked for / shown
std::wstring g_openLink;  // a link clicked in the chat: opened only after your yes
AddonDefinition_t g_def = {};
AddonAPI_t* g_api = nullptr;
HMODULE g_self = nullptr;
std::atomic<bool> g_disabled{false};  // set after an error: the add-on stays quiet for the rest of the session
std::atomic<bool> g_chatEventsLive{false};
std::atomic<bool> g_keyboardFree{true};  // ImGui does not hold the keyboard (keys reach the game)
std::atomic<int> g_fps{0};
bool g_show = true;
TestHooks g_test;
bool g_showOptions = false;  // our own options window (menu ≡ / the button on the Nexus page)
bool g_showDebugWindow = false;  // dedicated debug & diagnostic inspection window

bool IsCodePointer(const void* ptr) {
    if (!ptr) return false;
    const uintptr_t v = reinterpret_cast<uintptr_t>(ptr);
    if (v < 0x10000 || v >= 0x00007FFFFFFF0000ull) return false;
    MEMORY_BASIC_INFORMATION mbi = {};
    if (!VirtualQuery(ptr, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) return false;
    const DWORD p = mbi.Protect & 0xFF;
    return (p == PAGE_EXECUTE || p == PAGE_EXECUTE_READ ||
            p == PAGE_EXECUTE_READWRITE || p == PAGE_EXECUTE_WRITECOPY);
}

void Log(ELogLevel level, const char* text) {
    if (g_api && g_api->Log && IsCodePointer(reinterpret_cast<const void*>(g_api->Log))) {
        g_api->Log(level, kName, text);
    }
}

std::string U8(const std::wstring& w) { return ToUtf8(w); }
std::wstring W(const std::string& s) { return FromUtf8(s); }
size_t U8Offset(const std::wstring& w, size_t idx) { return ToUtf8(w.substr(0, std::min(idx, w.size()))).size(); }
// For drawing UI text: Nexus' font has no ≡ → … ▾ and curly quotes – plain stand-ins instead of "?".
std::string UI(std::wstring w) {
    static const std::pair<const wchar_t*, const wchar_t*> kSwap[] = {
        {L"\u2192", L">"}, {L"\u2190", L"<"}, {L"\u2261", L"menu"}, {L"\u25BE", L""}};
    for (const auto& [from, to] : kSwap)
        for (size_t at; (at = w.find(from)) != std::wstring::npos;) w.replace(at, std::wcslen(from), to);
    // Dashes, curly quotes, "\u2026" plain (our chat font lacks them: "ready \u2013 \u2026" showed "ready ? \u2026"); a direction mark or a
    // zero-width space would only be a "?" too.
    return ToUtf8(WithoutInvisible(PlainPunctuation(w)));
}
Worker g_worker;
SpellService* g_spell = nullptr;
SendGuard g_guard;
MumbleLink* g_mumbleWorker = nullptr;
std::mutex g_mu;
Config g_cfg;
std::shared_ptr<Translator> g_translator;
TranslationOrchestrator g_orchestrator;
std::deque<ChatLine> g_lines;
uint64_t g_nextId = 1;
bool g_scrollDown = false;
std::vector<std::wstring> g_context;  // speakers and words of the chat (offered as completions, never learned)
std::wstring g_ownName;               // your character (MumbleLink)
std::atomic<int> g_uiSize{-1};        // GW2 UI size (0 small .. 3 larger) from MumbleLink
std::atomic<uint32_t> g_mapId{0};     // current map ID (MumbleLink)
char g_input[1024] = {};
float g_wrapWidth = 0;  // the input's text width: soft line breaks ('\n' in place of a space) keep words whole
std::string g_flatInput;
std::wstring g_wFlatInput;
gct::Snippets g_snippets;
bool g_openSnippets = false;
std::string g_lastInput;
ULONGLONG g_inputChanged = 0;
uint64_t g_inputGen = 0;
std::wstring g_preview;  // the translation of the input
std::wstring g_back;     // ... and that translated back into your language (small, under it)
bool g_inMap = false;    // MumbleLink: a map is loaded (not character select, not a loading screen)
bool g_mapOpen = false;  // MumbleLink: the world map is open – the window steps aside like GW2's own chat
uint64_t g_previewGen = 0;
bool g_sendPending = false;  // Enter before the translation was there
std::wstring g_status;
ULONGLONG g_statusUntil = 0;
TypingHelp g_help;
bool g_helpRequested = false;
std::deque<std::wstring> g_history;
int g_historyPos = -1;
Channel g_channel = Channel::Unknown;  // Unknown = the channel active in GW2
std::wstring g_whisperTo;
int g_sendIdx = 0;                     // index into g_cfg.writeLangs; == size: send as written
bool g_sending = false;
ULONGLONG g_lastUse = 0;
bool g_gameChatOpen = false;  // GW2's chat line (MumbleLink), g_mu
bool g_focusInput = false;
PendingEdit g_edit;
int g_boxLines = 4;  // the input's lines this frame: your setting, more while a longer text waits to be sent
int g_pick = -1;  // Tab / Shift+Tab move through the word bar; Space writes the picked word
AutoUndo g_undo;
std::wstring g_finishedLast;  // learning mode: the last word when a space last followed it (see NoteSelfFix)
size_t g_finishedAt = 0;
bool g_voiceReady = false;       // the helper said "ready"
bool g_recording = false;        // a recording runs
std::wstring g_voiceLive;        // the live text while speaking (only drawn, never written into the input)
std::wstring g_lastDictated;     // the last recognized text as it came (learning misheard words on send)
std::wstring g_dictLang;         // its language
VoiceFixBook g_voiceFixes;       // learned misheard words (<data>\voice-fixes.txt), per language
bool g_autoSendPending = false;
bool g_userRecording = false;
std::string g_voicePending;        // a command that waits for the helper to be ready ("begin-auto", "listen")
bool g_listening = false;          // voice activation listens
std::wstring g_listenPausedFor;    // ... paused while this voice chat uses the microphone
bool g_listenWanted = false;       // we asked the helper to listen
bool g_listenUserOff = false;      // you switched voice activation off with the microphone (this session)
std::atomic<unsigned> g_pttVk{0}, g_pttMods{0};
std::atomic<bool> g_captureKey{false};
std::wstring g_capturedKey;        // the captured key, picked up by the options (g_mu)

void LoadLayout() {  // g_mu held
    auto get = [](const wchar_t* key, int def) { return static_cast<int>(GetPrivateProfileIntW(L"NexusWindow", key, def, g_cfg.iniPath.c_str())); };
    g_layout.chat = get(L"Chat", 1) != 0;
    g_layout.preview = get(L"Preview", 1) != 0;
    g_layout.words = get(L"WordBar", 1) != 0;
    g_layout.footer = get(L"Footer", 1) != 0;
    g_layout.header = get(L"Header", 1) != 0;
    g_layout.infoText = get(L"InfoText", 1) != 0;
    g_layout.minButton = get(L"MinButton", 1) != 0;
    g_layout.autoMinimize = get(L"AutoMinimize", 0) != 0;
    g_layout.minimizeAfter = std::clamp(get(L"MinimizeAfter", 10), 2, 120);
    g_layout.autoOpen = get(L"AutoOpen", 0) != 0;
    g_layout.inputLines = std::clamp(get(L"InputLines", 1), 1, 8);
    g_layout.previewLines = std::clamp(get(L"PreviewLines", 1), 1, 8);
    g_look.glow = get(L"Glow", 1) != 0;
    g_look.brightness = std::clamp(get(L"Brightness", 100), 60, 160);
    g_look.gameColors = get(L"GameColors", 1) != 0;
    g_look.chatBg = get(L"ChatBg", 1) != 0;
    {  // the window's theme ([NexusWindow] Theme; 1.9.5's OwnColours=0 meant Nexus' own)
        wchar_t theme[16] = {};
        GetPrivateProfileStringW(L"NexusWindow", L"Theme", L"", theme, 16, g_cfg.iniPath.c_str());
        const std::wstring t = theme;
        g_theme = t == L"nexus" ? Theme::Nexus : t == L"grey" ? Theme::Grey : t == L"cba" ? Theme::Cba  // "cyan": Slate
                : t.empty() && get(L"OwnColours", 1) == 0 ? Theme::Nexus : Theme::Slate;
    }
    {  // the CBA theme's colours, as a theme code (ASCII), and whether the text keeps its contrast by itself
        wchar_t code[128] = {};
        GetPrivateProfileStringW(L"NexusTheme", L"Colours", L"", code, 128, g_cfg.iniPath.c_str());
        if (!ParseThemeCode(ToUtf8(code), kRoleCount, &g_cbaRoles)) g_cbaRoles = CbaPreset(2);
        g_cbaAutoContrast = GetPrivateProfileIntW(L"NexusTheme", L"AutoContrast", 1, g_cfg.iniPath.c_str()) != 0;
    }
    auto nx = [](const wchar_t* key, int def) { return GetPrivateProfileIntW(L"Nexus", key, def, g_cfg.iniPath.c_str()) != 0; };
    g_nx.translateChat = nx(L"TranslateChat", 0);
    g_nx.snap = nx(L"Snap", 1);
    g_nx.setupDone = nx(L"SetupDone", 0);
    g_nx.voiceFallback = nx(L"VoiceFallback", 1);
    {  // what the waypoint click (2.2.0 – 2.3.0) left in the ini goes (only written when it is there)
        wchar_t old[8] = {};
        static const std::pair<const wchar_t*, const wchar_t*> kGone[] = {
            {L"Nexus", L"WaypointPoiClick"}, {L"Nexus", L"NativeWaypointClick"}, {L"Nexus", L"WpBefore"},
            {L"Nexus", L"WpHeld"}, {L"Nexus", L"WpAfter"}};
        for (const auto& [section, key] : kGone)
            if (GetPrivateProfileStringW(section, key, L"\x01", old, 8, g_cfg.iniPath.c_str()) != 1 || old[0] != 1)
                WritePrivateProfileStringW(section, key, nullptr, g_cfg.iniPath.c_str());
    }
    {
        wchar_t lang[8] = {};
        GetPrivateProfileStringW(L"Nexus", L"NamesLang", L"", lang, 8, g_cfg.iniPath.c_str());
        g_nx.namesLang = lang;
    }
    LoadVoiceRuns();
}
void SaveNexusOptions() {  // g_mu held
    g_cfg.SaveBool(L"Nexus", L"TranslateChat", g_nx.translateChat);
    g_cfg.SaveBool(L"Nexus", L"Snap", g_nx.snap);
    g_cfg.SaveBool(L"Nexus", L"SetupDone", g_nx.setupDone);
    g_cfg.SaveBool(L"Nexus", L"VoiceFallback", g_nx.voiceFallback);
    g_cfg.SaveValue(L"Nexus", L"NamesLang", g_nx.namesLang);
}
void SaveLayout() {  // g_mu held
    g_cfg.SaveValue(L"NexusWindow", L"Chat", g_layout.chat ? L"1" : L"0");
    g_cfg.SaveValue(L"NexusWindow", L"Preview", g_layout.preview ? L"1" : L"0");
    g_cfg.SaveValue(L"NexusWindow", L"WordBar", g_layout.words ? L"1" : L"0");
    g_cfg.SaveValue(L"NexusWindow", L"Footer", g_layout.footer ? L"1" : L"0");
    g_cfg.SaveValue(L"NexusWindow", L"Header", g_layout.header ? L"1" : L"0");
    g_cfg.SaveValue(L"NexusWindow", L"InfoText", g_layout.infoText ? L"1" : L"0");
    g_cfg.SaveValue(L"NexusWindow", L"MinButton", g_layout.minButton ? L"1" : L"0");
    g_cfg.SaveValue(L"NexusWindow", L"AutoMinimize", g_layout.autoMinimize ? L"1" : L"0");
    g_cfg.SaveValue(L"NexusWindow", L"MinimizeAfter", std::to_wstring(g_layout.minimizeAfter));
    g_cfg.SaveValue(L"NexusWindow", L"AutoOpen", g_layout.autoOpen ? L"1" : L"0");
    g_cfg.SaveValue(L"NexusWindow", L"InputLines", std::to_wstring(g_layout.inputLines));
    g_cfg.SaveValue(L"NexusWindow", L"PreviewLines", std::to_wstring(g_layout.previewLines));
    g_cfg.SaveValue(L"NexusWindow", L"Glow", g_look.glow ? L"1" : L"0");
    g_cfg.SaveValue(L"NexusWindow", L"Brightness", std::to_wstring(g_look.brightness));
    g_cfg.SaveValue(L"NexusWindow", L"GameColors", g_look.gameColors ? L"1" : L"0");
    g_cfg.SaveValue(L"NexusWindow", L"ChatBg", g_look.chatBg ? L"1" : L"0");
    g_cfg.SaveValue(L"NexusWindow", L"Theme", ThemeKey(g_theme));
    if (g_cbaRoles.size() == kRoleCount) g_cfg.SaveValue(L"NexusTheme", L"Colours", W(MakeThemeCode(g_cbaRoles)));
    g_cfg.SaveValue(L"NexusTheme", L"AutoContrast", g_cbaAutoContrast ? L"1" : L"0");
}

void SetStatus(const std::wstring& s, ULONGLONG ms) {  // g_mu held
    g_status = s;
    g_statusUntil = ms ? GetTickCount64() + ms : 0;
}

const std::wstring& TypingLang() {  // the language you write and read in
    static std::wstring s_cachedTyping;
    static std::wstring s_lastWriteIn;
    if (s_lastWriteIn != g_cfg.writeIn || s_cachedTyping.empty()) {
        s_lastWriteIn = g_cfg.writeIn;
        if (!g_cfg.writeIn.empty()) {
            s_cachedTyping = PrimaryLang(g_cfg.writeIn);
        } else {
            wchar_t locale[LOCALE_NAME_MAX_LENGTH] = {};
            GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH);
            s_cachedTyping = PrimaryLang(locale);
        }
    }
    return s_cachedTyping;
}
std::wstring TypingLocale() {
    if (g_cfg.writeIn.empty()) {
        static const std::wstring s_defaultLocale = [] {
            wchar_t locale[LOCALE_NAME_MAX_LENGTH] = {};
            GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH);
            return std::wstring(locale);
        }();
        return s_defaultLocale;
    }
    const std::wstring p = ToLowerAscii(PrimaryLang(g_cfg.writeIn));
    return p + L"-" + (p == L"en" ? std::wstring(L"US") : ToUpperAscii(p));
}
bool g_inputSpoken = false;

bool VoiceFirst() { return g_inputSpoken && g_cfg.voiceEnabled && g_cfg.voiceTranslate; }

// The message's route (core/send_route: one pure function, its equations in core_tests). g_mu held.
gct::SendRoute CurrentRoute(bool asSpokenWithSwitch = false) {
    gct::SendState s;
    s.spoken = asSpokenWithSwitch || g_inputSpoken;
    s.voiceOn = asSpokenWithSwitch || g_cfg.voiceEnabled;
    s.voiceTranslate = asSpokenWithSwitch || g_cfg.voiceTranslate;
    s.tabLang = g_sendOverride;
    s.sendIdx = g_sendIdx;
    s.writeLangs = &g_cfg.writeLangs;
    s.dictLang = g_dictLang;
    s.typingLang = TypingLang();
    return gct::RouteMessage(s);
}

// What you say goes into ("Translate what I say"): the language at "Send" – there "as written": the first of yours.
std::wstring VoiceLang() { return CurrentRoute(true).lang; }
bool SendOriginal() { return CurrentRoute().lang.empty(); }  // what you said, the switch on: it has priority
std::wstring SendLang() { return CurrentRoute().lang; }
// A detected language as the translators take it for sending ("en" -> "EN-GB", "pt" -> "PT-BR", "zh" -> "ZH-HANS").
std::wstring SendCodeFor(const std::wstring& detected) {
    const std::wstring p = ToUpperAscii(PrimaryLang(detected));
    if (p == L"EN") return L"EN-GB";
    if (p == L"PT") return L"PT-BR";
    if (p == L"ZH") return L"ZH-HANS";
    return p;
}
bool Translating() { return CurrentRoute().translating; }
const std::wstring& ComposePrefix() {
    static std::wstring s_cachedPrefix;
    static Channel s_cachedChannel = Channel::Unknown;
    static std::wstring s_cachedWhisper;
    if (s_cachedChannel != g_channel || (g_channel == Channel::Whisper && s_cachedWhisper != g_whisperTo)) {
        s_cachedChannel = g_channel;
        s_cachedWhisper = g_whisperTo;
        if (g_channel == Channel::Whisper) {
            s_cachedPrefix = g_whisperTo.empty() ? L"/r " : L"/w " + g_whisperTo + L", ";
        } else if (const wchar_t* cmd = ChannelCommand(g_channel)) {
            s_cachedPrefix = std::wstring(cmd) + L" ";
        } else {
            s_cachedPrefix.clear();
        }
    }
    return s_cachedPrefix;
}

std::shared_ptr<Translator> MakeTranslator(const Config& c) {  // the engine you set, else the best key you have
    if (c.engine == Engine::Basic) return MakeMyMemoryTranslator(c.basicEmail);
    if (c.engine == Engine::DeepL && !c.deeplKey.empty()) return MakeDeepLTranslator(c.deeplKey);
    if (c.engine == Engine::Google && !c.googleKey.empty()) return MakeGoogleTranslator(c.googleKey);
    if (c.engine == Engine::Microsoft && !c.msKey.empty()) return MakeMicrosoftTranslator(c.msKey, c.msRegion);
    if (c.engine == Engine::Libre && !c.libreUrl.empty()) return MakeLibreTranslator(c.libreUrl, c.libreKey);
    if (c.engine == Engine::Local) return MakeLocalMtTranslator(AddonsDir() + L"GW2Keyboard\\gw2kb_translate.exe", c.dataDir);
    if (c.engine == Engine::Nllb) return MakeLocalMtTranslator(AddonsDir() + L"GW2Keyboard\\gw2kb_translate.exe", c.dataDir, true /*forceNllb*/);
    if (!c.deeplKey.empty()) return MakeDeepLTranslator(c.deeplKey);
    if (!c.googleKey.empty()) return MakeGoogleTranslator(c.googleKey);
    if (!c.msKey.empty()) return MakeMicrosoftTranslator(c.msKey, c.msRegion);
    if (!c.libreUrl.empty()) return MakeLibreTranslator(c.libreUrl, c.libreKey);
    return MakeMyMemoryTranslator(c.basicEmail);
}

bool UsingMyMemory() { return g_translator && g_translator->Name().rfind(L"MyMemory", 0) == 0; }  // g_mu held

std::wstring Today() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t day[16];
    swprintf(day, 16, L"%04u%02u%02u", st.wYear, st.wMonth, st.wDay);
    return day;
}

void CountMyMemory(size_t chars) {  // the free contingent of a day (g_mu held)
    if (!UsingMyMemory() || chars == 0) return;
    const std::wstring day = Today();
    if (g_cfg.myMemoryDay != day) {
        g_cfg.myMemoryDay = day;
        g_cfg.myMemoryUsed = 0;
        g_cfg.SaveValue(L"Basic", L"UsedDay", day);
    }
    g_cfg.myMemoryUsed += static_cast<int>(chars);
    g_cfg.SaveValue(L"Basic", L"UsedChars", std::to_wstring(g_cfg.myMemoryUsed));
}

static std::wstring Kilo(int v) {  // 25, 4.9k
    wchar_t b[16];
    if (v < 1000) swprintf(b, 16, L"%d", v);
    else swprintf(b, 16, L"%.1fk", v / 1000.0);
    return b;
}
static int MyMemoryLimit() { return Trim(g_cfg.basicEmail).empty() ? 5000 : 50000; }  // characters a day (g_mu held)
static int MyMemoryUsedToday() { return g_cfg.myMemoryDay == Today() ? g_cfg.myMemoryUsed : 0; }

std::wstring MyMemoryQuotaText() {  // "MyMemory 25 / 5.0k today" (g_mu held)
    if (!UsingMyMemory()) return {};
    return TrF(L"MyMemory {1} / {2} today", {Kilo(std::min(MyMemoryUsedToday(), MyMemoryLimit())), Kilo(MyMemoryLimit())});
}

std::wstring MyMemoryLeftText() {  // "4.9k left today" beside "Translate visible chat once" (g_mu held)
    if (!UsingMyMemory()) return {};
    return TrF(L"{1} left today", {Kilo(std::max(0, MyMemoryLimit() - MyMemoryUsedToday()))});
}

// Whether the translator has a contingent (or costs per character): every one but LibreTranslate – your own server
// has no limit, so the chat's automatic translation is not braked for it (g_mu held).
bool LimitedContingent() {
    if (!g_translator) return true;
    const std::wstring name = g_translator->Name();
    return name != L"LibreTranslate" && name != L"OPUS-MT (local)";
}

// A language's own name as the font in use draws it, else its English name – never a "?" (a font without those
// letters made "Tieng Viet" or "Cestina" look misspelled, user 2026-10-22). Render thread.
std::wstring NativeName(const std::wstring& code) {
    const LangInfo* l = FindLanguage(code);
    if (!l) return ToUpperAscii(code);
    if (ImFont* f = ImGui::GetCurrentContext() ? ImGui::GetFont() : nullptr)
        for (const wchar_t* c = l->native; *c; ++c)
            if (*c >= 0x80 && !f->FindGlyphNoFallback(static_cast<ImWchar>(*c))) return l->english;
    return l->native;
}

const std::wstring& AddonsDir() {  // the folder our DLL lies in: <GW2>\addons\ .
    static const std::wstring s_addonsDir = [] {
        wchar_t path[MAX_PATH] = {};
        GetModuleFileNameW(g_self, path, MAX_PATH);
        std::wstring p = path;
        return p.substr(0, p.find_last_of(L"\\/") + 1);
    }();
    return s_addonsDir;
}

std::wstring KeyboardDir() { return AddonsDir() + L"GW2Keyboard"; }
bool g_micLocked = false;  // right-click on the microphone: on until the next right-click
// The two window profiles (menu and Options → Window → Window layout): presets of the window's parts. A profile is on
// exactly while the parts are what it says – change one part and it is your own layout again (its check goes off).
// Switching a profile off brings back the layout you had before it (this session; else every part shown).
Layout g_layoutBefore;
bool g_haveBefore = false;
bool ProfileOn(int profile) {  // 0 compact view, 1 mini mode (g_mu held)
    const Layout& l = g_layout;
    return profile == 0 ? l.header && l.chat && !l.preview && !l.words && l.footer
                        : !l.header && !l.chat && !l.preview && !l.words && !l.footer && !l.minButton;
}
void SetProfile(int profile, bool on) {  // g_mu held
    Layout& l = g_layout;
    if (on) {
        if (!ProfileOn(0) && !ProfileOn(1)) {  // your own layout: kept to come back to
            g_layoutBefore = l;
            g_haveBefore = true;
        }
        if (profile == 0) {  // compact: header, chat, input, footer – no word bar, no preview
            l.header = l.chat = l.footer = true;
            l.preview = l.words = false;
        } else {  // mini: nothing but the input
            l.header = l.chat = l.preview = l.words = l.footer = l.minButton = false;
        }
    } else if (ProfileOn(profile)) {
        const Layout& b = g_layoutBefore;
        const bool back = g_haveBefore && !(profile == 0 ? b.header && b.chat && !b.preview && !b.words && b.footer
                                                         : !b.header && !b.chat && !b.footer);
        l.header = back ? b.header : true;
        l.chat = back ? b.chat : true;
        l.preview = back ? b.preview : true;
        l.words = back ? b.words : true;
        l.footer = back ? b.footer : true;
        l.minButton = back ? b.minButton : true;
        g_haveBefore = false;
    }
    SaveLayout();
}
Layout Lay() { return g_layout; }

// Remove everything of GW2Chatlator (Options → Technical, at the bottom, after a yes): the voice helper ends, nothing is
// written any more (settings, learned words, my words, voice fixes), the worker drops the spelling without saving it,
// then our whole folder <addons>\GW2Keyboard\ goes – and Events: Chat too when asked (it is its own add-on). The DLL
// itself cannot remove itself while it runs: the notice says the last step (Nexus → Addons → Uninstall).
bool g_wiped = false;
std::atomic<int> g_wipeDone{0};  // 0 not asked, 1 removed, 2 something was left
void RemoveEverythingNow(bool chatEventsToo, bool decoderToo) {  // game thread
    UnloadVoiceModule();
    std::shared_ptr<Translator> localTranslator;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        g_wiped = true;
        g_show = false;
        g_showOptions = false;
        g_showDebugWindow = false;
        localTranslator = g_translator;
        g_translator.reset();
    }
    Config::BlockWrites();
    if (chatEventsToo) RemoveChatEventsNow();
    if (decoderToo) RemoveDecoderRingNow();
    g_worker.Post([dir = KeyboardDir(), localTranslator] {
        gct::StopLocalMtTranslator(localTranslator);
        if (g_spell) {  // the learned words would be saved on unload – they are what we remove
            delete g_spell;
            g_spell = nullptr;
        }
        bool ok = false;
        for (int i = 0; i < 10 && !ok; ++i) {
            ok = RemoveFolder(dir);
            if (!ok) Sleep(300);
        }
        g_wipeDone = ok ? 1 : 2;
    });
}

// Settings changed on the game thread reach the ini on the worker, at most every half second (a slider dragged writes
// once). A job dropped in a flood is posted again while writes still wait.
void KickSave() {
    static ULONGLONG s_posted = 0;
    if (GetTickCount64() - s_posted < 500 || !Config::WritesPending()) return;
    s_posted = GetTickCount64();
    g_worker.Post([] { Config::FlushWrites(); });
}

namespace {
struct CostClock {  // render thread only (the line costs are atomics: chat lines may come from another thread)
    LARGE_INTEGER freq{}, frameStart{}, partStart{};
    int part = kCostPre;
    double frame[kCostParts] = {};
    FrameCosts avg;
    std::atomic<long long> lineTicks{0}, lines{0};
} g_cost;

double TicksToMs(long long t) { return 1000.0 * static_cast<double>(t) / static_cast<double>(g_cost.freq.QuadPart); }
}  // namespace

CostFrame::CostFrame() {
    if (!g_cost.freq.QuadPart) QueryPerformanceFrequency(&g_cost.freq);
    QueryPerformanceCounter(&g_cost.frameStart);
    g_cost.partStart = g_cost.frameStart;
    g_cost.part = kCostPre;
    for (double& f : g_cost.frame) f = 0;
}

CostFrame::~CostFrame() {
    CostMark(kCostPre);  // closes the running part
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    constexpr double a = 0.05;  // rolling average over the last ~20 frames
    const double total = TicksToMs(now.QuadPart - g_cost.frameStart.QuadPart);
    for (int i = 0; i < kCostParts; ++i) {
        g_cost.avg.ms[i] += a * (g_cost.frame[i] - g_cost.avg.ms[i]);
        g_cost.avg.sumMs[i] += g_cost.frame[i];
    }
    g_cost.avg.totalMs += a * (total - g_cost.avg.totalMs);
    g_cost.avg.sumTotalMs += total;
    ++g_cost.avg.frames;
}

void CostReset() {
    for (double& v : g_cost.avg.sumMs) v = 0;
    g_cost.avg.sumTotalMs = 0;
    g_cost.avg.frames = 0;
}

void CostMark(int part) {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    g_cost.frame[g_cost.part] += TicksToMs(now.QuadPart - g_cost.partStart.QuadPart);
    g_cost.partStart = now;
    g_cost.part = part;
}

void CostLine(double us) {
    g_cost.lineTicks += static_cast<long long>(us * 1000.0);  // in nanoseconds
    ++g_cost.lines;
}

FrameCosts CostsNow() {
    FrameCosts c = g_cost.avg;
    c.lines = g_cost.lines;
    c.lineUs = c.lines ? static_cast<double>(g_cost.lineTicks) / 1000.0 / static_cast<double>(c.lines) : 0;
    return c;
}

}  // namespace kb
