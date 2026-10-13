// cpu_cores.cpp
#include "win/cpu_cores.hpp"

#include <windows.h>

namespace gct {
namespace {

// SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION (winternl.h hides it): times in 100 ns, kernel time includes idle time.
struct ProcessorTimes {
    LARGE_INTEGER idle, kernel, user, reserved1[2];
    ULONG reserved2;
};
using QueryFn = LONG(WINAPI*)(int, void*, ULONG, ULONG*);
constexpr int kSystemProcessorPerformanceInformation = 8;

}  // namespace

void CoreLoad::Sample() {
    static const QueryFn query = reinterpret_cast<QueryFn>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQuerySystemInformation")));
    if (!query) return;
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    const DWORD n = si.dwNumberOfProcessors > 0 && si.dwNumberOfProcessors <= 64 ? si.dwNumberOfProcessors : 64;
    std::vector<ProcessorTimes> t(n);
    ULONG got = 0;
    if (query(kSystemProcessorPerformanceInformation, t.data(), static_cast<ULONG>(t.size() * sizeof(ProcessorTimes)),
              &got) != 0)
        return;
    const size_t count = got / sizeof(ProcessorTimes);
    std::vector<uint64_t> idle(count), total(count);
    for (size_t i = 0; i < count; ++i) {
        idle[i] = static_cast<uint64_t>(t[i].idle.QuadPart);
        total[i] = static_cast<uint64_t>(t[i].kernel.QuadPart + t[i].user.QuadPart);
    }
    if (idle_.size() == count) {
        busy_.assign(count, 0.0f);
        for (size_t i = 0; i < count; ++i) {
            const uint64_t dt = total[i] - total_[i], di = idle[i] - idle_[i];
            busy_[i] = dt > 0 && di <= dt ? 1.0f - static_cast<float>(di) / static_cast<float>(dt) : 0.0f;
        }
    }
    idle_.swap(idle);
    total_.swap(total);
}

const std::vector<std::vector<int>>& PhysicalCores() {
    static const std::vector<std::vector<int>> cores = [] {
        std::vector<std::vector<int>> out;
        DWORD bytes = 0;
        GetLogicalProcessorInformation(nullptr, &bytes);
        std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> info(bytes / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION) + 1);
        bytes = static_cast<DWORD>(info.size() * sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION));
        if (!GetLogicalProcessorInformation(info.data(), &bytes)) return out;
        const size_t n = bytes / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
        for (size_t i = 0; i < n; ++i) {
            if (info[i].Relationship != RelationProcessorCore) continue;
            std::vector<int> core;
            for (int b = 0; b < 64; ++b)
                if (info[i].ProcessorMask & (ULONG_PTR{1} << b)) core.push_back(b);
            if (!core.empty()) out.push_back(core);
        }
        return out;
    }();
    return cores;
}

uint64_t CoreMask(const std::vector<int>& logical) {
    uint64_t m = 0;
    for (int i : logical)
        if (i >= 0 && i < 64) m |= uint64_t{1} << i;
    return m;
}

}  // namespace gct
