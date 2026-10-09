// screen_capture.cpp
#include "screen_capture.hpp"

#include <d3d11.h>
#include <dwmapi.h>
#include <dxgi1_6.h>

#include <inspectable.h>
#include <winstring.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#ifdef _MSC_VER
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwmapi.lib")
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
// Windows Graphics Capture (WGC) ABI
// ===========================================================================
namespace wgc_abi {

struct SizeInt32 {
    INT32 Width;
    INT32 Height;
};

// 3628E81B-3CAC-4C60-B7F4-23CE0E0C3356
static const IID IID_IGraphicsCaptureItemInterop = {
    0x3628e81b, 0x3cac, 0x4c60, {0xb7, 0xf4, 0x23, 0xce, 0x0e, 0x0c, 0x33, 0x56}};
struct IGraphicsCaptureItemInterop : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE CreateForWindow(HWND window, REFIID riid, void** result) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateForMonitor(HMONITOR monitor, REFIID riid, void** result) = 0;
};

// 79c3f95b-31f7-4ec2-a464-632ef5d30760
static const IID IID_IGraphicsCaptureItem = {
    0x79c3f95b, 0x31f7, 0x4ec2, {0xa4, 0x64, 0x63, 0x2e, 0xf5, 0xd3, 0x07, 0x60}};
struct IGraphicsCaptureItem : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_DisplayName(HSTRING* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Size(SizeInt32* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_Closed(void* handler, INT64* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_Closed(INT64 token) = 0;
};

// 589b103f-6bbc-5df5-a991-02e28b3b66d5
static const IID IID_IDirect3D11CaptureFramePoolStatics2 = {
    0x589b103f, 0x6bbc, 0x5df5, {0xa9, 0x91, 0x02, 0xe2, 0x8b, 0x3b, 0x66, 0xd5}};
struct IDirect3D11CaptureFramePoolStatics2 : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE CreateFreeThreaded(
        IInspectable* device,
        INT32 pixelFormat,
        INT32 numberOfBuffers,
        SizeInt32 size,
        void** result) = 0;
};

// 24eb6d22-1975-422e-82e7-780dbd8ddf24
static const IID IID_IDirect3D11CaptureFramePool = {
    0x24eb6d22, 0x1975, 0x422e, {0x82, 0xe7, 0x78, 0x0d, 0xbd, 0x8d, 0xdf, 0x24}};
struct IDirect3D11CaptureFramePool : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE Recreate(
        IInspectable* device,
        INT32 pixelFormat,
        INT32 numberOfBuffers,
        SizeInt32 size) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryGetNextFrame(void** result) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_FrameArrived(void* handler, INT64* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_FrameArrived(INT64 token) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateCaptureSession(
        IGraphicsCaptureItem* item,
        void** result) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_DispatcherQueue(void** value) = 0;
};

// 2265b47d-9d94-41e8-ab56-93857472b013
static const IID IID_IGraphicsCaptureSession = {
    0x2265b47d, 0x9d94, 0x41e8, {0xab, 0x56, 0x93, 0x85, 0x74, 0x72, 0xb0, 0x13}};
struct IGraphicsCaptureSession : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE StartCapture() = 0;
};

// 2c39ae40-7d2e-5044-804e-8b6799d4cf9e
static const IID IID_IGraphicsCaptureSession2 = {
    0x2c39ae40, 0x7d2e, 0x5044, {0x80, 0x4e, 0x8b, 0x67, 0x99, 0xd4, 0xcf, 0x9e}};
struct IGraphicsCaptureSession2 : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_IsCursorCaptureEnabled(boolean* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsCursorCaptureEnabled(boolean value) = 0;
};

// f2cdd966-22ae-5ea1-9596-3a289344c3be
static const IID IID_IGraphicsCaptureSession3 = {
    0xf2cdd966, 0x22ae, 0x5ea1, {0x95, 0x96, 0x3a, 0x28, 0x93, 0x44, 0xc3, 0xbe}};
struct IGraphicsCaptureSession3 : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_IsBorderRequired(boolean* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsBorderRequired(boolean value) = 0;
};

// fa50c623-38da-4b32-acf3-fa9734ad800e
static const IID IID_IDirect3D11CaptureFrame = {
    0xfa50c623, 0x38da, 0x4b32, {0xac, 0xf3, 0xfa, 0x97, 0x34, 0xad, 0x80, 0x0e}};
struct IDirect3D11CaptureFrame : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Surface(void** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_SystemRelativeTime(INT64* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ContentSize(SizeInt32* value) = 0;
};

// a9b3d012-3df2-4ee3-b8d1-8695f457d3c1
static const IID IID_IDirect3DDxgiInterfaceAccess = {
    0xa9b3d012, 0x3df2, 0x4ee3, {0xb8, 0xd1, 0x86, 0x95, 0xf4, 0x57, 0xd3, 0xc1}};
struct IDirect3DDxgiInterfaceAccess : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetInterface(REFIID iid, void** p) = 0;
};

// 30d5a761-69de-45f2-ac1c-4e0f52e1d0e2
static const IID IID_IClosable = {
    0x30d5a761, 0x69de, 0x45f2, {0xac, 0x1c, 0x4e, 0x0f, 0x52, 0xe1, 0xd0, 0xe2}};
struct IClosable : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE Close() = 0;
};

}  // namespace wgc_abi

struct WinRtApi {
    using PFN_RoInitialize = HRESULT(WINAPI*)(INT32);
    using PFN_WindowsCreateString = HRESULT(WINAPI*)(PCNZWCH, UINT32, HSTRING*);
    using PFN_WindowsDeleteString = HRESULT(WINAPI*)(HSTRING);
    using PFN_RoGetActivationFactory = HRESULT(WINAPI*)(HSTRING, REFIID, void**);
    using PFN_CreateDirect3D11DeviceFromDXGIDevice = HRESULT(WINAPI*)(IDXGIDevice*, IInspectable**);

    PFN_RoInitialize RoInit = nullptr;
    PFN_WindowsCreateString CreateString = nullptr;
    PFN_WindowsDeleteString DeleteString = nullptr;
    PFN_RoGetActivationFactory GetFactory = nullptr;
    PFN_CreateDirect3D11DeviceFromDXGIDevice CreateD3DDevice = nullptr;

    bool Load() {
        if (RoInit && CreateString && DeleteString && GetFactory && CreateD3DDevice) return true;
        HMODULE cb = GetModuleHandleW(L"combase.dll");
        if (!cb) cb = LoadLibraryW(L"combase.dll");
        if (!cb) return false;
        HMODULE d3d = GetModuleHandleW(L"d3d11.dll");
        if (!d3d) d3d = LoadLibraryW(L"d3d11.dll");
        if (!d3d) return false;

        RoInit = reinterpret_cast<PFN_RoInitialize>(GetProcAddress(cb, "RoInitialize"));
        CreateString = reinterpret_cast<PFN_WindowsCreateString>(GetProcAddress(cb, "WindowsCreateString"));
        DeleteString = reinterpret_cast<PFN_WindowsDeleteString>(GetProcAddress(cb, "WindowsDeleteString"));
        GetFactory = reinterpret_cast<PFN_RoGetActivationFactory>(GetProcAddress(cb, "RoGetActivationFactory"));
        CreateD3DDevice = reinterpret_cast<PFN_CreateDirect3D11DeviceFromDXGIDevice>(
            GetProcAddress(d3d, "CreateDirect3D11DeviceFromDXGIDevice"));

        return RoInit && CreateString && DeleteString && GetFactory && CreateD3DDevice;
    }

    HRESULT ActivationFactory(const wchar_t* name, REFIID iid, void** out) {
        if (!CreateString || !GetFactory || !DeleteString) return E_FAIL;
        HSTRING hs = nullptr;
        HRESULT hr = CreateString(name, static_cast<UINT32>(wcslen(name)), &hs);
        if (FAILED(hr)) return hr;
        hr = GetFactory(hs, iid, out);
        DeleteString(hs);
        return hr;
    }
};

// ===========================================================================
// WGC Window Capture
// ===========================================================================
struct ScreenCapture::Wgc {
    WinRtApi api;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    IInspectable* winrtDevice = nullptr;
    wgc_abi::IGraphicsCaptureItem* item = nullptr;
    wgc_abi::IDirect3D11CaptureFramePool* framePool = nullptr;
    wgc_abi::IGraphicsCaptureSession* session = nullptr;
    ID3D11Texture2D* staging = nullptr;
    D3D11_TEXTURE2D_DESC stagingDesc{};
    HWND trackedHwnd = nullptr;
    wgc_abi::SizeInt32 currentSize{};
    Image last;
    RECT lastArea{};

    ~Wgc() { Reset(); }

    void CloseClosable(IUnknown* p) {
        if (!p) return;
        wgc_abi::IClosable* c = nullptr;
        if (SUCCEEDED(p->QueryInterface(wgc_abi::IID_IClosable, reinterpret_cast<void**>(&c)))) {
            c->Close();
            c->Release();
        }
    }

    void Reset() {
        SafeRelease(staging);
        if (session) {
            CloseClosable(session);
            SafeRelease(session);
        }
        if (framePool) {
            CloseClosable(framePool);
            SafeRelease(framePool);
        }
        SafeRelease(item);
        SafeRelease(winrtDevice);
        SafeRelease(context);
        SafeRelease(device);
        trackedHwnd = nullptr;
        currentSize = {};
        last = Image{};
    }

    bool Start(HWND hwnd) {
        Reset();
        if (!hwnd || !IsWindow(hwnd)) return false;
        if (!api.Load()) return false;
        api.RoInit(1 /* RO_INIT_MULTITHREADED */);

        const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                     D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 3,
                                     D3D11_SDK_VERSION, &device, nullptr, &context)))
            return false;

        IDXGIDevice* dxgiDevice = nullptr;
        if (FAILED(device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice)))) return false;
        const HRESULT hrDev = api.CreateD3DDevice(dxgiDevice, &winrtDevice);
        SafeRelease(dxgiDevice);
        if (FAILED(hrDev) || !winrtDevice) return false;

        wgc_abi::IGraphicsCaptureItemInterop* interop = nullptr;
        if (FAILED(api.ActivationFactory(L"Windows.Graphics.Capture.GraphicsCaptureItem",
                                         wgc_abi::IID_IGraphicsCaptureItemInterop,
                                         reinterpret_cast<void**>(&interop))))
            return false;

        const HRESULT hrItem = interop->CreateForWindow(hwnd, wgc_abi::IID_IGraphicsCaptureItem,
                                                        reinterpret_cast<void**>(&item));
        SafeRelease(interop);
        if (FAILED(hrItem) || !item) return false;
        if (FAILED(item->get_Size(&currentSize)) || currentSize.Width <= 0 || currentSize.Height <= 0) return false;

        wgc_abi::IDirect3D11CaptureFramePoolStatics2* poolStatics = nullptr;
        if (FAILED(api.ActivationFactory(L"Windows.Graphics.Capture.Direct3D11CaptureFramePool",
                                         wgc_abi::IID_IDirect3D11CaptureFramePoolStatics2,
                                         reinterpret_cast<void**>(&poolStatics))))
            return false;

        const HRESULT hrPool = poolStatics->CreateFreeThreaded(winrtDevice, 87 /* B8G8R8A8UIntNormalized */, 1,
                                                               currentSize,
                                                               reinterpret_cast<void**>(&framePool));
        SafeRelease(poolStatics);
        if (FAILED(hrPool) || !framePool) return false;

        if (FAILED(framePool->CreateCaptureSession(item, reinterpret_cast<void**>(&session))) || !session) return false;

        // Try disable border (Win10 build 20348+ / Win11)
        wgc_abi::IGraphicsCaptureSession3* session3 = nullptr;
        if (SUCCEEDED(session->QueryInterface(wgc_abi::IID_IGraphicsCaptureSession3, reinterpret_cast<void**>(&session3)))) {
            session3->put_IsBorderRequired(false);
            session3->Release();
        }

        // Try disable cursor capture
        wgc_abi::IGraphicsCaptureSession2* session2 = nullptr;
        if (SUCCEEDED(session->QueryInterface(wgc_abi::IID_IGraphicsCaptureSession2, reinterpret_cast<void**>(&session2)))) {
            session2->put_IsCursorCaptureEnabled(false);
            session2->Release();
        }

        if (FAILED(session->StartCapture())) return false;
        trackedHwnd = hwnd;
        return true;
    }

    enum class Result { Ok, NoNewFrame, Error };

    Result Grab(HWND hwnd, const RECT& area, Image& out) {
        if (!session || trackedHwnd != hwnd || !IsWindow(hwnd)) {
            if (!Start(hwnd)) return Result::Error;
        }

        wgc_abi::SizeInt32 newSize{};
        if (SUCCEEDED(item->get_Size(&newSize))) {
            if (newSize.Width > 0 && newSize.Height > 0 &&
                (newSize.Width != currentSize.Width || newSize.Height != currentSize.Height)) {
                currentSize = newSize;
                framePool->Recreate(winrtDevice, 87, 1, currentSize);
            }
        }

        wgc_abi::IDirect3D11CaptureFrame* frame = nullptr;
        const HRESULT hr = framePool->TryGetNextFrame(reinterpret_cast<void**>(&frame));
        if (FAILED(hr)) return Result::Error;
        if (!frame) {
            if (!last.Empty() && EqualRect(&lastArea, &area)) {
                out = last;
                return Result::Ok;
            }
            return Result::NoNewFrame;
        }

        IInspectable* surface = nullptr;
        frame->get_Surface(reinterpret_cast<void**>(&surface));
        if (!surface) {
            SafeRelease(frame);
            return Result::Error;
        }

        wgc_abi::IDirect3DDxgiInterfaceAccess* access = nullptr;
        const HRESULT hrAccess = surface->QueryInterface(wgc_abi::IID_IDirect3DDxgiInterfaceAccess,
                                                         reinterpret_cast<void**>(&access));
        SafeRelease(surface);
        if (FAILED(hrAccess) || !access) {
            SafeRelease(frame);
            return Result::Error;
        }

        ID3D11Texture2D* frameTexture = nullptr;
        const HRESULT hrTex = access->GetInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&frameTexture));
        SafeRelease(access);
        if (FAILED(hrTex) || !frameTexture) {
            SafeRelease(frame);
            return Result::Error;
        }

        // WGC delivers the window as it is seen: the extended frame bounds.
        // GetWindowRect also counts the invisible resize borders (about 7 px
        // left/right/bottom on Windows 10), which shifted the area in windowed mode.
        RECT winRect{};
        if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &winRect, sizeof(winRect))))
            GetWindowRect(hwnd, &winRect);
        const bool ok = CopyArea(frameTexture, winRect, area, out);
        SafeRelease(frameTexture);
        SafeRelease(frame);

        if (!ok) return Result::Error;
        last = out;
        lastArea = area;
        return Result::Ok;
    }

    bool CopyArea(ID3D11Texture2D* frame, const RECT& winRect, const RECT& area, Image& out) {
        D3D11_TEXTURE2D_DESC fdesc{};
        frame->GetDesc(&fdesc);
        if (fdesc.Format != DXGI_FORMAT_B8G8R8A8_UNORM) return false;

        const LONG x0 = std::max(area.left - winRect.left, 0L);
        const LONG y0 = std::max(area.top - winRect.top, 0L);
        const LONG x1 = std::min(area.right - winRect.left, static_cast<LONG>(fdesc.Width));
        const LONG y1 = std::min(area.bottom - winRect.top, static_cast<LONG>(fdesc.Height));
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
            std::memcpy(&out.bgra[static_cast<size_t>(y) * w * 4],
                        static_cast<const uint8_t*>(map.pData) + y * map.RowPitch,
                        static_cast<size_t>(w) * 4);
        context->Unmap(staging, 0);
        return true;
    }
};

// ===========================================================================
// DXGI Desktop Duplication
// ===========================================================================
// HDR: with Windows HDR on, the desktop is linear FP16 (scRGB, 1.0 = 80 nits) and SDR content like GW2 sits in it
// scaled by the SDR white level (the user's "SDR brightness"). An 8-bit duplication lets Windows squeeze that in its
// own way (washed-out letters), so on an HDR screen we take FP16 and convert exactly: / white, clamp, sRGB curve.
// SDR screens keep the plain 8-bit path - no extra work at all.
namespace {

// The SDR white level of the monitor `deviceName` (GDI name of the output), as a factor of 80 nits; 1 if unknown.
double SdrWhiteFactor(const wchar_t* deviceName) {
    UINT32 nPaths = 0, nModes = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &nPaths, &nModes) != ERROR_SUCCESS) return 1.0;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(nPaths);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(nModes);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &nPaths, paths.data(), &nModes, modes.data(), nullptr) != ERROR_SUCCESS)
        return 1.0;
    for (UINT32 i = 0; i < nPaths; ++i) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
        source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        source.header.size = sizeof source;
        source.header.adapterId = paths[i].sourceInfo.adapterId;
        source.header.id = paths[i].sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS || wcscmp(source.viewGdiDeviceName, deviceName) != 0)
            continue;
        DISPLAYCONFIG_SDR_WHITE_LEVEL white{};
        white.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL;
        white.header.size = sizeof white;
        white.header.adapterId = paths[i].targetInfo.adapterId;
        white.header.id = paths[i].targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&white.header) == ERROR_SUCCESS && white.SDRWhiteLevel > 0)
            return white.SDRWhiteLevel / 1000.0;  // 1000 = 80 nits
    }
    return 1.0;
}

float HalfToFloat(uint16_t h) {
    const int sign = h >> 15, exp = (h >> 10) & 31, man = h & 1023;
    const float v = exp == 0    ? std::ldexp(static_cast<float>(man), -24)
                    : exp == 31 ? 65504.0f
                                : std::ldexp(static_cast<float>(man | 1024), exp - 25);
    return sign ? -v : v;
}

// Every half value -> 8-bit sRGB of the SDR picture: / white, clamp 0..1, sRGB curve. 64 KB, built per start.
std::vector<uint8_t> HdrLut(double white) {
    std::vector<uint8_t> lut(65536);
    for (uint32_t i = 0; i < 65536; ++i) {
        double v = std::clamp(HalfToFloat(static_cast<uint16_t>(i)) / white, 0.0, 1.0);
        v = v <= 0.0031308 ? 12.92 * v : 1.055 * std::pow(v, 1.0 / 2.4) - 0.055;
        lut[i] = static_cast<uint8_t>(std::lround(v * 255.0));
    }
    return lut;
}

}  // namespace

struct ScreenCapture::Dxgi {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    IDXGIOutputDuplication* dup = nullptr;
    ID3D11Texture2D* staging = nullptr;
    RECT outputRect{};  // desktop coordinates of the duplicated output
    D3D11_TEXTURE2D_DESC stagingDesc{};
    std::vector<uint8_t> hdrLut;  // filled only while duplicating an HDR screen in FP16
    Image last;         // last delivered image (returned when no new frame arrived)
    RECT lastArea{};

    ~Dxgi() { Reset(); }

    void Reset() {
        SafeRelease(staging);
        SafeRelease(dup);
        hdrLut.clear();
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
        // HDR screen: FP16 duplication + exact conversion (decided here; Start runs again after every mode change).
        IDXGIOutput6* output6 = nullptr;
        if (SUCCEEDED(output->QueryInterface(__uuidof(IDXGIOutput6), reinterpret_cast<void**>(&output6)))) {
            DXGI_OUTPUT_DESC1 d1{};
            if (SUCCEEDED(output6->GetDesc1(&d1)) && d1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020) {
                const DXGI_FORMAT formats[] = {DXGI_FORMAT_R16G16B16A16_FLOAT};
                if (SUCCEEDED(output6->DuplicateOutput1(device, 0, 1, formats, &dup)))
                    hdrLut = HdrLut(SdrWhiteFactor(d1.DeviceName));
            }
            SafeRelease(output6);
        }
        if (!dup) {
            IDXGIOutput1* output1 = nullptr;
            if (FAILED(output->QueryInterface(__uuidof(IDXGIOutput1), reinterpret_cast<void**>(&output1)))) return false;
            const HRESULT hr = output1->DuplicateOutput(device, &dup);
            SafeRelease(output1);
            if (FAILED(hr)) return false;
        }
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
        const bool hdr = fdesc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT && hdrLut.size() == 65536;
        if (fdesc.Format != DXGI_FORMAT_B8G8R8A8_UNORM && !hdr) return false;

        // Area relative to the output, clipped.
        const LONG x0 = std::max(area.left - outputRect.left, 0L);
        const LONG y0 = std::max(area.top - outputRect.top, 0L);
        const LONG x1 = std::min(area.right - outputRect.left, static_cast<LONG>(fdesc.Width));
        const LONG y1 = std::min(area.bottom - outputRect.top, static_cast<LONG>(fdesc.Height));
        if (x1 <= x0 || y1 <= y0) return false;
        const UINT w = static_cast<UINT>(x1 - x0), h = static_cast<UINT>(y1 - y0);

        if (!staging || stagingDesc.Width != w || stagingDesc.Height != h || stagingDesc.Format != fdesc.Format) {
            SafeRelease(staging);
            stagingDesc = {};
            stagingDesc.Width = w;
            stagingDesc.Height = h;
            stagingDesc.MipLevels = 1;
            stagingDesc.ArraySize = 1;
            stagingDesc.Format = fdesc.Format;
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
        if (hdr) {  // RGBA half -> BGRA8 through the table
            for (UINT y = 0; y < h; ++y) {
                const uint16_t* src =
                    reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(map.pData) + y * map.RowPitch);
                uint8_t* dst = &out.bgra[static_cast<size_t>(y) * w * 4];
                for (UINT x = 0; x < w; ++x, src += 4, dst += 4) {
                    dst[0] = hdrLut[src[2]];
                    dst[1] = hdrLut[src[1]];
                    dst[2] = hdrLut[src[0]];
                    dst[3] = 255;
                }
            }
        } else {
            for (UINT y = 0; y < h; ++y)
                std::memcpy(&out.bgra[static_cast<size_t>(y) * w * 4],
                            static_cast<const uint8_t*>(map.pData) + y * map.RowPitch, static_cast<size_t>(w) * 4);
        }
        context->Unmap(staging, 0);
        return true;
    }
};

// ===========================================================================
ScreenCapture::ScreenCapture() : dxgi_(std::make_unique<Dxgi>()) {}
ScreenCapture::~ScreenCapture() = default;

void ScreenCapture::SetTarget(HWND hwnd) {
    if (targetHwnd_ == hwnd) return;
    targetHwnd_ = hwnd;
    wgcFailures_ = 0;
    if (wgc_) wgc_->Reset();
}

const wchar_t* ScreenCapture::Method() const {
    if (usingWgc_) return L"WGC";
    if (usingDxgi_) return L"DXGI";
    return L"GDI";
}

void ScreenCapture::SetUseWindowCapture(bool on) {
    if (useWgc_ == on) return;
    useWgc_ = on;
    usingWgc_ = false;
    if (wgc_) wgc_->Reset();
}

bool ScreenCapture::BorderlessWindowCapture() {
    // GetVersionEx reports an old version to unmanifested callers; ntdll does not.
    using Fn = LONG(WINAPI*)(OSVERSIONINFOW*);
    static const DWORD build = [] {
        OSVERSIONINFOW v{};
        v.dwOSVersionInfoSize = sizeof(v);
        auto fn = reinterpret_cast<Fn>(
            reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion")));
        return fn && fn(&v) == 0 ? v.dwBuildNumber : 0ul;
    }();
    return build >= 22000;  // Windows 11
}

bool ScreenCapture::Grab(const RECT& area, Image& out) {
    if (area.right <= area.left || area.bottom <= area.top) return false;
    if (useWgc_ && targetHwnd_ && IsWindow(targetHwnd_) && wgcFailures_ < 3) {
        if (!wgc_) wgc_ = std::make_unique<Wgc>();
        switch (wgc_->Grab(targetHwnd_, area, out)) {
            case Wgc::Result::Ok:
                usingWgc_ = true;
                usingDxgi_ = false;
                wgcFailures_ = 0;
                return true;
            case Wgc::Result::NoNewFrame:
                usingWgc_ = true;
                return false;  // try again next round
            case Wgc::Result::Error:
                ++wgcFailures_;
                break;
        }
    }
    usingWgc_ = false;
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
