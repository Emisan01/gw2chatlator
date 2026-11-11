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
#include "win/http.hpp"

namespace kb {

void SwitchOffAfterFault();  // below: off for the session, one Nexus alert says so

UINT OnWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (g_disabled) return msg;
    g_gameHwnd = hwnd;

    // Our own keys (gw2_sender marks its SendInput keys with kGw2SenderExtraInfo) always reach GW2. The mark belongs to
    // the last input message: a key down carries it, but the characters TranslateMessage makes from it (WM_CHAR: "/m",
    // the name's letters, Ctrl+V) come as posted messages – a mouse move in between (an input message without the
    // mark) made them look like your typing, the key buffer swallowed them and later put "/m" into our input; GW2 got
    // only the keys, never the command. That is why one moved mouse changed the result (live 2026-11-02). Now a
    // character belongs to whoever pressed the last key down.
    static bool s_charsOurs = false;
    const bool ours = GetMessageExtraInfo() == gct::kGw2SenderExtraInfo;
    if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) s_charsOurs = ours;
    if (ours || (s_charsOurs && (msg == WM_CHAR || msg == WM_SYSCHAR || msg == WM_DEADCHAR || msg == WM_UNICHAR)))
        return msg;
    if (WaypointSwallowsMouse(msg)) return 0;
    if (msg == WM_LBUTTONDOWN && (wp & MK_SHIFT) && ImGui::GetCurrentContext() && !ImGui::GetIO().WantCaptureMouse) g_shiftClickAt = GetTickCount64();
    if (msg == WM_LBUTTONDOWN) WaypointSawClick(lp);

    // The key buffer: from the start of a send until the input has the focus back, your keys are kept for it
    if (g_bufferingKeys.load()) {
        // a send that stopped moving frees the keyboard; the keys kept so far still go into our input (game thread)
        if (GetTickCount64() - g_bufferingStartTime.load() >= static_cast<ULONGLONG>(BufferSafetyMs())) {
            g_bufferingKeys = false;
            return msg;
        }
        if (msg == WM_CHAR) {
            std::lock_guard<std::mutex> lock(g_keyBufferMu);
            if (g_keyBuffer.size() < 2048) g_keyBuffer.push_back({msg, wp, lp});  // more than the input holds
            return 0;
        }
        const bool isDown = (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN);
        if (isDown) {
            if (wp == VK_BACK || wp == VK_RETURN) {
                std::lock_guard<std::mutex> lock(g_keyBufferMu);
                if (g_keyBuffer.size() < 2048) g_keyBuffer.push_back({msg, wp, lp});  // more than the input holds
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
                const bool frameAlive = (GetTickCount64() - g_lastRenderFrameTime.load() < 250);
                capturable = g_escCapturable && frameAlive;
                if (capturable) {
                    g_escRequested = true;
                }
            }
            if (capturable) return 0;
            // Ebene 5: ohne offene Ebene oder bei ausgeblendeter Nexus-UI geht Esc an GW2 durch
            return msg;
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
        SwitchOffAfterFault();
        return msg;
    }
}

void OnToggle(const char*, bool release) {
    if (release) return;
    g_show = !g_show;
    TechLog('O', g_show ? L"window: shown" : L"window: hidden");
    if (g_show && g_closedWithX) {
        g_closedWithX = false;
        ChatSourceSetReading(true, false);
    }
}
// The chat's translation on or off, from your own key (it was Ctrl+Alt+T, fixed, until 2.10.2).
static void ToggleChatTranslationNow() {
    std::lock_guard<std::mutex> lock(g_mu);
    SetChatTranslation(!g_nx.translateChat);
    SetStatus(g_nx.translateChat ? Tr(L"Chat translation on") : Tr(L"Chat translation off"), 4000);
}
void OnTranslateKey(const char*, bool release) {
    if (release) return;
    ToggleChatTranslationNow();
}
void OnFocusKey(const char*, bool release) {
    if (release) return;
    std::lock_guard<std::mutex> lock(g_mu);
    g_show = true;
    g_focusInput = true;
    if (g_closedWithX) {
        g_closedWithX = false;
        ChatSourceSetReading(true, true);
    }
}

// The first start only notes that setup was shown. Nothing is downloaded by itself: the empty chat and Options → Start
// show the modules (Events: Chat, the voice module, a voice model, the offline translator), each loaded on your click.
void FirstStart() {
    std::lock_guard<std::mutex> lock(g_mu);
    g_nx.setupDone = true;
    SaveNexusOptions();
}

// Switched off after an error: once a Nexus alert says so – otherwise the add-on would just vanish (only nexus.log
// knew why). The text is made at load (PrepareFaultAlert), so the call after a fault allocates nothing, and it runs
// behind its own guard: a broken state may not take the game down with the message.
char g_faultAlert[512] = {};
std::atomic<bool> g_faultAlerted{false};
void PrepareFaultAlert() {
    const std::string text = UI(Tr(L"GW2Chatlator switched itself off after an error – the game goes on. Details are in "
                                   L"nexus.log; restart GW2 to use it again."));
    CopyUtf8(g_faultAlert, text);
}
void AlertFaultOnce() {  // SEH only, no C++ objects
    __try {
        if (g_faultAlerted.exchange(true) || !g_api || !g_faultAlert[0]) return;
        if (g_api->GUI_SendAlert && IsCodePointer(reinterpret_cast<const void*>(g_api->GUI_SendAlert)))
            g_api->GUI_SendAlert(g_faultAlert);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}
void SwitchOffAfterFault() {
    g_disabled = true;
    AlertFaultOnce();
}

// Every callback runs behind these two: a C++ exception or an access violation ends our add-on, never the game.
void GuardedRender(void (*fn)()) {
    if (g_disabled) return;
    try {
        fn();
    } catch (const std::exception& e) {
        Log(LOGL_CRITICAL, ("an error – switched off: " + std::string(e.what())).c_str());
        SwitchOffAfterFault();
    } catch (...) {
        Log(LOGL_CRITICAL, "an error – switched off for this session");
        SwitchOffAfterFault();
    }
}
bool ShieldedCall(void (*fn)(void*), void* arg) {
    __try {
        fn(arg);
        return true;
    } __except (GetExceptionCode() == 0xE06D7363 ? EXCEPTION_CONTINUE_SEARCH : EXCEPTION_EXECUTE_HANDLER) {  // C++: on
        SwitchOffAfterFault();
        return false;
    }
}
// Nexus calls these from its own code – the keys, its WndProc, the events – not behind SafeRender's net: the same rule
// for them. With /EHa catch(...) takes a C++ exception and a fault alike, and unwinding gives every lock back; our
// add-on is off for the session, the game goes on (AGENTS §2.4).
template <class F>
void CallbackNet(F&& f) {
    try {
        f();
    } catch (...) {
        Log(LOGL_CRITICAL, "an error in a key, input or event callback – switched off for this session");
        SwitchOffAfterFault();
    }
}
template <void (*F)(const char*, bool)>
void SafeKey(const char* id, bool release) {
    if (!g_disabled) CallbackNet([&] { F(id, release); });
}
template <void (*F)(void*)>
void SafeEvent(void* payload) {
    if (!g_disabled) CallbackNet([&] { F(payload); });
}
UINT SafeWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {  // a fault: the message goes on to the game as it came
    UINT out = msg;
    if (!g_disabled) CallbackNet([&] { out = OnWndProc(hwnd, msg, wp, lp); });
    return out;
}

void SafeRender() {
    __try {
        GuardedRender(RenderWindow);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        SwitchOffAfterFault();
    }
}
void SafeOptions() {
    __try {
        GuardedRender(RenderNexusOptions);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        SwitchOffAfterFault();
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
    g_unloading = false;
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
        PrepareFaultAlert();
        g_translator = MakeTranslator(g_cfg);
        g_lateStartDone = false;  // the model and the word lists wait for a map: the window starts them
        RestoreSend();  // the send language chosen last time, not a recomputed one
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
    // Versions up to 2.8.2 wrote a font nothing uses any more into the data folder: it goes.
    DeleteFileW((g_cfg.dataDir + L"\\fonts\\Exo2-Regular.ttf").c_str());
    RemoveDirectoryW((g_cfg.dataDir + L"\\fonts").c_str());  // only if empty
    Config::DeferWrites(true);  // the ini is written on the worker (KickSave), never on the game thread
    g_worker.Start();
    {
        std::lock_guard<std::mutex> lock(g_mu);
        if (GetPrivateProfileIntW(L"Debug", L"SendLog", 0, g_cfg.iniPath.c_str())) TechLogToFile(g_cfg.dataDir + L"\\send-steps.log");
        if (GetPrivateProfileIntW(L"Debug", L"Protocol", 0, g_cfg.iniPath.c_str())) {
            ProtocolToFile(g_cfg.dataDir + L"\\session-protocol.log");
            LogState(L"the start");
        }
        g_links.Start(api, [](std::function<void()> job) {
            if (!g_test.noSend) g_worker.PostLookup(std::move(job));  // the self-test asks nobody
        }, g_cfg.dataDir);
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
    gct::SetLocalMtLog([](const std::wstring& line) { TechLog('H', line); });
    if (first) FirstStart();  // notes the first start; the modules come on your click
    if (api->GUI_Register && IsCodePointer(reinterpret_cast<const void*>(api->GUI_Register))) {
        api->GUI_Register(RT_Render, SafeRender);
        api->GUI_Register(RT_OptionsRender, SafeOptions);
    }
    if (api->InputBinds_RegisterWithString && IsCodePointer(reinterpret_cast<const void*>(api->InputBinds_RegisterWithString))) {
        // No key is set by us: every one is yours to bind in Nexus → Keybinds (a key of your own stays as you set it).
        api->InputBinds_RegisterWithString(kToggleBind, SafeKey<OnToggle>, "(null)");
        api->InputBinds_RegisterWithString(kTranslateBind, SafeKey<OnTranslateKey>, "(null)");
        api->InputBinds_RegisterWithString(kFocusBind, SafeKey<OnFocusKey>, "(null)");
        api->InputBinds_RegisterWithString(kTalkBind, SafeKey<OnTalkKey>, "(null)");
    }
    // The keys' names in Nexus → Keybinds, in Nexus' languages – none is set by us, so you find them there by name, not by
    // their technical identifier.
    if (api->Localization_Set && IsCodePointer(reinterpret_cast<const void*>(api->Localization_Set))) {
        static const std::pair<UiLang, const char*> kLangs[] = {{UiLang::En, "en"}, {UiLang::De, "de"}, {UiLang::Fr, "fr"}, {UiLang::Es, "es"},
                                                                {UiLang::It, "it"}, {UiLang::Pl, "pl"}, {UiLang::Pt, "br"}, {UiLang::Ru, "ru"}};
        static const std::pair<const char*, const wchar_t*> kKeys[] = {
            {kToggleBind, L"Window"}, {kFocusBind, L"Input"}, {kTranslateBind, L"Translate the chat"}, {kTalkBind, L"Voice"}};
        for (const auto& [lang, code] : kLangs)
            for (const auto& [id, english] : kKeys) {
                const wchar_t* said = lang == UiLang::En ? english : TranslationFor(lang, english);
                api->Localization_Set(id, code, ("GW2Chatlator: " + ToUtf8(said ? said : english)).c_str());
            }
    }
    ApplyPushToTalkKey(g_cfg.voiceGlobalKey);
    if (api->WndProc_Register && IsCodePointer(reinterpret_cast<const void*>(api->WndProc_Register))) {
        api->WndProc_Register(SafeWndProc);
    }
    if (api->Events_Subscribe && IsCodePointer(reinterpret_cast<const void*>(api->Events_Subscribe))) {
        api->Events_Subscribe(bridge::kChatEventsEvent, SafeEvent<OnChatEvents>);
        api->Events_Subscribe(EV_ADDON_LOADED, SafeEvent<OnAddonLoaded>);
        api->Events_Subscribe(EV_ADDON_UNLOADED, SafeEvent<OnAddonUnloaded>);
        g_addonEventsOn = true;
    }
    Log(LOGL_INFO, "loaded");
}

// Nexus' own Addons window: its key KB_ADDONS invoked through the API (Nexus handles it on the press only, on its own
// thread). There a new add-on gets its Load – we never load or remove another add-on ourselves.
bool OpenNexusAddons() {
    if (!g_api || !g_api->InputBinds_Invoke || !IsCodePointer(reinterpret_cast<const void*>(g_api->InputBinds_Invoke)))
        return false;
    g_api->InputBinds_Invoke("KB_ADDONS", false);
    return true;
}

// Nexus' own menu (its key KB_MENU, handled on the press only): where Keybinds lie – a key for our actions is set there.
bool OpenNexusMenu() {
    if (!g_api || !g_api->InputBinds_Invoke || !IsCodePointer(reinterpret_cast<const void*>(g_api->InputBinds_Invoke)))
        return false;
    g_api->InputBinds_Invoke("KB_MENU", false);
    return true;
}

void AddonUnload() {
    TechLog('O', L"== end of the session");
    g_unloading = true;  // first: every install thread stops at its next step (unload waits for them below)
    gct::StopHttpRequests();  // and one waiting on the network ends now, not after its read's 60 s
    if (g_api) {
        if (g_api->Events_Unsubscribe && IsCodePointer(reinterpret_cast<const void*>(g_api->Events_Unsubscribe))) {
            g_api->Events_Unsubscribe(bridge::kChatEventsEvent, SafeEvent<OnChatEvents>);
            if (g_closedWithX) ChatSourceSetReading(true, false);  // the X counts for this session, the next one reads again
            g_api->Events_Unsubscribe(EV_ADDON_LOADED, SafeEvent<OnAddonLoaded>);
            g_api->Events_Unsubscribe(EV_ADDON_UNLOADED, SafeEvent<OnAddonUnloaded>);
            g_addonEventsOn = false;
        }
        if (g_api->WndProc_Deregister && IsCodePointer(reinterpret_cast<const void*>(g_api->WndProc_Deregister))) {
            g_api->WndProc_Deregister(SafeWndProc);
        }
        if (g_api->InputBinds_Deregister && IsCodePointer(reinterpret_cast<const void*>(g_api->InputBinds_Deregister))) {
            g_api->InputBinds_Deregister(kToggleBind);
            g_api->InputBinds_Deregister(kFocusBind);
            g_api->InputBinds_Deregister(kTalkBind);
            g_api->InputBinds_Deregister(kTranslateBind);
        }
        if (g_api->GUI_Deregister && IsCodePointer(reinterpret_cast<const void*>(g_api->GUI_Deregister))) {
            g_api->GUI_Deregister(SafeRender);
            g_api->GUI_Deregister(SafeOptions);
        }
    }
    g_downloadCancel = true;
    if (g_download.joinable()) g_download.join();
    if (g_chatEventsThread.joinable()) g_chatEventsThread.join();
    if (g_voiceModuleThread.joinable()) g_voiceModuleThread.join();
    if (g_localMtThread.joinable()) g_localMtThread.join();
    if (g_localMtProbeThread.joinable()) g_localMtProbeThread.join();
    WaypointStop();
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
    g_test.bufferingKeys = &g_bufferingKeys;
    g_test.sendInFlight = &g_sendInFlight;
    g_test.bufferingStartTime = &g_bufferingStartTime;
    return &g_test;
}

extern "C" __declspec(dllexport) UINT Gw2KbTestWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {  // nexus_kb_selftest only
    if (!TestHost()) return msg;
    return OnWndProc(hwnd, msg, wp, lp);
}

extern "C" __declspec(dllexport) void Gw2KbTestLiveView() {  // nexus_kb_selftest only: the live view with made-up numbers
    if (!TestHost()) return;
    std::lock_guard<std::mutex> lock(g_mu);
    g_showMtLive = true;
    OpenWaypointPreview(1065);
    g_keys.total = 500;
    g_keys.presses[VK_BACK] = 90;
    g_keys.presses[VK_SPACE] = 80;
    for (int vk = 'A'; vk <= 'Z'; ++vk) g_keys.presses[vk] = static_cast<uint32_t>((vk * 7) % 31);
    g_keys.spaceWrote = 20;
    g_keys.spaceUndone = 9;
    g_keys.wordClicked = 3;
    g_keys.buffered = 14;
    g_keys.bufferRuns = 2;
    for (int i = 0; i < 40; ++i) {
        MtRun r;
        r.at = GetTickCount64() - static_cast<ULONGLONG>(40 - i) * 3000;
        r.kind = i % 7 == 3 ? 'I' : i % 7 == 4 ? 'B' : 'C';
        r.from = L"EN";
        r.to = L"DE";
        r.beamSet = 8;
        r.beamAsked = i % 5 == 0 ? 2 : 8;
        r.waiting = i % 5 == 0 ? 5 : 1;
        r.tag = L"M2M100 (local) · Beam " + std::to_wstring(r.beamAsked);
        r.run.requests = 1 + i % 3;
        r.run.beam = r.beamAsked;
        r.run.threads = 6;
        r.run.cores = 6;
        r.run.tokensIn = 10 + i % 9;
        r.run.tokensOut = 12 + i % 11;
        r.run.modelMs = 300 + i * 41 % 900;
        r.run.loadMs = i == 0 ? 3180 : 0;
        r.ms = r.run.modelMs + 40;
        r.score = -0.3 - (i % 8) * 0.17;
        r.scored = true;
        r.cached = i % 11 == 6;
        r.second = i % 13 == 7;
        r.doubt = i % 17 == 9;
        r.versions = i % 3;
        r.queueMs = i * 7 % 90;
        r.prepMs = 1 + i % 3;
        NoteMtRun(std::move(r));
    }
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
    if (lang) CopyUtf8(lang, 16, U8(SendLang()));
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

extern "C" __declspec(dllexport) void Gw2KbTestSubtitles(int on) {  // nexus_kb_selftest: subtitle mode on / off
    if (!TestHost()) return;
    std::lock_guard<std::mutex> lock(g_mu);
    SetProfile(2, on != 0);
}

// nexus_kb_selftest: version choice without the preview field (step 1: mini view + 3 versions, step 0: query, step 2: clean up).
extern "C" __declspec(dllexport) int Gw2KbTestCompactVersions(int step, char* previewOut, char* backOut) {
    if (!TestHost()) return -1;
    std::lock_guard<std::mutex> lock(g_mu);
    if (step == 1) {
        SetProfile(1, true);
        g_sendOverride = PrimaryLang(TypingLang()) == L"EN" ? L"DE" : L"EN";
        g_edit.active = false;
        CopyUtf8(g_input, "Wir warten am Wegpunkt");
        g_flatInput = g_lastInput = g_input;
        g_wFlatInput = W(g_flatInput);
        g_inputChanged = 0;
        g_previewGen = ++g_inputGen;
        g_previewVersions = {L"We wait at the waypoint", L"Waiting at the waypoint", L"We are waiting at the waypoint"};
        g_previewBacks = {L"Wir warten am Wegpunkt", L"Warten am Wegpunkt", L"Wir warten gerade am Wegpunkt"};
        g_previewPick = 0;
        g_preview = g_previewVersions[0];
        g_back = g_previewBacks[0];
        return 0;
    }
    if (step == 2) {
        SetProfile(1, false);
        g_sendOverride.clear();
        g_edit = {true, 0, std::strlen(g_input), ""};
        g_input[0] = '\0';
        g_flatInput.clear();
        g_wFlatInput.clear();
        g_lastInput.clear();
        g_inputChanged = 0;
        g_preview.clear();
        g_back.clear();
        g_previewVersions.clear();
        g_previewBacks.clear();
        g_previewPick = 0;
        return 0;
    }
    if (previewOut) CopyUtf8(previewOut, 128, U8(g_preview));
    if (backOut) CopyUtf8(backOut, 128, U8(g_back));
    return static_cast<int>(g_previewPick) + (g_collapsed ? 100 : 0);
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

extern "C" __declspec(dllexport) void Gw2KbTestTranslateLast(const char* text) {  // nexus_kb_selftest: a translation arrives
    if (!TestHost()) return;
    std::lock_guard<std::mutex> lock(g_mu);
    if (g_lines.empty()) return;
    ChatLine& l = g_lines.back();
    l.translated = W(text);
    l.u8translated = text;
    l.transSegs = PieUI::ChatLinks::SegmentLine(l.u8translated);
    l.transPending = false;
    l.transAttempted = true;
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
        case 6: if (text) CopyUtf8(text, 4096, ChatSourceProtocolText()); break;
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
            if (cmd == 8) g_optionsTab = text && *text ? std::atoi(text) : kPageTechnical;  // a tab by its number, else Technical
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
    if (start) RemoveEverythingNow();
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
    g_def.Description = "Input prediction, real-time translation, voice to text - the keyboard GW2 was always missing.";
    g_def.Load = AddonLoad;
    g_def.Unload = AddonUnload;
    g_def.Flags = AF_None;
    g_def.Provider = UP_GitHub;
    g_def.UpdateLink = "https://github.com/Emisan01/gw2chatlator";
    return &g_def;
}
