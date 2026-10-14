#include "map_focus.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <thread>

#include "win/mumble_link.hpp"

namespace gct {
namespace {

std::thread g_thread;
std::atomic<bool> g_busy{false};
std::atomic<bool> g_stop{false};

HWND GameWindow() {  // the window in front, if it belongs to this process (the game)
    HWND h = GetForegroundWindow();
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    return pid == GetCurrentProcessId() ? h : nullptr;
}

bool StillFront(HWND h) { return !g_stop && GetForegroundWindow() == h; }

void Mouse(POINT p, DWORD flags) {  // absolute, over the whole virtual desktop
    const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN), vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int vw = std::max(2, GetSystemMetrics(SM_CXVIRTUALSCREEN)), vh = std::max(2, GetSystemMetrics(SM_CYVIRTUALSCREEN));
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dx = static_cast<LONG>((p.x - vx) * 65535.0 / (vw - 1));
    in.mi.dy = static_cast<LONG>((p.y - vy) * 65535.0 / (vh - 1));
    in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK | flags;
    SendInput(1, &in, sizeof(in));
}

// Presses on `from`, moves in small steps to `to`, lets go. GW2 reads the mouse once per frame: every step waits.
bool Drag(HWND h, POINT from, POINT to) {
    if (!StillFront(h)) return false;
    Mouse(from, 0);
    Sleep(30);
    Mouse(from, MOUSEEVENTF_LEFTDOWN);
    Sleep(40);
    constexpr int kSteps = 14;
    for (int i = 1; i <= kSteps; ++i) {
        const POINT p{from.x + (to.x - from.x) * i / kSteps, from.y + (to.y - from.y) * i / kSteps};
        if (!StillFront(h)) {
            Mouse(p, MOUSEEVENTF_LEFTUP);
            return false;
        }
        Mouse(p, 0);
        Sleep(16);
    }
    Sleep(30);
    Mouse(to, MOUSEEVENTF_LEFTUP);
    Sleep(60);
    return true;
}

void Run(AddonAPI_t* api, float x, float y, RECT avoid) {
    HWND h = GameWindow();
    MumbleLink link;
    MumbleState s = link.Read();
    if (!h || !s.live) return;
    for (int i = 0; i < 150 && (GetAsyncKeyState(VK_LBUTTON) & 0x8000) && !g_stop; ++i) Sleep(20);  // your click ends first
    POINT saved{};
    GetCursorPos(&saved);
    if (!s.MapOpen()) {  // the game's own map key, pressed by Nexus
        if (!api || !api->GameBinds_InvokeAsync) return;
        api->GameBinds_InvokeAsync(GB_MapToggle, 50);
        for (int i = 0; i < 50 && !g_stop; ++i) {
            Sleep(50);
            s = link.Read();
            if (s.MapOpen()) break;
        }
        if (!s.MapOpen()) return;
        Sleep(450);  // the map fades in
    }
    // Where the drag starts: the middle of the game, or beside our window if that is in the way.
    RECT cr{};
    GetClientRect(h, &cr);
    POINT start{cr.right / 2, cr.bottom / 2};
    if (PtInRect(&avoid, start))
        start.x = avoid.left > cr.right - avoid.right ? avoid.left / 2 : (avoid.right + cr.right) / 2;
    ClientToScreen(h, &start);
    const float maxDrag = std::max(80.0f, static_cast<float>(std::min(cr.right, cr.bottom)) * 0.35f);
    // Continent units per pixel: MumbleLink's scale as the first guess, then measured on every drag (both axes).
    float kx = s.mapScale > 0 ? s.mapScale : 1.0f, ky = kx;
    bool measuredX = false, measuredY = false;
    for (int step = 0; step < 10 && StillFront(h); ++step) {
        s = link.Read();
        if (!s.MapOpen()) break;
        const float dx = x - s.mapCenterX, dy = y - s.mapCenterY;
        float px = -dx / kx, py = -dy / ky;
        if (std::fabs(px) < 3 && std::fabs(py) < 3) break;  // the spot is in the middle
        // Not measured yet: a drag long enough to measure on that axis.
        if (!measuredX && std::fabs(px) >= 1 && std::fabs(px) < 60) px = px < 0 ? -60.0f : 60.0f;
        if (!measuredY && std::fabs(py) >= 1 && std::fabs(py) < 60) py = py < 0 ? -60.0f : 60.0f;
        px = std::clamp(px, -maxDrag, maxDrag);
        py = std::clamp(py, -maxDrag, maxDrag);
        const POINT to{start.x + static_cast<LONG>(std::lround(px)), start.y + static_cast<LONG>(std::lround(py))};
        const float beforeX = s.mapCenterX, beforeY = s.mapCenterY;
        if (!Drag(h, start, to)) break;
        Sleep(120);
        s = link.Read();
        const float mx = s.mapCenterX - beforeX, my = s.mapCenterY - beforeY;
        if (std::fabs(px) >= 20 && std::fabs(mx) > 0.001f) {
            kx = -mx / px;
            measuredX = true;
        }
        if (std::fabs(py) >= 20 && std::fabs(my) > 0.001f) {
            ky = -my / py;
            measuredY = true;
        }
    }
    Mouse(saved, 0);  // the cursor back where it was
}

}  // namespace

void FocusWorldMap(AddonAPI_t* api, float x, float y, const RECT& avoid) {
    if (g_busy.exchange(true)) return;
    if (g_thread.joinable()) g_thread.join();
    g_stop = false;
    g_thread = std::thread([api, x, y, avoid] {
        Run(api, x, y, avoid);
        g_busy = false;
    });
}

void StopMapFocus() {
    g_stop = true;
    if (g_thread.joinable()) g_thread.join();
    g_busy = false;
}

}  // namespace gct
