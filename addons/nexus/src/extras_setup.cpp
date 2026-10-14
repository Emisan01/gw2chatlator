#include "extras_setup.hpp"

#include <psapi.h>

#include <algorithm>
#include <atomic>
#include <cwctype>
#include <mutex>
#include <thread>

#include "core/json.hpp"
#include "core/text.hpp"
#include "win/http.hpp"

namespace bridge {
namespace {

constexpr const char* kReleaseHost = "api.github.com";
constexpr const wchar_t* kReleasePath = L"/repos/Krappa322/arcdps_unofficial_extras_releases/releases/latest";
constexpr const char* kDownloadPrefix =
    "https://github.com/Krappa322/arcdps_unofficial_extras_releases/releases/download/";
constexpr const char* kAssetName = "arcdps_unofficial_extras.dll";

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

bool IsHex64(const std::string& s) {
    return s.size() == 64 && std::all_of(s.begin(), s.end(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}

void Install(std::wstring dir) {
    Set(InstallState::Running, "Asking GitHub for the newest Unofficial Extras ...");
    const gct::HttpResponse r = gct::HttpsRequest(L"GET", gct::FromUtf8(kReleaseHost), kReleasePath,
                                                  L"Accept: application/vnd.github+json\r\n", "");
    if (!r.transportOk || r.status != 200) {
        Set(InstallState::Failed, "GitHub did not answer (" +
                                      (r.transportOk ? "status " + std::to_string(r.status) : gct::ToUtf8(r.error)) +
                                      "). Try again later.");
        return;
    }
    gct::JsonValue release;
    std::string url, sha;
    uint64_t size = 0;
    if (gct::ParseJson(r.body, release)) {
        if (const gct::JsonValue* assets = release.Get("assets"); assets && assets->IsArray()) {
            for (const gct::JsonValue& a : assets->arr) {
                if (a.GetString("name") != kAssetName) continue;
                url = a.GetString("browser_download_url");
                const std::string digest = a.GetString("digest");
                if (digest.rfind("sha256:", 0) == 0) sha = digest.substr(7);
                if (const gct::JsonValue* s = a.Get("size"); s && s->type == gct::JsonValue::Type::Number)
                    size = static_cast<uint64_t>(s->n);
            }
        }
    }
    // Only the official release page, and only with a checksum GitHub computed for the file.
    if (url.rfind(kDownloadPrefix, 0) != 0 || !IsHex64(sha) || size == 0) {
        Set(InstallState::Failed, "The newest release has no checked arcdps_unofficial_extras.dll. "
                                  "Please install it from its GitHub page.");
        return;
    }
    Set(InstallState::Running, "Downloading Unofficial Extras (" + std::to_string(size / 1024) + " KB) ...");
    std::wstring error;
    const bool ok = gct::HttpDownloadToFile(
        gct::FromUtf8(url), dir + gct::FromUtf8(kAssetName), size, sha,
        [](uint64_t, uint64_t) { return !g_cancel.load(); }, &error);
    if (!ok) {
        Set(InstallState::Failed, "Download failed: " + gct::ToUtf8(error));
        return;
    }
    Set(InstallState::Done, "Installed. Nexus loads it now; if party chat does not arrive, restart the game.");
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
    p.extrasFile = GetFileAttributesW((AddonsDir(self) + gct::FromUtf8(kAssetName)).c_str()) != INVALID_FILE_ATTRIBUTES;
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

}  // namespace bridge
