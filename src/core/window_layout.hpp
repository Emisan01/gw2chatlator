// window_layout.hpp — the main window's place as one small formula. Its whole state is four numbers: the bottom-left
// corner, the width and the big window's height (what you set by moving and resizing). Every frame its rect follows
// from that and the mode – the big window (at least as high as its content needs: a long input grows it, it shrinks
// back by itself), the fitted one (no chat: exactly as high as its parts), the minimised button. Nothing accumulates,
// so switching modes, growing and shrinking, minimising and opening again cannot move it. Portable, no ImGui.
#pragma once

namespace gct {

struct WinAnchor {
    float left = 0, bottom = 0;      // the bottom-left corner (whole pixels)
    float width = 520, height = 360;  // the big window as you sized it (the fitted one takes only its width)
    bool known = false;               // false until the first frame told where the window is
};

enum class WinMode { Big, Fitted, Button };

struct WinRect {
    float x = 0, y = 0, w = 0, h = 0;
    float Bottom() const { return y + h; }
};

struct WinSpace {                     // what the frame knows
    float screenW = 1920, screenBottom = 1080;  // the screen, and where GW2's bottom bar begins
    float minW = 380, minH = 240;      // the big window's minimum (the fitted one: minW only)
    float fittedH = 0;                 // the fitted window: the height of its parts (measured last frame)
    float needH = 0;                   // the big window: the height its content needs at least (measured last frame)
    float button = 32;                 // the minimised button
    float minSizeW = 32, minSizeH = 32;  // ImGui's own minimum for any window (it would enlarge a smaller one)
};

// The rect for this frame: whole pixels, on the screen (a corner off the screen is shown on it, not moved).
WinRect LayoutWindow(WinMode mode, const WinAnchor& a, const WinSpace& s);
// The window was moved or resized (by you, by ImGui's ini, by our own drag): `now` is its rect, `was` the one it had
// (what we set, or last frame's). Only what changed is taken over – AdoptRect(m, a, r, r) == a, so a window grown for a
// long input never keeps that height as yours. The first frame (a not known) takes all of it.
WinAnchor AdoptRect(WinMode mode, const WinAnchor& a, const WinRect& now, const WinRect& was);
// A move ended: near a screen edge (within `t` px) the corner goes onto it – for the button also onto the left edge of
// GW2's own chat (`chatLeft`, 0 = none); never below the bottom bar, never off the screen.
WinAnchor SnapAnchor(WinMode mode, const WinAnchor& a, const WinSpace& s, float t, float chatLeft);

}  // namespace gct
