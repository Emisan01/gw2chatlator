// screen_capture.hpp — grabs a screen area (the GW2 chat panel).
//
// DXGI Desktop Duplication first: it sees DirectX games in borderless mode
// reliably, also when Windows bypasses desktop composition. GDI BitBlt as
// fallback (remote desktop, Wine, old drivers). A method that failed is tried
// again every 30 s (HDR switched, UAC prompt, fullscreen switch pass by).
// Use from one thread only.
#pragma once

#include <windows.h>

#include <memory>
#include <string>

#include "core/image.hpp"

namespace gct {

// What the capture is doing right now – for the technical page.
struct CaptureStatus {
    bool hdr = false;        // the screen is in HDR mode and is duplicated in FP16 (exact conversion)
    int sdrWhiteNits = 0;    // its SDR white level ("SDR content brightness"), HDR only
    std::wstring fallback;   // why a better method is not in use ("" = none), e.g. "DXGI: AcquireNextFrame 0x887A0026"
};

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
    // HDR, SDR white level and the reason of a fallback.
    CaptureStatus Status() const;

    // Requests saving the next captured frame as an .f16 file.
    void SaveNextF16(const std::wstring& path);

    // After this many errors in a row a method is left for kRetryMs.
    static constexpr int kMaxFailures = 3;
    static constexpr ULONGLONG kRetryMs = 30000;

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
    ULONGLONG wgcRetryAt_ = 0, dxgiRetryAt_ = 0;  // GetTickCount64() when a left method is tried again
    std::wstring wgcError_, dxgiError_;           // why it was left (cleared when it works again)
};

}  // namespace gct
