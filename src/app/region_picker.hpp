// region_picker.hpp — the player drags a frame around the GW2 chat once.
// A dimmed full-screen overlay; the selected area stays clear so you can see
// exactly which lines will be read. With an analyzer the frame snaps to the
// text lines after dragging and gets a traffic light (green: good, yellow:
// small text, red: no lines) and a preview of the first recognized lines;
// Enter takes it, dragging again redoes it.
#pragma once

#include <windows.h>

#include <functional>
#include <string>

#include "app/theme.hpp"

namespace gct {

struct PickCheck {
    RECT snapped{};        // screen pixels
    int quality = 0;       // 0 no lines found, 1 small text, 2 good
    std::wstring summary;  // "12 lines · text 20 px · good"
};
// Called on the UI thread right after the drag (fast: pixels only).
using PickAnalyzer = std::function<PickCheck(const RECT& rough)>;
// Called on a worker thread with the snapped frame: the first recognized lines.
using PickPreview = std::function<std::wstring(const RECT& snapped)>;

// Blocks with its own message loop. `out` in screen pixels. False when
// cancelled (Esc, right click) or the frame is too small.
bool PickScreenRegion(HINSTANCE inst, const Theme& theme, const std::wstring& hint, RECT* out,
                      PickAnalyzer analyze = nullptr, PickPreview preview = nullptr);

}  // namespace gct
