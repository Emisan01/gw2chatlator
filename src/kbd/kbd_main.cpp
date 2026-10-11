// kbd_main.cpp — GW2 Keyboard: the typing help of GW2 Chat Translator as its own small program.
//
// You type in your language, get spelling help, your learned words and typos, Windows' word prediction and – when
// the chat gets another language – the translation with a back-translation; Enter puts the line into the GW2 chat.
// Nothing of the chat is read. No hooks, no memory reading, nothing in the game: only the clipboard and the keys you
// trigger yourself.
#include <windows.h>
#include <objbase.h>

#include <string>

#include "kbd/keyboard_window.hpp"

namespace {

constexpr wchar_t kMutexName[] = L"Local\\GW2Keyboard.SingleInstance";
constexpr wchar_t kClassName[] = L"GW2KeyboardWindow";

void EnableDpiAwareness() {
    using SetCtxFn = BOOL(WINAPI*)(HANDLE);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    void* fn = user32 ? reinterpret_cast<void*>(GetProcAddress(user32, "SetProcessDpiAwarenessContext")) : nullptr;
    if (fn) {
        auto set = reinterpret_cast<SetCtxFn>(fn);
        if (set(reinterpret_cast<HANDLE>(static_cast<LONG_PTR>(-4)))) return;  // PER_MONITOR_AWARE_V2
        if (set(reinterpret_cast<HANDLE>(static_cast<LONG_PTR>(-3)))) return;  // PER_MONITOR_AWARE
    }
    SetProcessDPIAware();
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmdLine, int) {
    const std::wstring args = cmdLine ? cmdLine : L"";
    // One instance only; a second start brings the first one forward.
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
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);  // spell checker, prediction
    int code;
    {
        gct::KeyboardWindow window;
        code = window.Run(inst, args);
    }
    if (SUCCEEDED(com)) CoUninitialize();
    if (mutex) CloseHandle(mutex);
    return code;
}
