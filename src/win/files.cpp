// files.cpp
#include "files.hpp"

#include <windows.h>

#include <iterator>

namespace gct {

std::wstring ExeDir() {
    wchar_t buf[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    const std::wstring p(buf, n);
    const size_t slash = p.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : p.substr(0, slash);
}

static bool IsWritableDir(const std::wstring& dir) {
    const std::wstring probe = dir + L"\\.gct-write-test";
    HANDLE h = CreateFileW(probe.c_str(), GENERIC_WRITE | DELETE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    return true;
}

std::wstring DataDir() {
    static const std::wstring dir = [] {
        const std::wstring exe = ExeDir();
        if (IsWritableDir(exe)) return exe;
        wchar_t buf[MAX_PATH * 2];
        const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, static_cast<DWORD>(std::size(buf)));
        std::wstring base = (n > 0 && n < std::size(buf)) ? std::wstring(buf, n) : exe;
        std::wstring d = base + L"\\GW2ChatTranslator";
        EnsureDir(d);
        return d;
    }();
    return dir;
}

bool EnsureDir(const std::wstring& path) {
    if (CreateDirectoryW(path.c_str(), nullptr)) return true;
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

bool ReadFileBytes(const std::wstring& path, std::string& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(h, &size) && size.QuadPart < (64LL << 20);
    if (ok) {
        out.resize(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        ok = out.empty() || (ReadFile(h, out.data(), static_cast<DWORD>(out.size()), &read, nullptr) &&
                             read == out.size());
    }
    CloseHandle(h);
    return ok;
}

bool WriteFileAtomic(const std::wstring& path, const std::string& data) {
    const std::wstring tmp = path + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = data.empty() || (WriteFile(h, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) &&
                                     written == data.size());
    CloseHandle(h);
    if (!ok) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
}

bool AppendFileBytes(const std::wstring& path, const std::string& data) {
    HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = WriteFile(h, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) &&
                    written == data.size();
    CloseHandle(h);
    return ok;
}

double FileAgeDays(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA a{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &a)) return -1.0;
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER t1{}, t2{};
    t1.LowPart = a.ftLastWriteTime.dwLowDateTime;
    t1.HighPart = a.ftLastWriteTime.dwHighDateTime;
    t2.LowPart = now.dwLowDateTime;
    t2.HighPart = now.dwHighDateTime;
    if (t2.QuadPart <= t1.QuadPart) return 0.0;
    return static_cast<double>(t2.QuadPart - t1.QuadPart) / (10'000'000.0 * 60 * 60 * 24);
}

}  // namespace gct
