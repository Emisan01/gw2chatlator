// kb_cost.hpp — the frame profiler: what we cost on GW2's render thread, by part (kb_state.cpp).
// Included by kb_shared.hpp at its place (it relies on what stands before it there) – include that, not this.
#pragma once

namespace kb {

// What our frame costs on GW2's render thread, by part (Options > Technical; the smoke test checks the parts add up to
// the whole). A few QueryPerformanceCounter calls per frame, rolling averages – no measurable cost of its own.
enum CostPart { kCostPre, kCostOptions, kCostWindow, kCostHeader, kCostChat, kCostInput, kCostFooter, kCostParts };
struct FrameCosts {
    double ms[kCostParts] = {};  // rolling average per part
    double totalMs = 0;          // rolling average of the whole RenderWindow
    double lineUs = 0;           // average per chat line arriving (OnChatEvents)
    long long lines = 0;
    double sumMs[kCostParts] = {};  // exact sums since CostReset (the smoke test's equation: like with like)
    double sumTotalMs = 0;
    long long frames = 0;
};
struct CostFrame {  // RenderWindow: starts the frame, ends it on every return
    CostFrame();
    ~CostFrame();
};
void CostMark(int part);  // the running part ends, `part` starts
void CostLine(double us);
FrameCosts CostsNow();
void CostReset();  // the sums start again

}  // namespace kb
