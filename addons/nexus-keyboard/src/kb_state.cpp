// kb_state.cpp — shared state: settings of the window, languages, the translator, status.

#include "kb_shared.hpp"
#include "imgui/imgui_internal.h"
#include <tlhelp32.h>

namespace kb {

Layout g_layout;
NexusOptions g_nx;
TextLook g_look;
MyWords g_myWords;  // your own words and what they mean (<data>\my-words.txt), g_mu
uint32_t g_nextTabUid = 1;
std::map<std::wstring, std::wstring> g_playerLang;
gct::NameList g_chatNames;
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
std::atomic<bool> g_unloading{false};  // Nexus unloads us (game end, update, uninstall): every install stops now
std::atomic<bool> g_chatEventsLive{false};
std::atomic<bool> g_rawTranslate{false};  // Technical tab, this session only: our translation logic off, for comparing
std::atomic<bool> g_keyboardFree{true};  // ImGui does not hold the keyboard (keys reach the game)
std::atomic<int> g_fps{0};
bool g_show = true;
TestHooks g_test;
bool g_testToggleFallenAngel = false;
bool g_testFakeNoDictionary = false;
std::atomic<int> g_healthWordsFinished{0};
std::atomic<int> g_healthCorrectionChecks{0};
std::atomic<int> g_healthLtRequests{0};
std::atomic<int> g_healthLtFailsInRow{0};
std::atomic<int> g_healthMtFailsInRow{0};
std::atomic<int> g_healthSendFails{0};
std::atomic<int> g_healthWhisperElsewhere{0};
std::atomic<ULONGLONG> g_lastChatLineTime{0};
std::vector<gct::HealthFinding> g_health;
std::set<gct::HealthKind> g_healthFindingsAll;
std::set<gct::HealthKind> g_healthLastLogged;
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
thread_local Lane g_workerLane = Lane::Unknown;
Worker g_worker;
SpellService* g_spell = nullptr;
std::atomic<bool> g_spellDictReady{true};
std::wstring g_spellDictLocale;
SendGuard g_guard;
WhisperWatch g_whisperWatch;
MumbleLink* g_mumbleWorker = nullptr;
bool g_lateStartDone = false;
std::mutex g_mu;
Config g_cfg;
std::shared_ptr<Translator> g_translator;
TranslationOrchestrator g_orchestrator;
std::deque<ChatLine> g_lines;
uint64_t g_nextId = 1;
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
PendingSend g_pendingSend;
std::atomic<unsigned long long> g_shiftClickAt{0};
bool g_lineHoldsCode = false;
std::wstring g_syncedChannel;
std::atomic<bool> g_letGoKeyboard{false};
std::wstring g_status;
ULONGLONG g_statusUntil = 0;
std::deque<InfoEntry> g_infoLog;
TypingHelp g_help;
bool g_helpRequested = false;
std::deque<std::wstring> g_history;
int g_historyPos = -1;
Channel g_channel = Channel::Unknown;  // Unknown = the channel active in GW2
int g_guildSlot = 0;
std::wstring g_whisperTo;
int g_sendIdx = 0;                     // index into g_cfg.writeLangs; == size: send as written
bool g_sending = false;
std::wstring g_lastSendTrace;
std::vector<std::wstring> g_previewVersions, g_previewBacks;
size_t g_previewPick = 0;
std::deque<MtRun> g_mtRuns;
std::atomic<int> g_mtWorking{0};
bool g_closedWithX = false;
bool g_showMtLive = false;
gct::CpuUse g_cpuNow, g_cpuPeak, g_cpuAtPeak;
ULONGLONG g_cpuPeakAt = 0;
std::wstring g_cpuPeakDuring;
KeyStats g_keys;
LoadTest g_loadTest;
std::atomic<bool> g_gameThreadsKeep{false};
namespace {
std::mutex s_techMu;  // nothing else is locked while it is held
std::deque<TechEntry> s_techLog;
std::wstring s_techFile;
GameThreads s_gameThreads;
std::map<DWORD, uint64_t> s_threadTimes;
ULONGLONG s_threadsAt = 0;
std::atomic<DWORD> s_renderThread{0};

void MeasureGameThreads(float helperShare, int fps) {
    const ULONGLONG now = GetTickCount64();
    const uint64_t wall = s_threadsAt ? (now - s_threadsAt) * 10000 : 0;
    const float seconds = static_cast<float>(wall) / 1e7f;
    s_threadsAt = now;
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return;
    using DescribeFn = HRESULT(WINAPI*)(HANDLE, PWSTR*);
    static const auto describe = reinterpret_cast<DescribeFn>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetThreadDescription"));
    std::vector<GameThreadRow> rows;
    std::map<DWORD, uint64_t> times;
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    const DWORD self = GetCurrentProcessId();
    for (BOOL more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry)) {
        if (entry.th32OwnerProcessID != self) continue;
        const HANDLE thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ThreadID);
        if (!thread) continue;
        FILETIME created, ended, kernel, user;
        if (GetThreadTimes(thread, &created, &ended, &kernel, &user)) {
            const uint64_t busy = (static_cast<uint64_t>(kernel.dwHighDateTime) << 32 | kernel.dwLowDateTime) +
                                  (static_cast<uint64_t>(user.dwHighDateTime) << 32 | user.dwLowDateTime);
            times[entry.th32ThreadID] = busy;
            const auto before = s_threadTimes.find(entry.th32ThreadID);
            if (wall && before != s_threadTimes.end()) {
                GameThreadRow row;
                row.id = entry.th32ThreadID;
                row.core = gct::CpuPercent(gct::CpuDelta(busy, before->second), wall, 1);
                row.render = row.id == s_renderThread.load();
                row.ours = g_worker.IsOurs(row.id);
                if (row.core >= 1.0f && describe) {
                    PWSTR name = nullptr;
                    if (SUCCEEDED(describe(thread, &name)) && name) {
                        row.name = name;
                        LocalFree(name);
                    }
                }
                rows.push_back(std::move(row));
            }
        }
        CloseHandle(thread);
    }
    CloseHandle(snapshot);
    s_threadTimes = std::move(times);
    if (!wall) return;
    std::sort(rows.begin(), rows.end(), [](const GameThreadRow& a, const GameThreadRow& b) { return a.core > b.core; });
    float busiest = 0;
    for (const GameThreadRow& r : rows)
        if (!r.ours) {
            busiest = r.core;
            break;
        }
    if (rows.size() > 8) rows.resize(8);
    std::lock_guard<std::mutex> lock(s_techMu);
    GameThreads& g = s_gameThreads;
    g.top = std::move(rows);
    g.history.push_back(busiest);
    if (g.history.size() > 120) g.history.erase(g.history.begin());
    g.measuredS += seconds;
    const bool atLimit = busiest >= 90.0f;
    if (atLimit) {
        if (!g.atLimit) {
            ++g.episodes;
            g.nowS = 0;
        }
        g.nowS += seconds;
        g.totalS += seconds;
        g.longestS = std::max(g.longestS, g.nowS);
        if (helperShare >= 1.0f) {
            g.fpsHelperSum += fps;
            ++g.fpsHelperN;
        } else {
            g.fpsIdleSum += fps;
            ++g.fpsIdleN;
        }
    }
    g.atLimit = atLimit;
}
}  // namespace

void TechLogToFile(std::wstring path) {
    std::lock_guard<std::mutex> lock(s_techMu);
    s_techFile = std::move(path);
}

// The session protocol ([Debug] Protocol=1, for development): every entry of the logbook also goes into a file, with
// the time of day – what was set, which view, which language a text was taken for, what failed. Never a text, never
// a name: states, codes and counts only (it may be shown to others). The helper's own lines ('H') stay out of it.
static std::wstring s_protocolFile;
static void ProtocolLine(const std::wstring& text) {  // s_techMu held
    FILE* file = nullptr;
    if (_wfopen_s(&file, s_protocolFile.c_str(), L"ab") != 0 || !file) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    char stamp[24];
    sprintf_s(stamp, "%02u:%02u:%02u.%03u ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    const std::string line = stamp + U8(text) + "\n";
    fwrite(line.data(), 1, line.size(), file);
    fclose(file);
}
void ProtocolToFile(std::wstring path) {
    std::lock_guard<std::mutex> lock(s_techMu);
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info) && (info.nFileSizeHigh || info.nFileSizeLow > 2u * 1024 * 1024))
        MoveFileExW(path.c_str(), (path + L".old").c_str(), MOVEFILE_REPLACE_EXISTING);  // it never grows without end
    s_protocolFile = std::move(path);
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t day[16];
    swprintf_s(day, L"%04u-%02u-%02u", t.wYear, t.wMonth, t.wDay);
    ProtocolLine(std::wstring(L"== session ") + GCT_BUILD_DISPLAY_WSTR + L", " + day);
}
// One line of what is set right now, after `why` changed it (g_mu held).
void LogState(const wchar_t* why) {
    const Layout& l = g_layout;
    const std::wstring parts = std::wstring(l.header ? L"H" : L"-") + (l.chat ? L"C" : L"-") + (l.preview ? L"P" : L"-") +
                               (l.words ? L"W" : L"-") + (l.footer ? L"F" : L"-") + (l.minButton ? L"M" : L"-");
    static const wchar_t* const kView[] = {L"parts", L"compact", L"mini", L"subtitles"};
    const std::wstring send = SendLang();
    TechLog('O', std::wstring(L"state after ") + why + L": translator " + (g_translator ? g_translator->Name() : std::wstring(L"-")) +
                     L", beam " + std::to_wstring(g_cfg.localQuality) + L", versions " + std::to_wstring(g_cfg.versions) +
                     L", card " + (g_cfg.useGpu ? L"1" : L"0") + L", typing " + (g_cfg.writeIn.empty() ? L"system(" + TypingLang() + L")" : g_cfg.writeIn) +
                     L", reading " + (g_cfg.readLang.empty() ? std::wstring(L"-") : g_cfg.readLang) +
                     L", send " + (send.empty() ? std::wstring(L"as written") : send) + (g_sendOverride.empty() ? L"" : L" (tab)") +
                     L", view " + kView[std::clamp(static_cast<int>(l.view), 0, 3)] + L", own parts " + parts +
                     L", chat translation " + (g_nx.translateChat ? L"1" : L"0"));
}

void TechLog(char area, std::wstring text) {
    std::lock_guard<std::mutex> lock(s_techMu);
    if (area != 'H' && !s_protocolFile.empty()) ProtocolLine(std::wstring(1, static_cast<wchar_t>(area)) + L" " + text);
    if (area == 'S' && !s_techFile.empty()) {
        FILE* file = nullptr;
        if (_wfopen_s(&file, s_techFile.c_str(), L"ab") == 0 && file) {
            const std::string line = std::to_string(GetTickCount64()) + " " + U8(text) + "\n";
            fwrite(line.data(), 1, line.size(), file);
            fclose(file);
        }
    }
    s_techLog.push_back({GetTickCount64(), area, std::move(text)});
    if (s_techLog.size() > 400) s_techLog.pop_front();
}
std::vector<TechEntry> TechLogNow() {
    std::lock_guard<std::mutex> lock(s_techMu);
    return std::vector<TechEntry>(s_techLog.begin(), s_techLog.end());
}
void TechLogClear() {
    std::lock_guard<std::mutex> lock(s_techMu);
    s_techLog.clear();
}
GameThreads GameThreadsNow() {
    std::lock_guard<std::mutex> lock(s_techMu);
    return s_gameThreads;
}
void GameThreadsReset() {
    std::lock_guard<std::mutex> lock(s_techMu);
    s_gameThreads = {};
}

void NoteMtRun(MtRun run) {
    {
        wchar_t line[160];
        swprintf_s(line, L"%ls %ls>%ls · beam %d/%d · %d thr · %lld ms (model %d, queue %lld) · %d>%d tok · %d req · %.2f",
                   run.kind == 'C' ? L"chat" : run.kind == 'I' ? L"input" : run.kind == 'B' ? L"back" : L"load", run.from.c_str(), run.to.c_str(),
                   run.run.beam ? run.run.beam : run.beamAsked, run.beamSet, run.run.threads, run.ms, run.run.modelMs, run.queueMs,
                   run.run.tokensIn, run.run.tokensOut, run.run.requests, run.scored ? run.score : 0.0);
        std::wstring text = line;
        if (run.run.sentencesKept) text += L" · " + std::to_wstring(run.run.sentencesKept) + L" kept";
        if (run.second) text += L" · second try";
        if (run.failed) text += L" · failed: " + run.error;
        TechLog('T', text + L" · " + run.tag);
    }
    g_mtRuns.push_back(std::move(run));
    if (g_mtRuns.size() > 120) g_mtRuns.pop_front();
}
ULONGLONG g_lastUse = 0;
bool g_gameChatOpen = false;  // GW2's chat line (MumbleLink), g_mu
bool g_focusInput = false;
PendingEdit g_edit;
int g_boxLines = 4;  // the input's lines this frame: your setting, more while a longer text waits to be sent
int g_wrapWantLines = 0;
int g_pick = -1;  // Tab / Shift+Tab move through the word bar; Space writes the picked word
AutoUndo g_undo;
std::atomic<bool> g_ltInFlight{false};
std::wstring g_ltInFlightText;
SendAfterCheck g_sendAfterCheck;
std::set<std::wstring> g_revertedWords;
std::set<std::wstring> g_revertedRules;
std::wstring g_finishedLast;  // learning mode: the last word when a space last followed it (see NoteSelfFix)
size_t g_finishedAt = 0;
bool g_voiceReady = false;       // the helper said "ready"
bool g_recording = false;        // a recording runs
std::atomic<bool> g_voiceMuted{false}; // Discord-safe mute for voice recognition
std::wstring g_voiceLive;        // the live text while speaking (only drawn, never written into the input)
std::wstring g_lastDictated;     // the last recognized text as it came (learning misheard words on send)
std::wstring g_dictLang;         // its language
VoiceFixBook g_voiceFixes;       // learned misheard words (<data>\voice-fixes.txt), per language
bool g_autoSendPending = false;
bool g_userRecording = false;
bool g_voiceStoppedManually = false;
HWND g_gameHwnd = nullptr;
TranslateErrorKind g_lastErrorKind = TranslateErrorKind::None;
std::wstring g_lastErrorTime = L"-";
bool g_myMemorySessionFallback = false;
uint64_t g_lastErrorGen = 0;
bool InputErrorNow() { return g_lastErrorKind != TranslateErrorKind::None && g_lastErrorGen == g_inputGen; }
bool g_inputActive = false;

bool g_escRequested = false;
bool g_escCapturable = false;
std::vector<BufferedKey> g_keyBuffer;
std::mutex g_keyBufferMu;
std::atomic<bool> g_bufferingKeys{false};
std::atomic<ULONGLONG> g_bufferingStartTime{0};
std::atomic<bool> g_sendInFlight{false};
std::atomic<bool> g_bufferHadInputFocus{false};
// Once a send let go of the keyboard, everything you type is kept and goes into our input when it is done (up to
// the input's 1023 bytes: more than five full GW2 lines). The safety counts from the send's last sign of life: while
// it is translated, as long as a translator may take (an online one waits up to 20 s); while it is typed into GW2,
// 60 of its steps at the game's frame rate, at least 3 s – each step renews it. A send that stops moving frees the
// keyboard; the keys kept so far still go into our input.
int BufferSafetyMs() {
    if (!g_sending) return 25000;
    const int fps = g_fps.load();
    const int action = fps > 0 ? std::clamp(1000 / fps + 10, 15, 1010) : 60;  // one frame + 10 ms, as the send timing
    return std::clamp(60 * action, 3000, 15000);
}
std::atomic<ULONGLONG> g_lastRenderFrameTime{0};

void RequestAutoEnter() {
    if (!g_cfg.voiceAutoSend) return;
    g_autoSendPending = true;
}

void CancelAutoEnter() {
    g_autoSendPending = false;
    g_sendInFlight = false;
}

bool IsAutoEnterPending() {
    return g_autoSendPending;
}
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
    g_layout.infoText = get(L"InfoText", 1) != 0;  // on at the first start: the hints explain the window
    g_layout.minButton = get(L"MinButton", 1) != 0;
    g_layout.backLine = get(L"BackLine", 1) != 0;
    if (const int view = get(L"View", -1); view >= 0) {
        g_layout.view = static_cast<gct::WindowView>(std::clamp(view, 0, 3));
    } else if (get(L"Subtitles", 0) != 0) {  // settings from before the views stood over the parts
        g_layout.view = gct::WindowView::Subtitles;
    } else {
        gct::WindowParts p{g_layout.header, g_layout.chat, g_layout.preview, g_layout.words, g_layout.footer, g_layout.minButton};
        g_layout.view = gct::ViewOfOldParts(p);
        g_layout.header = p.header, g_layout.chat = p.chat, g_layout.preview = p.preview;
        g_layout.words = p.words, g_layout.footer = p.footer, g_layout.minButton = p.minButton;
    }
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
// The one switch for the chat's translation (Options → Start, the chat menu, Ctrl+Alt+T; user 2026-10-29: instead of
// "Translate the chat", the "Active" chip and the pause): kept, and on, what is in view comes at once. g_mu held.
void SetChatTranslation(bool on) {
    g_nx.translateChat = on;
    SaveNexusOptions();
    if (on) TranslateRecentLines(false);
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
    g_cfg.SaveValue(L"NexusWindow", L"BackLine", g_layout.backLine ? L"1" : L"0");
    g_cfg.SaveValue(L"NexusWindow", L"View", std::to_wstring(static_cast<int>(g_layout.view)));
    g_cfg.SaveValue(L"NexusWindow", L"Subtitles", g_layout.view == gct::WindowView::Subtitles ? L"1" : L"0");  // read by 2.10.x
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
    if (s.empty() || !ms) return;  // without an end it says what is running right now: its result comes with one
    SYSTEMTIME now;
    GetLocalTime(&now);
    char clock[16];
    snprintf(clock, sizeof(clock), "%02u:%02u", static_cast<unsigned>(now.wHour), static_cast<unsigned>(now.wMinute));
    if (!g_infoLog.empty() && g_infoLog.back().text == s) {
        ++g_infoLog.back().count;
        g_infoLog.back().clock = clock;
        return;
    }
    g_infoLog.push_back({clock, s});
    if (g_infoLog.size() > 50) g_infoLog.pop_front();
}

const std::wstring& ReadLang() {
    static std::wstring s_cached, s_last = L"\x01";
    if (s_last != g_cfg.readLang || s_cached.empty()) {
        s_last = g_cfg.readLang;
        if (!g_cfg.readLang.empty()) {
            s_cached = PrimaryLang(g_cfg.readLang);
        } else {
            wchar_t locale[LOCALE_NAME_MAX_LENGTH] = {};
            GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH);
            s_cached = PrimaryLang(locale);
        }
    }
    return s_cached;
}

const std::wstring& TypingLang() {  // the language you write in
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
    const bool voiceRoute = (asSpokenWithSwitch || g_inputSpoken) && (asSpokenWithSwitch || g_cfg.voiceEnabled) && (asSpokenWithSwitch || g_cfg.voiceTranslate);
    if (!g_nx.translateChat && !TestHost() && !voiceRoute) {
        gct::SendRoute r;
        r.translating = false;
        r.lang.clear();
        return r;
    }
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
bool Translating() { return (g_nx.translateChat || TestHost() || VoiceFirst()) && CurrentRoute().translating; }
std::atomic<bool> g_mtLoading{false};
void RememberSend() {
    const size_t n = g_cfg.writeLangs.size();
    g_cfg.sendLang = g_sendIdx >= 0 && static_cast<size_t>(g_sendIdx) < n ? g_cfg.writeLangs[g_sendIdx] : L"=";
    g_cfg.SaveValue(L"Translate", L"SendLang", g_cfg.sendLang);
}
void RestoreSend() {
    const int asWritten = static_cast<int>(g_cfg.writeLangs.size());
    g_sendIdx = g_cfg.translateWriting ? 0 : asWritten;  // nothing chosen yet: as before
    if (g_cfg.sendLang == L"=") {
        g_sendIdx = asWritten;
    } else if (!g_cfg.sendLang.empty()) {
        const auto it = std::find(g_cfg.writeLangs.begin(), g_cfg.writeLangs.end(), g_cfg.sendLang);
        if (it != g_cfg.writeLangs.end()) g_sendIdx = static_cast<int>(it - g_cfg.writeLangs.begin());
    }
}
bool SwapsNames() {
    const gct::SendRoute r = CurrentRoute();
    return !r.translating && !r.lang.empty();
}
bool NamesSwapped() {
    if (g_previewGen != g_inputGen || g_preview.empty() || !SwapsNames()) return false;
    const std::wstring typed = SanitizeChatText(g_wFlatInput);
    const ChatSplit own = SplitChatCommand(typed);
    return g_preview != (own.prefix.empty() ? typed : own.body);
}
const std::wstring& ComposePrefix() {
    static std::wstring s_cachedPrefix;
    static Channel s_cachedChannel = Channel::Unknown;
    static std::wstring s_cachedWhisper;
    static int s_cachedSlot = 0;
    if (s_cachedChannel != g_channel || s_cachedSlot != g_guildSlot || (g_channel == Channel::Whisper && s_cachedWhisper != g_whisperTo)) {
        s_cachedChannel = g_channel;
        s_cachedSlot = g_guildSlot;
        s_cachedWhisper = g_whisperTo;
        if (g_channel == Channel::Whisper) {
            s_cachedPrefix = g_whisperTo.empty() ? L"/r " : L"/w " + g_whisperTo + L", ";
        } else if (g_channel == Channel::Guild && g_guildSlot > 0) {
            s_cachedPrefix = L"/g" + std::to_wstring(g_guildSlot) + L" ";
        } else if (const wchar_t* cmd = ChannelCommand(g_channel)) {
            s_cachedPrefix = std::wstring(cmd) + L" ";
        } else {
            s_cachedPrefix.clear();
        }
    }
    return s_cachedPrefix;
}

// The card's generation, from NVIDIA's own driver library (nvml.dll, in System32 with every driver), asked once on the
// worker: the card's helper needs compute capability 7.5 or more – GTX 16, RTX 20 and up. Older cards are not offered its
// 560 MB module. -1 not known yet, 0 no answer (no NVIDIA card, or the library missing).
static int ReadCardCapability() {
    HMODULE nvml = LoadLibraryExW(L"nvml.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!nvml) return 0;
    int cc = 0;
    const auto init = reinterpret_cast<int (*)()>(GetProcAddress(nvml, "nvmlInit_v2"));
    const auto shutdown = reinterpret_cast<int (*)()>(GetProcAddress(nvml, "nvmlShutdown"));
    const auto handle = reinterpret_cast<int (*)(unsigned, void**)>(GetProcAddress(nvml, "nvmlDeviceGetHandleByIndex_v2"));
    const auto capability = reinterpret_cast<int (*)(void*, int*, int*)>(GetProcAddress(nvml, "nvmlDeviceGetCudaComputeCapability"));
    if (init && shutdown && handle && capability && init() == 0) {
        void* device = nullptr;
        int major = 0, minor = 0;
        if (handle(0, &device) == 0 && capability(device, &major, &minor) == 0) cc = major * 10 + minor;
        shutdown();
    }
    FreeLibrary(nvml);
    return cc;
}
static std::atomic<int> s_cardCapability{-1};
static std::atomic<bool> s_cardAsked{false};
bool CardCanRunHelper() {
    if (!s_cardAsked.exchange(true)) g_worker.Post([] { s_cardCapability = ReadCardCapability(); });
    return s_cardCapability.load() >= 75;
}

// A term taught or removed: lines translated before it must not come back from the repetition cache with the old words
// ("comms" as "Kommunikationen"). Under g_mu, as the chat and the worker use the cache.
void ForgetRepetitions() {
    std::lock_guard<std::mutex> lock(g_mu);
    g_orchestrator.ClearCache();
}

// The card's module whole – its helper and NVIDIA's libraries, none empty (one check, the same the Translator page shows).
// One missing: Windows would end the helper at once, maybe with its own message box over the game – so the CPU's helper
// is taken right away.
// Render thread only (the options pages): four file sizes, looked at once a second at most instead of every frame.
bool CardModuleComplete() {
    static ULONGLONG s_next = 0;
    static bool s_present = false;
    const ULONGLONG now = GetTickCount64();
    if (now >= s_next) {
        s_present = gct::GpuModulePresent(KeyboardDir());
        s_next = now + 1000;
    }
    return s_present;
}

bool NvidiaDriverPresent() {
    static const bool s_present = [] {
        wchar_t dir[MAX_PATH] = {};
        const UINT n = GetSystemDirectoryW(dir, MAX_PATH);
        return n > 0 && n < MAX_PATH && GetFileAttributesW((std::wstring(dir) + L"\\nvcuda.dll").c_str()) != INVALID_FILE_ATTRIBUTES;
    }();
    return s_present;
}

Engine EffectiveEngine(const Config& c, const LocalMtSummary& local) {
    if (c.engine != Engine::Auto) return c.engine;
    if (local.module && local.nllb) return Engine::Nllb;
    if (local.module && local.m2m) return Engine::M2m;
    if (local.module && local.opus) return Engine::Local;
    if (!c.deeplKey.empty()) return Engine::DeepL;
    if (!c.googleKey.empty()) return Engine::Google;
    if (!c.msKey.empty()) return Engine::Microsoft;
    if (!c.supertextKey.empty()) return Engine::Supertext;
    if (!c.libreUrl.empty()) return Engine::Libre;
    return Engine::Basic;
}

std::shared_ptr<Translator> MakeTranslator(const Config& c) {  // the engine you set, else the best key you have
    if (g_myMemorySessionFallback) return MakeMyMemoryTranslator(c.basicEmail);
    const LocalMtSummary local = LocalMtNow(c.dataDir);
    const Engine eng = EffectiveEngine(c, local);
    if (eng == Engine::Basic) return MakeMyMemoryTranslator(c.basicEmail);
    if (eng == Engine::DeepL && !c.deeplKey.empty()) return MakeDeepLTranslator(c.deeplKey);
    if (eng == Engine::Google && !c.googleKey.empty()) return MakeGoogleTranslator(c.googleKey);
    if (eng == Engine::Microsoft && !c.msKey.empty()) return MakeMicrosoftTranslator(c.msKey, c.msRegion);
    if (eng == Engine::Supertext && !c.supertextKey.empty()) return MakeSupertextTranslator(c.supertextKey);
    if (eng == Engine::Libre && !c.libreUrl.empty()) return MakeLibreTranslator(c.libreUrl, c.libreKey);
    if (eng == Engine::Local || eng == Engine::Nllb || eng == Engine::M2m) {
        const std::wstring gpuHelper = AddonsDir() + L"GW2Keyboard\\gw2kb_translate_gpu.exe";
        // the files themselves, not the options pages' once-a-second look: right after the card's download this decides,
        // and it may run on the worker
        const bool gpu = c.useGpu && NvidiaDriverPresent() && gct::GpuModulePresent(KeyboardDir());
        const std::wstring helper = gpu ? gpuHelper : AddonsDir() + L"GW2Keyboard\\gw2kb_translate.exe";
        auto localMt = eng == Engine::M2m ? MakeM2mTranslator(helper, c.dataDir)
                                          : MakeLocalMtTranslator(helper, c.dataDir, eng == Engine::Nllb);
        if (eng == Engine::M2m) SetLocalMtDecode(localMt, c.m2m);
        SetLocalMtVersions(localMt, c.versions);
        return localMt;
    }
    if (!c.deeplKey.empty()) return MakeDeepLTranslator(c.deeplKey);
    if (!c.googleKey.empty()) return MakeGoogleTranslator(c.googleKey);
    if (!c.msKey.empty()) return MakeMicrosoftTranslator(c.msKey, c.msRegion);
    if (!c.supertextKey.empty()) return MakeSupertextTranslator(c.supertextKey);
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
ULONGLONG g_micLockedStart = 0;
// The window's views (menu, Start and Options → Window → Window layout), all three by one rule (core/window_view): a
// view stands over your parts and shows its own; your parts stay as they are – from any view to any other nothing is
// lost, and off they are simply back (Shown). One view at a time.
static gct::WindowView ViewOf(int profile) {  // 0 compact view, 1 mini mode, 2 subtitle mode
    return profile == 0 ? gct::WindowView::Compact : profile == 1 ? gct::WindowView::Mini : gct::WindowView::Subtitles;
}
bool ProfileOn(int profile) { return g_layout.view == ViewOf(profile); }  // g_mu held
void SetProfile(int profile, bool on) {  // g_mu held
    g_layout.view = gct::SwitchView(g_layout.view, ViewOf(profile), on);
    SaveLayout();
    LogState(L"a view's switch");
}
Layout& Lay() { return g_layout; }
Layout Shown() {
    Layout s = g_layout;
    const gct::WindowParts p = gct::ShownParts({s.header, s.chat, s.preview, s.words, s.footer, s.minButton}, s.view);
    s.header = p.header, s.chat = p.chat, s.preview = p.preview;
    s.words = p.words, s.footer = p.footer, s.minButton = p.minButton;
    return s;
}

// Remove everything of GW2Chatlator (Options → Technical, at the bottom, after a yes): the voice helper ends, nothing is
// written any more (settings, learned words, my words, voice fixes), the worker drops the spelling without saving it,
// then our whole folder <addons>\GW2Keyboard\ goes – never Events: Chat, which is removed in Nexus only (a file deleted behind Nexus' back stays in its list). The DLL
// itself cannot remove itself while it runs: the notice says the last step (Nexus → Addons → Uninstall).
bool g_wiped = false;
std::atomic<int> g_wipeDone{0};  // 0 not asked, 1 removed, 2 something was left
void RemoveEverythingNow() {  // game thread
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

void SampleCpu() {
    s_renderThread = GetCurrentThreadId();
    LoadTestTick();
    static ULONGLONG lastAt = 0, lastThreads = 0;
    static uint64_t lastHelper = 0, lastVoice = 0, lastWorkers = 0, lastGame = 0, lastIdle = 0, lastKernel = 0, lastUser = 0;
    const ULONGLONG now = GetTickCount64();
    if ((g_showMtLive || g_gameThreadsKeep) && now - lastThreads >= 1000) {
        lastThreads = now;
        g_worker.PostInteractive([helper = g_cpuNow.helper, fps = g_fps.load()] { MeasureGameThreads(helper, fps); });
    }
    if (lastAt && now - lastAt < 500) return;
    const auto ticks = [](const FILETIME& f) { return static_cast<uint64_t>(f.dwHighDateTime) << 32 | f.dwLowDateTime; };
    std::shared_ptr<Translator> tr;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        tr = g_translator;
    }
    FILETIME created, ended, kernel, user, idle;
    const uint64_t helper = LocalMtHelperCpu(tr);
    const HANDLE voiceProcess = g_voice.Process();
    const uint64_t voice = voiceProcess && GetProcessTimes(voiceProcess, &created, &ended, &kernel, &user) ? ticks(kernel) + ticks(user) : 0;
    const uint64_t threads = g_worker.CpuTime();
    const uint64_t game = GetProcessTimes(GetCurrentProcess(), &created, &ended, &kernel, &user) ? ticks(kernel) + ticks(user) : lastGame;
    if (!GetSystemTimes(&idle, &kernel, &user)) return;
    if (lastAt) {
        const unsigned logical = std::max(1u, std::thread::hardware_concurrency());
        const uint64_t wall = (now - lastAt) * 10000;  // ms in 100 ns
        gct::CpuUse use;
        use.helper = gct::CpuPercent(gct::CpuDelta(helper, lastHelper), wall, logical);
        use.voice = gct::CpuPercent(gct::CpuDelta(voice, lastVoice), wall, logical);
        use.threads = gct::CpuPercent(gct::CpuDelta(threads, lastWorkers), wall, logical);
        use.draw = static_cast<float>(g_cost.avg.totalMs * g_fps.load() / 10.0 / logical);  // ms per frame * frames per second
        use.ours = use.helper + use.voice + use.threads + use.draw;
        use.game = std::max(0.0f, gct::CpuPercent(gct::CpuDelta(game, lastGame), wall, logical) - use.threads - use.draw);
        const uint64_t all = gct::CpuDelta(ticks(kernel), lastKernel) + gct::CpuDelta(ticks(user), lastUser);  // the kernel's time holds the idle time
        const uint64_t idled = gct::CpuDelta(ticks(idle), lastIdle);
        use.pc = all > idled ? gct::CpuPercent(all - idled, all, 1) : 0.0f;
        g_cpuNow = use;
        g_cpuPeak.KeepHighest(use);
        if (use.ours > g_cpuAtPeak.ours) {
            g_cpuAtPeak = use;
            g_cpuPeakAt = now;
            std::lock_guard<std::mutex> lock(g_mu);
            g_cpuPeakDuring = g_mtRuns.empty() ? std::wstring() : g_mtRuns.back().tag;
        }
    }
    lastAt = now;
    lastHelper = helper;
    lastVoice = voice;
    lastWorkers = threads;
    lastGame = game;
    lastIdle = ticks(idle);
    lastKernel = ticks(kernel);
    lastUser = ticks(user);
}

FrameCosts CostsNow() {
    FrameCosts c = g_cost.avg;
    c.lines = g_cost.lines;
    c.lineUs = c.lines ? static_cast<double>(g_cost.lineTicks) / 1000.0 / static_cast<double>(c.lines) : 0;
    return c;
}

std::wstring HealthStatusText(gct::HealthKind kind) {
    switch (kind) {
        case gct::HealthKind::NoDictionary:
            return Tr(L"Spell checking: no Windows dictionary installed");
        case gct::HealthKind::CorrectionIdle:
            return Tr(L"Spell checking: no words checked");
        case gct::HealthKind::HelperMissing:
            return Tr(L"Translator: local helper missing");
        case gct::HealthKind::HelperCrashed:
            return Tr(L"Translator: local helper crashed");
        case gct::HealthKind::TranslatorFailing:
            return Tr(L"Translator: repeated errors");
        case gct::HealthKind::SendFailing:
            return Tr(L"Sending to chat failed");
        default:
            return {};
    }
}

}  // namespace kb
