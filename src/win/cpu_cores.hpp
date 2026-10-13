// cpu_cores.hpp — how busy each logical processor is right now (system-wide numbers from Windows; no handle to any
// other process, invariant 1) and which logical processors share a physical core. Used to keep the voice recognizer
// off the cores the game is busy on (core/voice PickVoiceCores).
#pragma once

#include <cstdint>
#include <vector>

namespace gct {

class CoreLoad {
public:
    // Call regularly (every few hundred ms); the load is the share since the previous call.
    void Sample();
    // 0..1 per logical processor of the first processor group; empty until two samples were taken.
    const std::vector<float>& Busy() const { return busy_; }

private:
    std::vector<uint64_t> idle_, total_;
    std::vector<float> busy_;
};

// The logical processors (first group, < 64) of each physical core.
const std::vector<std::vector<int>>& PhysicalCores();

// Bit mask of logical processors.
uint64_t CoreMask(const std::vector<int>& logical);

}  // namespace gct
