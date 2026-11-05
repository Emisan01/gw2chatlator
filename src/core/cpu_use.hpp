// cpu_use.hpp — what we cost the CPU, in percent of all the PC's processors (12 logical processors: one busy = 8.3 %).
#pragma once

#include <algorithm>
#include <cstdint>

namespace gct {

struct CpuUse {
    float helper = 0, voice = 0, threads = 0, draw = 0;  // ours: the translator's helper, the voice helper, our worker threads, our drawing
    float ours = 0;                                      // their sum
    float game = 0;                                      // the game without our threads and drawing
    float pc = 0;                                        // the whole PC
    void KeepHighest(const CpuUse& now) {  // every part's own highest
        helper = std::max(helper, now.helper);
        voice = std::max(voice, now.voice);
        threads = std::max(threads, now.threads);
        draw = std::max(draw, now.draw);
        ours = std::max(ours, now.ours);
        game = std::max(game, now.game);
        pc = std::max(pc, now.pc);
    }
};

// CPU time used in a stretch of time, both in the same unit, as a share of all processors.
inline float CpuPercent(uint64_t busy, uint64_t wall, unsigned logical) {
    if (wall == 0 || logical == 0) return 0.0f;
    return std::clamp(static_cast<float>(100.0 * static_cast<double>(busy) / static_cast<double>(wall) / logical), 0.0f, 100.0f);
}

// How far a counter went; a process started anew counts from zero.
inline uint64_t CpuDelta(uint64_t now, uint64_t before) { return now >= before ? now - before : now; }

}  // namespace gct
