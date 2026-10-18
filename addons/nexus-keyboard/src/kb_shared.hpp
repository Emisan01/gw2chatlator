// kb_shared.hpp — what the modules of the add-on share: types, state (defined in its module), functions.
// Generated from the one kb_main.cpp it was (same order, so it compiles as before); keep it in that order.
#pragma once

#include <windows.h>
#include <objbase.h>
#include <psapi.h>
#include <shellapi.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <map>
#include <atomic>
#include <cwctype>
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
#include "core/contrast.hpp"
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

namespace kb {


constexpr const char* kName = "GW2Chatlator";
constexpr const char* kToggleBind = "KB_GW2KEYBOARD_TOGGLE";
constexpr const char* kFocusBind = "KB_GW2KEYBOARD_FOCUS";
constexpr const char* kTalkBind = "KB_GW2KEYBOARD_TALK";  // push to talk: held while you speak
constexpr size_t kMaxLines = 200;
constexpr size_t kHistoryMax = 50;
constexpr size_t kMaxPartsPerEnter = 3;  // one Enter never pours more chat lines than this into the game
// The window is built from parts you can switch on / off and size (Options → General → Window layout).
struct Layout {
    bool chat = true, preview = true, words = true, footer = true;
    bool header = true;  // the tabs, microphone, minimise, menu, close; hidden: a right-click on the window is the menu
    bool minButton = true;
    bool autoMinimize = false;  // not in use for `minimizeAfter` s: down to the small button
    int minimizeAfter = 10;
    bool autoOpen = false;      // GW2's chat line opening (Enter) opens it from the small button (the focus key always does)  // the small ">" that minimises (in the footer, without it left of the input)
    bool infoText = true;  // the grey status lines (what is going on, modules, counter); never the prediction's grey word
    int inputLines = 4;    // the input box, wrapping
    int previewLines = 3;  // the translation and its back-translation
};
extern Layout g_layout;
// The add-on's own switches ([Nexus] in the settings file).
struct NexusOptions {
    bool translateChat = false;  // foreign chat lines into your language (off: a click on a line translates it)
    bool snap = true;            // the window snaps to the screen edges when you move it there
    bool setupDone = false;      // the first start loaded Events: Chat, the voice module and the base model
    bool voiceFallback = true;   // a model too slow twice in a row: the next smaller installed one is used
    std::wstring namesLang;      // item / place names: "en", "de", "fr", "es", "zh"; "" = the language you write in
};
extern NexusOptions g_nx;
// How chat text looks ([NexusWindow] Glow, Brightness, GameColors): GW2's colours (the name in the channel colour, the
// message a light tint of it), a dark halo around the letters like the game's text outline, a brightness of your own.
struct TextLook {
    bool glow = true;
    int brightness = 100;  // %
    bool gameColors = true;
    bool chatBg = true;  // the darker field behind the chat (it fades with the window's transparency); off: none
};
extern TextLook g_look;

// The window's own theme – only the window itself (frames, tabs, buttons, fields), never the chat text with its
// channel colours. "Nexus" keeps whatever theme is chosen in Nexus (its "Classic" one is blue / purple); the others
// look the same on every PC. Pushed for everything we draw: the options, our window, its popups.
enum class Theme { Nexus, Slate, Grey, Cba };
struct ThemeColours {
    uint32_t text, textDim, bg, child, popup, border, separator;
    uint32_t frame, frameHovered, frameActive;     // input, preview, sliders
    uint32_t button, buttonHovered, buttonActive;  // buttons, menu entries
    uint32_t tab, tabHovered, tabActive;
    uint32_t check, grab, grabActive;              // check marks, slider and scroll bar grips
    uint32_t selection;
};
constexpr uint32_t Rgb(uint32_t rgb, uint32_t alpha = 255) {  // 0xRRGGBB + alpha -> ImGui's ABGR
    return ((rgb >> 16) & 0xFF) | (rgb & 0xFF00) | ((rgb & 0xFF) << 16) | (alpha << 24);
}
// The CBA theme (from cba4gw2): your own colours in eight roles, the rest derived from them; with "automatic contrast"
// the text is moved just as far as needed to stay readable (WCAG 4.5:1, quiet text and accents 3:1) on every fill.
enum CbaRole { kRoleBg, kRoleField, kRoleButton, kRoleButtonActive, kRoleTabActive, kRoleText, kRoleTextDim, kRoleAccent,
               kRoleCount };
extern std::vector<uint32_t> g_cbaRoles;
extern bool g_cbaAutoContrast;
const std::vector<uint32_t>& CbaPreset(int i);
uint32_t CbaShown(int role);
const ThemeColours* ColoursOf(Theme t);
extern Theme g_theme;
int PushTheme();
const wchar_t* ThemeKey(Theme t);

extern MyWords g_myWords;
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
extern uint32_t g_nextTabUid;
// The language each player writes in, as far as it is sure (CaseFold(name) -> code). Only for this session, never
// stored: answering someone in their language (their whisper tab sends in it).
extern std::map<std::wstring, std::wstring> g_playerLang;
// The send language of the whisper tab in front ("" = none: the footer's choice; "=" = as written; else a code).
extern std::wstring g_sendOverride;
extern std::vector<WhisperTab> g_tabs;
extern int g_activeTab;
extern int g_selectTab;
extern int g_tabSettings, g_tabSettingsOpen;
extern std::wstring g_openLink;
std::wstring LinkHost(const std::wstring& url);

extern AddonDefinition_t g_def;
extern AddonAPI_t* g_api;

// The text of our window as a real font at its size in pt (sharp) instead of Nexus' font scaled (blurry). Nexus builds
// it from a Windows font file and sends it to OnTextFont; until it arrives (or without Nexus' font functions, as in
// the self-test) the window scales Nexus' font as before.
constexpr const char* kTextFontId = "GW2Chatlator_Text";
extern std::atomic<ImFont*> g_textFont;
extern float g_textFontPx;
void OnTextFont(const char*, void* font);
float PtToPx(int pt);
void RequestTextFont(int pt);
extern HMODULE g_self;
extern std::atomic<bool> g_disabled;
extern std::atomic<bool> g_chatEventsLive;
extern std::atomic<bool> g_keyboardFree;
extern std::atomic<int> g_fps;
extern bool g_show;
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
extern TestHooks g_test;
extern bool g_showOptions;
void Log(ELogLevel level, const char* text);
std::string U8(const std::wstring& w);
std::wstring W(const std::string& s);
size_t U8Offset(const std::wstring& w, size_t idx);
std::string UI(std::wstring w);

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

extern Worker g_worker;

// Only the worker touches these (no lock needed).
extern SpellService* g_spell;
extern SendGuard g_guard;
extern MumbleLink* g_mumbleWorker;

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

extern std::mutex g_mu;
extern Config g_cfg;
extern std::shared_ptr<Translator> g_translator;
extern std::deque<ChatLine> g_lines;
extern uint64_t g_nextId;
extern bool g_scrollDown;
extern std::vector<std::wstring> g_context;
extern std::wstring g_ownName;
extern std::atomic<int> g_uiSize;
extern std::atomic<uint32_t> g_mapId;
// The input line.
extern char g_input[1024];
extern float g_wrapWidth;
std::string Flat(const char* s);
extern std::wstring g_teachWord;
size_t RawOffset(const char* raw, size_t rawLen, size_t flatByte);
extern std::string g_flatInput;
extern std::wstring g_wFlatInput;
extern std::string g_lastInput;
extern ULONGLONG g_inputChanged;
extern uint64_t g_inputGen;
extern std::wstring g_preview;
extern std::wstring g_back;
extern bool g_inMap;
extern bool g_mapOpen;
extern ULONGLONG g_showOnMapUntil;
extern uint64_t g_previewGen;
extern bool g_sendPending;
extern std::wstring g_status;
extern ULONGLONG g_statusUntil;
extern ULONGLONG g_suppressChatFocusUntil;
extern TypingHelp g_help;
extern bool g_helpRequested;
extern std::deque<std::wstring> g_history;
extern int g_historyPos;
// The channel to write in and the send language.
extern Channel g_channel;
extern std::wstring g_whisperTo;
extern int g_sendIdx;
extern bool g_sending;
// "Minimise when not in use" (g_layout.autoMinimize): the last moment the window was in use – the input had the focus,
// the mouse was over it, a menu was open, it was sending or recording, GW2's chat line was open. 0: count from now.
extern ULONGLONG g_lastUse;
extern bool g_gameChatOpen;
extern bool g_focusInput;
// An edit the worker or a click wants in the input (applied by the input callback on the game thread).
struct PendingEdit {
    bool active = false;
    size_t startByte = 0, lenBytes = 0;
    std::string insert;
};
extern PendingEdit g_edit;
extern int g_boxLines;
extern int g_pick;

struct AutoUndo {  // the last autocorrection: Backspace right after it brings the typed word back
    bool active = false;
    std::string after;   // the buffer right after the correction (with its space)
    std::string before;  // the buffer as typed (without the space)
    std::wstring original;
};
extern AutoUndo g_undo;
extern std::wstring g_finishedLast;
extern size_t g_finishedAt;

// Voice (stage 4): the helper process gw2kb_voice.exe runs the recognizer outside the game.
extern bool g_voiceReady;
extern bool g_recording;
extern std::wstring g_voiceLive;
extern std::wstring g_lastDictated;
extern std::wstring g_dictLang;
extern VoiceFixBook g_voiceFixes;
extern bool g_autoSendPending;
extern std::string g_voicePending;
extern bool g_listening;
extern std::wstring g_listenPausedFor;
extern bool g_listenWanted;
extern bool g_listenUserOff;
// The push-to-talk key (our own, through Nexus' WndProc: GW2 does not get it) and "Set key ..." capturing the next one.
extern std::atomic<unsigned> g_pttVk, g_pttMods;
extern std::atomic<bool> g_captureKey;
extern std::wstring g_capturedKey;

// What each voice model needed when it last recognized you ([VoiceStats] model = peak %,recognize ms,speech ms,
// highest peak %): shown in Options → Voice, and the reason for the automatic fallback.
struct VoiceRun {
    int peak = 0, rms = 0, rec = 0, maxPeak = 0;
    int slow = 0;  // too slow in a row (this session)
};
extern std::map<std::wstring, VoiceRun> g_voiceRuns;
void LoadVoiceRuns();
void LoadLayout();
void SaveNexusOptions();
void SaveLayout();
void SetStatus(const std::wstring& s, ULONGLONG ms = 6000);
const std::wstring& TypingLang();
std::wstring TypingLocale();
bool SendOriginal();
const std::wstring& SendLang();
std::wstring SendCodeFor(const std::wstring& detected);
bool Translating();
const std::wstring& ComposePrefix();
std::shared_ptr<Translator> MakeTranslator(const Config& c);
bool UsingMyMemory();
std::wstring Today();
void CountMyMemory(size_t chars);
std::wstring MyMemoryQuotaText();
void InitSpelling();
void FreeWorkerState();
void TranslateLine(uint64_t id, std::wstring text, bool asked = false, bool once = false);

// The chat lines of other players that stand in our window right now (the visible part of the chat; filled while the
// chat is drawn, g_mu). Only these are translated in one go – an old chat is never translated in full.
extern std::vector<uint64_t> g_visibleLines;
int TranslateRecentLines(bool once);
void TranslateInput(uint64_t gen, std::wstring text);
void ComputeTyping();
void SendNow(std::vector<std::wstring> parts, std::wstring typed, std::wstring learnText);

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

extern VoiceHelper g_voice;
void VoiceReload();
void NoteVoiceRun(const JsonValue& v);
std::wstring HelperExe();
void VoiceCommand(const char* cmd);
void TalkDown();
void TalkUp();
void TalkLocked();
void StartListening();
void StopListening();
void OnTalkKey(const char*, bool release);
UINT OnWndProc(HWND, UINT msg, WPARAM wp, LPARAM lp);
void ApplyPushToTalkKey(const std::wstring& key);
bool AllowAutoTranslate();
void AddLine(Channel ch, bool outgoing, const char* speaker, const char* text);
void OnChatEvents(void* args);
void OpenWhisperTab(const std::wstring& name);
void ActivateCustomTab(int i, bool focus = true);
void ActivateTab(int i);
void SaveCustomTabs();
void LoadCustomTabs();
int NewCustomTab();
void LeaveWhisperTab();
void OnToggle(const char*, bool release);
void OnFocusKey(const char*, bool release);

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
ImVec4 Rgb32(uint32_t rgb);
ImVec4 Bright(ImVec4 c);
const ChannelLook* LookOf(Channel ch);
ImVec4 ColorOf(Channel ch);
ImVec4 NameColorOf(Channel ch);
void LookText(const char* text, ImVec4 color, bool wrap);
std::wstring GhostRest(const std::wstring& text, bool* correction = nullptr);
void TakeWord(const std::wstring& word);
bool SpaceWrites();
int InputCallback(ImGuiInputTextCallbackData* d);
void StartSend();
std::wstring NativeName(const std::wstring& code);

extern bool g_collapsed;
extern float g_lastW, g_expandedH, g_restoreH;
// The bottom edge the window had when it was minimised: the small button sits exactly on it (its size depends on the
// text size and ImGui's minimum window size – computing it twice made the window creep up a little every time).
extern float g_anchorBottom;
extern ImVec2 g_savedPos;
extern bool g_buttonMoved;
extern ImVec2 g_buttonBottomLeft;
ImVec2 RestorePos();
void MinimizeWindow();
void RenderFooter();

// ---- "Events: Chat": is it there, and installing it the way the Nexus library would.
extern std::atomic<bool> g_chatEventsPresent;
extern std::atomic<bool> g_chatEventsFile;
extern std::atomic<bool> g_chatEventsInstalling;
extern std::thread g_chatEventsThread;
const std::wstring& AddonsDir();
bool ChatEventsPresent();
void InstallChatEventsNow();

// ---- the header's icons: microphone, menu (three lines), close
enum class Icon { Mic, Menu, Close, Collapse, Expand };
bool IconButton(const char* id, Icon kind, bool active, bool* rightClick, ImU32 tint = 0);
std::string Fit(const std::string& s, float width);
std::wstring StatusText();

// ---- voice model download (its own thread: minutes, never on the worker or the game's threads)
extern std::thread g_download;
extern std::atomic<bool> g_downloadCancel;
extern std::atomic<int> g_downloadPct;
void StartModelDownload(const VoiceModelInfo& m);

// ---- the modules: the chat (Events: Chat), the voice module (gw2kb_voice.exe + whisper DLLs), the voice model
extern std::atomic<bool> g_voiceModuleInstalling;
extern std::atomic<int> g_voiceModulePct;
extern std::thread g_voiceModuleThread;
extern int g_optionsTab;
std::wstring KeyboardDir();

struct ModuleState {
    bool voiceModule = false;  // helper and DLLs are there
    bool model = false;        // the chosen (or recommended) model is installed
    std::string modelName;
};
extern ModuleState g_modules;
extern ULONGLONG g_modulesNext;
const ModuleState& Modules();
void InstallVoiceModuleNow();
void FirstStart();
void LoadVoiceModule();
void UnloadVoiceModule();
void VoiceIdleUnload();
void RemoveVoiceModuleNow();
void RemoveVoiceModelsNow();
void RemoveChatEventsNow();
void RemoveEverythingNow(bool chatEventsToo);
void RenderRemovedNotice();
extern bool g_wiped;
extern std::atomic<int> g_wipeDone;
void StatusRow(const std::string& what, int state, const std::string& text);
void RenderModules(bool guide);

// ---- chat codes: their own module (chat_links) – "[Item name]" as the game links them, the details on mouse-over.
extern ChatLinks g_links;
std::wstring ApiLang();

// Words you corrected yourself while typing ("shcon" → "schon", this session): a right-click in the input offers to
// learn them, so autocorrection writes them right next time.
struct SelfFix {
    std::wstring typed, fixed;
};
extern std::vector<SelfFix> g_selfFixes;
extern std::vector<std::wstring> g_lastWords;
void AddSelfFix(const std::wstring& was, const std::wstring& now);
void NoteSelfFix(const std::wstring& text);
void SaveMyWords();

// A chat line that wraps word by word with its links (blue, underlined: a click asks before anything opens) and chat
// codes (item/skill/waypoint name in rarity colour via Decoder Ring, hover tooltip with details, click copies code).
extern bool g_openLinkAsk;

// A chat code clicked: the original in GW2's own chat is what works (the game's own map jump, its own tooltip). Our
// window gets out of the way and GW2's chat line opens (Nexus presses the game's chat key) – you click the link there.
// The window comes back when the map was open and closed again (a waypoint) or GW2's chat line closes, at the latest
// after 45 s. A waypoint older than 15 s (its line has probably scrolled away): the game map opens instead and our
// window stays, saying where to look. The code is always copied too.
extern bool g_minimizeRequest, g_expandRequest;
extern int g_redirect;
extern ULONGLONG g_redirectSince;
void HandleChatLinkClick(const std::string& raw, ULONGLONG receivedTime, const std::string& where);
bool RichLine(const std::wstring& text, const std::vector<PieUI::ChatLinks::LinkSegment>& segs, ImVec4 color, ULONGLONG receivedTime);

extern bool g_micLocked;
bool ProfileOn(int profile);
void SetProfile(int profile, bool on);
Layout Lay();
void Hint(const std::string& text);
void RenderOptionsWindow();
void SafeOpenUrl(const std::wstring& rawLink);
void BoxText(const ImVec2& at, const ImVec4& clip, ImU32 color, const std::string& text, float scale = 1.0f);
std::wstring WordAtMouse(const ImVec2& textAt);
void KickSave();
void RenderWindow();
std::wstring FormatSeconds(int ms);
bool LanguageCombo(const char* id, std::wstring& value, const std::wstring& emptyLabel);
void RenderCbaTheme();
void RenderOptionsContent();
void RenderNexusOptions();
void GuardedRender(void (*fn)());
void SafeRender();
void SafeOptions();
std::wstring OwnDataDir();
void AddonLoad(AddonAPI_t* api);
void AddonUnload();

}  // namespace kb
