// screen_capture.cpp
#include "screen_capture.hpp"

#include <d3d11.h>
#include <dxgi1_2.h>

#include <algorithm>
#include <cstring>

#ifdef _MSC_VER
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#endif

namespace gct {

namespace {
template <typename T>
void SafeRelease(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}
}  // namespace

// ===========================================================================
// DXGI Desktop Duplication
// ===========================================================================
struct ScreenCapture::Dxgi {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    IDXGIOutputDuplication* dup = nullptr;
    ID3D11Texture2D* staging = nullptr;
    RECT outputRect{};  // desktop coordinates of the duplicated output
    D3D11_TEXTURE2D_DESC stagingDesc{};
    Image last;         // last delivered image (returned when no new frame arrived)
    RECT lastArea{};

    ~Dxgi() { Reset(); }

    void Reset() {
        SafeRelease(staging);
        SafeRelease(dup);
        SafeRelease(context);
        SafeRelease(device);
        last = Image{};
    }

    // Finds the output that contains the area and starts duplicating it.
    bool Start(const RECT& area) {
        Reset();
        IDXGIFactory1* factory = nullptr;
        if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) return false;
        const POINT centre{(area.left + area.right) / 2, (area.top + area.bottom) / 2};
        bool ok = false;
        IDXGIAdapter1* adapter = nullptr;
        for (UINT a = 0; !ok && factory->EnumAdapters1(a, &adapter) != DXGI_ERROR_NOT_FOUND; ++a) {
            IDXGIOutput* output = nullptr;
            for (UINT o = 0; !ok && adapter->EnumOutputs(o, &output) != DXGI_ERROR_NOT_FOUND; ++o) {
                DXGI_OUTPUT_DESC desc{};
                output->GetDesc(&desc);
                if (desc.AttachedToDesktop && PtInRect(&desc.DesktopCoordinates, centre) &&
                    desc.Rotation <= DXGI_MODE_ROTATION_IDENTITY) {
                    ok = StartOn(adapter, output, desc.DesktopCoordinates);
                }
                SafeRelease(output);
            }
            SafeRelease(adapter);
        }
        SafeRelease(factory);
        return ok;
    }

    bool StartOn(IDXGIAdapter1* adapter, IDXGIOutput* output, const RECT& rect) {
        const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
        if (FAILED(D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels,
                                     3, D3D11_SDK_VERSION, &device, nullptr, &context)))
            return false;
        IDXGIOutput1* output1 = nullptr;
        if (FAILED(output->QueryInterface(__uuidof(IDXGIOutput1), reinterpret_cast<void**>(&output1)))) return false;
        const HRESULT hr = output1->DuplicateOutput(device, &dup);
        SafeRelease(output1);
        if (FAILED(hr)) return false;
        outputRect = rect;
        return true;
    }

    enum class Result { Ok, NoNewFrame, Error };

    Result Grab(const RECT& area, Image& out) {
        if (!dup && !Start(area)) return Result::Error;
        if (!PtInRect(&outputRect, POINT{area.left, area.top})) {  // area moved to another monitor
            if (!Start(area)) return Result::Error;
        }

        IDXGIResource* resource = nullptr;
        DXGI_OUTDUPL_FRAME_INFO info{};
        const HRESULT hr = dup->AcquireNextFrame(40, &info, &resource);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) {  // nothing changed on screen since the last frame
            if (!last.Empty() && EqualRect(&lastArea, &area)) {
                out = last;
                return Result::Ok;
            }
            return Result::NoNewFrame;
        }
        if (hr == DXGI_ERROR_ACCESS_LOST) {  // mode change, UAC prompt, fullscreen switch
            Reset();
            return Result::NoNewFrame;
        }
        if (FAILED(hr)) return Result::Error;

        bool ok = false;
        ID3D11Texture2D* frame = nullptr;
        if (SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&frame)))) {
            ok = CopyArea(frame, area, out);
            SafeRelease(frame);
        }
        SafeRelease(resource);
        dup->ReleaseFrame();
        if (!ok) return Result::Error;
        last = out;
        lastArea = area;
        return Result::Ok;
    }

    bool CopyArea(ID3D11Texture2D* frame, const RECT& area, Image& out) {
        D3D11_TEXTURE2D_DESC fdesc{};
        frame->GetDesc(&fdesc);
        if (fdesc.Format != DXGI_FORMAT_B8G8R8A8_UNORM) return false;

        // Area relative to the output, clipped.
        const LONG x0 = std::max(area.left - outputRect.left, 0L);
        const LONG y0 = std::max(area.top - outputRect.top, 0L);
        const LONG x1 = std::min(area.right - outputRect.left, static_cast<LONG>(fdesc.Width));
        const LONG y1 = std::min(area.bottom - outputRect.top, static_cast<LONG>(fdesc.Height));
        if (x1 <= x0 || y1 <= y0) return false;
        const UINT w = static_cast<UINT>(x1 - x0), h = static_cast<UINT>(y1 - y0);

        if (!staging || stagingDesc.Width != w || stagingDesc.Height != h) {
            SafeRelease(staging);
            stagingDesc = {};
            stagingDesc.Width = w;
            stagingDesc.Height = h;
            stagingDesc.MipLevels = 1;
            stagingDesc.ArraySize = 1;
            stagingDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            stagingDesc.SampleDesc.Count = 1;
            stagingDesc.Usage = D3D11_USAGE_STAGING;
            stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            if (FAILED(device->CreateTexture2D(&stagingDesc, nullptr, &staging))) return false;
        }
        D3D11_BOX box{static_cast<UINT>(x0), static_cast<UINT>(y0), 0, static_cast<UINT>(x1), static_cast<UINT>(y1), 1};
        context->CopySubresourceRegion(staging, 0, 0, 0, 0, frame, 0, &box);

        D3D11_MAPPED_SUBRESOURCE map{};
        if (FAILED(context->Map(staging, 0, D3D11_MAP_READ, 0, &map))) return false;
        out.width = static_cast<int>(w);
        out.height = static_cast<int>(h);
        out.bgra.resize(static_cast<size_t>(w) * h * 4);
        for (UINT y = 0; y < h; ++y)
            std::memcpy(&out.bgra[static_cast<size_t>(y) * w * 4], static_cast<const uint8_t*>(map.pData) + y * map.RowPitch,
                        static_cast<size_t>(w) * 4);
        context->Unmap(staging, 0);
        return true;
    }
};

// ===========================================================================
ScreenCapture::ScreenCapture() : dxgi_(std::make_unique<Dxgi>()) {}
ScreenCapture::~ScreenCapture() = default;

bool ScreenCapture::Grab(const RECT& area, Image& out) {
    if (area.right <= area.left || area.bottom <= area.top) return false;
    if (dxgiFailures_ < 3) {
        switch (dxgi_->Grab(area, out)) {
            case Dxgi::Result::Ok:
                usingDxgi_ = true;
                dxgiFailures_ = 0;
                return true;
            case Dxgi::Result::NoNewFrame:
                return false;  // try again next round
            case Dxgi::Result::Error:
                ++dxgiFailures_;  // after three errors in a row stay on GDI
                break;
        }
    }
    usingDxgi_ = false;
    return GrabGdi(area, out);
}

bool ScreenCapture::GrabGdi(const RECT& area, Image& out) {
    const int w = area.right - area.left, h = area.bottom - area.top;
    HDC screen = GetDC(nullptr);
    if (!screen) return false;
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;  // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    bool ok = false;
    if (bmp && bits) {
        HGDIOBJ old = SelectObject(mem, bmp);
        ok = BitBlt(mem, 0, 0, w, h, screen, area.left, area.top, SRCCOPY | CAPTUREBLT) != FALSE;
        GdiFlush();
        if (ok) {
            out.width = w;
            out.height = h;
            out.bgra.assign(static_cast<const uint8_t*>(bits), static_cast<const uint8_t*>(bits) + static_cast<size_t>(w) * h * 4);
            for (size_t i = 3; i < out.bgra.size(); i += 4) out.bgra[i] = 255;
        }
        SelectObject(mem, old);
    }
    if (bmp) DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    return ok;
}

}  // namespace gct
