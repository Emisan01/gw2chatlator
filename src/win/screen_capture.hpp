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

    // `area` in physical virtual-screen pixels. On success `out` holds BGRA.
    bool Grab(const RECT& area, Image& out);

    // "DXGI" or "GDI" — for the diagnostics line.
    const wchar_t* Method() const { return usingDxgi_ ? L"DXGI" : L"GDI"; }

private:
    struct Dxgi;
    bool GrabGdi(const RECT& area, Image& out);

    std::unique_ptr<Dxgi> dxgi_;
    bool usingDxgi_ = false;
    int dxgiFailures_ = 0;
};

}  // namespace gct
