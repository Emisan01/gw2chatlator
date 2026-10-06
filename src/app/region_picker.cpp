// region_picker.cpp
#include "core/i18n.hpp"
#include "region_picker.hpp"

#include <windowsx.h>

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <thread>

namespace gct {
namespace {

constexpr wchar_t kClass[] = L"GW2ChatTranslatorPicker";
constexpr COLORREF kKey = RGB(255, 0, 255);  // colour key: fully transparent
constexpr COLORREF kShade = RGB(8, 9, 12);
constexpr UINT WM_APP_PREVIEW = WM_APP + 1;  // wParam: generation, lParam: std::wstring* (owned)

struct PickState {
    const Theme* theme = nullptr;
    std::wstring hint;
    POINT origin{};  // virtual screen top-left
    bool dragging = false;
    POINT a{}, b{};
    bool done = false;
    bool ok = false;
    RECT result{};
    // Snapping and preview (optional).
    PickAnalyzer analyze;
    PickPreview preview;
    bool checked = false;
    PickCheck check;
    std::wstring previewText;
    unsigned gen = 0;
};

RECT Normalized(POINT a, POINT b) {
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y)};
}

COLORREF QualityColor(int q) { return q >= 2 ? Theme::kOk : q == 1 ? Theme::kWarn : Theme::kError; }

void Border(HDC dc, RECT r, int thickness, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    RECT outer = r;
    InflateRect(&outer, thickness, thickness);
    const RECT parts[4] = {{outer.left, outer.top, outer.right, r.top},
                           {outer.left, r.bottom, outer.right, outer.bottom},
                           {outer.left, r.top, r.left, r.bottom},
                           {r.right, r.top, outer.right, r.bottom}};
    for (const RECT& p : parts) FillRect(dc, &p, brush);
    DeleteObject(brush);
}

std::wstring HintText(const PickState* s) {
    if (!s->checked) return s->hint;
    std::wstring t = s->check.summary;
    if (s->check.quality > 0) {
        if (!s->previewText.empty()) t += L"\n\n" + s->previewText;
        t += L"\n\n" + Tr(L"Enter or a click into the frame: take it  ·  drag again: redo  ·  Esc: cancel");
    } else {
        t += L"\n\n" + Tr(L"Drag again around the text lines of the chat  ·  Esc: cancel");
    }
    return t;
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
    auto clear = [&](RECT sel, COLORREF color) {
        Border(dc, sel, t.S(2), color);
        HBRUSH key = CreateSolidBrush(kKey);
        FillRect(dc, &sel, key);
        DeleteObject(key);
    };
    if (s->dragging) {
        clear(Normalized(s->a, s->b), Theme::kAccent);
    } else if (s->checked) {
        RECT snapped = s->check.snapped;
        OffsetRect(&snapped, -s->origin.x, -s->origin.y);
        clear(snapped, QualityColor(s->check.quality));
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
    const std::wstring hint = HintText(s);
    const UINT rtl = UiRtl() ? DT_RTLREADING : 0;
    RECT calc{0, 0, std::min(static_cast<int>(mon.right - mon.left) - t.S(40), t.S(640)), 0};
    DrawTextW(dc, hint.c_str(), -1, &calc, DT_WORDBREAK | DT_CENTER | DT_NOPREFIX | DT_CALCRECT | rtl);
    const int boxW = calc.right + t.S(32), boxH = calc.bottom + t.S(24);
    RECT box{mon.left + (mon.right - mon.left - boxW) / 2, mon.top + t.S(40), 0, 0};
    box.right = box.left + boxW;
    box.bottom = box.top + boxH;
    HBRUSH panel = CreateSolidBrush(Theme::kPanel);
    FillRect(dc, &box, panel);
    DeleteObject(panel);
    if (s->checked) Border(dc, box, t.S(2), QualityColor(s->check.quality));
    RECT text = box;
    InflateRect(&text, -t.S(16), -t.S(12));
    SetTextColor(dc, Theme::kText);
    DrawTextW(dc, hint.c_str(), -1, &text, DT_WORDBREAK | DT_CENTER | DT_NOPREFIX | rtl);

    BitBlt(wdc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldFont);
    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(dc);
    EndPaint(h, &ps);
}

void Accept(PickState* s, const RECT& r) {
    s->ok = (r.right - r.left) >= 40 && (r.bottom - r.top) >= 20;
    s->result = r;
    s->done = true;
}

// The drag ended: snap and rate it, then read the first lines in the background.
void Check(HWND h, PickState* s, const RECT& rough) {
    s->check = s->analyze(rough);
    s->checked = true;
    s->previewText.clear();
    const unsigned gen = ++s->gen;
    if (!s->preview || s->check.quality == 0) return;
    s->previewText = Tr(L"Reading the first lines …");
    std::thread([h, gen, preview = s->preview, rect = s->check.snapped] {
        auto text = std::make_unique<std::wstring>(preview(rect));
        if (PostMessageW(h, WM_APP_PREVIEW, gen, reinterpret_cast<LPARAM>(text.get()))) text.release();
    }).detach();
}

LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        SetWindowLongPtrW(h, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
        return DefWindowProcW(h, msg, wp, lp);
    }
    auto* s = reinterpret_cast<PickState*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (msg == WM_APP_PREVIEW) {  // also after the picker closed: free the text
        std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(lp));
        if (s && s->checked && static_cast<unsigned>(wp) == s->gen) {
            s->previewText = text->empty() ? Tr(L"(no lines recognized yet)") : *text;
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    }
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
                if (s->checked && (std::abs(s->b.x - s->a.x) > 4 || std::abs(s->b.y - s->a.y) > 4)) {
                    s->checked = false;  // a new frame
                    ++s->gen;
                }
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
                const bool click = (r.right - r.left) < 5 && (r.bottom - r.top) < 5;
                if (click) {
                    const POINT pt{r.left, r.top};
                    if (s->checked && s->check.quality > 0 && PtInRect(&s->check.snapped, pt))
                        Accept(s, s->check.snapped);
                } else if (s->analyze) {
                    Check(h, s, r);
                } else {
                    Accept(s, r);
                }
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        case WM_RBUTTONUP:
        case WM_CANCELMODE:
            s->done = true;
            return 0;
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) s->done = true;
            if ((wp == VK_RETURN || wp == VK_SPACE) && s->checked && s->check.quality > 0) Accept(s, s->check.snapped);
            return 0;
        case WM_CLOSE:
            s->done = true;
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

}  // namespace

bool PickScreenRegion(HINSTANCE inst, const Theme& theme, const std::wstring& hint, RECT* out, PickAnalyzer analyze,
                      PickPreview preview) {
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
    s.analyze = std::move(analyze);
    s.preview = std::move(preview);
    s.origin = {GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN)};
    const int w = GetSystemMetrics(SM_CXVIRTUALSCREEN), h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    HWND wnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED, kClass, Tr(L"Chat area").c_str(), WS_POPUP,
                               s.origin.x, s.origin.y, w, h, nullptr, nullptr, inst, &s);
    if (!wnd) return false;
    SetLayeredWindowAttributes(wnd, kKey, 150, LWA_ALPHA | LWA_COLORKEY);
    ShowWindow(wnd, SW_SHOW);
    SetForegroundWindow(wnd);
    SetFocus(wnd);

    MSG m{};
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
    // A preview still on its way finds no state any more and only frees its text.
    SetWindowLongPtrW(wnd, GWLP_USERDATA, 0);
    DestroyWindow(wnd);
    if (quit) PostQuitMessage(static_cast<int>(m.wParam));
    if (!s.ok) return false;
    *out = s.result;
    return true;
}

}  // namespace gct
