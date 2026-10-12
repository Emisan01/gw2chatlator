// voice_models.cpp — voice model files: check, download (streamed, SHA-256), delete; catalog; what the PC can carry.
#include "win/voice_models.hpp"

#include "core/i18n.hpp"
#include "core/text.hpp"
#include "win/files.hpp"
#include "win/http.hpp"

#include <windows.h>

#include <shlobj.h>

#ifndef PF_AVX2_INSTRUCTIONS_AVAILABLE
#define PF_AVX2_INSTRUCTIONS_AVAILABLE 40
#endif

namespace gct {
namespace {

// The catalog already rejects these; checked again because the path is also used for deleting.
bool PlainFileName(const std::string& f) {
    return !f.empty() && f.find_first_of("\\/:") == std::string::npos && f.find("..") == std::string::npos;
}

std::wstring Mb(uint64_t bytes) { return std::to_wstring((bytes + (1u << 20) - 1) >> 20); }

}  // namespace

std::wstring VoiceModelPath(const std::wstring& dir, const VoiceModelInfo& m) {
    return dir + L"\\" + FromUtf8(m.file);
}

bool VoiceModelInstalled(const std::wstring& dir, const VoiceModelInfo& m) {
    if (!PlainFileName(m.file) || m.bytes == 0) return false;
    HANDLE h = CreateFileW(VoiceModelPath(dir, m).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    unsigned char magic[4] = {};
    DWORD read = 0;
    const bool ok = GetFileSizeEx(h, &size) && static_cast<uint64_t>(size.QuadPart) == m.bytes &&
                    ReadFile(h, magic, 4, &read, nullptr) && read == 4;
    CloseHandle(h);
    return ok && magic[0] == 0x6c && magic[1] == 0x6d && magic[2] == 0x67 && magic[3] == 0x67;  // "lmgg"
}

bool DownloadVoiceModel(const std::wstring& dir, const VoiceModelInfo& m,
                        const std::function<bool(uint64_t done, uint64_t total)>& progress,
                        const std::atomic<bool>* cancel, std::wstring* error) {
    auto fail = [error](const std::wstring& why) {
        if (error) *error = why;
        return false;
    };
    if (!PlainFileName(m.file) || m.bytes == 0 || m.url.compare(0, 8, "https://") != 0)
        return fail(Tr(L"This voice model entry is invalid."));
    if (cancel && *cancel) return fail(Tr(L"Download cancelled."));
    const uint64_t need = m.bytes + (50ull << 20);  // the model plus room to spare
    const uint64_t freeBytes = FreeDiskBytes(dir);
    if (freeBytes && freeBytes < need)
        return fail(TrF(L"Not enough free disk space: {1} MB are needed, {2} MB are free.", {Mb(need), Mb(freeBytes)}));
    const int made = SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);  // with missing parents
    if (made != ERROR_SUCCESS && made != ERROR_ALREADY_EXISTS && made != ERROR_FILE_EXISTS)
        return fail(TrF(L"Cannot create the folder {1}.", {dir}));
    auto step = [&](uint64_t done, uint64_t total) {
        if (cancel && *cancel) return false;
        return !progress || progress(done, total);
    };
    return HttpDownloadToFile(FromUtf8(m.url), VoiceModelPath(dir, m), m.bytes, m.sha256, step, error);
}

bool DeleteVoiceModel(const std::wstring& dir, const VoiceModelInfo& m) {
    if (!PlainFileName(m.file)) return false;
    const std::wstring path = VoiceModelPath(dir, m);
    DeleteFileW((path + L".tmp").c_str());  // a left-over partial download
    return DeleteFileW(path.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND;
}

VoiceCatalog LoadVoiceCatalog(const std::wstring& dataDir) {
    VoiceCatalog c;
    std::string json;
    if (ReadFileBytes(dataDir + L"\\voice_models.json", json) && ParseVoiceCatalog(json, &c)) return c;
    c = {};
    ParseVoiceCatalog(DefaultVoiceCatalogJson(), &c);
    return c;
}

VoiceHardware ProbeVoiceHardware() {
    VoiceHardware hw;
    hw.logicalProcessors = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    if (hw.logicalProcessors == 0) {
        SYSTEM_INFO si{};
        GetSystemInfo(&si);
        hw.logicalProcessors = si.dwNumberOfProcessors;
    }
    hw.avx2 = IsProcessorFeaturePresent(PF_AVX2_INSTRUCTIONS_AVAILABLE) != 0;
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
        hw.ramTotal = ms.ullTotalPhys;
        hw.ramAvailable = ms.ullAvailPhys;
    }
    return hw;
}

uint64_t FreeDiskBytes(const std::wstring& dir) {
    // The folder may not exist yet: ask for the nearest parent that does.
    std::wstring d = dir;
    while (!d.empty()) {
        ULARGE_INTEGER avail{};
        if (GetDiskFreeSpaceExW(d.c_str(), &avail, nullptr, nullptr)) return avail.QuadPart;
        const size_t cut = d.find_last_of(L"\\/");
        if (cut == std::wstring::npos) break;
        d.resize(cut);
    }
    return 0;
}

}  // namespace gct
