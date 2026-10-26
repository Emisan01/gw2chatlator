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

#include "kb_shared.hpp"
#include "win/gw2_sender.hpp"

namespace kb {


UINT OnWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (g_disabled) return msg;
    g_gameHwnd = hwnd;

    // Eigene Tasten erkennen: SendToGw2Chat markiert seine SendInput-Tasten mit kGw2SenderExtraInfo
    if (GetMessageExtraInfo() == gct::kGw2SenderExtraInfo) return msg;

    // Tastenpuffer (Punkt 4b): vom Start des Sendens bis Fokus zurück im Eingabefeld alle Nutzertasten puffern
    if (g_bufferingKeys.load()) {
        if (!g_sendInFlight.load() && !g_sending && GetTickCount64() - g_bufferingStartTime.load() >= 3000) {
            g_bufferingKeys = false;
            return msg;
        }
        if (msg == WM_CHAR) {
            std::lock_guard<std::mutex> lock(g_keyBufferMu);
            g_keyBuffer.push_back({msg, wp, lp});
            return 0;
        }
        const bool isDown = (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN);
        if (isDown) {
            if (wp == VK_BACK || wp == VK_DELETE || wp == VK_RETURN || wp == VK_LEFT || wp == VK_RIGHT || wp == VK_HOME || wp == VK_END) {
                std::lock_guard<std::mutex> lock(g_keyBufferMu);
                g_keyBuffer.push_back({msg, wp, lp});
            }
            return 0;  // Block game movement/skills while sending
        }
        // Key-ups (WM_KEYUP/WM_SYSKEYUP) are NOT swallowed: letting go of held movement keys (e.g. W) reaches GW2
        return msg;
    }

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
        if (down && vk == VK_ESCAPE && mods == 0) {
            bool capturable = false;
            {
                std::lock_guard<std::mutex> lock(g_mu);
                capturable = g_escCapturable;
                if (capturable) {
                    g_escRequested = true;
                }
            }
            if (capturable) return 0;
            // Ebene 5: ohne offene Ebene geht Esc an GW2 durch
            return msg;
        }
        // Panic Switch: Ctrl + Alt + T pauses/resumes chat translation immediately
        if (down && (mods & (kModCtrl | kModAlt)) == (kModCtrl | kModAlt) && (vk == 'T' || vk == 't')) {
            g_orchestrator.TogglePaused();
            std::lock_guard<std::mutex> lock(g_mu);
            SetStatus(g_orchestrator.IsPaused() ? Tr(L"Chat translation paused (Ctrl+Alt+T)")
                                                : Tr(L"Chat translation resumed (Ctrl+Alt+T)"), 4000);
            if (!g_orchestrator.IsPaused()) TranslateRecentLines();
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

void OnToggle(const char*, bool release) {
    if (!release) g_show = !g_show;
}
void OnFocusKey(const char*, bool release) {
    if (release) return;
    std::lock_guard<std::mutex> lock(g_mu);
    g_show = true;
    g_focusInput = true;
}

// The first start: what GW2 Keyboard needs comes by itself – Events: Chat (Nexus loads it), the voice module and,
// when no voice model is there yet, the small base model. Then it says what is ready and where translation is switched on.
void FirstStart() {  // nothing is downloaded by itself: the empty chat shows the modules, each loaded on your click
    std::lock_guard<std::mutex> lock(g_mu);
    g_nx.setupDone = true;
    SaveNexusOptions();
}

// Every callback runs behind these two: a C++ exception or an access violation ends our add-on, never the game.
void GuardedRender(void (*fn)()) {
    if (g_disabled) return;
    try {
        fn();
    } catch (const std::exception& e) {
        g_disabled = true;
        Log(LOGL_CRITICAL, ("an error – switched off: " + std::string(e.what())).c_str());
    } catch (...) {
        g_disabled = true;
        Log(LOGL_CRITICAL, "an error – switched off for this session");
    }
}
bool ShieldedCall(void (*fn)(void*), void* arg) {
    __try {
        fn(arg);
        return true;
    } __except (GetExceptionCode() == 0xE06D7363 ? EXCEPTION_CONTINUE_SEARCH : EXCEPTION_EXECUTE_HANDLER) {  // C++: on
        g_disabled = true;
        return false;
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

uint32_t GetNexusBuildTimestamp() {
    HMODULE h = GetModuleHandleA("d3d11.dll");
    if (!h) return 0;
    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(h);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const char*>(h) + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
    return nt->FileHeader.TimeDateStamp;
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
    if (!api || !api->ImguiContext || !api->GUI_Register || !IsCodePointer(reinterpret_cast<const void*>(api->GUI_Register))) {
        g_disabled = true;
        return;
    }
    g_api = api;
    g_disabled = false;
    g_chatEventsLive = false;
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(api->ImguiContext));
    if (api->ImguiMalloc && api->ImguiFree &&
        IsCodePointer(reinterpret_cast<const void*>(api->ImguiMalloc)) &&
        IsCodePointer(reinterpret_cast<const void*>(api->ImguiFree))) {
        ImGui::SetAllocatorFunctions(reinterpret_cast<void* (*)(size_t, void*)>(api->ImguiMalloc),
                                     reinterpret_cast<void (*)(void*, void*)>(api->ImguiFree));
    }
    {
        std::lock_guard<std::mutex> lock(g_mu);
        g_cfg.Load(OwnDataDir());
        // Nexus airlock & patch sensor: detect upstream loader updates
        const uint32_t curNexusStamp = GetNexusBuildTimestamp();
        if (curNexusStamp != 0) {
            wchar_t lastSeenStr[16] = {};
            GetPrivateProfileStringW(L"Nexus", L"LastSeenTimestamp", L"", lastSeenStr, 16, g_cfg.iniPath.c_str());
            const uint32_t lastStamp = static_cast<uint32_t>(_wtoi(lastSeenStr));
            if (lastStamp != 0 && lastStamp != curNexusStamp) {
                Log(LOGL_INFO, "Nexus update detected (new d3d11.dll build)");
            }
            g_cfg.SaveValue(L"Nexus", L"LastSeenTimestamp", std::to_wstring(curNexusStamp));
        }
        if (g_cfg.writeLangs.empty()) g_cfg.writeLangs = {L"EN-GB"};
        SetUiLang(g_cfg.uiLang);
        g_translator = MakeTranslator(g_cfg);
        g_sendIdx = g_cfg.translateWriting ? 0 : static_cast<int>(g_cfg.writeLangs.size());
        LoadLayout();
        LoadCustomTabs();
        ChatSourceLoad();  // reading on / off, the version of Events: Chat the last session had
        {  // voice input is something you switch on (it needs the voice module): off unless you chose it or the module is there
            wchar_t on[8] = {};
            GetPrivateProfileStringW(L"Voice", L"Enabled", L"", on, 8, g_cfg.iniPath.c_str());
            if (!on[0]) g_cfg.voiceEnabled = VoiceModulePresent(KeyboardDir());
        }
        // 1.9.10 – 1.9.13 kept the views as flags over the parts; now they are presets of the parts themselves.
        const bool oldMini = GetPrivateProfileIntW(L"NexusWindow", L"Mini", 0, g_cfg.iniPath.c_str()) != 0;
        const bool oldCompact = GetPrivateProfileIntW(L"NexusWindow", L"Compact", 0, g_cfg.iniPath.c_str()) != 0;
        if (oldMini || oldCompact) {
            SetProfile(oldMini ? 1 : 0, true);
            g_cfg.SaveValue(L"NexusWindow", L"Mini", L"0");
            g_cfg.SaveValue(L"NexusWindow", L"Compact", L"0");
        }
        std::string words;
        if (ReadFileBytes(g_cfg.dataDir + L"\\my-words.txt", words)) g_myWords.Parse(FromUtf8(words));
        std::string blocks;
        if (ReadFileBytes(g_cfg.dataDir + L"\\snippets.txt", blocks)) g_snippets.Parse(FromUtf8(blocks));
    }
    RequestTextFont(g_cfg.fontPt, g_cfg.fontFace);
    RequestUiFont(FollowUiPx());
    Config::DeferWrites(true);  // the ini is written on the worker (KickSave), never on the game thread
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
    if (api->GUI_Register && IsCodePointer(reinterpret_cast<const void*>(api->GUI_Register))) {
        api->GUI_Register(RT_Render, SafeRender);
        api->GUI_Register(RT_OptionsRender, SafeOptions);
    }
    if (api->InputBinds_RegisterWithString && IsCodePointer(reinterpret_cast<const void*>(api->InputBinds_RegisterWithString))) {
        api->InputBinds_RegisterWithString(kToggleBind, OnToggle, "CTRL+ALT+K");
        api->InputBinds_RegisterWithString(kFocusBind, OnFocusKey, "(null)");
        api->InputBinds_RegisterWithString(kTalkBind, OnTalkKey, "(null)");
    }
    ApplyPushToTalkKey(g_cfg.voiceGlobalKey);
    if (api->WndProc_Register && IsCodePointer(reinterpret_cast<const void*>(api->WndProc_Register))) {
        api->WndProc_Register(OnWndProc);
    }
    if (api->Events_Subscribe && IsCodePointer(reinterpret_cast<const void*>(api->Events_Subscribe))) {
        api->Events_Subscribe(bridge::kChatEventsEvent, OnChatEvents);
    }
    Log(LOGL_INFO, "loaded");
}

void AddonUnload() {
    if (g_api) {
        if (g_api->Events_Unsubscribe && IsCodePointer(reinterpret_cast<const void*>(g_api->Events_Unsubscribe))) {
            g_api->Events_Unsubscribe(bridge::kChatEventsEvent, OnChatEvents);
        }
        if (g_api->WndProc_Deregister && IsCodePointer(reinterpret_cast<const void*>(g_api->WndProc_Deregister))) {
            g_api->WndProc_Deregister(OnWndProc);
        }
        if (g_api->InputBinds_Deregister && IsCodePointer(reinterpret_cast<const void*>(g_api->InputBinds_Deregister))) {
            g_api->InputBinds_Deregister(kToggleBind);
            g_api->InputBinds_Deregister(kFocusBind);
            g_api->InputBinds_Deregister(kTalkBind);
        }
        if (g_api->GUI_Deregister && IsCodePointer(reinterpret_cast<const void*>(g_api->GUI_Deregister))) {
            g_api->GUI_Deregister(SafeRender);
            g_api->GUI_Deregister(SafeOptions);
        }
    }
    g_downloadCancel = true;
    if (g_download.joinable()) g_download.join();
    if (g_chatEventsThread.joinable()) g_chatEventsThread.join();
    if (g_decoderThread.joinable()) g_decoderThread.join();
    if (g_voiceModuleThread.joinable()) g_voiceModuleThread.join();
    if (g_localMtThread.joinable()) g_localMtThread.join();
    g_links.Stop();
    ReleaseTextFont();  // no call into a gone DLL
    ReleaseUiFont();
    g_voice.Stop();  // the helper ends (it would also end by itself when its input closes)
    g_worker.Stop(FreeWorkerState);  // saved and freed on the worker (it owns them), before the DLL goes
    Config::DeferWrites(false);      // what is still waiting goes into the ini now
    Config::FlushWrites();           // synchronously flush waiting writes to target INI on unload
    std::lock_guard<std::mutex> lock(g_mu);
    g_lines.clear();
    g_translator.reset();
    g_api = nullptr;
}

}  // namespace kb

using namespace kb;

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) g_self = module;
    return TRUE;
}

// The test exports work only outside the game (nexus_kb_selftest, the smoke test): inside GW2 no other add-on can reach
// our input or our sending through them.
static bool TestHost() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    const wchar_t* name = wcsrchr(path, L'\\');
    name = name ? name + 1 : path;
    return _wcsicmp(name, L"Gw2-64.exe") != 0 && _wcsicmp(name, L"Gw2.exe") != 0;
}

extern "C" __declspec(dllexport) void* Gw2KbTestHooks() {  // nexus_kb_selftest only
    if (!TestHost()) return nullptr;
    g_test.input = g_input;
    g_test.focus = &g_focusInput;
    return &g_test;
}

extern "C" __declspec(dllexport) void Gw2KbTestNewTab(uint32_t channels, bool settings) {  // nexus_kb_selftest only
    if (!TestHost()) return;
    std::lock_guard<std::mutex> lock(g_mu);
    const int at = NewCustomTab();
    if (at < 0) return;
    g_tabs[at].channels = channels;
    if (settings) g_tabSettings = at;
}

// nexus_kb_selftest only: a fault in a worker job while it holds g_mu (step 1); step 0: the add-on is off (1) and g_mu
// is free again (2) – the fault did not take the process down, nothing stays locked.
extern "C" __declspec(dllexport) int Gw2KbTestFault(int step) {
    if (!TestHost()) return -1;
    if (step == 1) {
        g_worker.Post([] {
            std::lock_guard<std::mutex> lock(g_mu);
            volatile int* nowhere = nullptr;
            *nowhere = 1;  // an access violation, on purpose
        });
        return 0;
    }
    const bool free = g_mu.try_lock();
    if (free) g_mu.unlock();
    return (g_disabled ? 1 : 0) + (free ? 2 : 0);
}

// nexus_kb_selftest only: the window's shape – 0 minimise (as the menu does), 1 open again.
extern "C" __declspec(dllexport) void Gw2KbTestShape(int step) {
    if (!TestHost()) return;
    std::lock_guard<std::mutex> lock(g_mu);
    if (step == 0) g_minimizeRequest = true;
    else g_expandRequest = true;
}

// nexus_kb_selftest, "Translate what I say": what a message does when spoken with voice translation on/off,
// or typed – the output language in the chat window defines the translation (user, 2026-10-23).
extern "C" __declspec(dllexport) int Gw2KbTestVoiceRoute(int spoken, int voiceTranslate, char* lang) {
    if (!TestHost()) return -1;
    std::lock_guard<std::mutex> lock(g_mu);
    const bool wasSpoken = g_inputSpoken, wasOn = g_cfg.voiceEnabled, wasTr = g_cfg.voiceTranslate;
    const int wasIdx = g_sendIdx;
    const std::wstring wasOverride = g_sendOverride, wasDict = g_dictLang;
    g_inputSpoken = spoken != 0;
    g_cfg.voiceEnabled = true;
    g_cfg.voiceTranslate = voiceTranslate != 0;
    g_sendOverride.clear();
    g_sendIdx = 0;  // output language in the chat window (e.g. EN-GB or DE)
    g_dictLang = PrimaryLang(SendLang()) == L"EN" ? L"DE" : L"EN";  // spoken in a different language -> translates
    const int translated = Translating() ? 1 : 0;
    if (lang) strncpy_s(lang, 16, U8(SendLang()).c_str(), _TRUNCATE);
    g_inputSpoken = wasSpoken;
    g_cfg.voiceEnabled = wasOn;
    g_cfg.voiceTranslate = wasTr;
    g_sendIdx = wasIdx;
    g_sendOverride = wasOverride;
    g_dictLang = wasDict;
    return translated;
}

// nexus_kb_selftest: a voice result as the helper sends it (`text`, or nullptr: none) – returns whether the input counts
// as spoken now.
extern "C" __declspec(dllexport) int Gw2KbTestVoiceFinal(const char* text) {
    if (!TestHost()) return -1;
    if (text) VoiceHelper::OnLine(std::string("{\"t\":\"final\",\"text\":\"") + text + "\",\"lang\":\"en\"}");
    std::lock_guard<std::mutex> lock(g_mu);
    return g_inputSpoken ? 1 : 0;
}

extern "C" __declspec(dllexport) void Gw2KbTestMini(int on) {  // nexus_kb_selftest: the mini mode on / off
    if (!TestHost()) return;
    std::lock_guard<std::mutex> lock(g_mu);
    SetProfile(1, on != 0);
}

extern "C" __declspec(dllexport) int Gw2KbTestAutoMinimize(int seconds) {  // nexus_kb_selftest: -1 ask, 0 off; 1 = small
    if (!TestHost()) return 0;
    std::lock_guard<std::mutex> lock(g_mu);
    if (seconds >= 0) {
        g_layout.autoMinimize = seconds > 0;
        g_layout.minimizeAfter = seconds;
        g_lastUse = 0;
    }
    return g_collapsed ? 1 : 0;
}

extern "C" __declspec(dllexport) void Gw2KbTestWhisper(const char* name) {  // nexus_kb_selftest only: a click on a name
    if (!TestHost()) return;
    std::lock_guard<std::mutex> lock(g_mu);
    OpenWhisperTab(W(name));
}

// nexus_kb_selftest only – the chat source guard: 0 ask, 1 scan the modules now, 2 resume, 4 reading off, 5 reading on,
// 6 the protocol into `text` (4096 bytes), 7 the chat tab in front, 8 Options > Technical open, 9 the options closed.
extern "C" __declspec(dllexport) int Gw2KbTestChatSource(int cmd, ChatSourceTest* state, char* text) {
    if (!TestHost()) return -1;
    switch (cmd) {
        case 1: ChatEventsPresent(true); break;
        case 2: ChatSourceResume(); break;
        case 4: ChatSourceSetReading(false, false); break;
        case 5: ChatSourceSetReading(true, false); break;
        case 6: if (text) strncpy_s(text, 4096, ChatSourceProtocolText().c_str(), _TRUNCATE); break;
        case 7: {  // the chat tab in front, the window open (lines drawn: chat codes resolved)
            std::lock_guard<std::mutex> lock(g_mu);
            g_selectTab = -1;
            if (g_collapsed) g_expandRequest = true;
            break;
        }
        case 10: {  // the picture: the text blocks open (invented ones, not saved)
            std::lock_guard<std::mutex> lock(g_mu);
            g_snippets.Add(L"Meet at the waypoint in five, bring food and repair kits.");
            g_snippets.Add(L"LFG fractals T4, need 2 – whisper me.");
            g_openSnippets = true;
            break;
        }
        case 8:
        case 9: {
            std::lock_guard<std::mutex> lock(g_mu);
            g_showOptions = cmd == 8;
            if (cmd == 8) g_optionsTab = text && *text ? std::atoi(text) : 6;  // a tab by its number, else Technical
            break;
        }
        default: break;
    }
    if (state) ChatSourceTestState(state);
    return 0;
}

extern "C" __declspec(dllexport) int Gw2KbTestCosts(FrameCosts* out, int reset) {  // nexus_kb_smoketest: the frame by part
    if (!TestHost()) return -1;
    if (reset) CostReset();
    if (out) *out = CostsNow();
    return 0;
}

extern "C" __declspec(dllexport) int Gw2KbTestRemoveAll(int start) {  // nexus_kb_selftest only: 1 start, 0 ask
    if (!TestHost()) return -1;
    if (start) RemoveEverythingNow(false, false);
    return g_wipeDone;
}

extern "C" __declspec(dllexport) AddonDefinition_t* GetAddonDef() {
    g_def.Signature = 0xB8B4BDBC;  // Raidcore Nexus Addon Manager ID 3098852796
    g_def.APIVersion = NEXUS_API_VERSION;
    g_def.Name = kName;
    g_def.Version.Major = GCT_VER_MAJOR;
    g_def.Version.Minor = GCT_VER_MINOR;
    g_def.Version.Build = GCT_VER_PATCH;
    g_def.Version.Revision = 0;
    g_def.Author = "Emi";
    g_def.Description = "Smart typing assistance, real-time chat translation, voice-to-text input, and whisper tabs "
                        "built natively into Guild Wars 2. The chat comes from \"Events: Chat\" (Nexus library).";
    g_def.Load = AddonLoad;
    g_def.Unload = AddonUnload;
    g_def.Flags = AF_None;
    g_def.Provider = UP_GitHub;
    g_def.UpdateLink = "https://github.com/Emisan01/gw2chatlator";
    return &g_def;
}
