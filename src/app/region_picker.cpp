// region_picker.cpp
#include "region_picker.hpp"

#include <windowsx.h>

#include <algorithm>

namespace gct {
namespace {

constexpr wchar_t kClass[] = L"GW2ChatTranslatorPicker";
constexpr COLORREF kKey = RGB(255, 0, 255);  // colour key: fully transparent
constexpr COLORREF kShade = RGB(8, 9, 12);

struct PickState {
    const Theme* theme = nullptr;
    std::wstring hint;
    POINT origin{};  // virtual screen top-left
    bool dragging = false;
    POINT a{}, b{};
    bool done = false;
    bool ok = false;
    RECT result{};
};

RECT Normalized(POINT a, POINT b) {
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y)};
}

void Paint(HWND h, PickState* s) {
    PAINTSTRUCT ps;
    HDC wdc = BeginPaint(h, &ps);
    RECT rc;
    GetClientRect(h, &rc);
    HDC dc = CreateCompatibleDC(wdc);
    HBITMAP bmp = CreateCompatibleBitmap(wdc, std::max(1L, rc.right), std::max(1L, rc.bottom));
    HGDIOBJ oldBmp = SelectObject(dc, bmp);
    HBRUSH shade = CreateSolidBrush(kShade);
    FillRect(dc, &rc, shade);
    DeleteObject(shade);

    const Theme& t = *s->theme;
    if (s->dragging) {
        RECT sel = Normalized(s->a, s->b);
        HBRUSH border = CreateSolidBrush(Theme::kAccent);
        RECT outer = sel;
        InflateRect(&outer, t.S(2), t.S(2));
        FillRect(dc, &outer, border);
        DeleteObject(border);
        HBRUSH key = CreateSolidBrush(kKey);
        FillRect(dc, &sel, key);
        DeleteObject(key);
    }

    // Hint box at the top of the monitor under the mouse.
    POINT cursor;
    GetCursorPos(&cursor);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY), &mi);
    RECT mon = mi.rcMonitor;
    OffsetRect(&mon, -s->origin.x, -s->origin.y);
    HGDIOBJ oldFont = SelectObject(dc, t.fontText);
    SetBkMode(dc, TRANSPARENT);
    RECT calc{0, 0, std::min(static_cast<int>(mon.right - mon.left) - t.S(40), t.S(560)), 0};
    DrawTextW(dc, s->hint.c_str(), -1, &calc, DT_WORDBREAK | DT_CENTER | DT_NOPREFIX | DT_CALCRECT);
    const int boxW = calc.right + t.S(32), boxH = calc.bottom + t.S(24);
    RECT box{mon.left + (mon.right - mon.left - boxW) / 2, mon.top + t.S(40), 0, 0};
    box.right = box.left + boxW;
    box.bottom = box.top + boxH;
    HBRUSH panel = CreateSolidBrush(Theme::kPanel);
    FillRect(dc, &box, panel);
    DeleteObject(panel);
    RECT text = box;
    InflateRect(&text, -t.S(16), -t.S(12));
    SetTextColor(dc, Theme::kText);
    DrawTextW(dc, s->hint.c_str(), -1, &text, DT_WORDBREAK | DT_CENTER | DT_NOPREFIX);

    BitBlt(wdc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldFont);
    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(dc);
    EndPaint(h, &ps);
}

LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        SetWindowLongPtrW(h, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
        return DefWindowProcW(h, msg, wp, lp);
    }
    auto* s = reinterpret_cast<PickState*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (!s) return DefWindowProcW(h, msg, wp, lp);
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            Paint(h, s);
            return 0;
        case WM_SETCURSOR:
            SetCursor(LoadCursorW(nullptr, IDC_CROSS));
            return TRUE;
        case WM_LBUTTONDOWN:
            s->dragging = true;
            s->a = s->b = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            SetCapture(h);
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        case WM_MOUSEMOVE:
            if (s->dragging) {
                s->b = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        case WM_LBUTTONUP:
            if (s->dragging) {
                s->b = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                s->dragging = false;
                ReleaseCapture();
                RECT r = Normalized(s->a, s->b);
                OffsetRect(&r, s->origin.x, s->origin.y);
                s->ok = (r.right - r.left) >= 40 && (r.bottom - r.top) >= 20;
                s->result = r;
                s->done = true;
            }
            return 0;
        case WM_RBUTTONUP:
        case WM_CANCELMODE:
            s->done = true;
            return 0;
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) s->done = true;
            return 0;
        case WM_CLOSE:
            s->done = true;
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

}  // namespace

bool PickScreenRegion(HINSTANCE inst, const Theme& theme, const std::wstring& hint, RECT* out) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = Proc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_CROSS);
        wc.lpszClassName = kClass;
        registered = RegisterClassExW(&wc) != 0;
    }

    PickState s;
    s.theme = &theme;
    s.hint = hint;
    s.origin = {GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN)};
    const int w = GetSystemMetrics(SM_CXVIRTUALSCREEN), h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    HWND wnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED, kClass, L"Chat-Bereich", WS_POPUP,
                               s.origin.x, s.origin.y, w, h, nullptr, nullptr, inst, &s);
    if (!wnd) return false;
    SetLayeredWindowAttributes(wnd, kKey, 150, LWA_ALPHA | LWA_COLORKEY);
    ShowWindow(wnd, SW_SHOW);
    SetForegroundWindow(wnd);
    SetFocus(wnd);

    MSG m;
    bool quit = false;
    while (!s.done) {
        const BOOL r = GetMessageW(&m, nullptr, 0, 0);
        if (r <= 0) {
            quit = true;
            break;
        }
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    DestroyWindow(wnd);
    if (quit) PostQuitMessage(static_cast<int>(m.wParam));
    if (!s.ok) return false;
    *out = s.result;
    return true;
}

}  // namespace gct
