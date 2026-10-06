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
    // Whether window capture (WGC) may be used. On Windows 10 Windows draws a
    // yellow frame around a window captured that way and it cannot be switched
    // off (only from Windows 11 on): there the screen is captured instead
    // (DXGI, no frame; our own window is left out of it by Windows).
    void SetUseWindowCapture(bool on);

    // True from Windows 11 on, where the yellow capture frame can be switched off.
    static bool BorderlessWindowCapture();

    // `area` in physical virtual-screen pixels. On success `out` holds BGRA.
    bool Grab(const RECT& area, Image& out);

    // "WGC", "DXGI" or "GDI" — for the diagnostics line.
    const wchar_t* Method() const;

private:
    struct Wgc;
    struct Dxgi;
    bool GrabGdi(const RECT& area, Image& out);

    HWND targetHwnd_ = nullptr;
    bool useWgc_ = true;
    std::unique_ptr<Wgc> wgc_;
    std::unique_ptr<Dxgi> dxgi_;
    bool usingWgc_ = false;
    bool usingDxgi_ = false;
    int wgcFailures_ = 0;
    int dxgiFailures_ = 0;
};

}  // namespace gct
