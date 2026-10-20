// kb_waypoint.cpp — the Waypoint/POI click, the "quantum macro" (optional: [Nexus] WaypointPoiClick, off by default).
// A click on a waypoint or point of interest (never anything else) in our chat clicks the original link in GW2's own
// chat – the game then does what it always does (the map opens and flies to the spot). No hook, no game memory: GW2's
// chat line is opened, one frame of the game is read (the swap chain's back buffer, before any add-on draws, only in
// memory), the link found by its name (Decoder Ring, Windows' own text recognition) and clicked once – one click of
// yours, one click of ours, every step only while GW2 is in front. Not found: the chat line stays open and you click
// it there, as before.
// A test key (Nexus keybinds, unbound): one click where the mouse stands – does GW2 take a synthetic click?

#include "kb_shared.hpp"
#include "core/link_find.hpp"
#include "win/ocr.hpp"

#include <d3d11.h>

#include <random>

namespace kb {

namespace {

std::thread g_macroThread;
std::atomic<int> g_synthetic{0};  // every synthetic mouse input sent (the self-test: none without GW2)

void Send(INPUT& in) {
    SendInput(1, &in, sizeof(in));
    ++g_synthetic;
}

std::atomic<bool> g_macroBusy{false}, g_macroStop{false};
std::atomic<bool> g_passThrough{false};  // our window lets the mouse through (kb_window: ImGuiWindowFlags_NoInputs)
std::atomic<unsigned> g_frames{0};       // frames drawn (WaypointFrameTick): waits counted in frames, not guessed

// The frame (render thread): asked → copied into a staging texture → read two frames later (no wait on the GPU).
enum GrabState { kGrabIdle, kGrabAsked, kGrabCopied, kGrabDone, kGrabFailed };
std::atomic<int> g_grab{kGrabIdle};
ID3D11Texture2D* g_staging = nullptr;
DXGI_FORMAT g_stagingFormat = DXGI_FORMAT_UNKNOWN;
int g_grabFrames = 0;
std::mutex g_grabMu;
OcrImage g_frame;  // g_grabMu

int FrameMs() { return 1000 / std::max(10, static_cast<int>(g_fps)); }
struct Pauses {  // the user's from-to ranges (ms), copied when a click starts
    int before[2], held[2], after[2];
};

Pauses CurrentPauses() {
    std::lock_guard<std::mutex> lock(g_mu);
    Pauses p;
    std::memcpy(p.before, g_nx.wpBefore, sizeof(p.before));
    std::memcpy(p.held, g_nx.wpHeld, sizeof(p.held));
    std::memcpy(p.after, g_nx.wpAfter, sizeof(p.after));
    return p;
}

int DiceMs(const int* range) {  // every pause thrown anew within its range (the user's "dice logic", as since GW1)
    thread_local std::mt19937 dice(static_cast<unsigned>(GetTickCount64() ^ GetCurrentThreadId()));
    return std::uniform_int_distribution<int>(std::min(range[0], range[1]), std::max(range[0], range[1]))(dice);
}

void Say(const std::wstring& text, ULONGLONG ms) {
    std::lock_guard<std::mutex> lock(g_mu);
    SetStatus(text, ms);
}

bool WaitFrames(unsigned n) {  // until the render thread drew n more frames (at most half a second)
    const unsigned from = g_frames;
    for (int waited = 0; waited < 500 && !g_macroStop; waited += 2) {
        if (g_frames - from >= n) return true;
        Sleep(2);
    }
    return false;
}

bool ChatLineOpen() {
    MumbleLink mumble;
    const MumbleState ms = mumble.Read();
    return ms.live && ms.TextboxHasFocus();
}

void MoveCursor(POINT at) {  // as a real mouse would: absolute, over the whole virtual desktop
    const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN), vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int vw = std::max(1, GetSystemMetrics(SM_CXVIRTUALSCREEN) - 1), vh = std::max(1, GetSystemMetrics(SM_CYVIRTUALSCREEN) - 1);
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dx = static_cast<LONG>((static_cast<long long>(at.x - vx) * 65535) / vw);
    in.mi.dy = static_cast<LONG>((static_cast<long long>(at.y - vy) * 65535) / vh);
    in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    Send(in);
}

// One left click at `at` (screen pixels): our window lets the mouse through (two frames, so ImGui no longer holds it),
// the cursor goes there, a pause, the button down, held, up, a pause, the cursor back – the three pauses thrown within
// the user's ranges. Every step only while GW2 is in front (invariant 3). Right before the button goes down: the point
// lies on GW2's own window (no other window over it), the cursor is still where we put it (else your hand took over:
// no click, the cursor stays yours), no button of yours is held, and – for a waypoint – GW2's chat line is still open.
// A button that went down always comes up again.
bool SyntheticClick(POINT at, bool needChatLine) {
    const Pauses pause = CurrentPauses();
    const HWND gw2 = FindGw2Window(GetCurrentProcessId());
    auto front = [gw2] { return gw2 && GetForegroundWindow() == gw2; };
    POINT was{};
    GetCursorPos(&was);
    if (!front()) return false;
    struct Through {  // our window lets the mouse through only while this click runs
        Through() { g_passThrough = true; }
        ~Through() { g_passThrough = false; }
    } through;
    if (!WaitFrames(2) || !front()) return false;
    MoveCursor(at);
    Sleep(DiceMs(pause.before));  // GW2 sees the cursor there before the click
    POINT now{};
    GetCursorPos(&now);
    if (std::abs(now.x - at.x) > 2 || std::abs(now.y - at.y) > 2) return false;  // your hand moved it: yours
    const HWND under = WindowFromPoint(at);
    const bool buttonHeld = (GetAsyncKeyState(VK_LBUTTON) | GetAsyncKeyState(VK_RBUTTON) | GetAsyncKeyState(VK_MBUTTON)) & 0x8000;
    if (!front() || !under || GetAncestor(under, GA_ROOT) != gw2 || buttonHeld || (needChatLine && !ChatLineOpen())) {
        MoveCursor(was);
        return false;
    }
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    Send(in);
    Sleep(DiceMs(pause.held));
    in.mi.dwFlags = MOUSEEVENTF_LEFTUP;
    Send(in);
    Sleep(DiceMs(pause.after));
    MoveCursor(was);
    return true;
}

void StartMacro(std::function<void()> run) {  // one at a time, on its own thread (never the game's, never the worker)
    if (g_macroBusy.exchange(true)) return;
    if (g_macroThread.joinable()) g_macroThread.join();
    g_macroStop = false;
    g_macroThread = std::thread([run = std::move(run)] {
        run();
        g_macroBusy = false;
    });
}

bool GrabFrame(OcrImage& out) {  // asks the render thread for one frame of the game; waits at most a second
    g_grab = kGrabAsked;
    for (int waited = 0; waited < 1000 && !g_macroStop; waited += 10) {
        const int st = g_grab;
        if (st == kGrabDone || st == kGrabFailed) break;
        Sleep(10);
    }
    const bool ok = g_grab == kGrabDone;
    if (ok) {
        std::lock_guard<std::mutex> lock(g_grabMu);
        out = std::move(g_frame);
        g_frame = OcrImage{};
    }
    g_grab = kGrabIdle;
    return ok;
}

OcrImage Region(const OcrImage& src, int x, int y, int w, int h, int scale) {  // a part of the frame, enlarged
    OcrImage out;
    out.width = w * scale;
    out.height = h * scale;
    out.bgra.resize(static_cast<size_t>(out.width) * out.height * 4);
    for (int oy = 0; oy < out.height; ++oy)
        for (int ox = 0; ox < out.width; ++ox) {
            const int sx = x + ox / scale, sy = y + oy / scale;
            std::memcpy(&out.bgra[(static_cast<size_t>(oy) * out.width + ox) * 4],
                        &src.bgra[(static_cast<size_t>(sy) * src.width + sx) * 4], 4);
        }
    return out;
}

// Reads the frame in tiles the recognizer accepts, at `scale` (GW2's chat text is small), inside x0..x1, y0..y1.
void ReadText(OcrEngine& ocr, const OcrImage& frame, int x0, int y0, int x1, int y1, int scale, std::vector<TextRow>& rows) {
    const int maxDim = ocr.MaxImageDimension() > 0 ? ocr.MaxImageDimension() : 2600;
    const int tile = std::max(256, maxDim / scale - 64);
    for (int ty = y0; ty < y1; ty += tile - 48) {
        for (int tx = x0; tx < x1; tx += tile - 48) {
            const int w = std::min(tile, x1 - tx), h = std::min(tile, y1 - ty);
            if (w < 16 || h < 16) continue;
            std::vector<OcrTextLine> lines;
            std::string error;
            if (!ocr.Recognize(Region(frame, tx, ty, w, h, scale), lines, &error)) continue;
            for (const OcrTextLine& l : lines) {
                TextRow row;
                for (const OcrWordBox& b : l.words)
                    row.words.push_back({b.text, tx + b.x / scale, ty + b.y / scale, b.w / scale, b.h / scale});
                rows.push_back(std::move(row));
            }
        }
    }
}

void RunWaypoint(ChatLineHint line) {
    const std::wstring& label = line.label;
    // 1. GW2's chat line was asked to open (HandleChatLinkClick): MumbleLink read here every 10 ms (the render
    //    thread looks only every 250 ms), then a few frames for its lines.
    {
        MumbleLink mumble;
        for (int waited = 0; waited < 1500 && !g_macroStop; waited += 10) {
            const MumbleState ms = mumble.Read();
            if (ms.live && ms.TextboxHasFocus()) break;
            Sleep(10);
        }
    }
    Sleep(FrameMs() * 3);
    if (g_macroStop) return;
    // 2. One frame of the game, only in memory.
    OcrImage frame;
    if (!GrabFrame(frame)) {
        Say(Tr(L"The waypoint is copied – click it in GW2's chat (the frame could not be read)."), 12000);
        return;
    }
    // 3. The very line: its link's name, then who wrote it and what else it says (core/link_find) – better no click
    //    than a wrong one. GW2's usual chat corner enlarged first, then the whole frame.
    OcrEngine ocr;
    std::string error;
    TextBox hit;
    bool found = false;
    auto find = [&](const std::vector<TextRow>& rows) {
        return line.text.empty() ? FindLinkText(rows, label, &hit) : FindChatLink(rows, line, &hit);
    };
    if (ocr.Init(&error)) {
        std::vector<TextRow> rows;
        ReadText(ocr, frame, 0, frame.height * 2 / 5, frame.width / 2, frame.height, 2, rows);  // GW2's chat corner
        found = find(rows);
        if (!found && !g_macroStop) {  // the chat moved elsewhere: the whole frame
            rows.clear();
            ReadText(ocr, frame, 0, 0, frame.width, frame.height, 1, rows);
            found = find(rows);
        }
    }
    const int frameW = frame.width, frameH = frame.height;
    frame = OcrImage{};  // gone: nothing of the screen is kept
    if (!found || g_macroStop) {
        Say(TrF(L"{1} is copied – click it in GW2's chat (not found on the screen).", {label}), 12000);
        return;
    }
    // 4. The frame's pixels on the screen: GW2's client area (it may be scaled), then the click – our window lets the
    //    mouse through meanwhile (the minimised button may stand over GW2's chat).
    const float cx = hit.x + hit.w / 2.0f, cy = hit.y + hit.h / 2.0f;
    const HWND gw2 = FindGw2Window(GetCurrentProcessId());
    RECT client{};
    if (!gw2 || !GetClientRect(gw2, &client)) return;
    const float sx = static_cast<float>(client.right) / std::max(1, frameW);
    const float sy = static_cast<float>(client.bottom) / std::max(1, frameH);
    POINT at{static_cast<LONG>(cx * sx), static_cast<LONG>(cy * sy)};
    ClientToScreen(gw2, &at);
    if (!SyntheticClick(at, true)) Say(TrF(L"{1} is copied – click it in GW2's chat.", {label}), 12000);
}

void ConvertRow(const uint8_t* src, uint8_t* dst, int width, DXGI_FORMAT f) {  // one row into BGRA
    for (int x = 0; x < width; ++x, dst += 4) {
        if (f == DXGI_FORMAT_R10G10B10A2_UNORM) {
            uint32_t v;
            std::memcpy(&v, src + x * 4, 4);
            dst[2] = static_cast<uint8_t>((v & 0x3FF) >> 2);
            dst[1] = static_cast<uint8_t>(((v >> 10) & 0x3FF) >> 2);
            dst[0] = static_cast<uint8_t>(((v >> 20) & 0x3FF) >> 2);
            dst[3] = 255;
        } else if (f == DXGI_FORMAT_R8G8B8A8_UNORM || f == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) {
            dst[0] = src[x * 4 + 2];
            dst[1] = src[x * 4 + 1];
            dst[2] = src[x * 4 + 0];
            dst[3] = 255;
        } else {  // B8G8R8A8 / B8G8R8X8
            std::memcpy(dst, src + x * 4, 3);
            dst[3] = 255;
        }
    }
}

bool Readable(DXGI_FORMAT f) {
    return f == DXGI_FORMAT_B8G8R8A8_UNORM || f == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB || f == DXGI_FORMAT_B8G8R8X8_UNORM ||
           f == DXGI_FORMAT_R8G8B8A8_UNORM || f == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || f == DXGI_FORMAT_R10G10B10A2_UNORM;
}

}  // namespace

// Render thread, every frame: the frame the macro asked for. The back buffer holds the game's own picture here –
// Nexus draws the add-ons later in the same frame, so none of our windows is in it.
void WaypointFrameTick() {
    ++g_frames;
    const int st = g_grab;
    if (st == kGrabAsked) {
        auto* chain = g_api ? static_cast<IDXGISwapChain*>(g_api->SwapChain) : nullptr;
        ID3D11Texture2D* back = nullptr;
        if (!chain || FAILED(chain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back))) || !back) {
            g_grab = kGrabFailed;
            return;
        }
        D3D11_TEXTURE2D_DESC d{};
        back->GetDesc(&d);
        ID3D11Device* device = nullptr;
        back->GetDevice(&device);
        D3D11_TEXTURE2D_DESC s = d;
        s.Usage = D3D11_USAGE_STAGING;
        s.BindFlags = 0;
        s.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        s.MiscFlags = 0;
        s.MipLevels = 1;
        s.ArraySize = 1;
        bool ok = device && d.SampleDesc.Count == 1 && Readable(d.Format) &&
                  SUCCEEDED(device->CreateTexture2D(&s, nullptr, &g_staging));
        if (ok) {
            ID3D11DeviceContext* ctx = nullptr;
            device->GetImmediateContext(&ctx);
            if (ctx) {
                ctx->CopyResource(g_staging, back);
                ctx->Release();
                g_stagingFormat = d.Format;
                g_grabFrames = 2;  // read when the GPU is surely done – no waiting on the game's thread
            } else {
                ok = false;
            }
        }
        if (device) device->Release();
        back->Release();
        if (!ok && g_staging) {
            g_staging->Release();
            g_staging = nullptr;
        }
        g_grab = ok ? kGrabCopied : kGrabFailed;
    } else if (st == kGrabCopied && --g_grabFrames <= 0) {
        bool ok = false;
        ID3D11Device* device = nullptr;
        g_staging->GetDevice(&device);
        ID3D11DeviceContext* ctx = nullptr;
        if (device) device->GetImmediateContext(&ctx);
        D3D11_TEXTURE2D_DESC d{};
        g_staging->GetDesc(&d);
        D3D11_MAPPED_SUBRESOURCE m{};
        if (ctx && SUCCEEDED(ctx->Map(g_staging, 0, D3D11_MAP_READ, 0, &m))) {
            std::lock_guard<std::mutex> lock(g_grabMu);
            g_frame.width = static_cast<int>(d.Width);
            g_frame.height = static_cast<int>(d.Height);
            g_frame.bgra.resize(static_cast<size_t>(d.Width) * d.Height * 4);
            for (UINT y = 0; y < d.Height; ++y)
                ConvertRow(static_cast<const uint8_t*>(m.pData) + static_cast<size_t>(y) * m.RowPitch,
                           &g_frame.bgra[static_cast<size_t>(y) * d.Width * 4], static_cast<int>(d.Width), g_stagingFormat);
            ctx->Unmap(g_staging, 0);
            ok = true;
        }
        if (ctx) ctx->Release();
        if (device) device->Release();
        g_staging->Release();
        g_staging = nullptr;
        g_grab = ok ? kGrabDone : kGrabFailed;
    }
}

void StartWaypointClick(const ChatLineHint& line) {  // g_mu held (HandleChatLinkClick)
    StartMacro([line] { RunWaypoint(line); });
}

void OnClickTestKey(const char*, bool release) {  // the test key: one click where the mouse stands
    if (release) return;
    StartMacro([] {
        Sleep(200);  // the key is let go first
        POINT at{};
        GetCursorPos(&at);
        Say(SyntheticClick(at, false) ? Tr(L"Click test: one click where the mouse is.")
                               : Tr(L"Click test: GW2 is not in front – no click."),
            8000);
    });
}

int SyntheticInputs() { return g_synthetic; }

bool WaypointPassThrough() { return g_passThrough; }

void StopWaypointMacro() {  // unload: the macro ends before the DLL goes
    g_macroStop = true;
    if (g_macroThread.joinable()) g_macroThread.join();
    if (g_staging) {
        g_staging->Release();
        g_staging = nullptr;
    }
    g_grab = kGrabIdle;
}

}  // namespace kb
