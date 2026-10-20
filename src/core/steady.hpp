// steady.hpp — a number for the eye (Options > Technical): smoothed over about the last 20 samples and shown at most
// once per period, so it can be read. Its equations in core_tests (always within what came in, changes at most once per
// period, settles on a constant input). Portable, header only.
#pragma once

#include <cstdint>

namespace gct {

struct SteadyNumber {
    double avg = 0, shown = 0;
    uint64_t at = 0;
    bool started = false;
    double Next(double v, uint64_t nowMs, uint64_t periodMs = 250) {
        if (!started) {
            avg = shown = v;
            at = nowMs;
            started = true;
            return shown;
        }
        avg = avg * 0.9 + v * 0.1;
        if (nowMs - at >= periodMs) {
            shown = avg;
            at = nowMs;
        }
        return shown;
    }
};

}  // namespace gct
