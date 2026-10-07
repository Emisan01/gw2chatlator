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

#include "app/config.hpp"
#include "app/main_window.hpp"
#include "core/i18n.hpp"
#include "win/gw2_locate.hpp"

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

// A downloaded copy started while the program is installed: start the installed one instead, or – when this one
// is newer – update the installed copy first (settings and learned words stay; they live next to it). Like Discord or
// VS Code: the download folder is never where the program keeps running. True when this process should end.
bool HandOverToInstalledCopy(const std::wstring& args) {
    if (args.find(L"--restarted") != std::wstring::npos || args.find(L"--portable") != std::wstring::npos) return false;
    if (gct::RunningInstalledCopy()) return false;
    const std::wstring installed = gct::FindInstalledExe();
    if (installed.empty()) return false;
    const std::wstring mine = gct::ExeVersion(gct::CurrentExePath()), theirs = gct::ExeVersion(installed);
    std::wstring target = installed;
    // Newer: a higher version, or the same version built later (a fresh build being tested).
    const int cmp = gct::CompareVersions(mine, theirs);
    if (cmp > 0 || (cmp == 0 && gct::FileNewer(gct::CurrentExePath(), installed))) {
        gct::SetUiLang(gct::EffectiveUiLang(L""));
        const int answer = MessageBoxW(
            nullptr,
            gct::TrF(L"GW2 Chat Translator {1} is installed. Update it to {2} and start it from there?\n\nYour settings, "
                     L"learned words and corrections stay.",
                     {theirs.empty() ? std::wstring(L"?") : theirs, mine})
                .c_str(),
            L"GW2 Chat Translator", MB_YESNO | MB_ICONQUESTION);
        if (answer != IDYES) return false;  // runs from here this time
        // A running old copy ends first (its files are in use while it runs).
        if (HWND other = FindWindowW(kClassName, nullptr)) PostMessageW(other, WM_CLOSE, 0, 0);
        for (int i = 0; i < 50; ++i) {
            HANDLE m = OpenMutexW(SYNCHRONIZE, FALSE, kMutexName);
            if (!m) break;
            CloseHandle(m);
            Sleep(100);
        }
        const size_t slash = installed.find_last_of(L"\\/");
        const gct::InstallResult r = gct::InstallTo(installed.substr(0, slash));
        if (!r.ok) {
            MessageBoxW(nullptr, r.error.c_str(), L"GW2 Chat Translator", MB_OK | MB_ICONWARNING);
            return false;
        }
        target = r.exePath;
    } else if (HWND other = FindWindowW(kClassName, nullptr)) {  // already running: bring it forward
        ShowWindow(other, SW_SHOW);
        SetForegroundWindow(other);
        return true;
    }
    std::wstring cmd = L"\"" + target + L"\" --restarted";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmdLine, int) {
    const std::wstring args = cmdLine ? cmdLine : L"";
    if (HandOverToInstalledCopy(args)) return 0;
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
