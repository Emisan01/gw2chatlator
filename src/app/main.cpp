// main.cpp — GW2 Chat Translator.
//
// A small always-on-top window next to the game: type in your language,
// get spelling marks and Windows' autocorrection, see the translation with
// official GW2 names, press Enter and the line lands in the GW2 chat.
// No hooks, no memory reading, nothing injected into the game: only the
// clipboard and the keystrokes you trigger yourself.
#include <windows.h>
#include <objbase.h>

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

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    // One instance only; a second start just brings the first one forward.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, kMutexName);
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
    {
        gct::MainWindow window;
        code = window.Run(inst);
    }  // services (incl. COM objects) released before CoUninitialize

    if (SUCCEEDED(com)) CoUninitialize();
    if (mutex) CloseHandle(mutex);
    return code;
}
