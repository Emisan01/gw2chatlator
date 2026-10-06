// region_picker.hpp — the player drags a frame around the GW2 chat once.
// A dimmed full-screen overlay; the selected area stays clear so you can see
// exactly which lines will be read.
#pragma once

#include <windows.h>

#include <string>

#include "app/theme.hpp"

namespace gct {

// Blocks with its own message loop. `out` in screen pixels. False when
// cancelled (Esc, right click) or the frame is too small.
bool PickScreenRegion(HINSTANCE inst, const Theme& theme, const std::wstring& hint, RECT* out);

}  // namespace gct
