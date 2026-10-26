// kb_voice.cpp — the voice helper, push to talk, voice models, the voice module.

#include "kb_shared.hpp"

namespace kb {

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
        SetStatus(g_micLocked ? Tr(L"Hearing you …") : Tr(L"Hearing you … release the key when you are done"), 0);
    } else if (t == "partial") {
        g_voiceLive = W(v.GetString("stable")) + W(v.GetString("unsure"));
        g_micLockedStart = 0;
    } else if (t == "final") {
        g_recording = false;
        g_voiceLive.clear();
        const std::wstring text = Trim(W(v.GetString("text")));
        if (v.GetBool("empty") || text.empty()) {
            g_voiceStoppedManually = false;
            if (g_micLocked) SetStatus(Tr(L"Nothing recognized – right-click to listen for 3 s"), 5000);
            else SetStatus(Tr(L"Nothing recognized – hold the key while you speak"), 5000);
            return;
        }
        NoteVoiceRun(v);
        g_dictLang = g_cfg.voiceIntoEnglish ? std::wstring(L"EN") : ToUpperAscii(W(v.GetString("lang")));
        const std::wstring fixed = g_voiceFixes.Apply(text, g_dictLang);
        g_lastDictated = fixed;
        const size_t len = std::strlen(g_input);
        if (len == 0) g_inputSpoken = true;  // it began with what you said: "Translate what I say" decides
        g_edit.active = true;
        g_edit.startByte = len;
        g_edit.lenBytes = 0;
        g_edit.insert = (len && g_input[len - 1] != ' ' && g_input[len - 1] != '\n' ? " " : "") + U8(fixed);
        g_focusInput = true;
        // Hands free (Punkt 1 & 8): Ende einer Spracheingabe – Sprechpause, PTT-Loslassen oder Mausklick-Stopp.
        // Nur wenn Esc gedrückt wurde (Ebene 2), bricht Auto-Enter ab.
        const bool cancelledByEsc = g_voiceStoppedManually;
        g_voiceStoppedManually = false;
        if (!cancelledByEsc) {
            RequestAutoEnter(AutoEnterTrigger::Voice);
        }
        g_userRecording = false;
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
        g_userRecording = false;
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

// Whisper translates itself only when what you say goes into English (g_mu held); true: changed – the helper reads it
// again before the next recording.
bool KeepWhisperEnglish() {
    const bool en = g_cfg.voiceTranslate && PrimaryLang(VoiceLang()) == L"EN";
    if (en == g_cfg.voiceIntoEnglish) return false;
    g_cfg.voiceIntoEnglish = en;
    g_cfg.SaveBool(L"Voice", L"IntoEnglish", en);
    return true;
}

// The helper starts on first use; `cmd` runs as soon as it is ready.
void VoiceCommand(const char* cmd) {
    bool ready, english;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        english = KeepWhisperEnglish();
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
        if (english) {  // rare: "Send" went between English and another language
            g_worker.Post([cmd = std::string(cmd)] {
                Config::FlushWrites();
                g_voice.Send("reload");
                g_voice.Send(cmd.c_str());
            });
            return;
        }
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
    g_worker.Post([dataDir] {
        Config::FlushWrites();  // the helper reads the ini when it starts (normally nothing waits)
        if (!g_voice.Start(HelperExe(), dataDir)) {
            std::lock_guard<std::mutex> lock(g_mu);
            g_voicePending.clear();
            SetStatus(VoiceModulePresent(KeyboardDir()) ? Tr(L"The voice module could not be started")
                                                        : Tr(L"The voice module is not loaded yet – Options → Voice."),
                      10000);
        }
    });
}

// Push to talk: pressed starts, released ends (the recognized text then waits for your Enter). Started while the
// helper still loads, the recording begins when it is ready and a pause ends it.
void TalkDown() {
    if (g_voiceMuted.load()) return;
    bool rec, toggle, ready;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        rec = g_recording;
        toggle = g_cfg.voiceToggle;
        ready = g_voiceReady;
    }
    if (rec) return;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        g_userRecording = true;
        g_micLocked = false;
        g_micLockedStart = 0;
    }
    VoiceCommand(toggle || !ready ? "begin-auto" : "begin");
}
void TalkUp() {
    {
        std::lock_guard<std::mutex> lock(g_mu);
        g_micLocked = false;
        g_micLockedStart = 0;
    }
    g_voice.Send("end");
}
void TalkCancel() {
    {
        std::lock_guard<std::mutex> lock(g_mu);
        g_recording = false;
        g_userRecording = false;
        g_micLocked = false;
        g_micLockedStart = 0;
        g_voiceLive.clear();
        g_autoSendPending = false;
    }
    g_voice.Send("cancel");
}
void TalkLocked() {  // right-click on the microphone: on until a pause is heard
    if (g_voiceMuted.load()) return;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        if (g_recording) return;
        g_userRecording = true;
        g_micLocked = true;
        g_micLockedStart = GetTickCount64();
        SetStatus(Tr(L"Hearing you …"), 0);
    }
    VoiceCommand("begin-auto");
}
void StartListening() {  // voice activation: a recording starts when you speak, a pause ends it
    if (g_voiceMuted.load()) return;
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

void SetVoiceMuted(bool mute) {
    g_voiceMuted.store(mute);
    if (mute) {
        TalkUp();
        StopListening();
        std::lock_guard<std::mutex> lock(g_mu);
        g_recording = false;
        g_userRecording = false;
    } else {
        bool va = false;
        {
            std::lock_guard<std::mutex> lock(g_mu);
            va = g_cfg.voiceEnabled && g_cfg.voiceActivation && !g_listenUserOff;
        }
        if (va) StartListening();
    }
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
        bool ok = false;
        Shielded([&] {
            ok = DownloadVoiceModel(
                dir, m,
                [](uint64_t done, uint64_t total) {
                    if (total) g_downloadPct = static_cast<int>(done * 100 / total);
                    return true;
                },
                &g_downloadCancel, &error);
        });
        g_downloadPct = -1;
        std::lock_guard<std::mutex> lock(g_mu);
        SetStatus(ok ? Tr(L"Voice model installed") : error, 10000);
        if (ok) VoiceReload();
    });
}
std::atomic<bool> g_voiceModuleInstalling{false};
std::atomic<int> g_voiceModulePct{-1};
std::thread g_voiceModuleThread;
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
        ExtrasInstallResult r;
        if (!Shielded([&] {
                r = InstallVoiceModule(dir, W(GCT_VERSION_STR), [](uint64_t done, uint64_t total) {
                    if (total) g_voiceModulePct = static_cast<int>(done * 100 / total);
                    return !g_disabled.load();
                });
            }))
            r.code = ExtrasInstallCode::DownloadFailed;
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
    g_worker.Post([dataDir] {
        Config::FlushWrites();
        if (!g_voice.Running() && VoiceModulePresent(KeyboardDir())) g_voice.Start(HelperExe(), dataDir);
    });
}
// Lean when not in use: the voice helper holds its model in memory (hundreds of MB for the larger ones). Nothing
// recorded, nothing listening for [Voice] UnloadMinutes (default 10): it ends and gives that memory back; the next
// press of the microphone or the key starts it again. Voice activation keeps it (it listens). Game thread, once a second.
void VoiceIdleUnload() {
    static ULONGLONG s_next = 0, s_lastUse = 0;
    const ULONGLONG now = GetTickCount64();
    if (now < s_next) return;
    s_next = now + 1000;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        const bool idle = g_voice.Running() && !g_recording && !g_listening && !g_listenWanted && g_voicePending.empty();
        if (!idle || s_lastUse == 0) {
            s_lastUse = now;  // in use (or not running): the idle time counts from here
            return;
        }
        if (now - s_lastUse < static_cast<ULONGLONG>(std::max(1, g_cfg.voiceUnloadMin)) * 60000) return;
    }
    s_lastUse = 0;
    g_voice.Stop();  // the model's memory goes back; the next press starts the helper again
    std::lock_guard<std::mutex> lock(g_mu);
    g_voicePending.clear();
    g_recording = g_listening = g_userRecording = false;
}

// Options → Voice: the voice module goes (the helper ends first; voice input is switched off) …
void RemoveVoiceModuleNow() {  // game thread
    UnloadVoiceModule();
    {
        std::lock_guard<std::mutex> lock(g_mu);
        g_cfg.voiceEnabled = false;
        g_cfg.SaveBool(L"Voice", L"Enabled", false);
    }
    g_worker.Post([] {
        bool ok = false;
        for (int i = 0; i < 10 && !ok; ++i) {  // the helper may still be closing its files
            ok = RemoveVoiceModule(KeyboardDir());
            if (!ok) Sleep(300);
        }
        std::lock_guard<std::mutex> lock(g_mu);
        g_modulesNext = 0;
        SetStatus(ok ? Tr(L"Voice module removed") : Tr(L"The voice module could not be removed completely – a file is in use"), 6000);
    });
}

// … or every downloaded voice model (the helper ends first, it may hold one).
void RemoveVoiceModelsNow() {  // game thread
    UnloadVoiceModule();
    std::wstring dir;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        dir = g_cfg.VoiceModelDir();
    }
    g_worker.Post([dir] {
        bool ok = false;
        for (int i = 0; i < 10 && !ok; ++i) {
            ok = RemoveFolder(dir);
            if (!ok) Sleep(300);
        }
        std::lock_guard<std::mutex> lock(g_mu);
        g_modulesNext = 0;
        SetStatus(ok ? Tr(L"Voice models removed") : Tr(L"The voice models could not be removed completely – a file is in use"), 6000);
    });
}

void UnloadVoiceModule() {
    StopListening();
    g_voice.Stop();  // the helper ends: the model and its memory are freed
    std::lock_guard<std::mutex> lock(g_mu);
    g_voicePending.clear();
    g_recording = g_listening = g_userRecording = false;
    g_listenUserOff = true;  // voice activation does not start it again by itself
}

void InstallVoiceQuickSetup() {
    {
        std::lock_guard<std::mutex> lock(g_mu);
        g_cfg.voiceEnabled = true;
        g_cfg.SaveBool(L"Voice", L"Enabled", true);
    }
    InstallVoiceModuleNow();
    Config c;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        c = g_cfg;
    }
    const VoiceCatalog catalog = LoadVoiceCatalog(c.dataDir);
    const VoiceModelInfo* chosen = catalog.Find(U8(c.voiceModel));
    if (!chosen) chosen = catalog.Recommended();
    if (chosen && !VoiceModelInstalled(c.VoiceModelDir(), *chosen)) StartModelDownload(*chosen);
}

}  // namespace kb
