#include "extras_setup.hpp"

#include <psapi.h>

#include <algorithm>
#include <atomic>
#include <cwctype>
#include <mutex>
#include <thread>

#include "core/text.hpp"
#include "win/extras_install.hpp"

namespace bridge {
namespace {

std::mutex g_mutex;
InstallState g_state = InstallState::Idle;
std::string g_message;
std::atomic<bool> g_cancel{false};
std::thread g_thread;

void Set(InstallState s, const std::string& msg) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_state = s;
    g_message = msg;
}

std::wstring AddonsDir(HMODULE self) {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(self, path, MAX_PATH);
    std::wstring p = path;
    const size_t slash = p.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : p.substr(0, slash + 1);
}

void Install(std::wstring dir) {
    Set(InstallState::Running, "Downloading the newest Unofficial Extras from its GitHub page ...");
    const gct::ExtrasInstallResult r = gct::InstallUnofficialExtras(dir, [] { return g_cancel.load(); });
    switch (r.code) {
        case gct::ExtrasInstallCode::Ok:
            Set(InstallState::Done, "Installed. Nexus loads it now; if party chat does not arrive, restart the game.");
            break;
        case gct::ExtrasInstallCode::NoAnswer:
            Set(InstallState::Failed, "GitHub did not answer (" + gct::ToUtf8(r.detail) + "). Try again later.");
            break;
        case gct::ExtrasInstallCode::NotChecked:
            Set(InstallState::Failed, "The newest release has no checked arcdps_unofficial_extras.dll. "
                                      "Please install it from its GitHub page.");
            break;
        case gct::ExtrasInstallCode::DownloadFailed:
            Set(InstallState::Failed, "Download failed: " + gct::ToUtf8(r.detail));
            break;
    }
}

}  // namespace

Presence DetectPresence(HMODULE self) {
    Presence p;
    HMODULE mods[1024];
    DWORD needed = 0;
    if (K32EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &needed)) {
        const DWORD n = std::min<DWORD>(needed / sizeof(HMODULE), 1024);
        for (DWORD i = 0; i < n; ++i) {
            if (GetProcAddress(mods[i], "addextension2")) p.arcdps = true;
            wchar_t path[MAX_PATH] = {};
            GetModuleFileNameW(mods[i], path, MAX_PATH);
            std::wstring name = path;
            const size_t slash = name.find_last_of(L"\\/");
            if (slash != std::wstring::npos) name.erase(0, slash + 1);
            for (wchar_t& c : name) c = static_cast<wchar_t>(std::towlower(c));
            if (name.find(L"unofficial_extras") != std::wstring::npos || GetProcAddress(mods[i], "get_key_bind"))
                p.extras = true;
            if (name == L"arcdps_integration64.dll") p.relay = true;
        }
    }
    p.extrasFile = GetFileAttributesW((AddonsDir(self) + gct::kExtrasDllName).c_str()) != INVALID_FILE_ATTRIBUTES;
    return p;
}

void StartExtrasInstall(HMODULE self) {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_state == InstallState::Running) return;
        g_state = InstallState::Running;
        g_message = "Starting ...";
    }
    if (g_thread.joinable()) g_thread.join();
    g_cancel = false;
    g_thread = std::thread(Install, AddonsDir(self));
}

InstallState GetInstallState(std::string* message) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (message) *message = g_message;
    return g_state;
}

void StopExtrasInstall() {
    g_cancel = true;
    if (g_thread.joinable()) g_thread.join();
}

// <addons>\GW2ChatBridge\settings.ini – the add-on's own folder, nothing else is written.
static std::wstring SettingsFile(HMODULE self) {
    const std::wstring dir = AddonsDir(self) + L"GW2ChatBridge\\";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"settings.ini";
}

Settings LoadSettings(HMODULE self) {
    const std::wstring f = SettingsFile(self);
    Settings s;
    s.party = GetPrivateProfileIntW(L"Bridge", L"Party", 1, f.c_str()) != 0;
    s.squad = GetPrivateProfileIntW(L"Bridge", L"Squad", 1, f.c_str()) != 0;
    return s;
}

void SaveSettings(HMODULE self, const Settings& s) {
    const std::wstring f = SettingsFile(self);
    WritePrivateProfileStringW(L"Bridge", L"Party", s.party ? L"1" : L"0", f.c_str());
    WritePrivateProfileStringW(L"Bridge", L"Squad", s.squad ? L"1" : L"0", f.c_str());
}

}  // namespace bridge
