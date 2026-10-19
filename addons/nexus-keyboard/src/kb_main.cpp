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

namespace kb {


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
    }
    RequestTextFont(g_cfg.fontPt);
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
    if (g_api && g_api->Fonts_Release) g_api->Fonts_Release(kTextFontId, OnTextFont);  // no call into a gone DLL
    g_textFont = nullptr;
    g_textFontPx = 0;
    g_voice.Stop();  // the helper ends (it would also end by itself when its input closes)
    g_worker.Stop(FreeWorkerState);  // saved and freed on the worker (it owns them), before the DLL goes
    Config::DeferWrites(false);      // what is still waiting goes into the ini now
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

extern "C" __declspec(dllexport) void Gw2KbTestWaypoint(int step) {  // nexus_kb_selftest only: 0 click, 1 map closed
    if (!TestHost()) return;
    std::lock_guard<std::mutex> lock(g_mu);
    if (step == 0) HandleChatLinkClick("[&BAwEAAA=]", GetTickCount64(), std::string());
    else g_expandRequest = true;
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
        case 8:
        case 9: {
            std::lock_guard<std::mutex> lock(g_mu);
            g_showOptions = cmd == 8;
            if (cmd == 8) g_optionsTab = 6;
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
    if (start) RemoveEverythingNow(false);
    return g_wipeDone;
}

extern "C" __declspec(dllexport) AddonDefinition_t* GetAddonDef() {
    g_def.Signature = static_cast<uint32_t>(-0x474B4244);  // 0xB8B4BDBC – GW2Chatlator on Raidcore (ID 150)
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
