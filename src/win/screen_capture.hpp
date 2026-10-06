// screen_capture.hpp — grabs a screen area (the GW2 chat panel).
//
// DXGI Desktop Duplication first: it sees DirectX games in borderless mode
// reliably, also when Windows bypasses desktop composition. GDI BitBlt as
// fallback (remote desktop, Wine, old drivers). Use from one thread only.
#pragma once

#include <windows.h>

#include <memory>

#include "core/image.hpp"

namespace gct {

class ScreenCapture {
public:
    ScreenCapture();
    ~ScreenCapture();
    ScreenCapture(const ScreenCapture&) = delete;
    ScreenCapture& operator=(const ScreenCapture&) = delete;

    // Sets the window to capture (e.g. GW2 window). When set and supported,
    // WGC captures the window's DirectX backbuffer directly, ignoring overlays.
    void SetTarget(HWND hwnd);

    // `area` in physical virtual-screen pixels. On success `out` holds BGRA.
    bool Grab(const RECT& area, Image& out);

    // "WGC", "DXGI" or "GDI" — for the diagnostics line.
    const wchar_t* Method() const;

private:
    struct Wgc;
    struct Dxgi;
    bool GrabGdi(const RECT& area, Image& out);

    HWND targetHwnd_ = nullptr;
    std::unique_ptr<Wgc> wgc_;
    std::unique_ptr<Dxgi> dxgi_;
    bool usingWgc_ = false;
    bool usingDxgi_ = false;
    int wgcFailures_ = 0;
    int dxgiFailures_ = 0;
};

}  // namespace gct
