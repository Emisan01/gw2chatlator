#pragma once
// Keeps the tool's folders tidy: decides which files to delete so diagnostic
// captures, caches and leftover temp files never pile up. Portable: the
// Windows side lists the folder and deletes what this returns.

#include <cstdint>
#include <string>
#include <vector>

namespace gct {

struct FileEntry {
    std::wstring name;     // file name inside the folder (no path)
    uint64_t size = 0;     // bytes
    int64_t mtime = 0;     // seconds, any epoch (only compared with `now`)
};

struct CleanupLimits {
    size_t maxFiles = 0;        // 0 = unlimited
    uint64_t maxBytes = 0;      // 0 = unlimited
    int64_t maxAgeSeconds = 0;  // 0 = unlimited
};

// Files to delete so that what remains satisfies all limits. Oldest go first;
// age is applied before count and size. Only names for which `matches`
// returns true are considered (others are never touched).
std::vector<std::wstring> SelectForCleanup(const std::vector<FileEntry>& files, const CleanupLimits& limits,
                                           int64_t now, bool (*matches)(const std::wstring& name));

// Name filters used by the app.
bool IsCaptureFile(const std::wstring& name);    // capture_NN.txt / capture_NN_raw.bmp / capture_NN_ocr.bmp (+ .png)
bool IsStaleTempFile(const std::wstring& name);  // *.tmp left behind by an interrupted atomic write

}  // namespace gct
