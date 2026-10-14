#include "win/extras_install.hpp"

#include <windows.h>

#include <algorithm>

#include "core/json.hpp"
#include "core/text.hpp"
#include "win/http.hpp"

#ifndef GCT_BRIDGE_DLL
#include "win/gw2_locate.hpp"
#endif

namespace gct {
namespace {

constexpr const wchar_t* kReleasePath = L"/repos/Krappa322/arcdps_unofficial_extras_releases/releases/latest";
constexpr const char* kDownloadPrefix =
    "https://github.com/Krappa322/arcdps_unofficial_extras_releases/releases/download/";

bool IsHex64(const std::string& s) {
    return s.size() == 64 && std::all_of(s.begin(), s.end(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}

}  // namespace

ExtrasInstallResult InstallUnofficialExtras(const std::wstring& addonsDir, const std::function<bool()>& cancelled) {
    ExtrasInstallResult res;
    const HttpResponse r =
        HttpsRequest(L"GET", L"api.github.com", kReleasePath, L"Accept: application/vnd.github+json\r\n", "");
    if (!r.transportOk || r.status != 200) {
        res.code = ExtrasInstallCode::NoAnswer;
        res.detail = r.transportOk ? L"HTTP " + std::to_wstring(r.status) : r.error;
        return res;
    }
    JsonValue release;
    std::string url, sha;
    const std::string asset = ToUtf8(kExtrasDllName);
    if (ParseJson(r.body, release)) {
        if (const JsonValue* assets = release.Get("assets"); assets && assets->IsArray()) {
            for (const JsonValue& a : assets->arr) {
                if (a.GetString("name") != asset) continue;
                url = a.GetString("browser_download_url");
                const std::string digest = a.GetString("digest");
                if (digest.rfind("sha256:", 0) == 0) sha = digest.substr(7);
                if (const JsonValue* s = a.Get("size"); s && s->type == JsonValue::Type::Number)
                    res.bytes = static_cast<uint64_t>(s->n);
            }
        }
    }
    // Only the official release page, and only with a checksum GitHub computed for the file.
    if (url.rfind(kDownloadPrefix, 0) != 0 || !IsHex64(sha) || res.bytes == 0) {
        res.code = ExtrasInstallCode::NotChecked;
        return res;
    }
    std::wstring error;
    const bool ok = HttpDownloadToFile(
        FromUtf8(url), addonsDir + kExtrasDllName, res.bytes, sha,
        [&](uint64_t, uint64_t) { return !(cancelled && cancelled()); }, &error);
    res.code = ok ? ExtrasInstallCode::Ok : ExtrasInstallCode::DownloadFailed;
    res.detail = error;
    return res;
}

#ifndef GCT_BRIDGE_DLL
std::wstring Gw2AddonsDir() {
    const std::wstring dir = FindGw2Dir();
    if (dir.empty()) return {};
    std::wstring addons = dir;
    if (addons.back() != L'\\' && addons.back() != L'/') addons += L'\\';
    addons += L"addons\\";
    return GetFileAttributesW(addons.c_str()) != INVALID_FILE_ATTRIBUTES ? addons : std::wstring();
}

std::wstring BridgeInPackage() {
    const std::wstring p = CurrentExeDir() + L"\\nexus\\" + kBridgeDllName;
    return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES ? p : std::wstring();
}
#endif

}  // namespace gct
