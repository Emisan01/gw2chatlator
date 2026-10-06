// main.cpp — GW2 Chat Translator.
//
// A small always-on-top window next to (or over) the GW2 chat: the chat is
// read from the screen and shown translated into your language; you type in
// your language, get spelling help and phone-style autocorrection, see the
// translation with official GW2 names, press Enter and the line lands in the
// GW2 chat. No hooks, no memory reading, nothing injected into the game:
// only screen pixels, the clipboard and the keystrokes you trigger yourself.
#include <windows.h>
#include <objbase.h>

#include <string>

#include "app/main_window.hpp"

namespace {

constexpr wchar_t kMutexName[] = L"Local\\GW2ChatTranslator.SingleInstance";
constexpr wchar_t kClassName[] = L"GW2ChatTranslatorWindow";

// Per-monitor aware: window and screen coordinates are real pixels on every
// monitor. The chat reader depends on that — with "system aware", Windows
// would scale the GW2 window's coordinates on a monitor with another scale
// and the reader would look at the wrong pixels.
void EnableDpiAwareness() {
    using SetCtxFn = BOOL(WINAPI*)(HANDLE);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    void* fn = user32 ? reinterpret_cast<void*>(GetProcAddress(user32, "SetProcessDpiAwarenessContext")) : nullptr;
    if (fn) {
        auto set = reinterpret_cast<SetCtxFn>(fn);
        if (set(reinterpret_cast<HANDLE>(static_cast<LONG_PTR>(-4))))  // PER_MONITOR_AWARE_V2 (Win10 1703+)
            return;
        if (set(reinterpret_cast<HANDLE>(static_cast<LONG_PTR>(-3))))  // PER_MONITOR_AWARE
            return;
    }
    SetProcessDPIAware();
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmdLine, int) {
    const std::wstring args = cmdLine ? cmdLine : L"";
    // One instance only; a second start just brings the first one forward.
    // A restart into the installed copy waits a moment for the old one to end.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, kMutexName);
    for (int i = 0; i < 40 && GetLastError() == ERROR_ALREADY_EXISTS && args.find(L"--restarted") != std::wstring::npos;
         ++i) {
        CloseHandle(mutex);
        Sleep(100);
        mutex = CreateMutexW(nullptr, TRUE, kMutexName);
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND other = FindWindowW(kClassName, nullptr)) {
            ShowWindow(other, SW_SHOW);
            SetForegroundWindow(other);
        }
        if (mutex) CloseHandle(mutex);
        return 0;
    }

    EnableDpiAwareness();
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);  // spell checker

    int code;
    std::wstring restart;
    {
        gct::MainWindow window;
        code = window.Run(inst, args);
        restart = window.RestartCommand();
    }  // services (incl. COM objects) released before CoUninitialize

    if (SUCCEEDED(com)) CoUninitialize();
    if (mutex) CloseHandle(mutex);
    if (!restart.empty()) {  // continue in the installed copy
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        if (CreateProcessW(nullptr, restart.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
    }
    return code;
}
