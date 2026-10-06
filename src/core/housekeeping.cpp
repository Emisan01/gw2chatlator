#include "core/housekeeping.hpp"

#include <algorithm>
#include <cwctype>

namespace gct {
namespace {

bool EndsWithNoCase(const std::wstring& s, const wchar_t* suffix) {
    std::wstring suf(suffix);
    if (s.size() < suf.size()) return false;
    for (size_t i = 0; i < suf.size(); ++i)
        if (std::towlower(s[s.size() - suf.size() + i]) != std::towlower(suf[i])) return false;
    return true;
}

bool StartsWithNoCase(const std::wstring& s, const wchar_t* prefix) {
    std::wstring pre(prefix);
    if (s.size() < pre.size()) return false;
    for (size_t i = 0; i < pre.size(); ++i)
        if (std::towlower(s[i]) != std::towlower(pre[i])) return false;
    return true;
}

}  // namespace

std::vector<std::wstring> SelectForCleanup(const std::vector<FileEntry>& files, const CleanupLimits& limits,
                                           int64_t now, bool (*matches)(const std::wstring& name)) {
    std::vector<const FileEntry*> pool;
    for (const auto& f : files)
        if (!matches || matches(f.name)) pool.push_back(&f);
    // Oldest first; ties broken by name so the result is deterministic.
    std::sort(pool.begin(), pool.end(), [](const FileEntry* a, const FileEntry* b) {
        if (a->mtime != b->mtime) return a->mtime < b->mtime;
        return a->name < b->name;
    });

    std::vector<std::wstring> out;
    size_t first = 0;
    if (limits.maxAgeSeconds > 0) {
        while (first < pool.size() && now - pool[first]->mtime > limits.maxAgeSeconds) out.push_back(pool[first++]->name);
    }
    uint64_t total = 0;
    for (size_t i = first; i < pool.size(); ++i) total += pool[i]->size;
    size_t remaining = pool.size() - first;
    while (first < pool.size() && ((limits.maxFiles > 0 && remaining > limits.maxFiles) ||
                                   (limits.maxBytes > 0 && total > limits.maxBytes))) {
        total -= pool[first]->size;
        --remaining;
        out.push_back(pool[first++]->name);
    }
    return out;
}

bool IsCaptureFile(const std::wstring& name) {
    if (!StartsWithNoCase(name, L"capture_")) return false;
    return EndsWithNoCase(name, L".txt") || EndsWithNoCase(name, L".bmp") || EndsWithNoCase(name, L".png") ||
           EndsWithNoCase(name, L".pgm");
}

bool IsStaleTempFile(const std::wstring& name) {
    return EndsWithNoCase(name, L".tmp");
}

}  // namespace gct
