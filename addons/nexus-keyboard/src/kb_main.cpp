// kb_main.cpp — GW2 Keyboard for Nexus.
//
// An ImGui window inside the game:
//   - the chat from Vonsh's "Events: Chat" (every channel; installed on a click when missing), foreign lines translated
//     into your language; a click on a name answers with a whisper;
//   - the input line with the translation as you type, the word bar, the grey completion (Tab), autocorrection on
//     Space (Backspace undoes it), learned words, the history (Up/Down);
//   - Enter sends the translation into the GW2 chat – the safe send path (clipboard + keys, foreground checks,
//     invariants 1–3), only on your Enter, through the send guard.
//
// Inside the game process: the render callback only draws and copies; translations, spelling and sending run on our
// own worker thread; every callback is guarded (try/catch + SEH) – after an error the add-on switches itself off for
// the session. Nothing of other players is stored.

#include <windows.h>
#include <objbase.h>
#include <psapi.h>
#include <shellapi.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <map>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "chat_events.hpp"
#include "app/config.hpp"
#include "app/spell_service.hpp"
#include "core/chat_code.hpp"
#include "core/chat_line.hpp"
#include "core/gw2_text.hpp"
#include "core/hotkey.hpp"
#include "core/i18n.hpp"
#include "core/json.hpp"
#include "core/langs.hpp"
#include "core/my_words.hpp"
#include "core/voice.hpp"
#include "core/languages.hpp"
#include "core/protect.hpp"
#include "core/send_guard.hpp"
#include "core/slang.hpp"
#include "core/text.hpp"
#include "core/translator.hpp"
#include "core/voice_learn.hpp"
#include "core/json.hpp"
#include "imgui/imgui.h"
#include "nexus/Nexus.h"
#include "version.h"
#include "chat_links.hpp"
#include "chat/ChatLinks.h"
#include "voice_module.hpp"
#include "win/deepl_translator.hpp"
#include "win/els.hpp"
#include "win/extras_install.hpp"
#include "win/files.hpp"
#include "win/http.hpp"
#include "win/gw2_sender.hpp"
#include "win/mumble_link.hpp"
#include "win/audio_capture.hpp"
#include "win/voice_models.hpp"
#include "win/online_translators.hpp"

using namespace gct;

namespace {

constexpr const char* kName = "GW2Chatlator";
constexpr const char* kToggleBind = "KB_GW2KEYBOARD_TOGGLE";
constexpr const char* kFocusBind = "KB_GW2KEYBOARD_FOCUS";
constexpr const char* kTalkBind = "KB_GW2KEYBOARD_TALK";  // push to talk: held while you speak
constexpr size_t kMaxLines = 200;
constexpr size_t kHistoryMax = 50;
// The window is built from parts you can switch on / off and size (Options → General → Window layout).
struct Layout {
    bool chat = true, preview = true, words = true, footer = true;
    int inputLines = 4;    // the input box, wrapping
    int previewLines = 3;  // the translation and its back-translation
};
Layout g_layout;
// The add-on's own switches ([Nexus] in the settings file).
struct NexusOptions {
    bool translateChat = false;  // foreign chat lines into your language (off: a click on a line translates it)
    bool snap = true;            // the window snaps to the screen edges when you move it there
    bool setupDone = false;      // the first start loaded Events: Chat, the voice module and the base model
    bool voiceFallback = true;   // a model too slow twice in a row: the next smaller installed one is used
    std::wstring namesLang;      // item / place names: "en", "de", "fr", "es", "zh"; "" = the language you write in
};
NexusOptions g_nx;
// How chat text looks ([NexusWindow] Glow, Brightness, GameColors): GW2's colours (the name in the channel colour, the
// message a light tint of it), a dark halo around the letters like the game's text outline, a brightness of your own.
struct TextLook {
    bool glow = true;
    int brightness = 100;  // %
    bool gameColors = true;
};
TextLook g_look;
MyWords g_myWords;  // your own words and what they mean (<data>\my-words.txt), g_mu
// Whisper tabs: each keeps whom you write to (at most kMaxWhisperTabs, for this session).
constexpr size_t kMaxWhisperTabs = 5;
struct WhisperTab {
    std::wstring name;
    std::wstring nameFolded;
    bool unread = false;
    std::wstring sendLang;  // what you send from it ("" = like the chat tab, "=" = as written, else a code)
    // A tab of your own (right-click on the header): a copy of the chat with the channels you choose, writing into
    // `write` (Unknown = the channel active in GW2). Kept in [NexusTabs] (only its settings, never chat).
    bool custom = false;
    uint32_t channels = 0xFFFFFFFFu;
    Channel write = Channel::Unknown;
    uint32_t uid = 0;  // its ImGui id
    mutable std::string label;

    void SetName(const std::wstring& n) {
        name = n;
        nameFolded = CaseFold(n);
        label.clear();
    }
    const char* Label() const;
};
constexpr size_t kMaxCustomTabs = 6;
uint32_t g_nextTabUid = 1;
// The language each player writes in, as far as it is sure (CaseFold(name) -> code). Only for this session, never
// stored: answering someone in their language (their whisper tab sends in it).
std::map<std::wstring, std::wstring> g_playerLang;
// The send language of the whisper tab in front ("" = none: the footer's choice; "=" = as written; else a code).
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
// For nexus_kb_selftest (never set in the game): the window without MumbleLink, and sending only recorded.
struct TestHooks {
    bool forceMap = false;
    bool noSend = false;
    int sentParts = 0;
    char lastSent[1024] = {};
    char* input = nullptr;
    bool* focus = nullptr;
    int selfFixes = 0;           // corrections of your own noticed (learning mode)
    char lastFix[128] = {};      // the newest: "typed>fixed"
    char sendLang[16] = {};      // what the whisper tab opened last sends in
};
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

inline const char* WhisperTab::Label() const {
    if (label.empty()) label = UI(name) + "###tab" + std::to_string(uid);
    return label.c_str();
}

// ---------------------------------------------------------------------------------------------------------------
// One worker thread: translations, spelling and sending never run on the game's threads.
class Worker {
public:
    void Start() {
        stop_ = false;
        thread_ = std::thread([this] { Run(); });
    }
    // Pending jobs are dropped; `last` still runs on the worker (it frees what the worker owns) before it ends.
    void Stop(std::function<void()> last = {}) {
        {
            std::lock_guard<std::mutex> lock(mu_);
            jobs_.clear();
            if (last) jobs_.push_back(std::move(last));
            stop_ = true;
        }
        cv_.notify_all();
        if (thread_.joinable()) thread_.join();
    }
    void Post(std::function<void()> job) {
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (jobs_.size() > 96) jobs_.pop_front();  // a flood (WvW): the oldest wait no longer
            jobs_.push_back(std::move(job));
        }
        cv_.notify_one();
    }

private:
    void Run() {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);  // language detection, spell checker, word prediction
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lock(mu_);
                cv_.wait(lock, [this] { return stop_ || !jobs_.empty(); });
                if (jobs_.empty()) break;  // stopped, nothing left
                job = std::move(jobs_.front());
                jobs_.pop_front();
            }
            try {
                job();
            } catch (...) {
                Log(LOGL_WARNING, "a background job failed");
            }
        }
        CoUninitialize();
    }
    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<std::function<void()>> jobs_;
    bool stop_ = false;
    std::thread thread_;
};

Worker g_worker;

// Only the worker touches these (no lock needed).
SpellService* g_spell = nullptr;
SendGuard g_guard;
MumbleLink* g_mumbleWorker = nullptr;

// ---------------------------------------------------------------------------------------------------------------
// State shared between the game thread (events, drawing) and the worker. All behind g_mu.
struct ChatLine {
    uint64_t id = 0;
    Channel channel = Channel::Unknown;
    bool outgoing = false;
    bool own = false;
    std::wstring speaker, text, translated;
    std::wstring speakerFolded;

    // Pre-parsed/pre-encoded cache for 0-allocation rendering on the main thread
    std::string speakerDisplay;
    std::string u8text;
    std::string u8translated;
    std::vector<PieUI::ChatLinks::LinkSegment> textSegs;
    std::vector<PieUI::ChatLinks::LinkSegment> transSegs;
    ULONGLONG receivedTime = 0;
    uint32_t mapId = 0;
};

struct TypingHelp {  // the worker's answer for one text + caret
    std::wstring text;
    size_t caret = 0;
    WordSuggestions bar;
    WordChoices choices;
};

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
// The input line.
char g_input[1024] = {};
float g_wrapWidth = 0;  // the input's text width: soft line breaks ('\n' in place of a space) keep words whole
std::string Flat(const char* s) {  // the input as it is meant: one line
    std::string f = s;
    for (char& c : f)
        if (c == '\n' || c == '\r') c = ' ';
    return f;
}
std::string g_flatInput;
std::wstring g_wFlatInput;
std::string g_lastInput;
ULONGLONG g_inputChanged = 0;
uint64_t g_inputGen = 0;
std::wstring g_preview;  // the translation of the input
std::wstring g_back;     // ... and that translated back into your language (small, under it)
bool g_inMap = false;    // MumbleLink: a map is loaded (not character select, not a loading screen)
uint64_t g_previewGen = 0;
bool g_sendPending = false;  // Enter before the translation was there
std::wstring g_status;
ULONGLONG g_statusUntil = 0;
ULONGLONG g_suppressChatFocusUntil = 0;
TypingHelp g_help;
bool g_helpRequested = false;
std::deque<std::wstring> g_history;
int g_historyPos = -1;
// The channel to write in and the send language.
Channel g_channel = Channel::Unknown;  // Unknown = the channel active in GW2
std::wstring g_whisperTo;
int g_sendIdx = 0;                     // index into g_cfg.writeLangs; == size: send as written
bool g_sending = false;
bool g_focusInput = false;
// An edit the worker or a click wants in the input (applied by the input callback on the game thread).
struct PendingEdit {
    bool active = false;
    size_t startByte = 0, lenBytes = 0;
    std::string insert;
} g_edit;
int g_pick = -1;  // Tab / Shift+Tab move through the word bar; Space writes the picked word

struct AutoUndo {  // the last autocorrection: Backspace right after it brings the typed word back
    bool active = false;
    std::string after;   // the buffer right after the correction (with its space)
    std::string before;  // the buffer as typed (without the space)
    std::wstring original;
} g_undo;
std::wstring g_finishedLast;  // learning mode: the last word when a space last followed it (see NoteSelfFix)
size_t g_finishedAt = 0;

// Voice (stage 4): the helper process gw2kb_voice.exe runs the recognizer outside the game.
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
// The push-to-talk key (our own, through Nexus' WndProc: GW2 does not get it) and "Set key ..." capturing the next one.
std::atomic<unsigned> g_pttVk{0}, g_pttMods{0};
std::atomic<bool> g_captureKey{false};
std::wstring g_capturedKey;        // the captured key, picked up by the options (g_mu)

// What each voice model needed when it last recognized you ([VoiceStats] model = peak %,recognize ms,speech ms,
// highest peak %): shown in Options → Voice, and the reason for the automatic fallback.
struct VoiceRun {
    int peak = 0, rms = 0, rec = 0, maxPeak = 0;
    int slow = 0;  // too slow in a row (this session)
};
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
    g_layout.inputLines = std::clamp(get(L"InputLines", 4), 1, 8);
    g_layout.previewLines = std::clamp(get(L"PreviewLines", 3), 1, 8);
    g_look.glow = get(L"Glow", 1) != 0;
    g_look.brightness = std::clamp(get(L"Brightness", 100), 60, 160);
    g_look.gameColors = get(L"GameColors", 1) != 0;
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
    g_cfg.SaveValue(L"NexusWindow", L"InputLines", std::to_wstring(g_layout.inputLines));
    g_cfg.SaveValue(L"NexusWindow", L"PreviewLines", std::to_wstring(g_layout.previewLines));
    g_cfg.SaveValue(L"NexusWindow", L"Glow", g_look.glow ? L"1" : L"0");
    g_cfg.SaveValue(L"NexusWindow", L"Brightness", std::to_wstring(g_look.brightness));
    g_cfg.SaveValue(L"NexusWindow", L"GameColors", g_look.gameColors ? L"1" : L"0");
}

void SetStatus(const std::wstring& s, ULONGLONG ms = 6000) {  // g_mu held
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
void TranslateLine(uint64_t id, std::wstring text, bool asked = false) {
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
        if (!g_nx.translateChat && !asked) return;
        tr = g_translator;
        target = TypingLang();
        understood = g_cfg.understoodLangs;
        skipTyping = g_cfg.skipTypingLang;
    }
    if (!tr) return;
    if (asked && detected.empty()) detected = DetectLanguage(text);
    if (!asked) {
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

void SendNow(std::vector<std::wstring> parts, std::wstring typed, std::wstring learnText);

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

// ---------------------------------------------------------------------------------------------------------------
// The voice helper: a hidden process with pipes; its events are read on our own thread.
class VoiceHelper {
public:
    bool Start(const std::wstring& exe, const std::wstring& dataDir) {
        Stop();
        SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
        HANDLE inRead = nullptr, outWrite = nullptr;
        if (!CreatePipe(&inRead, &inWrite_, &sa, 0)) return false;
        if (!CreatePipe(&outRead_, &outWrite, &sa, 0)) {
            CloseHandle(inRead);
            Close();
            return false;
        }
        SetHandleInformation(inWrite_, HANDLE_FLAG_INHERIT, 0);  // our ends stay ours
        SetHandleInformation(outRead_, HANDLE_FLAG_INHERIT, 0);
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = inRead;
        si.hStdOutput = outWrite;
        si.hStdError = outWrite;
        std::wstring cmd = L"\"" + exe + L"\" --data \"" + dataDir + L"\"";
        PROCESS_INFORMATION pi{};
        const BOOL ok = CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, TRUE,
                                       CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, nullptr, nullptr, &si, &pi);
        CloseHandle(inRead);
        CloseHandle(outWrite);
        if (!ok) {
            Close();
            return false;
        }
        CloseHandle(pi.hThread);
        process_ = pi.hProcess;
        reader_ = std::thread([this] { Read(); });
        return true;
    }
    void Send(const char* cmd) {
        if (!inWrite_) return;
        const std::string line = std::string(cmd) + "\n";
        DWORD n = 0;
        WriteFile(inWrite_, line.data(), static_cast<DWORD>(line.size()), &n, nullptr);
    }
    void Stop() {
        if (process_) {
            Send("quit");
            if (WaitForSingleObject(process_, 2000) != WAIT_OBJECT_0) TerminateProcess(process_, 0);
        }
        Close();
        if (reader_.joinable()) reader_.join();  // its pipe is closed: ReadFile returns
    }
    bool Running() const { return process_ != nullptr; }

private:
    void Close() {
        if (inWrite_) CloseHandle(inWrite_);
        if (process_) CloseHandle(process_);
        inWrite_ = process_ = nullptr;
        if (outRead_ && !reader_.joinable()) {
            CloseHandle(outRead_);
            outRead_ = nullptr;
        }
    }
    void Read() {
        std::string pending;
        char buf[2048];
        DWORD n = 0;
        while (ReadFile(outRead_, buf, sizeof(buf), &n, nullptr) && n > 0) {
            pending.append(buf, n);
            size_t nl;
            while ((nl = pending.find('\n')) != std::string::npos) {
                const std::string line = pending.substr(0, nl);
                pending.erase(0, nl + 1);
                try {
                    OnLine(line);
                } catch (...) {
                }
            }
        }
        CloseHandle(outRead_);
        outRead_ = nullptr;
        std::lock_guard<std::mutex> lock(g_mu);
        g_voiceReady = g_recording = false;
        g_voiceLive.clear();
    }
    static void OnLine(const std::string& line);
    HANDLE process_ = nullptr, inWrite_ = nullptr, outRead_ = nullptr;
    std::thread reader_;
};

VoiceHelper g_voice;

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
    g_voice.Send("reload");
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

void InstallVoiceModuleNow();

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

UINT OnWndProc(HWND, UINT msg, WPARAM wp, LPARAM lp) {
    if (g_disabled) return msg;
    const bool down = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN, up = msg == WM_KEYUP || msg == WM_SYSKEYUP;
    if (!down && !up) return msg;
    const unsigned vk = static_cast<unsigned>(wp);
    if (vk == VK_CONTROL || vk == VK_SHIFT || vk == VK_MENU || vk == VK_LWIN || vk == VK_RWIN || vk == VK_LCONTROL ||
        vk == VK_RCONTROL || vk == VK_LSHIFT || vk == VK_RSHIFT || vk == VK_LMENU || vk == VK_RMENU)
        return msg;
    const unsigned mods = (GetKeyState(VK_CONTROL) < 0 ? kModCtrl : 0u) | (GetKeyState(VK_MENU) < 0 ? kModAlt : 0u) |
                          (GetKeyState(VK_SHIFT) < 0 ? kModShift : 0u);
    try {
        if (g_captureKey) {  // the options wait for a key
            if (down) {
                g_captureKey = false;
                Hotkey hk;
                hk.vk = vk;
                hk.mods = mods;
                std::lock_guard<std::mutex> lock(g_mu);
                g_capturedKey = vk == VK_ESCAPE ? L"-" : FormatHotkey(hk);
            }
            return 0;
        }
        if (!g_pttVk || vk != g_pttVk) return msg;
        if (down) {
            if (lp & (1 << 30)) return 0;           // held: the key repeats
            if (mods != g_pttMods) return msg;
            OnTalkKey(nullptr, false);
        } else {
            OnTalkKey(nullptr, true);
        }
        return 0;  // the push-to-talk key is ours
    } catch (...) {
        g_disabled = true;
        return msg;
    }
}

void ApplyPushToTalkKey(const std::wstring& key) {
    const auto hk = ParseHotkey(key);
    g_pttVk = hk ? hk->vk : 0u;
    g_pttMods = hk ? hk->mods : 0u;
}

// Rate limiter against chat flood / DoS attacks (max 8 auto-translations per 3s window)
static bool AllowAutoTranslate() {
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

std::wstring NativeName(const std::wstring& code);

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
void ActivateCustomTab(int i, bool focus = true) {  // focus: the input takes the keyboard (not under its settings)
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

void OnToggle(const char*, bool release) {
    if (!release) g_show = !g_show;
}
void OnFocusKey(const char*, bool release) {
    if (release) return;
    std::lock_guard<std::mutex> lock(g_mu);
    g_show = true;
    g_focusInput = true;
}

// ---------------------------------------------------------------------------------------------------------------
// Drawing (game thread, g_mu held).
// GW2's chat colours as the game shows them: the strong one (names) and the light one (the message).
struct ChannelLook {
    Channel channel;
    uint32_t strong, light;
};
constexpr ChannelLook kChannelLook[] = {
    {Channel::Say, 0x7DDC69, 0xCDF0C3},     {Channel::Map, 0xEB8C7D, 0xFADBCE},   {Channel::Party, 0x6EAFFF, 0xC8E1FF},
    {Channel::Squad, 0xA0E68C, 0xDFF5E2},   {Channel::Team, 0xFF554B, 0xFFBEB9},  {Channel::Whisper, 0xC88CFF, 0xE8D2FF},
    {Channel::Guild, 0xF5C850, 0xF5E6B4},   {Channel::System, 0xF7F92E, 0xF7F978},
};
ImVec4 Rgb32(uint32_t rgb) { return ImVec4(((rgb >> 16) & 0xFF) / 255.f, ((rgb >> 8) & 0xFF) / 255.f, (rgb & 0xFF) / 255.f, 1.f); }
ImVec4 Bright(ImVec4 c) {  // the brightness of your choice
    const float b = g_look.brightness / 100.0f;
    return ImVec4(std::min(1.0f, c.x * b), std::min(1.0f, c.y * b), std::min(1.0f, c.z * b), c.w);
}
const ChannelLook* LookOf(Channel ch) {
    for (const ChannelLook& l : kChannelLook)
        if (l.channel == ch) return &l;
    return nullptr;
}
ImVec4 ColorOf(Channel ch) {  // the message
    const ChannelLook* l = LookOf(ch);
    if (!l) return Bright(ImVec4(0.9f, 0.9f, 0.92f, 1.f));
    return Bright(Rgb32(g_look.gameColors ? l->light : l->strong));
}
ImVec4 NameColorOf(Channel ch) {  // the name in front of it
    const ChannelLook* l = LookOf(ch);
    if (!g_look.gameColors || !l) return Bright(ImVec4(0.95f, 0.95f, 0.97f, 1.f));
    return Bright(Rgb32(l->strong));
}

// Text as GW2 draws it: a soft dark halo around the letters (when on), then the letters. `wrap`: lines wrap at the
// right edge and stand under each other at the same x (like TextWrapped). It is an item: hover and click work.
void LookText(const char* text, ImVec4 color, bool wrap) {
    ImFont* font = ImGui::GetFont();
    const float fs = ImGui::GetFontSize();
    const float wrapW = wrap ? std::max(1.0f, ImGui::GetContentRegionAvail().x) : 0.0f;
    const ImVec2 size = font->CalcTextSizeA(fs, FLT_MAX, wrapW, text);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(size.x, std::max(size.y, ImGui::GetTextLineHeight())));
    if (!ImGui::IsItemVisible()) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (g_look.glow) {
        const float d = std::max(1.0f, std::round(fs / 15.0f));
        const ImU32 halo = IM_COL32(0, 0, 0, 160);
        const ImVec2 offsets[] = {ImVec2(-d, -d), ImVec2(d, -d), ImVec2(-d, d), ImVec2(d, d)};
        for (const ImVec2& o : offsets) dl->AddText(font, fs, ImVec2(p.x + o.x, p.y + o.y), halo, text, nullptr, wrapW);
    }
    dl->AddText(font, fs, p, ImGui::GetColorU32(color), text, nullptr, wrapW);
}

// The grey text after the caret: what Space writes (a completion shows its rest, a
// correction "> word"); after a space the sure next word or phrase. `replace` gets what Tab takes.
std::wstring GhostRest(const std::wstring& text, bool* correction = nullptr) {
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
                if (n > 0) g_pick = ImGui::GetIO().KeyShift ? (g_pick <= 0 ? n - 1 : g_pick - 1) : (g_pick + 1) % n;
                return 1;
            }
            if (ch == ' ') return SpaceWrites() ? 1 : 0;  // the picked / grey word, then the space
            return 0;
        }
        case ImGuiInputTextFlags_CallbackAlways: {
            if (g_edit.active) {  // a word of the bar or an autocorrection
                g_edit.active = false;
                if (g_edit.startByte + g_edit.lenBytes <= static_cast<size_t>(d->BufTextLen)) {
                    d->DeleteChars(static_cast<int>(g_edit.startByte), static_cast<int>(g_edit.lenBytes));
                    d->InsertChars(static_cast<int>(g_edit.startByte), g_edit.insert.c_str());
                    d->CursorPos = d->SelectionStart = d->SelectionEnd = d->BufTextLen;
                }
                break;
            }
            // Soft line breaks: the text is broken at spaces to the box's width (only when it changed).
            if (g_wrapWidth > 0) {
                static std::string lastSeen;
                const std::string raw(d->Buf, d->BufTextLen);
                if (raw != lastSeen) {
                    std::string wrapped = Flat(raw.c_str());
                    size_t lineStart = 0, lastSpace = std::string::npos;
                    for (size_t i = 0; i < wrapped.size(); ++i) {
                        if (wrapped[i] == ' ') lastSpace = i;
                        if ((static_cast<unsigned char>(wrapped[i]) & 0xC0) == 0x80) continue;  // inside a character
                        if (lastSpace != std::string::npos && lastSpace >= lineStart &&
                            ImGui::CalcTextSize(wrapped.data() + lineStart, wrapped.data() + i + 1).x > g_wrapWidth) {
                            wrapped[lastSpace] = '\n';
                            lineStart = lastSpace + 1;
                            lastSpace = std::string::npos;
                        }
                    }
                    if (wrapped != raw) {
                        std::memcpy(d->Buf, wrapped.data(), wrapped.size());
                        d->BufDirty = true;
                    }
                    lastSeen = wrapped;
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

bool g_collapsed = false;  // the V / > buttons: window minimizes to bottom-left corner
float g_headerH = 0, g_lastW = 520, g_expandedH = 360, g_restoreH = 0;
// The bottom edge the window had when it was minimised: the small button sits exactly on it (its size depends on the
// text size and ImGui's minimum window size – computing it twice made the window creep up a little every time).
float g_anchorBottom = -1.0f;
ImVec2 g_savedPos(-1, -1);   // where the window stood when it was minimised
bool g_buttonMoved = false;  // ... and whether you dragged the small button since (then it opens from there)
ImVec2 g_buttonBottomLeft(-1, -1);  // the small button's bottom-left corner (where a moved button opens from)
ImVec2 RestorePos() {  // where the window opens again
    if (!g_buttonMoved && g_savedPos.x >= 0) return g_savedPos;
    if (g_buttonBottomLeft.x >= 0) return ImVec2(g_buttonBottomLeft.x, g_buttonBottomLeft.y - g_expandedH);
    return ImVec2(-1, -1);
}

void MinimizeWindow() {  // g_mu held or main thread
    g_collapsed = true;
    const ImVec2 p = ImGui::GetWindowPos(), s = ImGui::GetWindowSize();
    g_lastW = s.x;
    g_expandedH = s.y;
    g_anchorBottom = p.y + s.y;  // the small button takes its place on the next frame
    g_savedPos = p;
    g_buttonMoved = false;
}

// The footer chips: channel | "Write: German" | "Send: English (UK) · MyMemory" (orange) | auto | counter.
void RenderFooter() {  // g_mu held
    static const Channel kChannels[] = {Channel::Unknown, Channel::Say,   Channel::Map,  Channel::Party,
                                        Channel::Squad,   Channel::Guild, Channel::Team, Channel::Whisper};
    auto chipWidth = [](const std::string& s) { return ImGui::CalcTextSize(s.c_str()).x + ImGui::GetFrameHeight() + 12; };
    const float iconH = ImGui::GetFrameHeight();

    // Native minimize button (bottom-left corner of the window)
    if (ImGui::Button(">##minchat", ImVec2(iconH, iconH))) {
        MinimizeWindow();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", UI(Tr(L"Minimize chat")).c_str());
    ImGui::SameLine();

    // Footer chips cached: precalculated strings and sizes avoid allocations on idle frames.
    struct FooterCache {
        Channel channel = Channel::Unknown;
        std::wstring whisperTo;
        std::wstring writeIn;
        std::wstring sendLang;
        bool sendOrig = false;
        std::string labelFull, labelNarrow;
        std::string labelBtnFull, labelBtnNarrow;
        std::string writeFull, writeNarrow;
        std::string writeBtnFull, writeBtnNarrow;
        std::string sendFull, sendNarrow;
        std::string sendBtnFull, sendBtnNarrow;
        float labelWFull = 0, labelWNarrow = 0;
        float writeWFull = 0, writeWNarrow = 0;
        float sendWFull = 0, sendWNarrow = 0;
        float totalNeedFull = 0;
    };
    static FooterCache s_fc;
    const bool sendOrig = SendOriginal();
    const std::wstring sendLang = SendLang();
    if (s_fc.channel != g_channel || s_fc.whisperTo != g_whisperTo || s_fc.writeIn != g_cfg.writeIn ||
        s_fc.sendLang != sendLang || s_fc.sendOrig != sendOrig) {
        s_fc.channel = g_channel;
        s_fc.whisperTo = g_whisperTo;
        s_fc.writeIn = g_cfg.writeIn;
        s_fc.sendLang = sendLang;
        s_fc.sendOrig = sendOrig;

        s_fc.labelFull = g_channel == Channel::Unknown ? UI(Tr(L"Active channel")) : UI(ChannelLabel(g_channel));
        if (g_channel == Channel::Whisper && !g_whisperTo.empty()) s_fc.labelFull += " " + U8(g_whisperTo);
        s_fc.labelNarrow = g_channel == Channel::Unknown ? UI(Tr(L"Active")) : s_fc.labelFull;
        s_fc.labelBtnFull = s_fc.labelFull + "##chbtn";
        s_fc.labelBtnNarrow = s_fc.labelNarrow + "##chbtn";

        s_fc.writeFull = UI(TrF(L"Write: {1}", {g_cfg.writeIn.empty() ? Tr(L"like Windows") : NativeName(g_cfg.writeIn)}));
        s_fc.writeNarrow = g_cfg.writeIn.empty() ? std::string("Win") : U8(ToUpperAscii(PrimaryLang(g_cfg.writeIn)));
        s_fc.writeBtnFull = s_fc.writeFull + "##writebtn";
        s_fc.writeBtnNarrow = s_fc.writeNarrow + "##writebtn";

        const std::wstring sendText = sendOrig ? Tr(L"as written") : NativeName(sendLang);
        s_fc.sendFull = UI(TrF(L"Send: {1}", {sendText}));
        s_fc.sendNarrow = "> " + (sendOrig ? UI(Tr(L"as written")) : U8(ToUpperAscii(PrimaryLang(sendLang))));
        s_fc.sendBtnFull = s_fc.sendFull + "##sendbtn";
        s_fc.sendBtnNarrow = s_fc.sendNarrow + "##sendbtn";

        s_fc.labelWFull = chipWidth(s_fc.labelFull);
        s_fc.labelWNarrow = chipWidth(s_fc.labelNarrow);
        s_fc.writeWFull = chipWidth(s_fc.writeFull);
        s_fc.writeWNarrow = chipWidth(s_fc.writeNarrow);
        s_fc.sendWFull = chipWidth(s_fc.sendFull);
        s_fc.sendWNarrow = chipWidth(s_fc.sendNarrow);
        s_fc.totalNeedFull = iconH + s_fc.labelWFull + s_fc.writeWFull + s_fc.sendWFull +
                             ImGui::GetFrameHeight() * 4 + ImGui::CalcTextSize("000/199").x;
    }

    const bool narrow = s_fc.totalNeedFull > ImGui::GetContentRegionAvail().x;
    const std::string& chBtnLabel = narrow ? s_fc.labelBtnNarrow : s_fc.labelBtnFull;
    const float chBtnW = narrow ? s_fc.labelWNarrow : s_fc.labelWFull;

    const ImVec2 chBtnPos = ImGui::GetCursorScreenPos();
    ImGui::PushStyleColor(ImGuiCol_Text, NameColorOf(g_channel == Channel::Unknown ? Channel::Say : g_channel));
    const bool chLeft = ImGui::Button(chBtnLabel.c_str(), ImVec2(chBtnW, iconH));
    ImGui::PopStyleColor();
    const bool chRight = ImGui::IsItemHovered() && ImGui::IsMouseClicked(1);

    if (chLeft || chRight) {
        ImGui::OpenPopup("##channel_popup");
    }

    // Dropdown menu opens UPWARDS directly above the button, overlaying the window upwards!
    ImGui::SetNextWindowPos(ImVec2(chBtnPos.x, chBtnPos.y - 4.0f), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(chBtnW, 0.0f), ImVec2(FLT_MAX, ImGui::GetFontSize() * 16.0f));
    if (ImGui::BeginPopup("##channel_popup")) {
        for (Channel c : kChannels) {
            const std::string name = c == Channel::Unknown ? UI(Tr(L"Active channel")) : UI(ChannelLabel(c));
            ImGui::PushStyleColor(ImGuiCol_Text, NameColorOf(c == Channel::Unknown ? Channel::Say : c));
            if (ImGui::Selectable(name.c_str(), c == g_channel)) {
                if (c != Channel::Whisper && g_activeTab >= 0) LeaveWhisperTab();  // the whisper tab is left
                g_channel = c;
                if (c != Channel::Whisper) g_whisperTo.clear();
                g_focusInput = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopStyleColor();
        }
        ImGui::EndPopup();
    }
    // The language you write in (spelling, learned words and the prediction follow it).
    ImGui::SameLine();
    const ImVec2 writeBtnPos = ImGui::GetCursorScreenPos();
    const float writeBtnW = narrow ? s_fc.writeWNarrow : s_fc.writeWFull;
    const std::string& writeBtnLabel = narrow ? s_fc.writeBtnNarrow : s_fc.writeBtnFull;
    const bool writeLeft = ImGui::Button(writeBtnLabel.c_str(), ImVec2(writeBtnW, iconH));
    const bool writeRight = ImGui::IsItemHovered() && ImGui::IsMouseClicked(1);
    if (writeLeft || writeRight) {
        ImGui::OpenPopup("##write_popup");
    }
    ImGui::SetNextWindowPos(ImVec2(writeBtnPos.x, writeBtnPos.y - 4.0f), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(writeBtnW, 0.0f), ImVec2(FLT_MAX, ImGui::GetFontSize() * 20.0f));
    if (ImGui::BeginPopup("##write_popup")) {
        std::wstring chosen = g_cfg.writeIn;
        if (ImGui::Selectable(UI(Tr(L"like Windows")).c_str(), chosen.empty())) {
            chosen.clear();
            ImGui::CloseCurrentPopup();
        }
        for (const LangInfo& l : Languages()) {
            if (l.latinScript && ImGui::Selectable(U8(l.native).c_str(), g_cfg.writeIn == l.code)) {
                chosen = l.code;
                ImGui::CloseCurrentPopup();
            }
        }
        if (chosen != g_cfg.writeIn) {
            g_cfg.writeIn = chosen;
            g_cfg.SaveValue(L"Spelling", L"WriteIn", chosen);
            g_worker.Post([] {  // spelling and learned words follow the language you write in
                FreeWorkerState();
                InitSpelling();
            });
            ++g_inputGen;
            g_inputChanged = GetTickCount64();
        }
        ImGui::EndPopup();
    }

    // What the chat gets (orange); who translates is told by the status line.
    ImGui::SameLine();
    const ImVec2 sendBtnPos = ImGui::GetCursorScreenPos();
    const float sendBtnW = narrow ? s_fc.sendWNarrow : s_fc.sendWFull;
    const std::string& sendBtnLabel = narrow ? s_fc.sendBtnNarrow : s_fc.sendBtnFull;
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.94f, 0.75f, 0.35f, 1.f));
    const bool sendLeft = ImGui::Button(sendBtnLabel.c_str(), ImVec2(sendBtnW, iconH));
    ImGui::PopStyleColor();
    const bool sendRight = ImGui::IsItemHovered() && ImGui::IsMouseClicked(1);
    if (sendLeft || sendRight) {
        ImGui::OpenPopup("##send_popup");
    }
    ImGui::SetNextWindowPos(ImVec2(sendBtnPos.x, sendBtnPos.y - 4.0f), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(sendBtnW, 0.0f), ImVec2(FLT_MAX, ImGui::GetFontSize() * 20.0f));
    if (ImGui::BeginPopup("##send_popup")) {
        for (size_t i = 0; i <= g_cfg.writeLangs.size(); ++i) {
            const std::string n = i == g_cfg.writeLangs.size() ? UI(Tr(L"as written")) : U8(NativeName(g_cfg.writeLangs[i]));
            const bool current = g_sendOverride.empty() ? static_cast<int>(i) == g_sendIdx
                                 : i == g_cfg.writeLangs.size() ? g_sendOverride == L"="
                                                                : g_sendOverride == g_cfg.writeLangs[i];
            if (ImGui::Selectable(n.c_str(), current)) {
                if (g_activeTab >= 0 && g_activeTab < static_cast<int>(g_tabs.size())) {  // this player's tab only
                    g_tabs[g_activeTab].sendLang = i == g_cfg.writeLangs.size() ? std::wstring(L"=") : g_cfg.writeLangs[i];
                    g_sendOverride = g_tabs[g_activeTab].sendLang;
                } else {
                    g_sendIdx = static_cast<int>(i);
                }
                ++g_inputGen;
                g_preview.clear();
                g_inputChanged = GetTickCount64();
                g_focusInput = true;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::Separator();
        for (const LangInfo& l : Languages()) {  // more languages: the chosen one becomes the first
            if (!l.latinScript || std::find(g_cfg.writeLangs.begin(), g_cfg.writeLangs.end(), l.code) != g_cfg.writeLangs.end())
                continue;
            if (ImGui::Selectable(U8(l.native).c_str(), false)) {
                g_cfg.writeLangs.insert(g_cfg.writeLangs.begin(), l.code);
                g_cfg.SaveAll();
                if (g_activeTab >= 0 && g_activeTab < static_cast<int>(g_tabs.size())) {  // this player's tab only
                    g_tabs[g_activeTab].sendLang = l.code;
                    g_sendOverride = l.code;
                } else {
                    g_sendIdx = 0;
                }
                ++g_inputGen;
                g_preview.clear();
                g_inputChanged = GetTickCount64();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
    if (g_cfg.voiceEnabled) {  // send right after speaking (off by default)
        ImGui::SameLine();
        if (ImGui::Checkbox(UI(Tr(L"auto") + L"##autosend").c_str(), &g_cfg.voiceAutoSend))
            g_cfg.SaveBool(L"Voice", L"AutoSend", g_cfg.voiceAutoSend);
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26);
            ImGui::TextUnformatted(UI(Tr(L"auto: send right after speaking")).c_str());
            ImGui::TextDisabled("%s", UI(Tr(L"When you stop talking (key released, or the pause after speaking), the recognized "
                                            L"text goes into the GW2 chat at once – translated if a send language is "
                                            L"chosen. Off: it waits in the box until you press Enter.")).c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    }
    const size_t n = CodePointCount(g_wFlatInput);
    std::string counter = std::to_string(n) + "/" + std::to_string(g_cfg.maxLength);
    if (n > static_cast<size_t>(g_cfg.maxLength)) {  // sent as several chat lines
        const bool current = g_previewGen == g_inputGen && !g_preview.empty();
        const size_t parts = SplitForChat(Translating() && current ? g_preview : g_wFlatInput, ComposePrefix(),
                                          static_cast<size_t>(g_cfg.maxLength)).size();
        counter += "  (" + std::to_string(parts) + "x)";
    }
    // At the right edge, never over the chips: without room it goes into the next line.
    const float counterX = ImGui::GetWindowContentRegionMax().x - ImGui::CalcTextSize(counter.c_str()).x;
    const float after = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x + ImGui::GetStyle().ItemSpacing.x * 2;
    if (counterX >= after) ImGui::SameLine(counterX);
    else ImGui::SetCursorPosX(std::max(counterX, ImGui::GetCursorPosX()));
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", counter.c_str());
}

// ---- "Events: Chat": is it there, and installing it the way the Nexus library would.
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

// ---- the header's icons: microphone, menu (three lines), close
enum class Icon { Mic, Menu, Close, Collapse, Expand };
bool IconButton(const char* id, Icon kind, bool active, bool* rightClick, ImU32 tint = 0) {
    const float s = ImGui::GetFrameHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(id, ImVec2(s, s));
    const bool hovered = ImGui::IsItemHovered();
    if (rightClick) *rightClick = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 col = active ? IM_COL32(235, 85, 85, 255)
                      : tint     ? tint
                                 : ImGui::GetColorU32(hovered ? ImGuiCol_Text : ImGuiCol_TextDisabled);
    const ImVec2 c(p.x + s * 0.5f, p.y + s * 0.5f);
    const float u = s / 18.0f;
    switch (kind) {
        case Icon::Mic:  // a capsule on a stand
            dl->AddRectFilled(ImVec2(c.x - 2.5f * u, c.y - 6 * u), ImVec2(c.x + 2.5f * u, c.y + 1.5f * u), col, 2.5f * u);
            dl->PathArcTo(ImVec2(c.x, c.y - 1 * u), 4.5f * u, 0.0f, 3.14159f, 10);
            dl->PathStroke(col, false, 1.4f * u);
            dl->AddLine(ImVec2(c.x, c.y + 3.5f * u), ImVec2(c.x, c.y + 6 * u), col, 1.4f * u);
            dl->AddLine(ImVec2(c.x - 3 * u, c.y + 6 * u), ImVec2(c.x + 3 * u, c.y + 6 * u), col, 1.4f * u);
            break;
        case Icon::Menu:  // ≡
            for (int i = -1; i <= 1; ++i)
                dl->AddLine(ImVec2(c.x - 5.5f * u, c.y + i * 4 * u), ImVec2(c.x + 5.5f * u, c.y + i * 4 * u), col, 1.6f * u);
            break;
        case Icon::Collapse:  // V: fold the window up
        case Icon::Expand: {  // A: open it again
            const float d = kind == Icon::Collapse ? 1.0f : -1.0f;
            dl->AddLine(ImVec2(c.x - 5 * u, c.y - 2.5f * u * d), ImVec2(c.x, c.y + 2.5f * u * d), col, 1.6f * u);
            dl->AddLine(ImVec2(c.x, c.y + 2.5f * u * d), ImVec2(c.x + 5 * u, c.y - 2.5f * u * d), col, 1.6f * u);
            break;
        }
        case Icon::Close:  // ×
            dl->AddLine(ImVec2(c.x - 4.5f * u, c.y - 4.5f * u), ImVec2(c.x + 4.5f * u, c.y + 4.5f * u), col, 1.6f * u);
            dl->AddLine(ImVec2(c.x - 4.5f * u, c.y + 4.5f * u), ImVec2(c.x + 4.5f * u, c.y - 4.5f * u), col, 1.6f * u);
            break;
    }
    return pressed;
}

// Cut to the width with "…" at the end.
std::string Fit(const std::string& s, float width) {
    if (ImGui::CalcTextSize(s.c_str()).x <= width) return s;
    const std::wstring w = W(s);
    const float ellipsisW = ImGui::CalcTextSize("...").x;
    const float targetW = width - ellipsisW;
    if (targetW <= 0.0f) return "...";
    size_t low = 0, high = w.size();
    while (low < high) {
        const size_t mid = low + (high - low + 1) / 2;
        const std::string sub = U8(w.substr(0, mid));
        if (ImGui::CalcTextSize(sub.c_str()).x <= targetW) low = mid;
        else high = mid - 1;
    }
    return U8(w.substr(0, low)) + "...";
}

// The status text (drawn in the input box): a message, else what is going on.
std::wstring StatusText() {  // g_mu held
    if (!g_status.empty()) return g_status;
    if (g_recording) return Tr(L"Hearing you … release the key when you are done");
    if (!g_listenPausedFor.empty()) return TrF(L"Voice activation paused – {1} uses the microphone", {g_listenPausedFor});
    if (g_listening) return Tr(L"Voice activation listens – just speak");
    const bool voiceEnglish = g_cfg.voiceEnabled && g_cfg.voiceTranslate;
    if (!g_nx.translateChat && !Translating())  // nothing goes to a translator: no counter of one
        return voiceEnglish ? Tr(L"Translation off · speech into English (whisper)") : Tr(L"Translation off");
    if (const std::wstring q = MyMemoryQuotaText(); !q.empty()) return q;
    std::wstring route = SendOriginal() ? Tr(L"as written") : ToUpperAscii(PrimaryLang(SendLang()));
    if (Translating() && g_translator) route += L" · " + g_translator->Name();
    return TrF(L"Send: {1}", {route});
}

// ---- voice model download (its own thread: minutes, never on the worker or the game's threads)
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
        if (ok) g_voice.Send("reload");
    });
}

// ---- the modules: the chat (Events: Chat), the voice module (gw2kb_voice.exe + whisper DLLs), the voice model
std::atomic<bool> g_voiceModuleInstalling{false};
std::atomic<int> g_voiceModulePct{-1};
std::thread g_voiceModuleThread;
int g_optionsTab = -1;  // a tab of the options window to bring to the front (General 0, Voice 2, Translator 4)

std::wstring KeyboardDir() { return AddonsDir() + L"GW2Keyboard"; }

struct ModuleState {
    bool voiceModule = false;  // helper and DLLs are there
    bool model = false;        // the chosen (or recommended) model is installed
    std::string modelName;
};
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

// The first start: what GW2 Keyboard needs comes by itself – Events: Chat (Nexus loads it), the voice module and,
// when no voice model is there yet, the small base model. Then it says what is ready and where translation is switched on.
void FirstStart() {
    if (GetFileAttributesW((AddonsDir() + kChatEventsDllName).c_str()) == INVALID_FILE_ATTRIBUTES) InstallChatEventsNow();
    if (!VoiceModulePresent(KeyboardDir())) InstallVoiceModuleNow();
    Config c;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        c = g_cfg;
    }
    const VoiceCatalog catalog = LoadVoiceCatalog(c.dataDir);
    const VoiceModelInfo* chosen = catalog.Find(U8(c.voiceModel));
    if (!chosen) chosen = catalog.Recommended();
    if (c.voiceEnabled && !(chosen && VoiceModelInstalled(c.VoiceModelDir(), *chosen))) {
        const VoiceModelInfo* use = nullptr;  // one that is there already, else the base model
        for (const VoiceModelInfo& m : catalog.models)
            if (!use && VoiceModelInstalled(c.VoiceModelDir(), m)) use = &m;
        const bool download = !use;
        if (!use) use = catalog.Find("base-q5_1");
        if (use) {
            {
                std::lock_guard<std::mutex> lock(g_mu);
                g_cfg.voiceModel = W(use->id);
                g_cfg.SaveValue(L"Voice", L"Model", g_cfg.voiceModel);
            }
            if (download) StartModelDownload(*use);
        }
    }
    std::lock_guard<std::mutex> lock(g_mu);
    g_nx.setupDone = true;
    SaveNexusOptions();
}

// Starting / ending the voice module by hand (it also starts by itself on the first use of the microphone).
void LoadVoiceModule() {
    std::wstring dataDir;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        dataDir = g_cfg.dataDir;
    }
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

void StatusRow(const std::string& what, int state, const std::string& text) {  // state: 2 ready, 1 busy, 0 missing
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(state == 2 ? ImVec4(0.45f, 0.85f, 0.45f, 1.f) : state == 1 ? ImVec4(0.94f, 0.75f, 0.35f, 1.f)
                                                                                  : ImVec4(0.95f, 0.55f, 0.35f, 1.f),
                       "%s", state == 2 ? "[OK]" : state == 1 ? "[..]" : "[--]");
    ImGui::SameLine();
    ImGui::TextUnformatted(what.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%s", text.c_str());
}

// What is ready (chat, voice, model), with the buttons that load what is missing; `guide`: and where to go next.
void RenderModules(bool guide) {  // g_mu NOT held
    const ModuleState& m = Modules();
    ImGui::PushID("modules");
    // The chat.
    const bool chatLive = g_chatEventsLive || g_chatEventsPresent;
    StatusRow(UI(Tr(L"Chat module")), chatLive ? 2 : g_chatEventsInstalling || g_chatEventsFile ? 1 : 0,
              UI(chatLive ? Tr(L"ready – Events: Chat is loaded")
                 : g_chatEventsInstalling ? Tr(L"loading Events: Chat …")
                 : g_chatEventsFile ? Tr(L"installed – Nexus loads it (else: Nexus → Addons → Events: Chat → Load)")
                                    : Tr(L"Events: Chat is missing")));
    if (!chatLive && !g_chatEventsFile && !g_chatEventsInstalling) {
        ImGui::SameLine();
        if (ImGui::SmallButton(UI(Tr(L"Install Events: Chat")).c_str())) InstallChatEventsNow();
    }
    // The voice module and its model.
    bool enabled, ready;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        enabled = g_cfg.voiceEnabled;
        ready = g_voiceReady;
    }
    if (enabled) {
        const int pct = g_voiceModulePct;
        const bool running = g_voice.Running();
        StatusRow(UI(Tr(L"Voice module")), m.voiceModule ? 2 : g_voiceModuleInstalling ? 1 : 0,
                  UI(g_voiceModuleInstalling ? TrF(L"loading … {1} %", {std::to_wstring(std::max(pct, 0))})
                     : !m.voiceModule        ? Tr(L"missing")
                     : ready                 ? Tr(L"ready – loaded")
                     : running               ? Tr(L"starting")
                                             : Tr(L"ready – loads on the first use of the microphone")));
        ImGui::SameLine();
        if (!m.voiceModule && !g_voiceModuleInstalling) {
            if (ImGui::SmallButton(UI(Tr(L"Load voice module")).c_str())) InstallVoiceModuleNow();
        } else if (m.voiceModule && !running) {
            if (ImGui::SmallButton(UI(Tr(L"Load")).c_str())) LoadVoiceModule();
        } else if (running) {
            if (ImGui::SmallButton(UI(Tr(L"Unload")).c_str())) UnloadVoiceModule();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", UI(Tr(L"Ends the voice module and frees its memory; the microphone loads it again.")).c_str());
        }
        const int dl = g_downloadPct;
        StatusRow(UI(Tr(L"Voice model")), m.model ? 2 : dl >= 0 ? 1 : 0,
                  dl >= 0 ? UI(TrF(L"downloading … {1} %", {std::to_wstring(dl)}))
                          : m.modelName + (m.model ? "" : "  " + UI(Tr(L"missing"))));
        if (!m.model && dl < 0) {
            ImGui::SameLine();
            if (ImGui::SmallButton(UI(Tr(L"Download the model")).c_str())) {
                Config c;
                {
                    std::lock_guard<std::mutex> lock(g_mu);
                    c = g_cfg;
                }
                const VoiceCatalog catalog = LoadVoiceCatalog(c.dataDir);
                const VoiceModelInfo* chosen = catalog.Find(U8(c.voiceModel));
                if (!chosen) chosen = catalog.Recommended();
                if (chosen) StartModelDownload(*chosen);
            }
        }
    }
    if (guide) {
        ImGui::Spacing();
        bool chat, mine, voiceEnglish;
        {
            std::lock_guard<std::mutex> lock(g_mu);
            chat = g_nx.translateChat;
            mine = g_cfg.translateWriting;
            voiceEnglish = g_cfg.voiceTranslate;
        }
        if (!chat || !mine) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", UI(Tr(L"Translate the chat or your text: Options → Translator")).c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton(UI(Tr(L"Open") + L"##tr").c_str())) {
                g_showOptions = true;
                g_optionsTab = 4;
            }
        }
        if (enabled && !voiceEnglish) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", UI(Tr(L"Turn what you say into English: Options → Voice")).c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton(UI(Tr(L"Open") + L"##vo").c_str())) {
                g_showOptions = true;
                g_optionsTab = 2;
            }
        }
    }
    ImGui::PopID();
}

// ---- chat codes: their own module (chat_links) – "[Item name]" as the game links them, the details on mouse-over.
ChatLinks g_links;

std::wstring ApiLang() {  // the GW2 API speaks en, de, fr, es, zh (g_mu held)
    if (!g_nx.namesLang.empty()) return g_nx.namesLang;  // like the game
    const std::wstring p = ToLowerAscii(PrimaryLang(TypingLang()));
    return p == L"de" || p == L"fr" || p == L"es" || p == L"zh" ? p : L"en";
}

// Words you corrected yourself while typing ("shcon" → "schon", this session): a right-click in the input offers to
// learn them, so autocorrection writes them right next time.
struct SelfFix {
    std::wstring typed, fixed;
};
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

// A chat line that wraps word by word with its links (blue, underlined: a click asks before anything opens) and chat
// codes (item/skill/waypoint name in rarity colour via Decoder Ring, hover tooltip with details, click copies code).
bool g_openLinkAsk = false;

// A chat code clicked. A waypoint: our window gets out of the way, you click the waypoint in GW2's own chat (the game
// opens the world map on it), and the window comes back once the map was open and closed again (or after 45 s).
// Anything else: the code is copied (Ctrl+V puts it into a message).
bool g_minimizeRequest = false, g_expandRequest = false;  // on the main window, never inside the chat area
int g_waypointWait = 0;                                     // 0 none, 1 waiting for the map, 2 the map is open
ULONGLONG g_waypointSince = 0;

void HandleChatLinkClick(const std::string& raw) {
    ImGui::SetClipboardText(raw.c_str());
    if (DecodeChatCode(W(raw)).kind == ChatCodeKind::Poi) {
        g_minimizeRequest = true;
        g_waypointWait = 1;
        g_waypointSince = GetTickCount64();
        SetStatus(Tr(L"Click the waypoint in GW2's chat – the window comes back when the map closes"), 8000);
    } else {
        SetStatus(Tr(L"Chat code copied – Ctrl+V puts it into a message"), 4000);
    }
}

bool RichLine(const std::wstring& text, const std::vector<PieUI::ChatLinks::LinkSegment>& segs, ImVec4 color) {  // g_mu held
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
                    HandleChatLinkClick(seg.raw);
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
                    ImGui::TextDisabled("%s", ToUtf8(Tr(L"Click: copy the code")).c_str());
                    ImGui::EndTooltip();
                }
                if (ImGui::IsItemClicked()) {
                    clicked = true;
                    const std::string toCopy = !seg.ae2ChatLink.empty() ? seg.ae2ChatLink : seg.raw;
                    HandleChatLinkClick(toCopy);
                }
                break;
            }
        }
    }
    ImGui::EndGroup();
    return clicked;
}

bool g_micLocked = false;  // right-click on the microphone: on until the next right-click
bool g_compact = false;    // menu ≡: like GW2's own chat – no preview, no word bar; the translation small in the input

void RenderOptionsContent();

void Hint(const std::string& text) {  // grey and wrapped: long hints are never cut off
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopStyleColor();
}

void RenderOptionsWindow() {
    if (!g_showOptions) return;
    ImGui::SetNextWindowSize(ImVec2(660, 620), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(600, 420), ImVec2(4000, 4000));
    static std::string s_optsTitle;
    static UiLang s_optsLang = static_cast<UiLang>(-1);
    if (s_optsLang != g_cfg.uiLang || s_optsTitle.empty()) {
        s_optsLang = g_cfg.uiLang;
        s_optsTitle = UI(Tr(L"GW2 Keyboard – Options")) + "###kbopts";
    }
    if (ImGui::Begin(s_optsTitle.c_str(), &g_showOptions)) RenderOptionsContent();
    ImGui::End();
}

static void SafeOpenUrl(const std::wstring& rawLink) {
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

// Text drawn into the input box (status, the translation in the compact view): on top, clipped to the box.
void BoxText(const ImVec2& at, const ImVec4& clip, ImU32 color, const std::string& text, float scale = 1.0f) {
    ImGui::GetForegroundDrawList()->AddText(ImGui::GetFont(), ImGui::GetFontSize() * scale, at, color, text.c_str(), nullptr,
                                            0, &clip);
}

void RenderWindow() {
    if (!ImGui::GetCurrentContext()) return;  // failsafe: no context, no drawing
    ImGuiIO& io = ImGui::GetIO();
    if (io.DisplaySize.x <= 0 || io.DisplaySize.y <= 0) return;
    g_keyboardFree = !io.WantCaptureKeyboard;
    g_fps = static_cast<int>(io.Framerate);
    {
        // MumbleLink (every 250 ms): shown only on a map – never at character select or on a loading screen (like the
        // exe waiting for GW2); your character's name; the GW2 chat line opening (focus behaviour).
        static ULONGLONG next = 0;
        static MumbleLink mumble;
        static bool chatWasOpen = false;
        if (GetTickCount64() > next) {
            next = GetTickCount64() + 250;
            const MumbleState ms = mumble.Read();
            std::lock_guard<std::mutex> lock(g_mu);
            g_inMap = ms.live && ms.inMap;
            if (ms.live) {
                g_ownName = ms.identity.name;
                g_uiSize.store(ms.identity.uiSize);
                g_mapId.store(ms.identity.mapId);
            }
            const bool chatOpen = ms.live && ms.TextboxHasFocus();
            if (chatOpen && !chatWasOpen && g_cfg.focusOnGameChat && g_show && GetTickCount64() > g_suppressChatFocusUntil &&
                g_waypointWait == 0)
                g_focusInput = true;
            if (g_waypointWait == 1 && ms.MapOpen()) {
                g_waypointWait = 2;
            } else if ((g_waypointWait == 2 && !ms.MapOpen()) ||
                       (g_waypointWait == 1 && GetTickCount64() - g_waypointSince > 45000)) {
                g_waypointWait = 0;
                g_expandRequest = true;
            }
            chatWasOpen = chatOpen;
        }
    }
    ChatEventsPresent();
    RenderOptionsWindow();
    bool inMap;
    static bool s_uncollapseRestore = false;
    static ImVec2 s_restorePos(-1, -1);
    {
        std::lock_guard<std::mutex> lock(g_mu);
        inMap = g_inMap || g_test.forceMap;
        if (g_focusInput && g_collapsed) {  // something wants the input: the window opens again
            g_collapsed = false;
            g_restoreH = g_expandedH;
            s_uncollapseRestore = true;
            s_restorePos = RestorePos();
        }
    }
    if (!g_show || !inMap) return;

    const float iconH = ImGui::GetFrameHeight();
    const float minBtn = iconH + 8.0f;
    if (g_collapsed) {
        ImGui::SetNextWindowSize(ImVec2(minBtn, minBtn), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(std::clamp(g_cfg.opacity, 60, 255) / 255.0f);
        ImGuiWindowFlags minFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_NoScrollWithMouse;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(2, 2));
        const bool beginOk = ImGui::Begin("GW2Chatlator###kbmain", nullptr, minFlags);
        ImGui::PopStyleVar();
        if (beginOk) {
            if (g_anchorBottom > 0) {  // just minimised: the button's bottom edge = the window's bottom edge
                const ImVec2 wp = ImGui::GetWindowPos();
                ImGui::SetWindowPos(ImVec2(wp.x, g_anchorBottom - ImGui::GetWindowSize().y));
                g_anchorBottom = -1.0f;
            }
            const float resScale = std::max(1.0f, io.DisplaySize.y / 1080.0f);
            const float maxBottom = std::max(minBtn, io.DisplaySize.y - Gw2BottomBarOffset(io.DisplaySize.y, g_uiSize.load()));
            const ImVec2 pos = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
            g_buttonBottomLeft = ImVec2(pos.x, pos.y + size.y);
            static bool s_btnDrag = false;
            static ImVec2 s_btnStart;
            if (ImGui::IsMouseClicked(0) && ImGui::IsWindowHovered()) {
                s_btnDrag = true;
                s_btnStart = pos;
            } else if (s_btnDrag && !ImGui::IsMouseDown(0)) {
                s_btnDrag = false;
                if (pos.x != s_btnStart.x || pos.y != s_btnStart.y) {  // you moved it: it opens from here, snapped
                    g_buttonMoved = true;
                    if (g_nx.snap) {
                        const float t = 14.0f * resScale;
                        ImVec2 p = pos;
                        const float uiScale = (g_uiSize.load() == 0 ? 0.89f : g_uiSize.load() == 2 ? 1.11f : g_uiSize.load() == 3 ? 1.25f : 1.0f);
                        const float nativeChatLeft = std::round(10.0f * uiScale * resScale);
                        if (std::abs(p.x - nativeChatLeft) < t) p.x = nativeChatLeft;
                        else if (p.x < t) p.x = 0.0f;
                        else if (p.x + size.x > io.DisplaySize.x - t) p.x = io.DisplaySize.x - size.x;
                        if (p.y < t) p.y = 0.0f;
                        else if (p.y + size.y > maxBottom - t) p.y = maxBottom - size.y;
                        if (p.x != pos.x || p.y != pos.y) ImGui::SetWindowPos(p);
                    }
                }
            }
            // Always prevent being pushed below the bottom bar or off-screen
            {
                ImVec2 p = ImGui::GetWindowPos();
                bool clamped = false;
                if (p.x < -0.5f) { p.x = 0.0f; clamped = true; }
                else if (p.x + size.x > io.DisplaySize.x + 0.5f) { p.x = std::max(0.0f, io.DisplaySize.x - size.x); clamped = true; }
                if (p.y < -0.5f) { p.y = 0.0f; clamped = true; }
                else if (p.y + size.y > maxBottom + 0.5f) { p.y = std::max(0.0f, maxBottom - size.y); clamped = true; }
                if (clamped && !ImGui::IsMouseDown(0)) ImGui::SetWindowPos(p);
            }
            const bool clickedOpen = ImGui::Button("<##expand", ImVec2(iconH, iconH));
            if (clickedOpen || g_expandRequest) {  // the bottom edge stays where it is; the window grows upwards
                std::lock_guard<std::mutex> lock(g_mu);
                g_expandRequest = false;
                g_waypointWait = 0;
                g_collapsed = false;
                g_restoreH = g_expandedH;
                s_uncollapseRestore = true;
                g_suppressChatFocusUntil = 0;
                const ImVec2 p = ImGui::GetWindowPos(), s = ImGui::GetWindowSize();
                g_buttonBottomLeft = ImVec2(p.x, p.y + s.y);
                s_restorePos = RestorePos();
                ImGui::SetWindowPos(s_restorePos);
                ImGui::SetWindowSize(ImVec2(g_lastW, g_expandedH));
                if (clickedOpen) g_focusInput = true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", UI(Tr(L"Open the window")).c_str());
            if (g_autoSendPending && !g_edit.active && g_input[0]) {
                g_autoSendPending = false;
                StartSend();
            }
            ImGui::End();
            return;
        }
        ImGui::End();
        return;
    }

    const float minW = 380, minH = 240;
    ImGui::SetNextWindowSize(ImVec2(520, 360), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(minW, minH), ImVec2(FLT_MAX, FLT_MAX));
    if (g_restoreH > 0) {
        ImGui::SetNextWindowSize(ImVec2(g_lastW, g_restoreH), ImGuiCond_Always);
        g_restoreH = 0;
    }
    ImGui::SetNextWindowBgAlpha(std::clamp(g_cfg.opacity, 60, 255) / 255.0f);
    ImGuiWindowFlags wflags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                              ImGuiWindowFlags_NoScrollWithMouse;
    if (!ImGui::Begin("GW2Chatlator###kbmain", nullptr, wflags)) {
        ImGui::End();
        return;
    }
    if (s_uncollapseRestore) {  // opened again: exactly where it stood (or above the moved button)
        s_uncollapseRestore = false;
        if (s_restorePos.x >= 0) {
            ImGui::SetWindowPos(s_restorePos);
            ImGui::SetWindowSize(ImVec2(g_lastW, g_expandedH));
            s_restorePos = ImVec2(-1, -1);
        }
    }
    if (g_minimizeRequest) {  // asked from inside the chat area (a waypoint click): done on the main window
        g_minimizeRequest = false;
        g_expandRequest = false;
        MinimizeWindow();
        ImGui::End();
        return;
    }
    if (g_expandRequest) g_expandRequest = false;  // already open
    ImGui::SetWindowFontScale(std::clamp(g_cfg.fontPercent, 70, 200) / 100.0f);
    g_lastW = ImGui::GetWindowWidth();
    g_expandedH = ImGui::GetWindowHeight();
    if (g_expandedH < 240.0f || g_lastW < 380.0f) {
        if (g_expandedH < 240.0f) g_expandedH = 360.0f;
        if (g_lastW < 380.0f) g_lastW = 520.0f;
        ImGui::SetWindowSize(ImVec2(g_lastW, g_expandedH));
    }
    std::unique_lock<std::mutex> lock(g_mu);
    {  // Snap: a move that ends near a screen edge puts the window onto it; dragging past screen edge pulls it back
        static bool dragging = false;
        static ImVec2 startPos;
        const ImVec2 pos = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
        const float resScale = std::max(1.0f, io.DisplaySize.y / 1080.0f);
        const float maxBottom = std::max(minH, io.DisplaySize.y - Gw2BottomBarOffset(io.DisplaySize.y, g_uiSize.load()));
        if (ImGui::IsMouseClicked(0) && ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows)) {
            dragging = true;
            startPos = pos;
        } else if (dragging && !ImGui::IsMouseDown(0)) {
            dragging = false;
            const bool moved = (pos.x != startPos.x || pos.y != startPos.y);
            if (g_nx.snap && moved) {
                const float t = 14.0f * resScale;
                ImVec2 p = pos;
                // Left / Right:
                if (p.x < t) p.x = 0.0f;
                else if (p.x + size.x > io.DisplaySize.x - t) p.x = io.DisplaySize.x - size.x;

                // Top / Bottom:
                if (p.y < t) p.y = 0.0f;
                else if (p.y + size.y > maxBottom - t) p.y = maxBottom - size.y;

                if (p.x != pos.x || p.y != pos.y) ImGui::SetWindowPos(p);
            }
        }
        // General hard floor & boundary clamp: always prevent window from being pushed below bottom bar or off-screen
        {
            ImVec2 p = ImGui::GetWindowPos();
            bool clamped = false;
            if (p.x < -0.5f) { p.x = 0.0f; clamped = true; }
            else if (p.x + size.x > io.DisplaySize.x + 0.5f) { p.x = std::max(0.0f, io.DisplaySize.x - size.x); clamped = true; }
            if (p.y < -0.5f) { p.y = 0.0f; clamped = true; }
            else if (p.y + size.y > maxBottom + 0.5f) { p.y = std::max(0.0f, maxBottom - size.y); clamped = true; }
            if (clamped && !ImGui::IsMouseDown(0)) ImGui::SetWindowPos(p);
        }
    }
    if (g_statusUntil && GetTickCount64() > g_statusUntil) {
        g_status.clear();
        g_statusUntil = 0;
    }
    const ImVec4 kAccent(0.94f, 0.75f, 0.35f, 1.f), kPreview(0.59f, 0.80f, 1.f, 1.f);
    const ImGuiStyle& st = ImGui::GetStyle();

    // ---- header: the tabs (the chat, whisper tabs; right-click: a new one) · microphone · V / A · menu · close
    const float icon = ImGui::GetFrameHeight();
    const float gap = std::round(icon * 0.6f);
    const float rightW = (g_cfg.voiceEnabled ? icon + gap : 0) + icon + gap + icon + st.ItemSpacing.x + icon;
    const float tabsW = std::max(60.0f, ImGui::GetContentRegionAvail().x - rightW - gap);
    bool newTab = false, tabMenu = false;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("##hdr", ImVec2(tabsW, icon), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar();
    if (ImGui::BeginTabBar("##wtabs", ImGuiTabBarFlags_FittingPolicyResizeDown | ImGuiTabBarFlags_NoTooltip)) {
        static std::string s_chatTabLabel;
        static UiLang s_chatTabLang = static_cast<UiLang>(-1);
        if (s_chatTabLang != g_cfg.uiLang || s_chatTabLabel.empty()) {
            s_chatTabLang = g_cfg.uiLang;
            s_chatTabLabel = UI(Tr(L"Chat")) + "###wt_all";
        }
        if (ImGui::BeginTabItem(s_chatTabLabel.c_str(), nullptr,
                                g_selectTab == -1 ? ImGuiTabItemFlags_SetSelected : 0)) {
            if (g_selectTab == -1) g_selectTab = -2;
            else if (g_selectTab == -2 && g_activeTab != -1) LeaveWhisperTab();
            ImGui::EndTabItem();
        }
        int close = -1;
        for (int i = 0; i < static_cast<int>(g_tabs.size()); ++i) {
            bool open = true;
            const ImGuiTabItemFlags f = (g_selectTab == i ? ImGuiTabItemFlags_SetSelected : 0) |
                                        (g_tabs[i].unread ? ImGuiTabItemFlags_UnsavedDocument : 0);
            const bool shown = ImGui::BeginTabItem(g_tabs[i].Label(), &open, f);
            if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                tabMenu = true;  // not the header's menu
                if (g_tabs[i].custom) g_tabSettings = static_cast<int>(i);
            }
            if (shown) {
                if (g_selectTab == i) g_selectTab = -2;
                else if (g_selectTab == -2 && g_activeTab != i) ActivateTab(i);
                ImGui::EndTabItem();
            }
            if (!open) close = i;
        }
        ImGui::EndTabBar();
        if (close >= 0) {
            const bool active = close == g_activeTab, custom = g_tabs[close].custom;
            g_tabs.erase(g_tabs.begin() + close);
            if (active) LeaveWhisperTab();
            else if (close < g_activeTab) g_selectTab = --g_activeTab;
            if (custom) SaveCustomTabs();
        }
    }
    if (!tabMenu && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        newTab = true;
    ImGui::EndChild();
    // Right-click on the header: a tab of your own (a copy of the chat to set up) or a whisper tab.
    if (newTab) ImGui::OpenPopup("##hdrmenu");
    static bool newTabNext = false;  // asked for in a menu (opened next frame, at this level)
    if (ImGui::BeginPopup("##hdrmenu")) {
        if (ImGui::MenuItem(UI(Tr(L"New tab")).c_str())) {
            const int at = NewCustomTab();
            if (at >= 0) g_tabSettings = at;
            else SetStatus(Tr(L"Six tabs of your own at most – close one first"), 6000);
        }
        if (ImGui::MenuItem(UI(Tr(L"New whisper tab …")).c_str())) newTabNext = true;
        ImGui::EndPopup();
    }
    if (newTabNext) ImGui::OpenPopup("##newtab");
    newTabNext = false;
    // The settings of a tab of your own (right-click on it): name, the channels it shows, where you write from it.
    if (g_tabSettings >= 0) {
        g_tabSettingsOpen = g_tabSettings;
        g_tabSettings = -1;
        ImGui::OpenPopup("##tabcfg");
    }
    if (ImGui::BeginPopup("##tabcfg")) {
        const int i = g_tabSettingsOpen;
        if (i < 0 || i >= static_cast<int>(g_tabs.size()) || !g_tabs[i].custom) {
            ImGui::CloseCurrentPopup();
        } else {
            WhisperTab& t = g_tabs[i];
            static char name[64] = {};
            if (ImGui::IsWindowAppearing()) strncpy_s(name, U8(t.name).c_str(), _TRUNCATE);
            bool changed = false;
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
            if (ImGui::InputText(UI(Tr(L"Name") + L"##tabname").c_str(), name, sizeof(name)) && !Trim(W(name)).empty()) {
                t.SetName(Trim(W(name)));
                changed = true;
            }
            ImGui::TextUnformatted(UI(Tr(L"Shows")).c_str());
            static const Channel kShown[] = {Channel::Say,   Channel::Map,     Channel::Party, Channel::Squad,
                                             Channel::Team,  Channel::Guild,   Channel::Whisper, Channel::System};
            for (int k = 0; k < 8; ++k) {
                bool on = (t.channels & ChannelBit(kShown[k])) != 0;
                if (k % 4) ImGui::SameLine(static_cast<float>(k % 4) * ImGui::GetFontSize() * 6.5f);
                if (ImGui::Checkbox((UI(ChannelLabel(kShown[k])) + "##ch" + std::to_string(k)).c_str(), &on)) {
                    t.channels = on ? (t.channels | ChannelBit(kShown[k])) : (t.channels & ~ChannelBit(kShown[k]));
                    if (kShown[k] == Channel::System)  // lines without a channel go with the system lines
                        t.channels = on ? (t.channels | ChannelBit(Channel::Unknown)) : (t.channels & ~ChannelBit(Channel::Unknown));
                    changed = true;
                }
            }
            static const Channel kWrite[] = {Channel::Unknown, Channel::Say, Channel::Map,  Channel::Party,
                                             Channel::Squad,   Channel::Team, Channel::Guild};
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
            if (ImGui::BeginCombo(UI(Tr(L"Write in") + L"##tabwrite").c_str(),
                                  t.write == Channel::Unknown ? UI(Tr(L"Active channel")).c_str() : UI(ChannelLabel(t.write)).c_str())) {
                for (Channel c : kWrite)
                    if (ImGui::Selectable(c == Channel::Unknown ? UI(Tr(L"Active channel")).c_str() : UI(ChannelLabel(c)).c_str(),
                                          c == t.write)) {
                        t.write = c;
                        changed = true;
                        if (g_activeTab == i) g_channel = c;
                    }
                ImGui::EndCombo();
            }
            if (ImGui::Button(UI(Tr(L"Remove tab")).c_str())) {
                g_tabs.erase(g_tabs.begin() + i);
                if (g_activeTab == i) LeaveWhisperTab();
                else if (i < g_activeTab) g_selectTab = --g_activeTab;
                changed = true;
                ImGui::CloseCurrentPopup();
            }
            if (changed) SaveCustomTabs();
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("##newtab")) {  // a whisper tab of your own: whom you want to write to
        static char name[96] = {};
        if (ImGui::IsWindowAppearing()) {
            name[0] = 0;
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::TextUnformatted(UI(Tr(L"New whisper tab")).c_str());
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14);
        bool go = ImGui::InputTextWithHint("##tabname", UI(Tr(L"Character or account name")).c_str(), name, sizeof(name),
                                           ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        go |= ImGui::Button(UI(Tr(L"Open")).c_str());
        if (go && !Trim(W(name)).empty()) {
            OpenWhisperTab(W(name));
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - rightW);
    if (g_cfg.voiceEnabled) {
        // Hold with the left button to speak; a right-click: on until a pause (or the next right-click).
        bool right = false;
        const ImU32 tint = g_listening ? IM_COL32(110, 205, 130, 255) : !g_listenPausedFor.empty() ? IM_COL32(240, 190, 90, 255) : 0;
        const bool clicked = IconButton("##mic", Icon::Mic, g_recording, &right, tint);
        static bool held = false;
        if (g_cfg.voiceActivation) {  // voice activation: a click switches listening on / off
            if (clicked) {
                const bool on = g_listening || g_listenWanted;
                g_listenUserOff = on;
                lock.unlock();
                if (on) StopListening();
                else StartListening();
                lock.lock();
            }
        } else if (ImGui::IsItemActivated()) {
            held = true;
            lock.unlock();
            TalkDown();
            lock.lock();
        } else if (held && !ImGui::IsItemActive()) {
            held = false;
            if (!g_micLocked && !g_cfg.voiceToggle) TalkUp();
        }
        if (right) {
            const bool stop = g_recording;
            g_micLocked = !stop;
            lock.unlock();
            if (stop) TalkUp();
            else TalkLocked();
            lock.lock();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", UI(g_cfg.voiceActivation ? Tr(L"Voice activation: click switches it on / off · right-click: speak until a pause")
                                                         : Tr(L"Hold to speak (or the push-to-talk key) · right-click: speak until a pause")).c_str());
        ImGui::SameLine(0, gap);
    }
    if (IconButton("##fold", Icon::Collapse, false, nullptr)) {
        g_collapsed = true;
        const ImVec2 p = ImGui::GetWindowPos(), s = ImGui::GetWindowSize();
        g_lastW = s.x;
        g_expandedH = s.y;
        g_anchorBottom = p.y + s.y;  // the small button takes its place on the next frame
        g_savedPos = p;
        g_buttonMoved = false;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", UI(Tr(L"Minimize chat")).c_str());
    ImGui::SameLine(0, gap);
    if (IconButton("##menu", Icon::Menu, false, nullptr)) ImGui::OpenPopup("kbmenu");
    if (ImGui::BeginPopup("kbmenu")) {
        if (ImGui::MenuItem(UI(Tr(L"Options …")).c_str())) g_showOptions = true;
        if (ImGui::MenuItem(UI(Tr(L"Compact view")).c_str(), nullptr, &g_compact)) g_cfg.SaveBool(L"NexusWindow", L"Compact", g_compact);
        if (ImGui::MenuItem(UI(Tr(L"New whisper tab …")).c_str())) newTabNext = true;
        if (!g_chatEventsPresent && !g_chatEventsFile && ImGui::MenuItem(UI(Tr(L"Install Events: Chat (every channel) …")).c_str()))
            g_showOptions = true;
        ImGui::Separator();
        if (ImGui::MenuItem(UI(Tr(L"Hide (Ctrl+Alt+K shows it again)")).c_str())) g_show = false;
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (IconButton("##close", Icon::Close, false, nullptr)) g_show = false;
    g_headerH = ImGui::GetCursorPosY() - st.ItemSpacing.y + st.WindowPadding.y;
    if (!g_recording && g_micLocked) g_micLocked = false;  // the recording ended (pause, maximum length)
    // Voice activation listens from the map on (unless you switched it off with the microphone).
    if (g_cfg.voiceEnabled && g_cfg.voiceActivation && !g_listenWanted && !g_listenUserOff) {
        lock.unlock();
        StartListening();
        lock.lock();
    }
    if (g_collapsed) {  // folded up: only the header; what was going on goes on (sending, dictation)
        if (g_autoSendPending && !g_edit.active && g_input[0]) {
            g_autoSendPending = false;
            StartSend();
        }
        lock.unlock();
        ImGui::End();
        return;
    }

    // ---- the chat (the newest at the bottom): name white – a click opens a whisper tab –, text in the channel colour,
    // the scroll bar at the left like GW2's own chat
    const float line = ImGui::GetTextLineHeightWithSpacing();
    const bool showPreview = g_layout.preview && !g_compact, showWords = g_layout.words && !g_compact;
    const float smallLine = ImGui::GetFontSize() * 0.85f + 2;
    const float boxH = ImGui::GetTextLineHeight() * g_layout.inputLines + st.FramePadding.y * 2 + (g_compact ? smallLine : 0);
    const float lowerH = (showPreview ? line * (g_layout.previewLines + 0.2f) + st.ItemSpacing.y : 0) +
                         (showWords ? ImGui::GetFrameHeightWithSpacing() : 0) +
                         (g_layout.footer ? ImGui::GetFrameHeightWithSpacing() : 0) + boxH + st.ItemSpacing.y;
    if (g_layout.chat) {
        static float scrollY = 0, scrollMax = 0, viewH = 1, dragOffset = 0, lastScrollMax = 0;
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
            if (scrollMax > 0) {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(track, ImVec2(track.x + sb, track.y + h), ImGui::GetColorU32(ImGuiCol_ScrollbarBg), sb * 0.5f);
                const ImGuiCol gc = ImGui::IsItemActive() ? ImGuiCol_ScrollbarGrabActive
                                    : ImGui::IsItemHovered() ? ImGuiCol_ScrollbarGrabHovered : ImGuiCol_ScrollbarGrab;
                dl->AddRectFilled(ImVec2(track.x + 1, grabY), ImVec2(track.x + sb - 1, grabY + grabH), ImGui::GetColorU32(gc), sb * 0.5f);
            }
        }
        ImGui::SameLine(0, 4);
        ImGui::BeginChild("chat", ImVec2(0, h), false, ImGuiWindowFlags_NoScrollbar);
        if (setScroll >= 0) ImGui::SetScrollY(setScroll);
        const ImVec2 childP0 = ImGui::GetWindowPos();
        const ImVec2 childP1 = ImVec2(childP0.x + ImGui::GetWindowSize().x, childP0.y + ImGui::GetWindowSize().y);
        const bool chatHovered = ImGui::IsMouseHoveringRect(childP0, childP1);
        const float wheel = io.MouseWheel;
        if (chatHovered && wheel > 0.0f) {
            atBottom = false;
        }
        if (g_activeTab != lastActiveTab) {
            lastActiveTab = g_activeTab;
            atBottom = true;
        }
        static const std::wstring s_emptyW;
        const WhisperTab* front = g_activeTab >= 0 && g_activeTab < static_cast<int>(g_tabs.size()) ? &g_tabs[g_activeTab] : nullptr;
        const std::wstring& tabName = front && !front->custom ? front->nameFolded : s_emptyW;
        const uint32_t tabChannels = front && front->custom ? front->channels : 0xFFFFFFFFu;
        if (g_lines.empty()) {  // nothing arrived yet: what is ready, what is missing, where translation is switched on
            lock.unlock();
            RenderModules(true);
            lock.lock();
        }
        for (const ChatLine& l : g_lines) {
            if (!tabName.empty() && (l.channel != Channel::Whisper || l.speakerFolded != tabName)) continue;
            if (!(tabChannels & ChannelBit(l.channel))) continue;  // a tab of your own: only its channels
            ImGui::PushID(static_cast<int>(l.id));
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
            const std::wstring& shown = l.translated.empty() ? l.text : l.translated;
            const std::string& u8shown = l.translated.empty() ? l.u8text : l.u8translated;
            const auto& segs = l.translated.empty() ? l.textSegs : l.transSegs;
            bool clicked = false;
            if (segs.size() <= 1 && (segs.empty() || segs[0].kind == PieUI::ChatLinks::SegmentKind::Plain)) {
                LookText(u8shown.c_str(), ColorOf(l.channel), true);
            } else {
                clicked = RichLine(shown, segs, ColorOf(l.channel));
            }
            if (!l.translated.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", l.u8text.c_str());
            // A click on the text of a line someone else wrote: just this line into your language.
            if (!clicked && !l.own && l.translated.empty() && (ImGui::IsItemClicked() || ImGui::IsItemClicked(ImGuiMouseButton_Right)))
                g_worker.Post([id = l.id, t = l.text] { TranslateLine(id, t, true); });
            ImGui::PopID();
        }
        // Auto-scroll to bottom only when new lines arrive or content expands (e.g. item names resolved),
        // provided the user was at the bottom and is not actively scrolling up with the mouse wheel.
        const bool contentGrew = (scrollMax > lastScrollMax + 1.0f);
        const bool shouldScrollDown = (g_scrollDown || contentGrew);
        if (atBottom && shouldScrollDown && setScroll < 0 && (!chatHovered || wheel <= 0.0f)) {
            ImGui::SetScrollHereY(1.0f);
        }
        g_scrollDown = false;
        scrollY = ImGui::GetScrollY();
        scrollMax = ImGui::GetScrollMaxY();
        lastScrollMax = scrollMax;
        viewH = ImGui::GetWindowHeight();
        atBottom = scrollY >= scrollMax - 4;
        ImGui::EndChild();
    }
    // A link from the chat opens only after your yes (http / https only).
    if (g_openLinkAsk) {
        g_openLinkAsk = false;
        ImGui::OpenPopup("###openlink");
    }
    if (ImGui::BeginPopupModal((UI(Tr(L"Leaving GW2Chatlator")) + "###openlink").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        // Like Discord: where it goes, big; the whole link small; you decide.
        ImGui::TextUnformatted(UI(Tr(L"This link goes to")).c_str());
        ImGui::TextColored(ImVec4(0.45f, 0.7f, 1.f, 1.f), "%s", U8(LinkHost(g_openLink)).c_str());
        ImGui::TextDisabled("%s", Fit(U8(g_openLink), ImGui::GetFontSize() * 30).c_str());
        ImGui::Spacing();
        ImGui::TextUnformatted(UI(Tr(L"Are you sure you want to open it?")).c_str());
        ImGui::Spacing();
        if (ImGui::Button(UI(Tr(L"Open link")).c_str())) {
            SafeOpenUrl(g_openLink);
            g_openLink.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(UI(Tr(L"Go back")).c_str()) || ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Escape))) {
            g_openLink.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // ---- the preview: what arrives in the GW2 chat, the back-translation small under it
    const bool previewCurrent = g_previewGen == g_inputGen && !g_preview.empty();
    if (showPreview) {
        ImVec4 quiet = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
        quiet.w *= g_input[0] ? 0.6f : 0.25f;  // nearly invisible while there is nothing to show
        ImGui::PushStyleColor(ImGuiCol_ChildBg, quiet);
        ImGui::BeginChild("preview", ImVec2(0, line * (g_layout.previewLines + 0.2f)), false);
        if (g_recording) {
            ImGui::TextColored(kPreview, "%s", g_voiceLive.empty() ? "..." : U8(g_voiceLive).c_str());
        } else if (!g_input[0]) {
            // empty: no hint text here – the chat above is what you read
        } else if (!Translating()) {
            ImGui::PushStyleColor(ImGuiCol_Text, kPreview);
            ImGui::TextWrapped("%s", U8(ComposePrefix() + SanitizeChatText(g_wFlatInput)).c_str());
            ImGui::PopStyleColor();
        } else if (previewCurrent) {
            ImGui::PushStyleColor(ImGuiCol_Text, kPreview);
            ImGui::TextWrapped("%s", U8(ComposePrefix() + g_preview).c_str());
            ImGui::PopStyleColor();
            if (!g_back.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                ImGui::TextWrapped("%s", U8(g_back).c_str());
                ImGui::PopStyleColor();
            }
        } else {
            ImGui::TextDisabled("%s", UI(Tr(L"translating …")).c_str());
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    // ---- the word bar: three cells across the line, what Tab takes in orange
    if (showWords) {
        const float w = ImGui::GetContentRegionAvail().x;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = ImGui::GetFrameHeight();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const bool have = g_cfg.suggestions && g_help.text == g_wFlatInput;
        for (int i = 0; i < 3; ++i) {
            const ImVec2 a(p.x + w * i / 3.0f, p.y), b(p.x + w * (i + 1) / 3.0f, p.y + h);
            if (i) dl->AddLine(ImVec2(a.x, a.y + h * 0.25f), ImVec2(a.x, b.y - h * 0.25f), ImGui::GetColorU32(ImGuiCol_Separator));
            if (!have || i >= static_cast<int>(g_help.bar.words.size())) continue;
            const std::string word = U8(g_help.bar.words[i]);
            ImGui::SetCursorScreenPos(a);
            ImGui::PushID(i);
            if (ImGui::InvisibleButton("##word", ImVec2(b.x - a.x, h))) TakeWord(g_help.bar.words[i]);
            const bool hot = ImGui::IsItemHovered();
            ImGui::PopID();
            if (hot) dl->AddRectFilled(a, b, ImGui::GetColorU32(ImGuiCol_FrameBgHovered));
            const ImVec2 size = ImGui::CalcTextSize(word.c_str());
            const bool pick = g_pick >= 0 ? i == g_pick : i == g_help.bar.autoIndex;
            dl->AddText(ImVec2((a.x + b.x - size.x) * 0.5f, a.y + (h - size.y) * 0.5f),
                        pick ? ImGui::GetColorU32(kAccent) : ImGui::GetColorU32(ImGuiCol_Text), word.c_str());
        }
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h + st.ItemSpacing.y));
    }

    // ---- the input line; momentary messages (status) appear inside it
    if (g_focusInput && !g_sending) {
        ImGui::SetKeyboardFocusHere();
        g_focusInput = false;
    }
    const ImVec2 boxSize(-1, boxH);
    g_wrapWidth = ImGui::GetContentRegionAvail().x - st.FramePadding.x * 2 - st.ScrollbarSize - 4;
    const ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CtrlEnterForNewLine |
                                      ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackCharFilter |
                                      ImGuiInputTextFlags_CallbackAlways;
    const bool enter = ImGui::InputTextMultiline("##input", g_input, sizeof(g_input), boxSize, flags, InputCallback);
    const std::string nowFlat = Flat(g_input);
    if (nowFlat != g_flatInput) {
        g_flatInput = nowFlat;
        g_wFlatInput = W(nowFlat);
    }
    const ImVec2 inputMin = ImGui::GetItemRectMin(), inputMax = ImGui::GetItemRectMax();
    const bool inputActive = ImGui::IsItemActive();
    if (inputActive && !g_history.empty() && !g_edit.active) {
        const bool up = ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_UpArrow));
        const bool down = ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_DownArrow));
        const std::wstring& cur = g_wFlatInput;
        const bool browsing = cur.empty() || (g_historyPos >= 0 && g_historyPos < static_cast<int>(g_history.size()) &&
                                              cur == g_history[g_historyPos]);
        if ((up || down) && browsing) {
            g_historyPos = up ? std::min<int>(g_historyPos + 1, static_cast<int>(g_history.size()) - 1)
                              : std::max(g_historyPos - 1, -1);
            g_edit.active = true;
            g_edit.startByte = 0;
            g_edit.lenBytes = std::strlen(g_input);
            g_edit.insert = g_historyPos >= 0 ? U8(g_history[g_historyPos]) : std::string();
        }
    }
    // Right-click in the input (your own words only): the learning mode – what you corrected yourself while typing is
    // learned (autocorrection writes it right next time), and abbreviations get their meaning (for the translator).
    static bool teach = false;
    if (ImGui::IsMouseHoveringRect(inputMin, inputMax) && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
        ImGui::IsMouseReleased(ImGuiMouseButton_Right))
        teach = true;
    if (teach) {
        teach = false;
        ImGui::OpenPopup("##teach");
    }
    if (ImGui::BeginPopup("##teach")) {
        static char typed[64] = {}, fixed[64] = {}, word[64] = {}, meaning[160] = {};
        if (ImGui::IsWindowAppearing()) {  // the last word you typed, and what is known of it
            const std::wstring& text = g_wFlatInput;
            const std::vector<Span> spans = WordSpans(text);
            const std::wstring last = spans.empty() ? std::wstring() : text.substr(spans.back().start, spans.back().length);
            typed[0] = 0;
            strncpy_s(fixed, U8(last).c_str(), _TRUNCATE);
            strncpy_s(word, U8(last).c_str(), _TRUNCATE);
            strncpy_s(meaning, U8(g_myWords.MeaningOf(last)).c_str(), _TRUNCATE);
        }
        auto teachFix = [](const std::wstring& t, const std::wstring& f) {
            g_worker.Post([t, f] {
                const bool ok = g_spell && g_spell->TeachFix(t, f);
                std::lock_guard<std::mutex> lock(g_mu);
                SetStatus(ok ? TrF(L"Learned: {1} > {2}", {t, f}) : TrF(L"\"{1}\" is a real word – not learned as a typo", {t}), 6000);
            });
        };
        ImGui::TextUnformatted(UI(Tr(L"Learn your corrections")).c_str());
        Hint(UI(Tr(L"What you corrected yourself while typing: learned once, autocorrection writes it right next time.")));
        int learned = -1;
        for (size_t i = 0; i < g_selfFixes.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s  >  %s", U8(g_selfFixes[i].typed).c_str(), U8(g_selfFixes[i].fixed).c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton(UI(Tr(L"Learn")).c_str())) learned = static_cast<int>(i);
            ImGui::PopID();
        }
        if (learned >= 0) {
            teachFix(g_selfFixes[learned].typed, g_selfFixes[learned].fixed);
            g_selfFixes.erase(g_selfFixes.begin() + learned);
        }
        if (g_selfFixes.empty()) ImGui::TextDisabled("%s", UI(Tr(L"No corrections of your own yet – or enter one:")).c_str());
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
        ImGui::InputTextWithHint("##typed", UI(Tr(L"I type")).c_str(), typed, sizeof(typed));
        ImGui::SameLine();
        ImGui::TextUnformatted(">");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
        ImGui::InputTextWithHint("##fixed", UI(Tr(L"I mean")).c_str(), fixed, sizeof(fixed));
        ImGui::SameLine();
        if (ImGui::Button(UI(Tr(L"Learn") + L"##fix").c_str()) && !Trim(W(typed)).empty() && !Trim(W(fixed)).empty()) {
            teachFix(Trim(W(typed)), Trim(W(fixed)));
            typed[0] = 0;
        }
        ImGui::Separator();
        ImGui::TextUnformatted(UI(Tr(L"Your word or abbreviation – what it means")).c_str());
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
        ImGui::InputText("##w", word, sizeof(word));
        ImGui::SameLine();
        ImGui::TextUnformatted("=");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14);
        const bool go = ImGui::InputTextWithHint("##m", UI(Tr(L"meaning, e.g. I think")).c_str(), meaning, sizeof(meaning),
                                                 ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if ((ImGui::Button(UI(Tr(L"Learn") + L"##mean").c_str()) || go) && !Trim(W(word)).empty()) {
            const std::wstring w = Trim(W(word)), m = Trim(W(meaning));
            if (m.empty()) g_myWords.Remove(w);
            else g_myWords.Set(w, m);
            SaveMyWords();
            g_worker.Post([w] { if (g_spell) g_spell->AddUserWord(w); });
            ++g_inputGen;  // translated again with it
            g_inputChanged = GetTickCount64();
            SetStatus(m.empty() ? TrF(L"\"{1}\" forgotten", {w}) : TrF(L"Learned: {1} = {2}", {w, m}), 6000);
        }
        Hint(UI(Tr(L"The translator gets the meaning; your text stays as you wrote it.")));
        ImGui::EndPopup();
    }
    const ImVec4 clip(inputMin.x, inputMin.y, inputMax.x, inputMax.y);
    const ImVec2 textAt(inputMin.x + st.FramePadding.x, inputMin.y + st.FramePadding.y);
    if (const std::wstring rest = GhostRest(g_wFlatInput); !rest.empty() && inputActive) {  // what Space / Tab writes
        // In the line where the text ends (the box is its own child window: drawn on top, clipped to the box).
        const size_t inLen = std::strlen(g_input);
        const char* lastLine = std::strrchr(g_input, '\n');
        const int lineNo = static_cast<int>(std::count(g_input, g_input + inLen, '\n'));
        const ImVec2 typed = ImGui::CalcTextSize(lastLine ? lastLine + 1 : g_input);
        if (lineNo < g_layout.inputLines)
            BoxText(ImVec2(textAt.x + typed.x, textAt.y + lineNo * ImGui::GetTextLineHeight()), clip,
                    ImGui::GetColorU32(ImGuiCol_TextDisabled), UI(rest));
    }
    // Status: in the empty box where you would write; while you write, a momentary message small at the bottom right.
    if (!g_input[0]) {
        if (g_recording && !showPreview && !g_voiceLive.empty())
            BoxText(textAt, clip, ImGui::GetColorU32(kPreview), U8(g_voiceLive));
        else {
            static std::wstring s_lastStatusW;
            static std::string s_lastStatusFit;
            static float s_lastStatusWidth = 0.0f;
            const float availW = inputMax.x - textAt.x - st.FramePadding.x;
            const std::wstring stW = StatusText();
            if (stW != s_lastStatusW || std::abs(availW - s_lastStatusWidth) > 1.0f) {
                s_lastStatusW = stW;
                s_lastStatusWidth = availW;
                s_lastStatusFit = Fit(UI(stW), availW);
            }
            BoxText(textAt, clip, g_status.empty() ? ImGui::GetColorU32(ImGuiCol_TextDisabled) : ImGui::GetColorU32(kAccent),
                    s_lastStatusFit);
        }
    } else if (!g_status.empty()) {
        const std::string s = Fit(UI(g_status), (inputMax.x - inputMin.x) * 0.6f);
        const float w = ImGui::CalcTextSize(s.c_str()).x * 0.85f;
        BoxText(ImVec2(inputMax.x - w - st.FramePadding.x - st.ScrollbarSize, inputMax.y - smallLine - 1), clip,
                ImGui::GetColorU32(kAccent), s, 0.85f);
    }
    // Compact view: what the chat will get, small in the box's last line (instead of the preview above).
    if (g_compact && g_input[0] && Translating()) {
        const std::string t = previewCurrent ? "> " + U8(g_preview) : UI(Tr(L"translating …"));
        BoxText(ImVec2(textAt.x, inputMax.y - smallLine - 1), clip, ImGui::GetColorU32(kPreview),
                Fit(t, (inputMax.x - inputMin.x) * (g_status.empty() ? 0.95f : 0.38f)), 0.85f);
    }

    // ---- the footer: channel | write | send · translator | auto | counter
    if (g_layout.footer) RenderFooter();
    // Changes of the text: a pause translates it; the typing help follows every key.
    if (g_flatInput != g_lastInput) {
        g_lastInput = g_flatInput;
        NoteSelfFix(g_wFlatInput);
        g_inputChanged = GetTickCount64();
        ++g_inputGen;
        g_back.clear();
        g_pick = -1;
        if (!g_helpRequested) {
            g_helpRequested = true;
            g_worker.Post(ComputeTyping);
        }
    }
    if (g_inputChanged && GetTickCount64() - g_inputChanged > 400 && !g_flatInput.empty() && Translating()) {
        g_inputChanged = 0;
        g_worker.Post([gen = g_inputGen, t = SanitizeChatText(g_wFlatInput)] { TranslateInput(gen, t); });
    }
    if (enter) StartSend();
    if (g_autoSendPending && !g_edit.active && g_input[0]) {  // the dictation is in the line now
        g_autoSendPending = false;
        StartSend();
    }
    lock.unlock();
    ImGui::End();
}

std::wstring FormatSeconds(int ms) {  // 1234 -> "1.2"
    wchar_t b[16];
    swprintf(b, 16, L"%.1f", ms / 1000.0);
    return b;
}

// A language combo over Languages(): `value` "" = `emptyLabel`.
bool LanguageCombo(const char* id, std::wstring& value, const std::wstring& emptyLabel) {
    std::string current = value.empty() ? U8(emptyLabel) : U8(value);
    for (const LangInfo& l : Languages())
        if (PrimaryLang(l.code) == PrimaryLang(value) && !value.empty()) current = U8(std::wstring(l.native) + L" (" + l.code + L")");
    bool changed = false;
    if (ImGui::BeginCombo(id, current.c_str())) {
        if (!emptyLabel.empty() && ImGui::Selectable(U8(emptyLabel).c_str(), value.empty())) {
            value.clear();
            changed = true;
        }
        for (const LangInfo& l : Languages()) {
            const std::string n = U8(std::wstring(l.native) + L" (" + l.code + L")");
            if (ImGui::Selectable(n.c_str(), value == l.code)) {
                value = l.code;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

// Our options window, in tabs. "Save" applies.
void RenderOptionsContent() {
    static Config edit;
    static bool loaded = false;
    static char keyBuf[256] = {}, regionBuf[64] = {}, urlBuf[256] = {}, mailBuf[128] = {};
    static std::vector<AudioDevice> mics;
    static bool forgetAsked = false;
    if (!loaded) {
        std::lock_guard<std::mutex> lock(g_mu);
        edit = g_cfg;
        loaded = true;
        const std::string key = U8(edit.engine == Engine::DeepL ? edit.deeplKey : edit.engine == Engine::Google ? edit.googleKey
                                   : edit.engine == Engine::Microsoft ? edit.msKey : edit.libreKey);
        strncpy_s(keyBuf, key.c_str(), _TRUNCATE);
        strncpy_s(regionBuf, U8(edit.msRegion).c_str(), _TRUNCATE);
        strncpy_s(urlBuf, U8(edit.libreUrl).c_str(), _TRUNCATE);
        strncpy_s(mailBuf, U8(edit.basicEmail).c_str(), _TRUNCATE);
        mics = AudioCapture::Devices();
        forgetAsked = false;
    }
    const float wide = 280;
    auto combo = [&](const char* id, const std::vector<std::string>& items, int* sel) {
        bool changed = false;
        ImGui::SetNextItemWidth(wide);
        if (ImGui::BeginCombo(id, items[std::clamp(*sel, 0, static_cast<int>(items.size()) - 1)].c_str())) {
            for (int i = 0; i < static_cast<int>(items.size()); ++i)
                if (ImGui::Selectable(items[i].c_str(), i == *sel)) {
                    *sel = i;
                    changed = true;
                }
            ImGui::EndCombo();
        }
        return changed;
    };
    auto TabFlag = [](int i) { return g_optionsTab == i ? ImGuiTabItemFlags_SetSelected : 0; };
    if (ImGui::BeginTabBar("##tabs")) {
        // ---- General
        if (ImGui::BeginTabItem(UI(Tr(L"General")).c_str(), nullptr, TabFlag(0))) {
            ImGui::SetNextItemWidth(wide);
            LanguageCombo(UI(Tr(L"Language of this window") + L"##ui").c_str(), edit.uiLangCode, Tr(L"like Windows"));
            int pt = std::clamp(edit.fontPercent, 70, 200);
            ImGui::SetNextItemWidth(wide);
            if (ImGui::SliderInt(UI(Tr(L"Text size") + L" (%)##size").c_str(), &pt, 70, 200)) edit.fontPercent = pt;
            int transparency = 100 - std::clamp(edit.opacity, 60, 255) * 100 / 255;
            ImGui::SetNextItemWidth(wide);
            if (ImGui::SliderInt(UI(Tr(L"Transparency") + L" (%)##alpha").c_str(), &transparency, 0, 75))
                edit.opacity = (100 - transparency) * 255 / 100;
            ImGui::Checkbox(UI(Tr(L"Show the window")).c_str(), &g_show);
            {
                std::lock_guard<std::mutex> lock(g_mu);
                if (ImGui::Checkbox(UI(Tr(L"Snap to the screen edges")).c_str(), &g_nx.snap)) SaveNexusOptions();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", UI(Tr(L"Moved near an edge of the screen, the window sits right on it.")).c_str());
            ImGui::Separator();
            ImGui::TextUnformatted(UI(Tr(L"Window layout")).c_str());
            {
                std::lock_guard<std::mutex> lock(g_mu);
                bool changed = ImGui::Checkbox(UI(Tr(L"Chat")).c_str(), &g_layout.chat);
                ImGui::SameLine();
                changed |= ImGui::Checkbox(UI(Tr(L"Translation preview")).c_str(), &g_layout.preview);
                ImGui::SameLine();
                changed |= ImGui::Checkbox(UI(Tr(L"Word bar")).c_str(), &g_layout.words);
                ImGui::SameLine();
                changed |= ImGui::Checkbox(UI(Tr(L"Footer")).c_str(), &g_layout.footer);
                ImGui::SetNextItemWidth(wide);
                changed |= ImGui::SliderInt(UI(Tr(L"Lines of the input") + L"##inl").c_str(), &g_layout.inputLines, 1, 8);
                ImGui::SetNextItemWidth(wide);
                changed |= ImGui::SliderInt(UI(Tr(L"Lines of the preview") + L"##prl").c_str(), &g_layout.previewLines, 1, 8);
                if (changed) SaveLayout();
            }
            ImGui::Separator();
            ImGui::TextUnformatted(UI(Tr(L"Chat text")).c_str());
            {
                std::lock_guard<std::mutex> lock(g_mu);
                bool changed = ImGui::Checkbox(UI(Tr(L"Glow like GW2's chat")).c_str(), &g_look.glow);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", UI(Tr(L"A dark halo around the letters, like the game draws its chat – readable on any background.")).c_str());
                ImGui::SameLine();
                changed |= ImGui::Checkbox(UI(Tr(L"GW2 colours")).c_str(), &g_look.gameColors);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", UI(Tr(L"The name in the channel colour, the message in a light tint of it – as in the game. Off: the message in the channel colour.")).c_str());
                ImGui::SetNextItemWidth(wide);
                changed |= ImGui::SliderInt(UI(Tr(L"Brightness") + L" (%)##bright").c_str(), &g_look.brightness, 60, 160);
                if (changed) SaveLayout();
                // Item and place names in the game's language (the GW2 API knows these five).
                static const wchar_t* kNames[][2] = {{L"", L"like I write"}, {L"en", L"English"}, {L"de", L"Deutsch"},
                                                     {L"fr", L"Français"}, {L"es", L"Español"}, {L"zh", L"中文"}};
                int ni = 0;
                for (int i = 0; i < 6; ++i)
                    if (g_nx.namesLang == kNames[i][0]) ni = i;
                ImGui::SetNextItemWidth(wide);
                if (ImGui::BeginCombo(UI(Tr(L"Item and place names") + L"##names").c_str(),
                                      ni == 0 ? UI(Tr(L"like I write")).c_str() : U8(kNames[ni][1]).c_str())) {
                    for (int i = 0; i < 6; ++i)
                        if (ImGui::Selectable(i == 0 ? UI(Tr(L"like I write")).c_str() : U8(kNames[i][1]).c_str(), i == ni)) {
                            g_nx.namesLang = kNames[i][0];
                            SaveNexusOptions();
                        }
                    ImGui::EndCombo();
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", UI(Tr(L"Choose the language your game runs in – item links and waypoints then read as in its own chat.")).c_str());
            }
            Hint(UI(Tr(L"Keys (show / hide: Ctrl+Alt+K, push to talk, focus the input): Nexus → Keybinds → GW2 Keyboard.")));
            ImGui::Separator();
            ImGui::TextUnformatted(UI(Tr(L"Modules")).c_str());
            RenderModules(false);
            ImGui::EndTabItem();
        }
        // ---- Writing
        if (ImGui::BeginTabItem(UI(Tr(L"Writing")).c_str(), nullptr, TabFlag(1))) {
            ImGui::SetNextItemWidth(wide);
            LanguageCombo(UI(Tr(L"I write in") + L"##writein").c_str(), edit.writeIn, Tr(L"like Windows"));
            ImGui::Checkbox(UI(Tr(L"Spell checking (Windows, offline)")).c_str(), &edit.spellEnabled);
            int ac = static_cast<int>(edit.autoCorrect);
            if (combo(UI(Tr(L"Autocorrection") + L"##ac").c_str(),
                      {UI(Tr(L"Off")), UI(Tr(L"Safe")), UI(Tr(L"Like a phone keyboard"))}, &ac))
                edit.autoCorrect = static_cast<AutoCorrectMode>(ac);
            ImGui::Checkbox(UI(Tr(L"Word suggestions while typing")).c_str(), &edit.suggestions);
            int learn = !edit.learnWords ? 2 : edit.learnLater ? 1 : 0;
            if (combo("##learn", {UI(Tr(L"Learn from what I send – while playing")), UI(Tr(L"Learn from what I send – later")),
                                  UI(Tr(L"Do not learn"))},
                      &learn)) {
                edit.learnWords = learn != 2;
                edit.learnLater = learn == 1;
            }
            if (!forgetAsked) {
                if (ImGui::Button(UI(Tr(L"Delete everything learned…")).c_str())) forgetAsked = true;
            } else {
                ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.35f, 1.f), "%s", UI(Tr(L"Really delete everything learned?")).c_str());
                ImGui::SameLine();
                if (ImGui::Button(UI(Tr(L"Yes")).c_str())) {
                    g_worker.Post([] { if (g_spell) g_spell->ForgetAll(); });
                    forgetAsked = false;
                }
                ImGui::SameLine();
                if (ImGui::Button(UI(Tr(L"No")).c_str())) forgetAsked = false;
            }
            ImGui::Checkbox(UI(Tr(L"Show the back-translation of my message")).c_str(), &edit.backTranslate);
            int enterDoes = edit.copyOnly ? 1 : 0;
            if (combo(UI(Tr(L"Enter does") + L"##enter").c_str(),
                      {UI(Tr(L"Send into the GW2 chat")), UI(Tr(L"Only copy (not a single key reaches the game)"))}, &enterDoes))
                edit.copyOnly = enterDoes == 1;
            ImGui::TextUnformatted(UI(Tr(L"Focus behaviour")).c_str());
            ImGui::Checkbox(UI(Tr(L"After sending, keep typing here")).c_str(), &edit.returnFocus);
            Hint(UI(Tr(L"The input keeps the keyboard after Enter – the next message goes right after.")));
            ImGui::Checkbox(UI(Tr(L"Take over the whole focus when the GW2 chat opens")).c_str(), &edit.focusOnGameChat);
            Hint(UI(Tr(L"Enter in the game (opening its chat line) brings you into this input instead.")));
            ImGui::EndTabItem();
        }
        // ---- Voice
        if (ImGui::BeginTabItem(UI(Tr(L"Voice")).c_str(), nullptr, TabFlag(2))) {
            ImGui::Checkbox(UI(Tr(L"Voice input")).c_str(), &edit.voiceEnabled);
            RenderModules(false);
            ImGui::Separator();
            const VoiceCatalog catalog = LoadVoiceCatalog(edit.dataDir);
            const VoiceModelInfo* chosen = catalog.Find(U8(edit.voiceModel));
            if (!chosen) chosen = catalog.Recommended();
            ImGui::SetNextItemWidth(wide);
            if (ImGui::BeginCombo(UI(Tr(L"Model") + L"##model").c_str(), chosen ? chosen->name.c_str() : "-")) {
                for (const VoiceModelInfo& m : catalog.models) {
                    const std::string label = m.name + "  (" + std::to_string(m.bytes / (1024 * 1024)) + " MB)" +
                                              (VoiceModelInstalled(edit.VoiceModelDir(), m) ? "  [OK]" : "");
                    if (ImGui::Selectable(label.c_str(), chosen && chosen->id == m.id)) edit.voiceModel = W(m.id);
                }
                ImGui::EndCombo();
            }
            if (chosen && !VoiceModelInstalled(edit.VoiceModelDir(), *chosen)) {
                const int pct = g_downloadPct;
                if (pct >= 0) ImGui::ProgressBar(pct / 100.f, ImVec2(wide, 0));
                else if (ImGui::Button(UI(Tr(L"Download the model")).c_str())) StartModelDownload(*chosen);
            }
            // The installed models: what each needed when it last recognized you; use one, or remove what does not suit.
            static std::string removeAsked;
            for (const VoiceModelInfo& m : catalog.models) {
                if (!VoiceModelInstalled(edit.VoiceModelDir(), m)) continue;
                ImGui::PushID(m.id.c_str());
                const bool inUse = chosen && chosen->id == m.id;
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(inUse ? ImVec4(0.45f, 0.85f, 0.45f, 1.f) : ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s  (%d MB)",
                                   m.name.c_str(), static_cast<int>(m.bytes / (1024 * 1024)));
                ImGui::SameLine();
                VoiceRun run;
                {
                    std::lock_guard<std::mutex> lock(g_mu);
                    if (auto it = g_voiceRuns.find(W(m.id)); it != g_voiceRuns.end()) run = it->second;
                }
                char ms[96];
                if (run.rec > 0)
                    snprintf(ms, sizeof(ms), "%s", UI(TrF(L"peak {1} % CPU (highest {2} %) · {3} s for {4} s speech",
                                                          {std::to_wstring(run.peak), std::to_wstring(run.maxPeak),
                                                           FormatSeconds(run.rms), FormatSeconds(run.rec)})).c_str());
                else
                    snprintf(ms, sizeof(ms), "%s", UI(Tr(L"not measured yet")).c_str());
                ImGui::TextDisabled("%s", ms);
                if (!inUse) {
                    ImGui::SameLine();
                    if (ImGui::SmallButton(UI(Tr(L"Use")).c_str())) edit.voiceModel = W(m.id);
                }
                ImGui::SameLine();
                if (removeAsked != m.id) {
                    if (ImGui::SmallButton(UI(Tr(L"Remove")).c_str())) removeAsked = m.id;
                } else {
                    if (ImGui::SmallButton(UI(Tr(L"Really remove")).c_str())) {
                        const bool ok = DeleteVoiceModel(edit.VoiceModelDir(), m);
                        std::lock_guard<std::mutex> lock(g_mu);
                        SetStatus(ok ? TrF(L"{1} removed", {W(m.name)}) : TrF(L"{1} is in use – try again after Unload", {W(m.name)}), 8000);
                        removeAsked.clear();
                        g_modulesNext = 0;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton(UI(Tr(L"No")).c_str())) removeAsked.clear();
                }
                ImGui::PopID();
            }
            {
                std::lock_guard<std::mutex> lock(g_mu);
                if (ImGui::Checkbox(UI(Tr(L"Too slow twice in a row: use the next smaller model")).c_str(), &g_nx.voiceFallback))
                    SaveNexusOptions();
            }
            Hint(UI(Tr(L"Too slow = recognizing took longer than you spoke (and over 2 s). Peak = the highest share of the "
                       L"whole CPU the voice module used.")));
            // Where it all lies.
            auto folder = [&](const std::wstring& label, const std::wstring& path) {
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("%s", UI(label).c_str());
                ImGui::SameLine();
                ImGui::TextUnformatted(Fit(U8(path), ImGui::GetContentRegionAvail().x - ImGui::GetFontSize() * 9).c_str());
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", U8(path).c_str());
                ImGui::SameLine();
                ImGui::PushID(U8(path).c_str());
                if (ImGui::SmallButton(UI(Tr(L"Open folder")).c_str())) {
                    EnsureDir(path);
                    std::thread([path] {
                        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
                        ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                        CoUninitialize();
                    }).detach();
                }
                ImGui::PopID();
            };
            folder(Tr(L"Models:"), edit.VoiceModelDir());
            folder(Tr(L"Voice module:"), KeyboardDir());
            std::vector<std::string> langs{UI(Tr(L"Like I write")), UI(Tr(L"Detect it"))};
            std::vector<std::wstring> codes{L"", L"auto"};
            for (const LangInfo& l : Languages()) {
                langs.push_back(U8(l.native));
                codes.push_back(l.code);
            }
            int li = 0;
            for (size_t i = 0; i < codes.size(); ++i)
                if (codes[i] == edit.voiceLang) li = static_cast<int>(i);
            if (combo(UI(Tr(L"Language") + L"##vlang").c_str(), langs, &li)) edit.voiceLang = codes[li];
            ImGui::Checkbox(UI(Tr(L"Translate into English (whisper)")).c_str(), &edit.voiceTranslate);
            ImGui::Checkbox(UI(Tr(L"Live text while speaking (more CPU)")).c_str(), &edit.voiceLive);
            ImGui::SetNextItemWidth(wide);
            ImGui::SliderInt(UI(Tr(L"Longest recording") + L" (s)##max").c_str(), &edit.voiceMaxSec, 5, 60);
            std::vector<std::string> threads{UI(Tr(L"Automatic"))};
            for (int i = 1; i <= 8; ++i) threads.push_back(std::to_string(i));
            int ti = std::clamp(edit.voiceThreads, 0, 8);
            if (combo(UI(Tr(L"CPU threads") + L"##thr").c_str(), threads, &ti)) edit.voiceThreads = ti;
            Hint(UI(Tr(L"Automatic: the least busy cores – the ones GW2 does not use.")));
            ImGui::Checkbox(UI(Tr(L"Send right after speaking (releasing the key counts as your Enter)")).c_str(), &edit.voiceAutoSend);
            Hint(UI(Tr(L"Runs on this PC – no sound leaves it, nothing is recorded to disk.")));
            ImGui::EndTabItem();
        }
        // ---- Microphone
        if (ImGui::BeginTabItem(UI(Tr(L"Microphone")).c_str(), nullptr, TabFlag(3))) {
            int mode = edit.voiceActivation ? 2 : edit.voiceToggle ? 1 : 0;
            static std::vector<std::string> s_modeLabels;
            static UiLang s_modeLang = static_cast<UiLang>(-1);
            if (s_modeLang != g_cfg.uiLang || s_modeLabels.empty()) {
                s_modeLang = g_cfg.uiLang;
                s_modeLabels = {UI(Tr(L"Hold the button or key while you speak")),
                                UI(Tr(L"Press once – a pause ends it")),
                                UI(Tr(L"Voice activation – starts when you speak"))};
            }
            if (combo(UI(Tr(L"Starting") + L"##mode").c_str(), s_modeLabels, &mode)) {
                edit.voiceToggle = mode == 1;
                edit.voiceActivation = mode == 2;
            }
            // The push-to-talk key: ours (GW2 does not get it). "Set key ..." takes the next key you press.
            {
                std::lock_guard<std::mutex> lock(g_mu);
                if (!g_capturedKey.empty()) {
                    if (g_capturedKey != L"-") edit.voiceGlobalKey = g_capturedKey;
                    g_capturedKey.clear();
                }
            }
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(UI(Tr(L"Push-to-talk key")).c_str());
            ImGui::SameLine(150);
            ImGui::TextUnformatted(g_captureKey ? UI(Tr(L"press a key … (Esc: cancel)")).c_str()
                                                : UI(edit.voiceGlobalKey.empty() ? Tr(L"none") : edit.voiceGlobalKey).c_str());
            ImGui::SameLine(330);
            if (ImGui::Button(UI(Tr(L"Set key…")).c_str())) g_captureKey = true;
            ImGui::SameLine();
            if (ImGui::Button(UI(Tr(L"None")).c_str())) edit.voiceGlobalKey.clear();
            Hint(UI(Tr(L"Works while GW2 is in front; GW2 does not get this key. The Nexus keybind (Keybinds > GW2 Keyboard) works too.")));
            std::vector<std::string> names{UI(Tr(L"Windows default"))};
            int mi = 0;
            for (size_t i = 0; i < mics.size(); ++i) {
                names.push_back(U8(mics[i].name));
                if (mics[i].id == edit.voiceDevice) mi = static_cast<int>(i) + 1;
            }
            if (combo(UI(Tr(L"Microphone") + L"##dev").c_str(), names, &mi))
                edit.voiceDevice = mi == 0 ? std::wstring() : mics[mi - 1].id;
            int boost = std::clamp(edit.voiceBoost, 0, 3);
            static std::vector<std::string> s_boostLabels;
            static UiLang s_boostLang = static_cast<UiLang>(-1);
            if (s_boostLang != g_cfg.uiLang || s_boostLabels.empty()) {
                s_boostLang = g_cfg.uiLang;
                s_boostLabels = {UI(Tr(L"Off")), "+6 dB", "+12 dB", UI(Tr(L"Automatic"))};
            }
            if (combo(UI(Tr(L"Microphone boost") + L"##boost").c_str(), s_boostLabels, &boost))
                edit.voiceBoost = boost;
            ImGui::SetNextItemWidth(wide);
            ImGui::SliderInt(UI(Tr(L"Pause to end") + L" (ms)##pause").c_str(), &edit.voicePauseMs, 400, 3000);
            Hint(UI(Tr(L"Right-click on the microphone: on until a pause.")));
            ImGui::Separator();
            ImGui::TextUnformatted(UI(Tr(L"Voice activation")).c_str());
            if (!edit.voiceActivation) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
            Hint(UI(Tr(L"Voice activation is not exclusive: everything you say goes into the box, and Discord keeps hearing it.")));
            ImGui::SetNextItemWidth(wide);
            ImGui::SliderInt(UI(Tr(L"Sensitivity") + L"##sens").c_str(), &edit.voiceSensitivity, 0, 100);
            ImGui::SetNextItemWidth(wide);
            ImGui::SliderInt(UI(Tr(L"Pre-roll") + L" (ms)##pre").c_str(), &edit.voicePreRollMs, 0, 1000);
            ImGui::SetNextItemWidth(wide);
            ImGui::SliderInt(UI(Tr(L"Speech to start") + L" (ms)##min").c_str(), &edit.voiceMinMs, 100, 1000);
            ImGui::Checkbox(UI(Tr(L"Pause while a voice chat uses the microphone (Discord, TeamSpeak …)")).c_str(), &edit.voiceChatPause);
            if (!edit.voiceActivation) ImGui::PopStyleVar();
            ImGui::EndTabItem();
        }
        // ---- Translator
        if (ImGui::BeginTabItem(UI(Tr(L"Translator")).c_str(), nullptr, TabFlag(4))) {
            {
                std::lock_guard<std::mutex> lock(g_mu);
                if (ImGui::Checkbox(UI(Tr(L"Translate the chat")).c_str(), &g_nx.translateChat)) SaveNexusOptions();
            }
            Hint(UI(Tr(L"Foreign lines of other players into your language. Off: a click on a line translates just that one.")));
            ImGui::Checkbox(UI(Tr(L"Do not translate the language I write in")).c_str(), &edit.skipTypingLang);
            // Languages you understand: never translated by themselves (a click still does) – only the others are.
            ImGui::TextUnformatted(UI(Tr(L"Languages I understand (not translated by themselves):")).c_str());
            {
                static const wchar_t* kCommon[] = {L"EN", L"DE", L"FR", L"ES", L"IT", L"PT", L"NL", L"PL",
                                                   L"RU", L"TR", L"ZH", L"KO", L"JA", L"AR"};
                int column = 0;
                for (const wchar_t* code : kCommon) {
                    bool on = false;
                    for (const std::wstring& u : edit.understoodLangs)
                        if (PrimaryLang(u) == PrimaryLang(code)) on = true;
                    if (column % 7) ImGui::SameLine(static_cast<float>(column % 7) * ImGui::GetFontSize() * 4.2f);
                    ++column;
                    if (ImGui::Checkbox((U8(code) + "##und").c_str(), &on)) {
                        auto& list = edit.understoodLangs;
                        list.erase(std::remove_if(list.begin(), list.end(),
                                                  [&](const std::wstring& u) { return PrimaryLang(u) == PrimaryLang(code); }),
                                   list.end());
                        if (on) list.push_back(code);
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", U8(NativeName(code)).c_str());
                }
            }
            Hint(UI(Tr(L"So the keyboard stays in the background: only a French or Spanish line (whatever you did not tick) "
                       L"is translated. A click on a name opens a whisper tab that answers in that player's language.")));
            ImGui::Checkbox(UI(Tr(L"Translate my text")).c_str(), &edit.translateWriting);
            Hint(UI(Tr(L"What you write goes into the chat in the language chosen at \"Send\" below the input.")));
            static const Engine kEngines[] = {Engine::Auto, Engine::Basic, Engine::DeepL, Engine::Google, Engine::Microsoft, Engine::Libre};
            static const wchar_t* kNames[] = {L"Automatic", L"MyMemory (free)", L"DeepL", L"Google", L"Microsoft", L"LibreTranslate"};
            int ei = 0;
            for (int i = 0; i < 6; ++i)
                if (kEngines[i] == edit.engine) ei = i;
            static std::vector<std::string> s_engines;
            static UiLang s_engineLang = static_cast<UiLang>(-1);
            if (s_engineLang != g_cfg.uiLang || s_engines.empty()) {
                s_engineLang = g_cfg.uiLang;
                s_engines.clear();
                for (const wchar_t* n : kNames) s_engines.push_back(UI(Tr(n)));
            }
            if (combo(UI(Tr(L"Translator") + L"##engine").c_str(), s_engines, &ei)) {
                edit.engine = kEngines[ei];
                const std::wstring k = edit.engine == Engine::DeepL ? edit.deeplKey : edit.engine == Engine::Google ? edit.googleKey
                                       : edit.engine == Engine::Microsoft ? edit.msKey : edit.libreKey;
                strncpy_s(keyBuf, U8(k).c_str(), _TRUNCATE);
            }
            if (edit.engine == Engine::DeepL || edit.engine == Engine::Google || edit.engine == Engine::Microsoft ||
                edit.engine == Engine::Libre) {
                ImGui::SetNextItemWidth(wide);
                ImGui::InputText(UI(Tr(L"API key") + L"##key").c_str(), keyBuf, sizeof(keyBuf), ImGuiInputTextFlags_Password);
            }
            if (edit.engine == Engine::Microsoft) {
                ImGui::SetNextItemWidth(wide);
                ImGui::InputText(UI(Tr(L"Region") + L"##region").c_str(), regionBuf, sizeof(regionBuf));
            }
            if (edit.engine == Engine::Libre) {
                ImGui::SetNextItemWidth(wide);
                ImGui::InputText(UI(Tr(L"Server") + L"##url").c_str(), urlBuf, sizeof(urlBuf));
            }
            if (edit.engine == Engine::Auto || edit.engine == Engine::Basic) {
                ImGui::SetNextItemWidth(wide);
                ImGui::InputText(UI(Tr(L"E-mail for MyMemory (more free text per day)") + L"##mail").c_str(), mailBuf, sizeof(mailBuf));
                Hint(UI(Tr(L"Any made-up address works – MyMemory only counts the free text per address (5,000 characters a "
                           L"day without one, 50,000 with one).")));
            }
            ImGui::EndTabItem();
        }
        // ---- Technical
        if (ImGui::BeginTabItem(UI(Tr(L"Technical")).c_str(), nullptr, TabFlag(5))) {
            ImGui::Checkbox(UI(Tr(L"Automatic timing (FPS-adapted)") + L"##autotiming").c_str(), &edit.send.autoTiming);
            Hint(UI(Tr(L"Calculates key hold and step delays dynamically based on the game's current framerate.")));

            const int oldHold = edit.send.keyHoldMs;
            const int oldStep = edit.send.stepDelayMs;
            ImGui::SetNextItemWidth(120);
            if (ImGui::InputInt(UI(Tr(L"Key hold when sending (ms)") + L"##hold").c_str(), &edit.send.keyHoldMs)) {
                if (edit.send.keyHoldMs != oldHold) edit.send.autoTiming = false;
            }
            ImGui::SetNextItemWidth(120);
            if (ImGui::InputInt(UI(Tr(L"Pause between keys (ms)") + L"##step").c_str(), &edit.send.stepDelayMs)) {
                if (edit.send.stepDelayMs != oldStep) edit.send.autoTiming = false;
            }
            edit.send.keyHoldMs = std::clamp(edit.send.keyHoldMs, 10, 500);
            edit.send.stepDelayMs = std::clamp(edit.send.stepDelayMs, 10, 1000);
            ImGui::Separator();
            std::lock_guard<std::mutex> lock(g_mu);
            const int fps = g_fps;
            const int hold = (fps > 0 && edit.send.autoTiming) ? std::clamp(1000 / std::max(fps, 10) + 10, 15, 1010) : edit.send.keyHoldMs;
            ImGui::Text("GW2Chatlator for Nexus %s", GCT_VERSION_STR);
            ImGui::Text("%s", UI(TrF(L"Game: {1} fps → keys held {2} ms", {std::to_wstring(fps), std::to_wstring(hold)})).c_str());
            ImGui::Text("%s", UI(TrF(L"Translator: {1}", {g_translator ? g_translator->Name() : std::wstring(L"-")})).c_str());
            if (const std::wstring q = MyMemoryQuotaText(); !q.empty() && (g_nx.translateChat || g_cfg.translateWriting))
                ImGui::Text("%s", UI(q).c_str());
            ImGui::Text("Events: Chat: %s", g_chatEventsPresent || g_chatEventsLive ? "[OK]" : "[--]");
            ImGui::Text("%s", UI(TrF(L"Voice: {1}", {!g_cfg.voiceEnabled ? Tr(L"off") : g_voiceReady ? Tr(L"ready – loaded")
                                                       : g_voice.Running() ? Tr(L"starting")
                                                       : g_modules.voiceModule ? Tr(L"ready – loads on the first use of the microphone")
                                                                               : Tr(L"the voice module is missing")})).c_str());
            ImGui::Text("%s", UI(TrF(L"Chat lines: {1}", {std::to_wstring(g_lines.size())})).c_str());
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
        g_optionsTab = -1;
    }

    ImGui::Separator();
    if (ImGui::Button(UI(Tr(L"Save")).c_str())) {
        const std::wstring key = W(keyBuf);
        if (edit.engine == Engine::DeepL) edit.deeplKey = key;
        else if (edit.engine == Engine::Google) edit.googleKey = key;
        else if (edit.engine == Engine::Microsoft) edit.msKey = key;
        else if (edit.engine == Engine::Libre) edit.libreKey = key;
        edit.msRegion = W(regionBuf);
        edit.libreUrl = W(urlBuf);
        edit.basicEmail = W(mailBuf);
        edit.uiLang = EffectiveUiLang(edit.uiLangCode);
        bool typingChanged, voiceChanged;
        {
            std::lock_guard<std::mutex> lock(g_mu);
            typingChanged = edit.writeIn != g_cfg.writeIn || edit.spellEnabled != g_cfg.spellEnabled;
            voiceChanged = edit.voiceEnabled != g_cfg.voiceEnabled;
            edit.myMemoryDay = g_cfg.myMemoryDay;  // counted meanwhile
            edit.myMemoryUsed = g_cfg.myMemoryUsed;
            g_cfg = edit;
            g_cfg.SaveAll();
            SetUiLang(g_cfg.uiLang);
            g_translator = MakeTranslator(g_cfg);
            g_sendIdx = g_cfg.translateWriting ? 0 : static_cast<int>(g_cfg.writeLangs.size());
            SetStatus(Tr(L"Saved"), 3000);
        }
        if (typingChanged) g_worker.Post([] {  // spelling and learned words follow the language you write in
            FreeWorkerState();
            InitSpelling();
        });
        else g_worker.Post([learn = edit.learnWords] { if (g_spell) g_spell->SetLearnChoices(learn); });
        ApplyPushToTalkKey(edit.voiceGlobalKey);
        if (!edit.voiceActivation || !edit.voiceEnabled) StopListening();
        else g_listenUserOff = false;  // asked for in the options: it listens (started by the window)
        if (voiceChanged && edit.voiceEnabled && !g_voice.Running() &&
            GetFileAttributesW(HelperExe().c_str()) != INVALID_FILE_ATTRIBUTES)
            g_voice.Start(HelperExe(), edit.dataDir);
        else if (g_voice.Running())
            g_voice.Send("reload");
    }
    ImGui::SameLine();
    if (ImGui::Button(UI(Tr(L"Undo changes")).c_str())) {
        loaded = false;
    }
}

// Nexus → Addons → GW2 Keyboard: only the button that opens / closes our own options window.
void RenderNexusOptions() {
    if (!ImGui::GetCurrentContext()) return;
    if (ImGui::Button(U8(g_showOptions ? Tr(L"Close the GW2 Keyboard options") : Tr(L"Open the GW2 Keyboard options")).c_str()))
        g_showOptions = !g_showOptions;
}

// Every callback runs behind these two: a C++ exception or an access violation ends our add-on, never the game.
void GuardedRender(void (*fn)()) {
    if (g_disabled) return;
    try {
        fn();
    } catch (...) {
        g_disabled = true;
        Log(LOGL_CRITICAL, "an error – switched off for this session");
    }
}
void SafeRender() {
    __try {
        GuardedRender(RenderWindow);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_disabled = true;
    }
}
void SafeOptions() {
    __try {
        GuardedRender(RenderNexusOptions);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_disabled = true;
    }
}

std::wstring OwnDataDir() {  // <addons>\GW2Keyboard\ – never the game's folder
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(g_self, path, MAX_PATH);
    std::wstring p = path;
    p = p.substr(0, p.find_last_of(L"\\/") + 1) + L"GW2Keyboard";
    CreateDirectoryW(p.c_str(), nullptr);
    return p;
}

void AddonLoad(AddonAPI_t* api) {
    g_api = api;
    g_disabled = false;
    g_chatEventsLive = false;
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(api->ImguiContext));
    ImGui::SetAllocatorFunctions(reinterpret_cast<void* (*)(size_t, void*)>(api->ImguiMalloc),
                                 reinterpret_cast<void (*)(void*, void*)>(api->ImguiFree));
    {
        std::lock_guard<std::mutex> lock(g_mu);
        g_cfg.Load(OwnDataDir());
        if (g_cfg.writeLangs.empty()) g_cfg.writeLangs = {L"EN-GB"};
        SetUiLang(g_cfg.uiLang);
        g_translator = MakeTranslator(g_cfg);
        g_sendIdx = g_cfg.translateWriting ? 0 : static_cast<int>(g_cfg.writeLangs.size());
        LoadLayout();
        LoadCustomTabs();
        g_compact = GetPrivateProfileIntW(L"NexusWindow", L"Compact", 0, g_cfg.iniPath.c_str()) != 0;
        std::string words;
        if (ReadFileBytes(g_cfg.dataDir + L"\\my-words.txt", words)) g_myWords.Parse(FromUtf8(words));
    }
    g_worker.Start();
    g_worker.Post(InitSpelling);
    {
        std::lock_guard<std::mutex> lock(g_mu);
        g_links.Start(api);
    }
    bool voice = false;
    std::wstring dataDir;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        voice = g_cfg.voiceEnabled;
        dataDir = g_cfg.dataDir;
        std::string fixes;
        if (ReadFileBytes(dataDir + L"\\voice-fixes.txt", fixes)) g_voiceFixes.Parse(FromUtf8(fixes));
    }
    (void)voice;  // the voice helper starts on the first press of the microphone or the key, not now
    bool first;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        first = !g_nx.setupDone && !g_test.noSend;
    }
    if (first) FirstStart();  // Events: Chat, the voice module, a voice model – loaded by themselves once
    if (api->GUI_Register) {
        api->GUI_Register(RT_Render, SafeRender);
        api->GUI_Register(RT_OptionsRender, SafeOptions);
    }
    if (api->InputBinds_RegisterWithString) {
        api->InputBinds_RegisterWithString(kToggleBind, OnToggle, "CTRL+ALT+K");
        api->InputBinds_RegisterWithString(kFocusBind, OnFocusKey, "(null)");
        api->InputBinds_RegisterWithString(kTalkBind, OnTalkKey, "(null)");
    }
    ApplyPushToTalkKey(g_cfg.voiceGlobalKey);
    if (api->WndProc_Register) api->WndProc_Register(OnWndProc);
    if (api->Events_Subscribe) {
        api->Events_Subscribe(bridge::kChatEventsEvent, OnChatEvents);
    }
    Log(LOGL_INFO, "loaded");
}

void AddonUnload() {
    if (g_api) {
        if (g_api->Events_Unsubscribe) {
            g_api->Events_Unsubscribe(bridge::kChatEventsEvent, OnChatEvents);
        }
        if (g_api->WndProc_Deregister) g_api->WndProc_Deregister(OnWndProc);
        if (g_api->InputBinds_Deregister) {
            g_api->InputBinds_Deregister(kToggleBind);
            g_api->InputBinds_Deregister(kFocusBind);
            g_api->InputBinds_Deregister(kTalkBind);
        }
        if (g_api->GUI_Deregister) {
            g_api->GUI_Deregister(SafeRender);
            g_api->GUI_Deregister(SafeOptions);
        }
    }
    g_downloadCancel = true;
    if (g_download.joinable()) g_download.join();
    if (g_chatEventsThread.joinable()) g_chatEventsThread.join();
    if (g_voiceModuleThread.joinable()) g_voiceModuleThread.join();
    g_links.Stop();
    g_voice.Stop();  // the helper ends (it would also end by itself when its input closes)
    g_worker.Stop(FreeWorkerState);  // saved and freed on the worker (it owns them), before the DLL goes
    std::lock_guard<std::mutex> lock(g_mu);
    g_lines.clear();
    g_translator.reset();
    g_api = nullptr;
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) g_self = module;
    return TRUE;
}

extern "C" __declspec(dllexport) void* Gw2KbTestHooks() {  // nexus_kb_selftest only
    g_test.input = g_input;
    g_test.focus = &g_focusInput;
    return &g_test;
}

extern "C" __declspec(dllexport) void Gw2KbTestNewTab(uint32_t channels, bool settings) {  // nexus_kb_selftest only
    std::lock_guard<std::mutex> lock(g_mu);
    const int at = NewCustomTab();
    if (at < 0) return;
    g_tabs[at].channels = channels;
    if (settings) g_tabSettings = at;
}

extern "C" __declspec(dllexport) void Gw2KbTestWaypoint(int step) {  // nexus_kb_selftest only: 0 click, 1 map closed
    std::lock_guard<std::mutex> lock(g_mu);
    if (step == 0) HandleChatLinkClick("[&BAwEAAA=]");
    else g_expandRequest = true;
}

extern "C" __declspec(dllexport) void Gw2KbTestWhisper(const char* name) {  // nexus_kb_selftest only: a click on a name
    std::lock_guard<std::mutex> lock(g_mu);
    OpenWhisperTab(W(name));
}

extern "C" __declspec(dllexport) AddonDefinition_t* GetAddonDef() {
    g_def.Signature = static_cast<uint32_t>(-0x474B4244);  // random negative: not hosted on Raidcore (yet)
    g_def.APIVersion = NEXUS_API_VERSION;
    g_def.Name = kName;
    g_def.Version.Major = GCT_VER_MAJOR;
    g_def.Version.Minor = GCT_VER_MINOR;
    g_def.Version.Build = GCT_VER_PATCH;
    g_def.Version.Revision = 0;
    g_def.Author = "Emisan01";
    g_def.Description = "Smart typing assistance, real-time chat translation, voice-to-text input, and whisper tabs "
                        "built natively into Guild Wars 2. The chat comes from \"Events: Chat\" (Nexus library).";
    g_def.Load = AddonLoad;
    g_def.Unload = AddonUnload;
    g_def.Flags = AF_None;
    g_def.Provider = UP_GitHub;
    g_def.UpdateLink = "https://github.com/Emisan01/gw2chatlator";
    return &g_def;
}
