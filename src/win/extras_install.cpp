#include "win/extras_install.hpp"

#include <windows.h>

#include <algorithm>

#include "core/json.hpp"
#include "core/text.hpp"
#include "win/http.hpp"

namespace gct {
namespace {

struct ReleaseSource {
    const wchar_t* releasePath;  // GitHub API: the newest release
    const char* downloadPrefix;  // the only place the file may come from
    const wchar_t* asset;        // its file name
};
constexpr ReleaseSource kChatEvents{L"/repos/jsantorek/GW2-Chat/releases/latest",
                                    "https://github.com/jsantorek/GW2-Chat/releases/download/", kChatEventsDllName};
constexpr ReleaseSource kDecoderRing{L"/repos/PieOrCake/decoder_ring/releases/latest",
                                     "https://github.com/PieOrCake/decoder_ring/releases/download/", kDecoderRingDllName};

bool IsHex64(const std::string& s) {
    return s.size() == 64 && std::all_of(s.begin(), s.end(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}

}  // namespace

ExtrasInstallResult DownloadReleaseAsset(const std::wstring& releaseApiPath, const char* downloadPrefix,
                                         const std::wstring& asset, const std::wstring& dest,
                                         const std::function<bool(uint64_t done, uint64_t total)>& progress) {
    ExtrasInstallResult res;
    const HttpResponse r =
        HttpsRequest(L"GET", L"api.github.com", releaseApiPath, L"Accept: application/vnd.github+json\r\n", "");
    if (!r.transportOk || r.status != 200) {
        res.code = ExtrasInstallCode::NoAnswer;
        res.detail = r.transportOk ? L"HTTP " + std::to_wstring(r.status) : r.error;
        return res;
    }
    JsonValue release;
    std::string url, sha;
    const std::string name = ToUtf8(asset);
    if (ParseJson(r.body, release)) {
        if (const JsonValue* assets = release.Get("assets"); assets && assets->IsArray()) {
            for (const JsonValue& a : assets->arr) {
                if (a.GetString("name") != name) continue;
                url = a.GetString("browser_download_url");
                const std::string digest = a.GetString("digest");
                if (digest.rfind("sha256:", 0) == 0) sha = digest.substr(7);
                if (const JsonValue* s = a.Get("size"); s && s->type == JsonValue::Type::Number)
                    res.bytes = static_cast<uint64_t>(s->n);
            }
        }
    }
    // Only the official release page, and only with a checksum GitHub computed for the file.
    if (url.rfind(downloadPrefix, 0) != 0 || !IsHex64(sha) || res.bytes == 0) {
        res.code = ExtrasInstallCode::NotChecked;
        return res;
    }
    std::wstring error;
    const bool ok = HttpDownloadToFile(
        FromUtf8(url), dest, res.bytes, sha,
        [&](uint64_t done, uint64_t total) { return !progress || progress(done, total); }, &error);
    res.code = ok ? ExtrasInstallCode::Ok : ExtrasInstallCode::DownloadFailed;
    res.detail = error;
    return res;
}

static ExtrasInstallResult InstallFromRelease(const ReleaseSource& src, const std::wstring& addonsDir,
                                              const std::function<bool()>& cancelled) {
    return DownloadReleaseAsset(src.releasePath, src.downloadPrefix, src.asset, addonsDir + src.asset,
                                [&](uint64_t, uint64_t) { return !(cancelled && cancelled()); });
}

ExtrasInstallResult InstallChatEvents(const std::wstring& addonsDir, const std::function<bool()>& cancelled) {
    return InstallFromRelease(kChatEvents, addonsDir, cancelled);
}

ExtrasInstallResult InstallDecoderRing(const std::wstring& addonsDir, const std::function<bool()>& cancelled) {
    return InstallFromRelease(kDecoderRing, addonsDir, cancelled);
}

}  // namespace gct
