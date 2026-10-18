// kb_logic.cpp — state, worker jobs, voice, chat and the input's logic.

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
std::wstring LinkHost(const std::wstring& url) {  // "https://www.example.org/a?b" -> "www.example.org"
    size_t a = url.find(L"://");
    a = a == std::wstring::npos ? 0 : a + 3;
    const size_t b = url.find_first_of(L"/?#:", a);
    return url.substr(a, b == std::wstring::npos ? std::wstring::npos : b - a);
}
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

void Log(ELogLevel level, const char* text) {
    if (g_api && g_api->Log) g_api->Log(level, kName, text);
}

std::string U8(const std::wstring& w) { return ToUtf8(w); }
std::wstring W(const std::string& s) { return FromUtf8(s); }
size_t U8Offset(const std::wstring& w, size_t idx) { return ToUtf8(w.substr(0, std::min(idx, w.size()))).size(); }
// For drawing UI text: Nexus' font has no ≡ → … ▾ and curly quotes – plain stand-ins instead of "?".
std::string UI(std::wstring w) {
    static const std::pair<const wchar_t*, const wchar_t*> kSwap[] = {
        {L"\u2026", L"..."}, {L"\u2192", L">"}, {L"\u2190", L"<"}, {L"\u2261", L"menu"}, {L"\u25BE", L""},
        {L"\u201C", L"\""}, {L"\u201D", L"\""}, {L"\u201E", L"\""}, {L"\u2018", L"'"}, {L"\u2019", L"'"}};
    for (const auto& [from, to] : kSwap)
        for (size_t at; (at = w.find(from)) != std::wstring::npos;) w.replace(at, std::wcslen(from), to);
    return ToUtf8(w);
}
Worker g_worker;
SpellService* g_spell = nullptr;
SendGuard g_guard;
MumbleLink* g_mumbleWorker = nullptr;
std::mutex g_mu;
Config g_cfg;
std::shared_ptr<Translator> g_translator;
std::deque<ChatLine> g_lines;
uint64_t g_nextId = 1;
bool g_scrollDown = false;
std::vector<std::wstring> g_context;  // speakers and words of the chat (offered as completions, never learned)
std::wstring g_ownName;               // your character (MumbleLink)
std::atomic<int> g_uiSize{-1};        // GW2 UI size (0 small .. 3 larger) from MumbleLink
std::atomic<uint32_t> g_mapId{0};     // current map ID (MumbleLink)
char g_input[1024] = {};
float g_wrapWidth = 0;  // the input's text width: soft line breaks ('\n' in place of a space) keep words whole
// The input as it is meant: one line. A soft break at a space is '\n' (it stands for the space); a word longer than
// the line (a code, a link) is broken by "\r\n" (it stands for nothing). '\r' never comes from you – the filter drops it.
std::string Flat(const char* s) {
    std::string f;
    for (const char* p = s; *p; ++p) {
        if (*p == '\r') {
            if (p[1] == '\n') ++p;  // a break inside a word: nothing
            continue;
        }
        f += *p == '\n' ? ' ' : *p;
    }
    return f;
}
std::wstring g_teachWord;  // the word a right-click hit (the learning window starts with it)

// Where byte `flatByte` of Flat(raw) stands in `raw` (the input with its soft breaks).
size_t RawOffset(const char* raw, size_t rawLen, size_t flatByte) {
    size_t flat = 0, i = 0;
    while (i < rawLen && flat < flatByte) {
        if (raw[i] == '\r') {
            i += (i + 1 < rawLen && raw[i + 1] == '\n') ? 2 : 1;
            continue;
        }
        ++flat;
        ++i;
    }
    while (i < rawLen && raw[i] == '\r') i += (i + 1 < rawLen && raw[i + 1] == '\n') ? 2 : 1;  // after a word break
    return i;
}
std::string g_flatInput;
std::wstring g_wFlatInput;
std::string g_lastInput;
ULONGLONG g_inputChanged = 0;
uint64_t g_inputGen = 0;
std::wstring g_preview;  // the translation of the input
std::wstring g_back;     // ... and that translated back into your language (small, under it)
bool g_inMap = false;    // MumbleLink: a map is loaded (not character select, not a loading screen)
bool g_mapOpen = false;  // MumbleLink: the world map is open – the window steps aside like GW2's own chat
ULONGLONG g_showOnMapUntil = 0;  // except while it names a waypoint's spot on the map (the emergency way)
uint64_t g_previewGen = 0;
bool g_sendPending = false;  // Enter before the translation was there
std::wstring g_status;
ULONGLONG g_statusUntil = 0;
ULONGLONG g_suppressChatFocusUntil = 0;
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
std::string g_voicePending;        // a command that waits for the helper to be ready ("begin-auto", "listen")
bool g_listening = false;          // voice activation listens
std::wstring g_listenPausedFor;    // ... paused while this voice chat uses the microphone
bool g_listenWanted = false;       // we asked the helper to listen
bool g_listenUserOff = false;      // you switched voice activation off with the microphone (this session)
std::atomic<unsigned> g_pttVk{0}, g_pttMods{0};
std::atomic<bool> g_captureKey{false};
std::wstring g_capturedKey;        // the captured key, picked up by the options (g_mu)
std::map<std::wstring, VoiceRun> g_voiceRuns;  // g_mu
void LoadVoiceRuns() {  // g_mu held
    const VoiceCatalog catalog = LoadVoiceCatalog(g_cfg.dataDir);
    for (const VoiceModelInfo& m : catalog.models) {
        wchar_t buf[64] = {};
        GetPrivateProfileStringW(L"VoiceStats", W(m.id).c_str(), L"", buf, 64, g_cfg.iniPath.c_str());
        VoiceRun r;
        if (swscanf_s(buf, L"%d,%d,%d,%d", &r.peak, &r.rms, &r.rec, &r.maxPeak) >= 3) g_voiceRuns[W(m.id)] = r;
    }
}

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
    g_layout.inputLines = std::clamp(get(L"InputLines", 4), 1, 8);
    g_layout.previewLines = std::clamp(get(L"PreviewLines", 3), 1, 8);
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
bool SendOriginal() {
    if (!g_sendOverride.empty()) return g_sendOverride == L"=";
    return g_sendIdx >= static_cast<int>(g_cfg.writeLangs.size());
}
const std::wstring& SendLang() {
    static const std::wstring s_empty;
    if (!g_sendOverride.empty()) return g_sendOverride == L"=" ? s_empty : g_sendOverride;
    return (SendOriginal() || g_sendIdx < 0 || g_sendIdx >= static_cast<int>(g_cfg.writeLangs.size()))
               ? s_empty : g_cfg.writeLangs[g_sendIdx];
}
// A detected language as the translators take it for sending ("en" -> "EN-GB", "pt" -> "PT-BR", "zh" -> "ZH-HANS").
std::wstring SendCodeFor(const std::wstring& detected) {
    const std::wstring p = ToUpperAscii(PrimaryLang(detected));
    if (p == L"EN") return L"EN-GB";
    if (p == L"PT") return L"PT-BR";
    if (p == L"ZH") return L"ZH-HANS";
    return p;
}
bool Translating() {
    if (SendOriginal()) return false;
    const std::wstring& send = SendLang();
    if (send.empty()) return false;
    return PrimaryLang(send) != TypingLang();
}
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

std::wstring MyMemoryQuotaText() {  // "MyMemory 25 / 5.0k today" (g_mu held)
    if (!UsingMyMemory()) return {};
    const int used = g_cfg.myMemoryDay == Today() ? g_cfg.myMemoryUsed : 0;
    const int limit = Trim(g_cfg.basicEmail).empty() ? 5000 : 50000;
    auto k = [](int v) {
        wchar_t b[16];
        if (v < 1000) swprintf(b, 16, L"%d", v);
        else swprintf(b, 16, L"%.1fk", v / 1000.0);
        return std::wstring(b);
    };
    return TrF(L"MyMemory {1} / {2} today", {k(std::min(used, limit)), k(limit)});
}

// ---------------------------------------------------------------------------------------------------------------
// Worker jobs.

void InitSpelling() {
    Config c;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        c = g_cfg;
    }
    g_spell = new SpellService();
    wchar_t locale[LOCALE_NAME_MAX_LENGTH] = {};
    GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH);
    std::wstring typing;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        typing = TypingLocale();
    }
    if (c.spellEnabled) {
        std::vector<std::wstring> tags = SpellTagCandidates(PrimaryLang(typing), typing);
        if (c.writeIn.empty())
            for (const std::wstring& t : SpellTagCandidates(PrimaryLang(locale), locale)) tags.push_back(t);
        g_spell->Init(tags, c.UserWordsPath());
    }
    EnsureDir(c.LearnedDir());
    g_spell->UseLearnedLanguage(PrimaryLang(typing), c.LearnedDir());
    g_spell->SetLearnChoices(c.learnWords);
    g_mumbleWorker = new MumbleLink();
}

void FreeWorkerState() {
    if (g_spell) {
        g_spell->SaveLearned();
        delete g_spell;
        g_spell = nullptr;
    }
    delete g_mumbleWorker;
    g_mumbleWorker = nullptr;
}

// A chat line into your language, when its language is sure and not yours (nor one you understand) – only while
// "Translate the chat" is on; `asked` (a click on the line) translates it anyway, whatever its language seems to be.
void TranslateLine(uint64_t id, std::wstring text, bool asked, bool once) {
    std::shared_ptr<Translator> tr;
    std::wstring target;
    std::vector<std::wstring> understood;
    bool skipTyping = true;
    ProtectedText p = ProtectForTranslation(text, nullptr, g_spell ? &g_spell->KeepWords() : &BuiltinKeepWords());
    if (!HasTranslatableText(p.segments)) return;
    std::wstring detected = SureLanguage(text, DetectLanguage(text));  // on this PC (Windows), no translator asked
    {
        std::lock_guard<std::mutex> lock(g_mu);
        if (!detected.empty())  // what this player writes in (answering them in it)
            for (const ChatLine& l : g_lines)
                if (l.id == id && !l.speaker.empty()) g_playerLang[CaseFold(Trim(l.speaker))] = detected;
        if (!g_nx.translateChat && !asked && !once) return;  // once / a click: whatever "Translate the chat" says
        tr = g_translator;
        target = TypingLang();
        understood = g_cfg.understoodLangs;
        skipTyping = g_cfg.skipTypingLang;
    }
    if (!tr) return;
    if ((asked || once) && detected.empty()) detected = DetectLanguage(text);
    if (once) {  // independent of every chat setting: into the language you write in – unless it already is in it
        if (!detected.empty() && PrimaryLang(detected) == PrimaryLang(target)) return;
    } else if (!asked) {
        if (detected.empty()) return;
        if (skipTyping && PrimaryLang(detected) == PrimaryLang(target)) return;
        for (const std::wstring& u : understood)
            if (PrimaryLang(u) == PrimaryLang(detected)) return;
    }
    {
        std::lock_guard<std::mutex> lock(g_mu);
        CountMyMemory(CodePointCount(text));
    }
    const TranslateResult r = tr->Translate(p.segments, SourceCode(detected), target);
    if (!r.ok || r.text.empty()) return;
    const std::wstring t = SanitizeChatText(r.text);
    std::lock_guard<std::mutex> lock(g_mu);
    for (ChatLine& l : g_lines) {
        if (l.id == id && NormalizeForCompare(t) != NormalizeForCompare(l.text)) {
            l.translated = t;
            l.u8translated = U8(l.translated);
            l.transSegs = PieUI::ChatLinks::SegmentLine(l.u8translated);
        }
    }
}
std::vector<uint64_t> g_visibleLines;

// What can be seen of the chat, into the language you write in (the chat is read in it) – when "Translate the chat" is
// switched on, when the language you write in changes, or once from the menu. Every call starts afresh: a translation
// into another language is cleared and done again in the language you write in now. `once` (the menu) follows none of
// the chat settings: everything not already in the language you write in.
int TranslateRecentLines(bool once) {  // g_mu held; how many lines went to the translator
    int n = 0;
    for (ChatLine& l : g_lines) {
        if (l.own || l.speaker.empty() || std::find(g_visibleLines.begin(), g_visibleLines.end(), l.id) == g_visibleLines.end())
            continue;
        l.translated.clear();
        l.u8translated.clear();
        l.transSegs.clear();
        g_worker.Post([id = l.id, t = l.text, once] { TranslateLine(id, t, false, once); });
        ++n;
    }
    return n;
}

// The input into the chat's language.
void TranslateInput(uint64_t gen, std::wstring text) {
    std::shared_ptr<Translator> tr;
    std::wstring target, typing;
    bool translating;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        tr = g_translator;
        target = SendLang();
        typing = TypingLang();
        translating = Translating();
    }
    const ChatSplit split = SplitChatCommand(text);
    const std::wstring body = split.prefix.empty() ? text : split.body;
    std::wstring result = body;
    std::wstring error;
    if (translating && tr) {
        std::wstring expanded;  // your own words replaced by what they mean (only for the translator)
        {
            std::lock_guard<std::mutex> lock(g_mu);
            expanded = g_myWords.Expand(body);
        }
        ProtectedText p = ProtectForTranslation(expanded, nullptr, g_spell ? &g_spell->KeepWords() : &BuiltinKeepWords());
        if (HasTranslatableText(p.segments)) {
            std::wstring source = DetectLanguage(body);
            if (source.empty()) source = typing;
            {
                std::lock_guard<std::mutex> lock(g_mu);
                CountMyMemory(CodePointCount(body));
            }
            const TranslateResult r = tr->Translate(p.segments, source, target);
            if (r.ok) result = r.text;
            else error = r.error;
        }
    }
    // The back-translation: what your message means in your language again (small under it).
    std::wstring back;
    bool wantBack;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        wantBack = g_cfg.backTranslate && gen == g_inputGen && !g_sendPending;
    }
    if (translating && tr && wantBack && error.empty() && result != body) {
        ProtectedText p = ProtectForTranslation(result, nullptr, g_spell ? &g_spell->KeepWords() : &BuiltinKeepWords());
        if (HasTranslatableText(p.segments)) {
            {
                std::lock_guard<std::mutex> lock(g_mu);
                CountMyMemory(CodePointCount(result));
            }
            const TranslateResult r = tr->Translate(p.segments, SourceCode(target), typing);
            if (r.ok) back = SanitizeChatText(r.text);
        }
    }
    std::vector<std::wstring> parts;
    bool sendNow = false;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        if (gen != g_inputGen && !g_sendPending) return;  // the text changed meanwhile
        g_preview = SanitizeChatText(result);
        g_back = back;
        g_previewGen = gen;
        if (!error.empty()) SetStatus(error);
        if (g_sendPending && gen == g_inputGen) {
            g_sendPending = false;
            const std::wstring prefix = split.prefix.empty() ? ComposePrefix() : split.prefix;
            parts = SplitForChat(g_preview, prefix, static_cast<size_t>(g_cfg.maxLength));
            sendNow = true;
        }
    }
    if (sendNow) SendNow(std::move(parts), text, body);
}

// The word bar, the grey completion and what Space writes, for the newest text + caret.
void ComputeTyping() {
    std::wstring text;
    size_t caret;
    AutoCorrectMode mode;
    std::vector<std::wstring> context;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        g_helpRequested = false;
        text = g_wFlatInput;
        caret = text.size();  // the help works at the end of the line (where you type)
        mode = g_cfg.autoCorrect;
        context = g_context;
    }
    if (!g_spell || !g_cfg.suggestions) return;
    g_spell->SetContext(std::move(context));
    TypingHelp h;
    h.text = text;
    h.caret = caret;
    h.bar = g_spell->Suggestions(text, caret, mode);
    h.choices = g_spell->Choices(text, caret, mode);
    std::lock_guard<std::mutex> lock(g_mu);
    if (g_wFlatInput == text) g_help = std::move(h);
}

// Enter: the parts go into the GW2 chat – only now, on your Enter (invariants 1–3).
void SendNow(std::vector<std::wstring> parts, std::wstring typed, std::wstring learnText) {
    if (parts.empty()) return;
    if (g_test.noSend) {  // the self-test: what would go into the chat
        std::lock_guard<std::mutex> lock(g_mu);
        g_test.sentParts += static_cast<int>(parts.size());
        strncpy_s(g_test.lastSent, ToUtf8(parts.front()).c_str(), _TRUNCATE);
        g_sending = false;  // as after a real send
        g_history.push_front(typed);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(g_mu);
        g_sending = true;
        SetStatus(L"…", 0);
    }
    // ImGui must let go of the keyboard first, else our own input line would eat the keys.
    for (int i = 0; i < 50 && !g_keyboardFree; ++i) Sleep(10);
    HWND gw2 = FindGw2Window(GetCurrentProcessId());
    SendOptions so;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        so = g_cfg.send;
    }
    if (so.autoTiming) {
        const int fps = g_fps;
        if (fps > 0) {
            const int action = std::clamp(1000 / std::max(fps, 10) + 10, 15, 1010);
            so.keyHoldMs = action;
            so.stepDelayMs = action;
        }
    } else if (const int fps = g_fps; fps > 0 && so.keyHoldMs == 30) {  // GW2 reads the keyboard once per frame
        const int action = std::clamp(1000 / std::max(fps, 10) + 10, 15, 1010);
        so.keyHoldMs = action;
        if (so.stepDelayMs == 80) so.stepDelayMs = action;
    }
    std::wstring error;
    bool ok = gw2 != nullptr;
    if (!gw2) error = Tr(L"GW2 window not found");
    bool copyOnly;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        copyOnly = g_cfg.copyOnly;
    }
    if (ok && copyOnly) {  // not a single key reaches the game (invariant 1): only the clipboard
        std::wstring all;
        for (const std::wstring& part : parts) all += (all.empty() ? L"" : L" ") + part;
        ok = CopyTextToClipboard(gw2, all);
        if (!ok) error = Tr(L"The clipboard is blocked");
        parts.clear();
    }
    for (const std::wstring& part : parts) {
        if (!ok) break;
        const MumbleState ms = g_mumbleWorker ? g_mumbleWorker->Read() : MumbleState{};
        const SendOutcome out = SendToGw2Chat(gw2, part, so, ms.live ? g_mumbleWorker : nullptr);
        if (!out.ok) {
            ok = false;
            error = out.error;
        }
    }
    if (ok && g_spell) {
        Config c;
        {
            std::lock_guard<std::mutex> lock(g_mu);
            c = g_cfg;
        }
        if (c.learnWords) g_spell->Learn(learnText);  // the word model learns only from what you send
    }
    {  // misheard words: what was dictated vs. what you finally sent ("heard = meant", per language)
        std::lock_guard<std::mutex> lock(g_mu);
        if (ok && !g_lastDictated.empty()) {
            if (!g_voiceFixes.Learn(g_lastDictated, learnText, g_dictLang).empty())
                WriteFileAtomic(g_cfg.dataDir + L"\\voice-fixes.txt", ToUtf8(g_voiceFixes.Serialize()));
            g_lastDictated.clear();
        }
    }
    std::lock_guard<std::mutex> lock(g_mu);
    g_sending = false;
    if (ok) {
        SetStatus(copyOnly ? Tr(L"Copied – paste it into the GW2 chat with Ctrl+V") : L"");
        g_history.push_front(typed);
        while (g_history.size() > kHistoryMax) g_history.pop_back();
        if (g_cfg.returnFocus) g_focusInput = true;
    } else {
        SetStatus(error, 10000);
        if (g_input[0] == 0) {  // nothing lost: the text comes back
            const std::string back = U8(typed);
            strncpy_s(g_input, back.c_str(), _TRUNCATE);
        }
    }
}
VoiceHelper g_voice;

// The helper reads the ini itself: what still waits (Config::DeferWrites) is written first – on the worker, it can be
// many writes after "Save". Safe from any thread.
void VoiceReload() {
    g_worker.Post([] {
        Config::FlushWrites();
        std::lock_guard<std::mutex> lock(g_mu);
        if (g_voice.Running()) g_voice.Send("reload");
    });
}

// A recognition finished: how long it took and the CPU peak, for the model in use (g_mu held). Recognizing took
// longer than the speech itself (and over 2 s) twice in a row → the next smaller installed model, if allowed.
void NoteVoiceRun(const JsonValue& v) {
    auto num = [&](const char* key) {
        const JsonValue* n = v.Get(key);
        return n && n->type == JsonValue::Type::Number ? static_cast<int>(n->n) : 0;
    };
    const int rec = num("rec"), rms = num("rms"), peak = num("peak");
    if (rec <= 0 && rms <= 0) return;
    const VoiceCatalog catalog = LoadVoiceCatalog(g_cfg.dataDir);
    const VoiceModelInfo* m = catalog.Find(U8(g_cfg.voiceModel));
    if (!m) m = catalog.Recommended();
    if (!m) return;
    VoiceRun& r = g_voiceRuns[W(m->id)];
    r.peak = peak;
    r.rms = rms;
    r.rec = rec;
    r.maxPeak = std::max(r.maxPeak, peak);
    g_cfg.SaveValue(L"VoiceStats", W(m->id).c_str(), std::to_wstring(peak) + L"," + std::to_wstring(rms) + L"," + std::to_wstring(rec) +
                                                   L"," + std::to_wstring(r.maxPeak));
    r.slow = rms > 2000 && rms > rec ? r.slow + 1 : 0;
    if (!g_nx.voiceFallback || r.slow < 2) return;
    const VoiceModelInfo* smaller = nullptr;  // the biggest installed one below it
    for (const VoiceModelInfo& o : catalog.models)
        if (o.bytes < m->bytes && VoiceModelInstalled(g_cfg.VoiceModelDir(), o) && (!smaller || o.bytes > smaller->bytes))
            smaller = &o;
    r.slow = 0;
    if (!smaller) {
        SetStatus(TrF(L"{1} is slow on this PC – a smaller model would answer faster (Options → Voice)", {W(m->name)}), 12000);
        return;
    }
    g_cfg.voiceModel = W(smaller->id);
    g_cfg.SaveValue(L"Voice", L"Model", g_cfg.voiceModel);
    VoiceReload();
    SetStatus(TrF(L"{1} was too slow – switched to {2}", {W(m->name), W(smaller->name)}), 12000);
}

// A recognized text goes into the input (after what is there, with a space), checked by you, sent by your Enter.
void VoiceHelper::OnLine(const std::string& line) {
    JsonValue v;
    if (!ParseJson(line, v) || !v.IsObject()) return;
    const std::string t = v.GetString("t");
    std::lock_guard<std::mutex> lock(g_mu);
    if (t == "ready") {
        g_voiceReady = true;
        if (!g_voicePending.empty()) {  // what started the helper: it runs now
            g_voice.Send(g_voicePending.c_str());
            g_voicePending.clear();
            SetStatus(L"");
        }
    } else if (t == "started") {
        g_recording = true;
        g_voiceLive.clear();
        SetStatus(Tr(L"Hearing you … release the key when you are done"), 0);
    } else if (t == "partial") {
        g_voiceLive = W(v.GetString("stable")) + W(v.GetString("unsure"));
    } else if (t == "final") {
        g_recording = false;
        g_voiceLive.clear();
        const std::wstring text = Trim(W(v.GetString("text")));
        if (v.GetBool("empty") || text.empty()) {
            SetStatus(Tr(L"Nothing recognized – hold the key while you speak"), 5000);
            return;
        }
        NoteVoiceRun(v);
        g_dictLang = g_cfg.voiceTranslate ? std::wstring(L"EN") : ToUpperAscii(W(v.GetString("lang")));
        const std::wstring fixed = g_voiceFixes.Apply(text, g_dictLang);
        g_lastDictated = fixed;
        const size_t len = std::strlen(g_input);
        g_edit.active = true;
        g_edit.startByte = len;
        g_edit.lenBytes = 0;
        g_edit.insert = (len && g_input[len - 1] != ' ' && g_input[len - 1] != '\n' ? " " : "") + U8(fixed);
        g_focusInput = true;
        g_autoSendPending = g_cfg.voiceAutoSend;  // your key release was the press; off by default
        SetStatus(L"");
    } else if (t == "listening") {
        g_listening = true;
        g_listenPausedFor.clear();
    } else if (t == "idle") {
        g_listening = false;
    } else if (t == "paused") {
        g_listening = false;
        g_listenPausedFor = W(v.GetString("app"));
    } else if (t == "error") {
        g_recording = false;
        g_voiceLive.clear();
        SetStatus(W(v.GetString("msg")), 10000);
    }
}

std::wstring HelperExe() {  // <addons>\GW2Keyboard\gw2kb_voice.exe
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(g_self, path, MAX_PATH);
    std::wstring p = path;
    return p.substr(0, p.find_last_of(L"\\/") + 1) + L"GW2Keyboard\\gw2kb_voice.exe";
}

// The helper starts on first use; `cmd` runs as soon as it is ready.
void VoiceCommand(const char* cmd) {
    bool ready;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        if (!g_cfg.voiceEnabled) {
            SetStatus(Tr(L"Voice input is off (options of GW2 Keyboard)"));
            return;
        }
        ready = g_voiceReady;
        if (!ready) {
            g_voicePending = cmd;
            SetStatus(Tr(L"Voice input is starting – a moment"), 0);
        }
    }
    if (ready) {
        g_voice.Send(cmd);
        return;
    }
    if (g_voice.Running()) return;
    if (GetFileAttributesW(HelperExe().c_str()) == INVALID_FILE_ATTRIBUTES) {  // only the add-on was copied: load it
        {
            std::lock_guard<std::mutex> lock(g_mu);
            g_voicePending.clear();
            SetStatus(Tr(L"Loading the voice module – the microphone works when it is ready"), 10000);
        }
        InstallVoiceModuleNow();
        return;
    }
    std::wstring dataDir;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        dataDir = g_cfg.dataDir;
    }
    Config::FlushWrites();  // the helper reads the ini when it starts (normally nothing waits)
    if (!g_voice.Start(HelperExe(), dataDir)) {
        std::lock_guard<std::mutex> lock(g_mu);
        g_voicePending.clear();
        SetStatus(Tr(L"The voice module could not be started"), 10000);
    }
}

// Push to talk: pressed starts, released ends (the recognized text then waits for your Enter). Started while the
// helper still loads, the recording begins when it is ready and a pause ends it.
void TalkDown() {
    bool rec, toggle, ready;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        rec = g_recording;
        toggle = g_cfg.voiceToggle;
        ready = g_voiceReady;
    }
    if (rec) return;
    VoiceCommand(toggle || !ready ? "begin-auto" : "begin");
}
void TalkUp() { g_voice.Send("end"); }
void TalkLocked() {  // right-click on the microphone: on until a pause is heard
    {
        std::lock_guard<std::mutex> lock(g_mu);
        if (g_recording) return;
    }
    VoiceCommand("begin-auto");
}
void StartListening() {  // voice activation: a recording starts when you speak, a pause ends it
    {
        std::lock_guard<std::mutex> lock(g_mu);
        g_listenWanted = true;
    }
    VoiceCommand("listen");
}
void StopListening() {
    {
        std::lock_guard<std::mutex> lock(g_mu);
        g_listenWanted = false;
        g_listening = false;
        g_listenPausedFor.clear();
    }
    g_voice.Send("stop-listen");
}

void OnTalkKey(const char*, bool release) {
    if (g_disabled) return;
    bool toggle;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        toggle = g_cfg.voiceToggle;
    }
    if (toggle) {  // press once: starts; a pause ends it (or the next press)
        if (release) return;
        bool rec;
        {
            std::lock_guard<std::mutex> lock(g_mu);
            rec = g_recording;
        }
        if (rec) TalkUp();
        else TalkDown();
        return;
    }
    if (release) TalkUp();
    else TalkDown();
}

void ApplyPushToTalkKey(const std::wstring& key) {
    const auto hk = ParseHotkey(key);
    g_pttVk = hk ? hk->vk : 0u;
    g_pttMods = hk ? hk->mods : 0u;
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

void OnChatEvents(void* args) {
    if (g_disabled) return;
    try {
        const auto* m = static_cast<const bridge::ChatEventsMessage*>(args);
        if (!m) return;
        g_chatEventsLive = true;
        using T = bridge::ChatEventsType;
        Channel ch;
        switch (m->type) {
            case T::Party: ch = Channel::Party; break;
            case T::Squad: ch = Channel::Squad; break;
            case T::SquadMessage:
            case T::SquadBroadcast: AddLine(Channel::Squad, false, "", m->squadMessage); return;
            case T::Map: ch = Channel::Map; break;
            case T::Local: ch = Channel::Say; break;
            case T::Whisper: ch = Channel::Whisper; break;
            case T::Guild: ch = Channel::Guild; break;
            case T::TeamPvP:
            case T::TeamWvW: ch = Channel::Team; break;
            default: return;
        }
        AddLine(ch, m->type == T::Whisper && (m->flags & bridge::kChatEventsWhisperFromMe) != 0,
                m->generic.characterName, m->generic.content);
    } catch (...) {
        g_disabled = true;
    }
}

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

// The grey text after the caret: what Space writes (a completion shows its rest, a
// correction "> word"); after a space the sure next word or phrase. `replace` gets what Tab takes.
std::wstring GhostRest(const std::wstring& text, bool* correction) {
    if (correction) *correction = false;
    if (g_help.text != text || text.empty()) return {};
    const WordSuggestions& bar = g_help.bar;
    if (g_pick >= 0 && g_pick < static_cast<int>(bar.words.size())) {  // picked with Tab
        const std::wstring& word = bar.words[g_pick];
        const Span r = bar.replace.end() == text.size() ? bar.replace : Span{text.size(), 0};
        const std::wstring typed = text.substr(r.start, r.length);
        if (typed.size() < word.size() && CaseFold(word.substr(0, typed.size())) == CaseFold(typed))
            return word.substr(typed.size());
        if (correction) *correction = true;
        return typed.empty() ? word : L"  > " + word;
    }
    const WordChoices& c = g_help.choices;
    if (text.back() != L' ' && c.highlight >= 0 && c.highlight < static_cast<int>(c.words.size()) &&
        c.replace.end() == text.size() && !(c.firstIsTyped && c.highlight == 0)) {
        const std::wstring& word = c.words[c.highlight];
        const std::wstring typed = text.substr(c.replace.start, c.replace.length);
        if (typed.size() < word.size() && CaseFold(word.substr(0, typed.size())) == CaseFold(typed))
            return word.substr(typed.size());
        if (correction) *correction = true;
        return L"  > " + word;
    }
    const WordSuggestions& b = g_help.bar;
    if (b.kind == WordSuggestions::Kind::Next && !b.words.empty() && text.back() == L' ')
        return b.phrase.empty() ? b.words[0] : b.phrase;
    // Without the word bar nothing shows what Tab would take: its highlighted completion becomes the grey word (Tab takes
    // it, Space writes it then). Space alone still keeps a valid word as typed – no surprises.
    if (!Lay().words && b.kind != WordSuggestions::Kind::Next && b.autoIndex >= 0 &&
        b.autoIndex < static_cast<int>(b.words.size()) && b.replace.end() == text.size() && text.back() != L' ') {
        const std::wstring& word = b.words[b.autoIndex];
        const std::wstring typed = text.substr(b.replace.start, b.replace.length);
        if (!typed.empty() && typed.size() < word.size() && CaseFold(word.substr(0, typed.size())) == CaseFold(typed))
            return word.substr(typed.size());
    }
    return {};
}

// A word of the bar replaces the word at the end (or goes after it), followed by a space.
void TakeWord(const std::wstring& word) {
    const std::wstring& text = g_wFlatInput;
    const WordSuggestions& b = g_help.bar;
    Span r = b.replace;
    if (g_help.text != text || r.end() > text.size()) r = Span{text.size(), 0};
    g_edit.active = true;
    g_edit.startByte = U8Offset(text, r.start);
    g_edit.lenBytes = U8Offset(text, r.end()) - g_edit.startByte;
    g_edit.insert = U8(word + L" ");
    g_focusInput = true;
    if (g_spell) g_worker.Post([text, start = r.start, word] { if (g_spell) g_spell->Chose(text, start, word, true); });
}

// Space at the end of a word: the word picked with Tab, else the grey one (what Space writes – only
// strong choices); the change comes in through g_edit, the space with it. False: just a space.
bool SpaceWrites() {
    const std::wstring& text = g_wFlatInput;
    if (g_help.text != text || text.empty() || text.back() == L' ' || g_cfg.autoCorrect == AutoCorrectMode::Off && g_pick < 0)
        return false;
    std::wstring word;
    Span r;
    bool deliberate = false;
    const WordSuggestions& bar = g_help.bar;
    const WordChoices& c = g_help.choices;
    if (g_pick >= 0 && g_pick < static_cast<int>(bar.words.size())) {
        word = bar.words[g_pick];
        r = bar.replace.end() == text.size() ? bar.replace : Span{text.size(), 0};
        deliberate = true;
    } else if (c.Changes() && c.highlight >= 0 && c.highlight < static_cast<int>(c.words.size()) &&
               c.replace.end() == text.size()) {
        word = c.words[c.highlight];
        r = c.replace;
    } else {
        return false;
    }
    g_pick = -1;
    const std::wstring fixed = text.substr(0, r.start) + word + L" ";
    g_edit.active = true;
    g_edit.startByte = U8Offset(text, r.start);
    g_edit.lenBytes = U8Offset(text, text.size()) - g_edit.startByte;
    g_edit.insert = U8(word + L" ");
    g_undo = {true, U8(fixed), U8(text), text.substr(r.start, r.length)};
    g_worker.Post([text, start = r.start, word, deliberate] { if (g_spell) g_spell->Chose(text, start, word, deliberate); });
    return true;
}

int InputCallback(ImGuiInputTextCallbackData* d) {
    switch (d->EventFlag) {
        case ImGuiInputTextFlags_CallbackCharFilter: {
            const ImWchar ch = d->EventChar;
            if (ch == '\n' || ch == '\r') return 1;  // no line breaks of your own: Enter sends
            if (ch == '\t') {  // Tab / Shift+Tab: the next / previous word of the bar (Space writes it)
                const int n = g_help.text == g_wFlatInput ? static_cast<int>(g_help.bar.words.size()) : 0;
                const int first = g_help.bar.autoIndex >= 0 && g_help.bar.autoIndex < n ? g_help.bar.autoIndex : 0;
                if (n > 0 && g_pick < 0 && !ImGui::GetIO().KeyShift) g_pick = first;  // what the bar / grey word offered
                else if (n > 0) g_pick = ImGui::GetIO().KeyShift ? (g_pick <= 0 ? n - 1 : g_pick - 1) : (g_pick + 1) % n;
                return 1;
            }
            if (ch == ' ') return SpaceWrites() ? 1 : 0;  // the picked / grey word, then the space
            return 0;
        }
        case ImGuiInputTextFlags_CallbackAlways: {
            if (g_edit.active) {  // a word of the bar or an autocorrection
                g_edit.active = false;
                const size_t len = static_cast<size_t>(d->BufTextLen);
                const size_t from = RawOffset(d->Buf, len, g_edit.startByte);  // flat offsets -> the box's own
                const size_t to = std::max(from, RawOffset(d->Buf, len, g_edit.startByte + g_edit.lenBytes));
                if (from <= len) {
                    d->DeleteChars(static_cast<int>(from), static_cast<int>(to - from));
                    d->InsertChars(static_cast<int>(from), g_edit.insert.c_str());
                    d->CursorPos = d->SelectionStart = d->SelectionEnd = d->BufTextLen;
                }
                break;
            }
            // Soft line breaks: the text is broken to the box's width (only when it changed) – at the last space of a line,
            // or inside a word that is longer than the line (a code, a link). The caret keeps its place in the text.
            if (g_wrapWidth > 0) {
                static std::string lastSeen;
                std::string raw(d->Buf, d->BufTextLen);
                int cursor = d->CursorPos;
                for (size_t i = 0; i < raw.size(); ++i)  // Backspace took the line end of a broken word: the letter before
                    if (raw[i] == '\r' && (i + 1 >= raw.size() || raw[i + 1] != '\n')) {
                        size_t start = i;
                        while (start > 0 && (static_cast<unsigned char>(raw[start - 1]) & 0xC0) == 0x80) --start;
                        if (start > 0) --start;  // the first byte of the letter before
                        raw.erase(start, i + 1 - start);
                        cursor = static_cast<int>(start);
                        break;
                    }
                if (raw != lastSeen) {
                    const std::string flat = Flat(raw.c_str());
                    const size_t flatCursor = Flat(raw.substr(0, std::min<size_t>(cursor, raw.size())).c_str()).size();
                    std::string out;
                    size_t lineStart = 0, lastSpace = std::string::npos;
                    for (size_t i = 0; i < flat.size();) {
                        size_t n = 1;  // one character (UTF-8)
                        while (i + n < flat.size() && (static_cast<unsigned char>(flat[i + n]) & 0xC0) == 0x80) ++n;
                        if (flat[i] == ' ') lastSpace = out.size();
                        out.append(flat, i, n);
                        i += n;
                        if (ImGui::CalcTextSize(out.data() + lineStart, out.data() + out.size()).x <= g_wrapWidth) continue;
                        if (lastSpace != std::string::npos && lastSpace >= lineStart) {
                            out[lastSpace] = '\n';
                            lineStart = lastSpace + 1;
                            lastSpace = std::string::npos;
                        } else if (out.size() - n > lineStart) {  // no space in this line: break before this letter
                            out.insert(out.size() - n, "\r\n");
                            lineStart = out.size() - n;
                        }
                    }
                    if (out != std::string(d->Buf, d->BufTextLen)) {
                        size_t at = 0, seen = 0;  // the caret: the same place in the text
                        while (at < out.size() && seen < flatCursor) {
                            if (out[at] == '\r') { at += 2; continue; }
                            ++seen;
                            ++at;
                        }
                        if (at + 1 < out.size() && out[at] == '\r') at += 2;
                        d->DeleteChars(0, d->BufTextLen);
                        d->InsertChars(0, out.c_str());
                        d->CursorPos = d->SelectionStart = d->SelectionEnd = static_cast<int>(std::min(at, out.size()));
                    }
                    lastSeen = out;
                }
            }
            const std::string now = Flat(std::string(d->Buf, d->BufTextLen).c_str());
            // Backspace right after an autocorrection: the word as you typed it comes back (and is not corrected
            // that way again).
            if (g_undo.active && now.size() + 1 == g_undo.after.size() && g_undo.after.compare(0, now.size(), now) == 0) {
                g_undo.active = false;
                d->DeleteChars(0, d->BufTextLen);
                d->InsertChars(0, g_undo.before.c_str());
                g_finishedLast.clear();  // an undone autocorrection is no correction of your own
                g_worker.Post([orig = g_undo.original] { if (g_spell) g_spell->RejectCorrection(orig); });
                break;
            }
            if (g_undo.active && now != g_undo.after) g_undo.active = false;
            break;
        }
        default:
            break;
    }
    return 0;
}

void StartSend() {  // Enter (g_mu held)
    const std::wstring text = SanitizeChatText(g_wFlatInput);
    if (text.empty() || g_sending) return;
    const SendGuard::Verdict v = g_guard.Check(text, false, false, GetTickCount64());
    if (!v.allow) {
        SetStatus(v.reason == SendGuard::Reason::Repeat ? Tr(L"Not sent: the same line too often in a short time.")
                  : v.reason == SendGuard::Reason::Rate  ? Tr(L"Not sent: more lines than a person writes in a minute.")
                                                         : Tr(L"Sending is switched off for this session (misuse detected). "
                                                              L"Restart the program to send again."),
                  10000);
        return;
    }
    const ChatSplit split = SplitChatCommand(text);
    const std::wstring body = split.prefix.empty() ? text : split.body;
    const std::wstring prefix = split.prefix.empty() ? ComposePrefix() : split.prefix;
    g_input[0] = 0;
    g_flatInput.clear();
    g_wFlatInput.clear();
    g_lastInput.clear();
    g_historyPos = -1;
    g_undo.active = false;
    ImGui::SetWindowFocus(nullptr);  // let go of the keyboard: the keys must reach the game
    if (!Translating()) {
        g_worker.Post([parts = SplitForChat(body, prefix, static_cast<size_t>(g_cfg.maxLength)), text, body] {
            SendNow(parts, text, body);
        });
    } else if (g_previewGen == g_inputGen && !g_preview.empty()) {
        g_worker.Post([parts = SplitForChat(g_preview, prefix, static_cast<size_t>(g_cfg.maxLength)), text, body] {
            SendNow(parts, text, body);
        });
    } else {  // the translation is not there yet: sent when it arrives
        g_sendPending = true;
        SetStatus(Tr(L"translating and sending …"), 0);
        g_worker.Post([gen = g_inputGen, text] { TranslateInput(gen, text); });
    }
}

std::wstring NativeName(const std::wstring& code) {
    if (const LangInfo* l = FindLanguage(code)) return l->native;
    return ToUpperAscii(code);
}
std::atomic<bool> g_chatEventsPresent{false};
std::atomic<bool> g_chatEventsFile{false};  // Events_Chat.dll lies in the add-ons folder (maybe not loaded in Nexus)
std::atomic<bool> g_chatEventsInstalling{false};
std::thread g_chatEventsThread;

const std::wstring& AddonsDir() {  // the folder our DLL lies in: <GW2>\addons\ .
    static const std::wstring s_addonsDir = [] {
        wchar_t path[MAX_PATH] = {};
        GetModuleFileNameW(g_self, path, MAX_PATH);
        std::wstring p = path;
        return p.substr(0, p.find_last_of(L"\\/") + 1);
    }();
    return s_addonsDir;
}

// A Nexus add-on in this process that says it is "Events: Chat" (its definition is only read). Every 3 s at most.
bool ChatEventsPresent() {
    static ULONGLONG next = 0;
    if (GetTickCount64() < next) return g_chatEventsPresent;
    next = GetTickCount64() + 3000;
    bool found = false;
    HMODULE mods[1024];
    DWORD needed = 0;
    if (K32EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &needed)) {
        const DWORD n = std::min<DWORD>(needed / sizeof(HMODULE), 1024);
        using GetDef = AddonDefinition_t* (*)();
        for (DWORD i = 0; i < n && !found; ++i) {
            if (mods[i] == g_self) continue;
            if (auto get = reinterpret_cast<GetDef>(GetProcAddress(mods[i], "GetAddonDef"))) {
                const AddonDefinition_t* def = get();
                found = def && def->Signature == bridge::kChatEventsSignature;
            }
        }
    }
    g_chatEventsPresent = found;
    g_chatEventsFile = GetFileAttributesW((AddonsDir() + kChatEventsDllName).c_str()) != INVALID_FILE_ATTRIBUTES;
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
std::thread g_download;
std::atomic<bool> g_downloadCancel{false};
std::atomic<int> g_downloadPct{-1};  // -1: none running

void StartModelDownload(const VoiceModelInfo& m) {
    if (g_downloadPct >= 0) return;
    if (g_download.joinable()) g_download.join();
    g_downloadCancel = false;
    g_downloadPct = 0;
    std::wstring dir;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        dir = g_cfg.VoiceModelDir();
    }
    g_download = std::thread([m, dir] {
        EnsureDir(dir);
        std::wstring error;
        const bool ok = DownloadVoiceModel(
            dir, m,
            [](uint64_t done, uint64_t total) {
                if (total) g_downloadPct = static_cast<int>(done * 100 / total);
                return true;
            },
            &g_downloadCancel, &error);
        g_downloadPct = -1;
        std::lock_guard<std::mutex> lock(g_mu);
        SetStatus(ok ? Tr(L"Voice model installed") : error, 10000);
        if (ok) VoiceReload();
    });
}
std::atomic<bool> g_voiceModuleInstalling{false};
std::atomic<int> g_voiceModulePct{-1};
std::thread g_voiceModuleThread;

std::wstring KeyboardDir() { return AddonsDir() + L"GW2Keyboard"; }
ModuleState g_modules;
ULONGLONG g_modulesNext = 0;

const ModuleState& Modules() {  // looked at every 2 s (file checks), at once after an install
    if (GetTickCount64() >= g_modulesNext) {
        g_modulesNext = GetTickCount64() + 2000;
        Config c;
        {
            std::lock_guard<std::mutex> lock(g_mu);
            c = g_cfg;
        }
        const VoiceCatalog catalog = LoadVoiceCatalog(c.dataDir);
        const VoiceModelInfo* m = catalog.Find(U8(c.voiceModel));
        if (!m) m = catalog.Recommended();
        g_modules.voiceModule = VoiceModulePresent(KeyboardDir());
        g_modules.model = m && VoiceModelInstalled(c.VoiceModelDir(), *m);
        g_modules.modelName = m ? m->name : std::string();
    }
    return g_modules;
}

// The voice module from this version's GitHub release (checksum checked); only on your click or the first start.
void InstallVoiceModuleNow() {
    if (g_voiceModuleInstalling.exchange(true)) return;
    if (g_voiceModuleThread.joinable()) g_voiceModuleThread.join();
    g_voiceModulePct = 0;
    g_voiceModuleThread = std::thread([dir = KeyboardDir()] {
        const ExtrasInstallResult r = InstallVoiceModule(dir, W(GCT_VERSION_STR), [](uint64_t done, uint64_t total) {
            if (total) g_voiceModulePct = static_cast<int>(done * 100 / total);
            return !g_disabled.load();
        });
        g_voiceModulePct = -1;
        g_voiceModuleInstalling = false;
        g_modulesNext = 0;
        std::lock_guard<std::mutex> lock(g_mu);
        SetStatus(r.code == ExtrasInstallCode::Ok ? Tr(L"Voice module ready")
                  : r.code == ExtrasInstallCode::NoAnswer ? TrF(L"GitHub did not answer ({1}) – please try again later", {r.detail})
                                                          : TrF(L"Loading {1} failed: {2}", {Tr(L"the voice module"), r.detail}),
                  10000);
    });
}

// Starting / ending the voice module by hand (it also starts by itself on the first use of the microphone).
void LoadVoiceModule() {
    std::wstring dataDir;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        dataDir = g_cfg.dataDir;
    }
    Config::FlushWrites();
    if (!g_voice.Running() && VoiceModulePresent(KeyboardDir())) g_voice.Start(HelperExe(), dataDir);
}
void UnloadVoiceModule() {
    StopListening();
    g_voice.Stop();  // the helper ends: the model and its memory are freed
    std::lock_guard<std::mutex> lock(g_mu);
    g_voicePending.clear();
    g_recording = g_listening = false;
    g_listenUserOff = true;  // voice activation does not start it again by itself
}
ChatLinks g_links;

std::wstring ApiLang() {  // the GW2 API speaks en, de, fr, es, zh (g_mu held)
    if (!g_nx.namesLang.empty()) return g_nx.namesLang;  // like the game
    const std::wstring p = ToLowerAscii(PrimaryLang(TypingLang()));
    return p == L"de" || p == L"fr" || p == L"es" || p == L"zh" ? p : L"en";
}
std::vector<SelfFix> g_selfFixes;  // newest first, g_mu
std::vector<std::wstring> g_lastWords;  // the words as they stood before a correction began

void AddSelfFix(const std::wstring& was, const std::wstring& now) {  // g_mu held
    g_selfFixes.erase(std::remove_if(g_selfFixes.begin(), g_selfFixes.end(),
                                     [&](const SelfFix& f) { return CaseFold(f.typed) == CaseFold(was); }),
                      g_selfFixes.end());
    if (was.size() < 3 || now.size() < 3 || CaseFold(was) == CaseFold(now) || EditDistance(was, now, 3) > 2) return;
    g_selfFixes.insert(g_selfFixes.begin(), {was, now});
    if (g_selfFixes.size() > 8) g_selfFixes.resize(8);
    g_test.selfFixes = static_cast<int>(g_selfFixes.size());
    strncpy_s(g_test.lastFix, U8(was + L">" + now).c_str(), _TRUNCATE);
}

void NoteSelfFix(const std::wstring& text) {  // g_mu held; the text changed
    std::vector<std::wstring> words;
    for (const Span& sp : WordSpans(text)) words.push_back(text.substr(sp.start, sp.length));
    const bool spaceAfter = !text.empty() && text.back() == L' ';
    if (words.empty()) g_finishedLast.clear();
    if (words.size() == g_lastWords.size() && !words.empty()) {
        int changed = -1, count = 0;
        for (size_t i = 0; i < words.size(); ++i)
            if (words[i] != g_lastWords[i]) changed = static_cast<int>(i), ++count;
        if (count == 1 && static_cast<size_t>(changed) + 1 < words.size()) {
            // A word before the last one is being corrected: compared with how it stood before (kept as it was).
            AddSelfFix(g_lastWords[changed], words[changed]);
            return;
        }
    }
    // The last word, retyped after a space had finished it ("shcon " → "schon "): when the space comes again.
    if (spaceAfter && !words.empty()) {
        if (g_finishedAt == words.size() - 1 && !g_finishedLast.empty() && words.back() != g_finishedLast)
            AddSelfFix(g_finishedLast, words.back());
        if (words.size() - 1 >= g_finishedAt || g_finishedLast.empty()) {  // a word deleted whole: it waits for its retyping
            g_finishedLast = words.back();
            g_finishedAt = words.size() - 1;
        }
    }
    g_lastWords = words;  // typing at the end, or more than one word changed: the new state to compare with
}

// Your own words ("finds = I think"): what the translator gets instead (<data>\my-words.txt), learned on a right-click.
void SaveMyWords() {  // g_mu held
    WriteFileAtomic(g_cfg.dataDir + L"\\my-words.txt", ToUtf8(g_myWords.Serialize()));
}
bool g_openLinkAsk = false;
bool g_minimizeRequest = false, g_expandRequest = false;  // on the main window, never inside the chat area
int g_redirect = 0;  // 0 none; 1 waypoint, waiting for the map; 2 the map is open; 3 waiting for GW2's chat line; 4 it is open
ULONGLONG g_redirectSince = 0;

void HandleChatLinkClick(const std::string& raw, ULONGLONG receivedTime, const std::string& where) {  // g_mu held
    ImGui::SetClipboardText(raw.c_str());
    const bool waypoint = DecodeChatCode(W(raw)).kind == ChatCodeKind::Poi;
    const bool fresh = receivedTime == 0 || GetTickCount64() - receivedTime < 15000;
    if (waypoint && !fresh) {  // the emergency way: the game map, and our window says where to look
        if (g_api && g_api->GameBinds_InvokeAsync) g_api->GameBinds_InvokeAsync(GB_MapToggle, 50);
        SetStatus(where.empty() ? Tr(L"The map is open – the waypoint code is copied (Ctrl+V)")
                                : TrF(L"On the map: {1}", {W(where)}),
                  20000);
        g_showOnMapUntil = GetTickCount64() + 20000;  // the window stays over the map while it names the spot
        return;
    }
    g_minimizeRequest = true;
    g_redirect = waypoint ? 1 : 3;
    g_redirectSince = GetTickCount64();
    g_suppressChatFocusUntil = GetTickCount64() + 2000;  // our own "come back when GW2's chat opens" waits
    if (g_api && g_api->GameBinds_InvokeAsync) g_api->GameBinds_InvokeAsync(GB_UiChatFocus, 20);
}

bool RichLine(const std::wstring& text, const std::vector<PieUI::ChatLinks::LinkSegment>& segs, ImVec4 color,
              ULONGLONG receivedTime) {  // g_mu held
    (void)text;
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
                    HandleChatLinkClick(seg.raw, receivedTime, g_links.Where(seg));
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
bool g_micLocked = false;  // right-click on the microphone: on until the next right-click
bool g_compact = false;    // menu ≡: header, chat, input, footer – no word bar, no preview (the translation small in the input)
bool g_mini = false;       // menu ≡: nothing but the input (your settings of the parts stay as they are underneath)
Layout Lay() {
    Layout l = g_layout;
    if (g_mini) {
        l.header = l.chat = l.preview = l.words = l.footer = l.minButton = false;
    } else if (g_compact) {
        l.words = false;
        l.preview = false;
    }
    return l;
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

// The word under the mouse in the input ("" when the mouse is not over a word): the line by its height, the letter by
// width, then the word around it in the text as it is meant (Flat).
std::wstring WordAtMouse(const ImVec2& textAt) {  // g_mu held
    const ImVec2 m = ImGui::GetIO().MousePos;
    const float lh = ImGui::GetTextLineHeight();
    if (m.y < textAt.y || m.x < textAt.x || !g_input[0]) return {};
    const char* p = g_input;
    for (int k = static_cast<int>((m.y - textAt.y) / lh); k > 0 && p; --k) {
        p = std::strchr(p, '\n');
        if (p) ++p;
    }
    if (!p || !*p) return {};
    const char* end = std::strchr(p, '\n');
    if (!end) end = p + std::strlen(p);
    const char* hit = nullptr;
    for (const char* q = p; q < end;) {
        const char* next = q + 1;
        while (next < end && (static_cast<unsigned char>(*next) & 0xC0) == 0x80) ++next;
        if (m.x < textAt.x + ImGui::CalcTextSize(p, next).x) {
            hit = q;
            break;
        }
        q = next;
    }
    if (!hit || *hit == ' ' || *hit == '\r') return {};
    const std::wstring& flat = g_wFlatInput;
    const size_t at = W(Flat(std::string(g_input, static_cast<size_t>(hit - g_input)).c_str())).size();
    auto inWord = [](wchar_t c) { return std::iswalnum(c) || c == L'\'' || c == L'-' || c == L'_'; };
    if (at >= flat.size() || !inWord(flat[at])) return {};
    size_t a = at, b = at;
    while (a > 0 && inWord(flat[a - 1])) --a;
    while (b < flat.size() && inWord(flat[b])) ++b;
    return flat.substr(a, b - a);
}

// Settings changed on the game thread reach the ini on the worker, at most every half second (a slider dragged writes
// once). A job dropped in a flood is posted again while writes still wait.
void KickSave() {
    static ULONGLONG s_posted = 0;
    if (GetTickCount64() - s_posted < 500 || !Config::WritesPending()) return;
    s_posted = GetTickCount64();
    g_worker.Post([] { Config::FlushWrites(); });
}

}  // namespace kb
