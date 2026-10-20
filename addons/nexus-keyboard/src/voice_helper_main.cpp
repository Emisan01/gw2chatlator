// voice_helper_main.cpp — gw2kb_voice.exe, the voice helper of GW2 Keyboard for Nexus.
//
// The add-on lives in the game process; whisper (a model of several hundred MB, several threads) does not belong
// there. This small process runs the VoiceSession: the microphone, the speech gate, whisper on the least busy
// cores below normal priority (its own process affinity – never the game's). It talks to the add-on through its
// standard input and output only:
//   in  (one command per line): begin | begin-auto (a pause ends it) | end | cancel | listen | stop-listen | reload | quit
//   out (one JSON object per line): {"t":"ready"} {"t":"started"} {"t":"partial","stable":..,"unsure":..}
//                                   {"t":"final","text":..,"empty":..,"lang":..,"rec":ms,"rms":ms,"peak":%}
//                                   {"t":"error","msg":..}  ("peak": the highest share of the whole CPU this process
//                                   used while recording + recognizing, measured every 100 ms)
//                                   voice activation: {"t":"listening"} {"t":"idle"} {"t":"paused","app":..}
// When the add-on (its parent) goes, standard input closes and the helper ends.

#include <windows.h>
#include <objbase.h>

#include <algorithm>
#include <cwctype>
#include <memory>
#include <string>
#include <thread>

#include "app/config.hpp"
#include "core/json.hpp"
#include "core/langs.hpp"
#include "core/text.hpp"
#include "core/voice.hpp"
#include "kbd/voice_session.hpp"
#include "win/cpu_cores.hpp"
#include "win/mic_sessions.hpp"
#include "win/voice_models.hpp"

using namespace gct;

namespace {

constexpr UINT WM_APP_VOICE = WM_APP + 1;
constexpr UINT WM_APP_COMMAND = WM_APP + 2;  // lParam: std::string* (owned)
constexpr UINT_PTR kTimerCores = 1;
constexpr UINT_PTR kTimerVoiceChat = 2;  // voice activation pauses while a voice chat uses the microphone
constexpr UINT_PTR kTimerCpu = 3;        // the CPU this process takes, while it works

// The share of the whole CPU (all logical processors) this process used since the last look.
struct CpuPeak {
    ULONGLONG lastCpu = 0, lastWall = 0;
    int peak = 0;
    static ULONGLONG ProcessCpu() {
        FILETIME c, e, k, u;
        if (!GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)) return 0;
        return ((static_cast<ULONGLONG>(k.dwHighDateTime) << 32) | k.dwLowDateTime) +
               ((static_cast<ULONGLONG>(u.dwHighDateTime) << 32) | u.dwLowDateTime);  // 100 ns units
    }
    void Sample(bool busy) {
        const ULONGLONG cpu = ProcessCpu(), wall = GetTickCount64();
        if (busy && lastWall && wall > lastWall) {
            const double used = static_cast<double>(cpu - lastCpu) / 10000.0;  // ms of CPU time
            const DWORD logical = std::max<DWORD>(1, GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
            const int pct = static_cast<int>(used * 100.0 / (static_cast<double>(wall - lastWall) * logical) + 0.5);
            peak = std::max(peak, std::min(pct, 100));
        }
        lastCpu = cpu;
        lastWall = wall;
    }
};
CpuPeak g_cpu;

Config g_cfg;
CoreLoad g_cores;
std::unique_ptr<VoiceSession> g_voice;
unsigned g_session = 0;
bool g_wantListen = false;      // voice activation is wanted (it may be paused for a voice chat)
std::wstring g_pausedFor;       // the voice chat that has the microphone now
HANDLE g_out = nullptr;

void Write(const std::string& json) {
    const std::string line = json + "\n";
    DWORD n = 0;
    WriteFile(g_out, line.data(), static_cast<DWORD>(line.size()), &n, nullptr);
}

std::string Str(const char* key, const std::wstring& value) {
    return std::string(",\"") + key + "\":\"" + JsonEscape(ToUtf8(value)) + "\"";
}

// The voice settings: model, language, cores, microphone.
bool BuildSettings(VoiceSettings* s, std::wstring* why) {
    const VoiceCatalog catalog = LoadVoiceCatalog(g_cfg.dataDir);
    const VoiceModelInfo* m = catalog.Find(ToUtf8(g_cfg.voiceModel));
    if (!m) m = catalog.Recommended();
    if (!m || !VoiceModelInstalled(g_cfg.VoiceModelDir(), *m)) {
        *why = L"no voice model installed (GW2 Keyboard options: download one)";
        return false;
    }
    s->modelPath = VoiceModelPath(g_cfg.VoiceModelDir(), *m);
    std::wstring typing = g_cfg.writeIn;
    if (typing.empty()) {
        wchar_t locale[LOCALE_NAME_MAX_LENGTH] = {};
        GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH);
        typing = locale;
    }
    if (m->englishOnly) s->language = "en";
    else if (g_cfg.voiceLang == L"auto") s->language.clear();
    else s->language = WhisperLang(g_cfg.voiceLang.empty() ? typing : g_cfg.voiceLang);
    s->translate = g_cfg.voiceIntoEnglish;
    const VoiceCorePick pick = PickVoiceCores(g_cores.Busy(), PhysicalCores(), g_cfg.voiceThreads);
    if (pick.threads > 0) {
        s->threads = pick.threads;
        s->affinity = CoreMask(pick.logical);  // this helper's affinity, never the game's
    } else {
        s->threads = VoiceThreads(g_cfg.voiceThreads, ProbeVoiceHardware().logicalProcessors);
    }
    s->live = g_cfg.voiceLive;
    s->maxSeconds = g_cfg.voiceMaxSec;
    s->deviceId = g_cfg.voiceDevice;
    s->initialPrompt = "Guild Wars 2";
    s->gainDb = g_cfg.voiceBoost == 1 ? 6.0f : g_cfg.voiceBoost == 2 ? 12.0f : 0.0f;
    s->levelSpeech = g_cfg.voiceBoost == 3;
    s->minSpeechMs = g_cfg.voiceMinMs;
    s->preRollMs = g_cfg.voicePreRollMs;
    return true;
}

// Voice activation: listens; a recording starts by itself when you speak and ends after the pause.
void StartListen() {
    VoiceSettings s;
    std::wstring why;
    if (!BuildSettings(&s, &why)) {
        Write("{\"t\":\"error\"" + Str("msg", why) + "}");
        return;
    }
    s.autoEndMs = g_cfg.voicePauseMs;
    s.thresholdDb = -20.0f - 0.4f * static_cast<float>(std::clamp(g_cfg.voiceSensitivity, 0, 100));  // 50 -> -40 dBFS
    AudioError audio = AudioError::None;
    if (g_voice->Listen(s, &why, &audio)) Write("{\"t\":\"listening\"}");
    else Write("{\"t\":\"error\"" + Str("msg", why) + "}");
}

// A voice chat (Discord, TeamSpeak ...) has the microphone: what you say is meant for it.
std::wstring VoiceChatOpen() {
    std::wstring list = L"," + g_cfg.voiceChatApps + L",";
    for (wchar_t& c : list) c = static_cast<wchar_t>(towlower(c));
    for (std::wstring exe : AppsUsingMicrophone(g_cfg.voiceDevice)) {
        std::wstring low = exe;
        for (wchar_t& c : low) c = static_cast<wchar_t>(towlower(c));
        if (list.find(L"," + low + L",") != std::wstring::npos || list.find(L", " + low + L",") != std::wstring::npos)
            return exe;
    }
    return {};
}

void CheckVoiceChat() {
    if (!g_wantListen || g_voice->Busy()) return;
    if (!g_cfg.voiceChatPause) {
        if (!g_voice->Listening()) StartListen();
        return;
    }
    const std::wstring app = VoiceChatOpen();
    if (!app.empty() && g_pausedFor.empty()) {
        g_pausedFor = app;
        g_voice->Cancel();
        Write("{\"t\":\"paused\"" + Str("app", app) + "}");
    } else if (app.empty() && (!g_pausedFor.empty() || !g_voice->Listening())) {
        g_pausedFor.clear();
        StartListen();
    }
}

void OnCommand(const std::string& cmd) {
    if (cmd == "quit") {
        PostQuitMessage(0);
    } else if (cmd == "reload") {  // the options changed (model, language, threads ...)
        g_cfg.Load(g_cfg.dataDir);
    } else if (cmd == "begin" || cmd == "begin-auto") {  // begin-auto: a pause after speech ends it
        if (g_voice->Busy()) return;
        VoiceSettings s;
        std::wstring why;
        const bool ok = BuildSettings(&s, &why);
        if (cmd == "begin-auto") s.autoEndMs = g_cfg.voicePauseMs;
        if (!ok) {
            Write("{\"t\":\"error\"" + Str("msg", why) + "}");
            return;
        }
        AudioError audio = AudioError::None;
        if (g_voice->Listening()) g_voice->Cancel();  // a press while listening: this recording, then listen again
        if (!g_voice->Begin(s, &g_session, &why, &audio)) Write("{\"t\":\"error\"" + Str("msg", why) + "}");
        else {
            g_cpu.peak = 0;  // measured from this recording on
            Write("{\"t\":\"started\"}");
        }
    } else if (cmd == "listen") {
        g_wantListen = true;
        g_pausedFor.clear();
        if (!g_voice->Listening()) StartListen();
        CheckVoiceChat();
    } else if (cmd == "stop-listen") {
        g_wantListen = false;
        g_pausedFor.clear();
        if (g_voice->Listening()) g_voice->Cancel();
        Write("{\"t\":\"idle\"}");
    } else if (cmd == "end") {
        g_voice->End();
    } else if (cmd == "cancel") {
        g_voice->Cancel();
    }
}

void OnVoiceEvent(VoiceEvent* raw) {
    std::unique_ptr<VoiceEvent> e(raw);
    if (!e || e->session != g_voice->Session()) return;  // an older session
    switch (e->kind) {
        case VoiceEvent::Kind::Partial:
            Write("{\"t\":\"partial\"" + Str("stable", e->live.stable) + Str("unsure", e->live.unsure) + "}");
            break;
        case VoiceEvent::Kind::Final:
            g_cpu.Sample(true);
            Write("{\"t\":\"final\"" + Str("text", e->text) + (e->empty ? ",\"empty\":true" : ",\"empty\":false") +
                  Str("lang", FromUtf8(e->language)) + ",\"rec\":" + std::to_string(e->recordMs) +
                  ",\"rms\":" + std::to_string(e->recognizeMs) + ",\"peak\":" + std::to_string(g_cpu.peak) +
                  (e->maxReached ? ",\"max\":true" : "") + "}");
            g_cpu.peak = 0;
            break;
        case VoiceEvent::Kind::Error:
            Write("{\"t\":\"error\"" + Str("msg", e->message.empty() ? std::wstring(L"voice input failed") : e->message) +
                  "}");
            break;
        case VoiceEvent::Kind::Started:
            g_cpu.peak = 0;
            Write("{\"t\":\"started\"}");
            break;
    }
}

LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_APP_VOICE:
            OnVoiceEvent(reinterpret_cast<VoiceEvent*>(lp));
            return 0;
        case WM_APP_COMMAND: {
            std::unique_ptr<std::string> cmd(reinterpret_cast<std::string*>(lp));
            if (cmd) OnCommand(*cmd);
            return 0;
        }
        case WM_TIMER:
            if (wp == kTimerCores) g_cores.Sample();  // the least busy cores, measured all the time
            if (wp == kTimerVoiceChat) CheckVoiceChat();
            if (wp == kTimerCpu) g_cpu.Sample(g_voice && g_voice->Busy());
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

std::wstring ArgAfter(const std::wstring& cmdLine, const std::wstring& name) {
    const size_t at = cmdLine.find(name);
    if (at == std::wstring::npos) return {};
    size_t i = at + name.size();
    while (i < cmdLine.size() && cmdLine[i] == L' ') ++i;
    if (i < cmdLine.size() && cmdLine[i] == L'"') {
        const size_t end = cmdLine.find(L'"', i + 1);
        return cmdLine.substr(i + 1, end == std::wstring::npos ? std::wstring::npos : end - i - 1);
    }
    const size_t end = cmdLine.find(L' ', i);
    return cmdLine.substr(i, end == std::wstring::npos ? std::wstring::npos : end - i);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmdLine, int) {
    const std::wstring dataDir = ArgAfter(cmdLine ? cmdLine : L"", L"--data");
    if (dataDir.empty()) return 2;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
    g_out = GetStdHandle(STD_OUTPUT_HANDLE);
    g_cfg.Load(dataDir);

    WNDCLASSW wc{};
    wc.lpfnWndProc = Proc;
    wc.hInstance = inst;
    wc.lpszClassName = L"GW2KeyboardVoiceHelper";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, inst, nullptr);
    std::wstring why;
    if (!hwnd || !VoiceSession::Available(&why)) {
        Write("{\"t\":\"error\"" + Str("msg", why.empty() ? std::wstring(L"the voice module is missing") : why) + "}");
        return 1;
    }
    g_voice = std::make_unique<VoiceSession>(hwnd, WM_APP_VOICE);
    SetTimer(hwnd, kTimerCores, 300, nullptr);
    SetTimer(hwnd, kTimerVoiceChat, 2000, nullptr);
    SetTimer(hwnd, kTimerCpu, 100, nullptr);
    g_cores.Sample();

    // Commands from the add-on; standard input closing (the add-on or the game went) ends the helper.
    std::thread([hwnd] {
        HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
        std::string pending;
        char buf[512];
        DWORD n = 0;
        while (ReadFile(in, buf, sizeof(buf), &n, nullptr) && n > 0) {
            pending.append(buf, n);
            size_t nl;
            while ((nl = pending.find('\n')) != std::string::npos) {
                std::string cmd = pending.substr(0, nl);
                pending.erase(0, nl + 1);
                while (!cmd.empty() && (cmd.back() == '\r' || cmd.back() == ' ')) cmd.pop_back();
                auto* copy = new std::string(cmd);
                if (!PostMessageW(hwnd, WM_APP_COMMAND, 0, reinterpret_cast<LPARAM>(copy))) delete copy;
            }
        }
        PostMessageW(hwnd, WM_APP_COMMAND, 0, reinterpret_cast<LPARAM>(new std::string("quit")));
    }).detach();

    Write("{\"t\":\"ready\"}");
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    g_voice.reset();  // joins its threads, closes the microphone, frees the model
    CoUninitialize();
    ExitProcess(0);  // the stdin thread may still sit in ReadFile
}
