#pragma once
// Windows side of core/housekeeping: list a folder and delete what the
// cleanup policy selects. Only plain files directly inside `dir` are touched.

#include <string>
#include <vector>

#include "core/housekeeping.hpp"

namespace gct {

std::vector<FileEntry> ListFolder(const std::wstring& dir);

// Returns the number of files deleted.
size_t CleanFolder(const std::wstring& dir, const CleanupLimits& limits, bool (*matches)(const std::wstring& name));

// Seconds since 1601 (FILETIME epoch), comparable with FileEntry::mtime.
int64_t NowSeconds();

}  // namespace gct
