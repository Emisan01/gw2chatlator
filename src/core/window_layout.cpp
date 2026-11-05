// window_layout.cpp — see window_layout.hpp.

#include "core/window_layout.hpp"

#include <algorithm>
#include <cmath>

namespace gct {

namespace {

float Width(WinMode mode, const WinAnchor& a, const WinSpace& s) {
    const float w = mode == WinMode::Button ? s.button : std::max(a.width, s.minW);
    return std::ceil(std::max(w, s.minSizeW));
}

float Height(WinMode mode, const WinAnchor& a, const WinSpace& s) {
    const float h = mode == WinMode::Button ? s.button : mode == WinMode::Fitted ? s.fittedH : std::max({a.height, s.needH, s.minH});
    return std::ceil(std::max(h, s.minSizeH));  // never smaller than ImGui would allow: its enlargement would move the corner
}

}  // namespace

WinRect LayoutWindow(WinMode mode, const WinAnchor& a, const WinSpace& s) {
    WinRect r;
    r.w = Width(mode, a, s);
    r.h = Height(mode, a, s);
    r.x = std::clamp(std::round(a.left), 0.0f, std::max(0.0f, std::floor(s.screenW - r.w)));
    const float bottom = std::min(std::round(a.bottom), std::floor(s.screenBottom));
    r.y = std::max(0.0f, bottom - r.h);
    return r;
}

WinAnchor AdoptRect(WinMode mode, const WinAnchor& a, const WinRect& now, const WinRect& was) {
    WinAnchor n = a;
    auto differs = [&](float x, float y) { return !a.known || std::fabs(x - y) > 0.5f; };
    if (differs(now.x, was.x)) n.left = std::round(now.x);
    if (differs(now.Bottom(), was.Bottom())) n.bottom = std::round(now.Bottom());
    if (mode != WinMode::Button && differs(now.w, was.w)) n.width = std::ceil(now.w);
    if (mode == WinMode::Big && differs(now.h, was.h)) n.height = std::ceil(now.h);
    // Resized at its left edge past the screen's edge: the right edge stays and the width ends at the screen – kept as
    // it was, the clamped left edge pushed the larger window out to the right.
    if (mode != WinMode::Button && now.x < 0 && differs(now.x, was.x) && differs(now.w, was.w)) {
        n.left = 0;
        n.width = std::ceil(now.x + now.w);
    }
    n.known = true;
    return n;
}

WinAnchor SnapAnchor(WinMode mode, const WinAnchor& a, const WinSpace& s, float t, float chatLeft) {
    const WinRect r = LayoutWindow(mode, a, s);
    float x = r.x, bottom = r.Bottom();
    if (chatLeft > 0 && std::fabs(x - chatLeft) < t) x = std::round(chatLeft);
    else if (x < t) x = 0;
    else if (x + r.w > s.screenW - t) x = std::max(0.0f, std::floor(s.screenW - r.w));
    if (r.y < t) bottom = r.h;  // onto the top edge
    else if (s.snapBottom > 0 && std::fabs(bottom - s.snapBottom) < t) bottom = std::floor(s.snapBottom);
    else if (bottom > s.screenBottom - t) bottom = std::floor(s.screenBottom);
    WinAnchor n = a;
    n.left = x;
    n.bottom = bottom;
    return n;
}

}  // namespace gct
