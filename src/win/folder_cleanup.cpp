#include "win/folder_cleanup.hpp"

#include <windows.h>

namespace gct {
namespace {

int64_t FileTimeSeconds(const FILETIME& ft) {
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return static_cast<int64_t>(u.QuadPart / 10000000ULL);
}

}  // namespace

int64_t NowSeconds() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return FileTimeSeconds(ft);
}

std::vector<FileEntry> ListFolder(const std::wstring& dir) {
    std::vector<FileEntry> out;
    if (dir.empty()) return out;
    std::wstring pattern = dir + (dir.back() == L'\\' ? L"*" : L"\\*");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (fd.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) continue;
        FileEntry e;
        e.name = fd.cFileName;
        e.size = (static_cast<uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
        e.mtime = FileTimeSeconds(fd.ftLastWriteTime);
        out.push_back(std::move(e));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return out;
}

size_t CleanFolder(const std::wstring& dir, const CleanupLimits& limits, bool (*matches)(const std::wstring& name)) {
    size_t deleted = 0;
    for (const auto& name : SelectForCleanup(ListFolder(dir), limits, NowSeconds(), matches)) {
        std::wstring path = dir + (dir.back() == L'\\' ? L"" : L"\\") + name;
        if (DeleteFileW(path.c_str())) ++deleted;
    }
    return deleted;
}

}  // namespace gct
