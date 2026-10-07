// choice_popup.cpp
#include "choice_popup.hpp"

#include <windowsx.h>

#include <algorithm>

#include "core/gw2_text.hpp"
#include "core/i18n.hpp"

namespace gct {
namespace {
constexpr wchar_t kClass[] = L"GW2ChatTranslatorChoices";
}

bool ChoicePopup::Register(HINSTANCE inst) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DROPSHADOW;
    wc.lpfnWndProc = Proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_HAND);
    wc.lpszClassName = kClass;
    return RegisterClassExW(&wc) != 0;
}

bool ChoicePopup::Create(HWND owner, HINSTANCE inst, const Theme* theme, std::function<void(size_t)> onPick) {
    theme_ = theme;
    onPick_ = std::move(onPick);
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, kClass, L"", WS_POPUP, 0, 0, 10, 10,
                            owner, nullptr, inst, this);
    if (hwnd_) SetWindowDisplayAffinity(hwnd_, WDA_EXCLUDEFROMCAPTURE);  // never read by our own reader
    return hwnd_ != nullptr;
}

int ChoicePopup::RowHeight() const { return theme_->textLineHeight + theme_->S(6); }

int ChoicePopup::RowAt(int y) const {
    const int row = (y - theme_->S(3)) / RowHeight();
    return row >= 0 && row < static_cast<int>(c_.words.size()) ? row : -1;
}

void ChoicePopup::Show(const WordChoices& c, POINT pos) {
    if (!hwnd_) return;
    if (c.words.empty()) {
        Hide();
        return;
    }
    c_ = c;
    hot_ = -1;
    // Width: the longest entry.
    HDC dc = GetDC(hwnd_);
    HGDIOBJ old = SelectObject(dc, theme_->fontText);
    int w = theme_->S(120);
    for (const std::wstring& s : c_.words) {
        SIZE sz{};
        const std::wstring shown = s + L"  ”“";
        GetTextExtentPoint32W(dc, shown.c_str(), static_cast<int>(shown.size()), &sz);
        w = std::max(w, static_cast<int>(sz.cx) + theme_->S(28));
    }
    SelectObject(dc, old);
    ReleaseDC(hwnd_, dc);
    const int h = static_cast<int>(c_.words.size()) * RowHeight() + theme_->S(6);
    // Stay on the monitor: above the word if there is no room below.
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(MonitorFromPoint(pos, MONITOR_DEFAULTTONEAREST), &mi);
    int x = std::min(static_cast<int>(pos.x), static_cast<int>(mi.rcWork.right) - w);
    int y = pos.y;
    if (y + h > mi.rcWork.bottom) y = pos.y - h - theme_->textLineHeight - theme_->S(8);
    x = std::max(x, static_cast<int>(mi.rcWork.left));
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    visible_ = true;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void ChoicePopup::Hide() {
    if (!visible_ || !hwnd_) return;
    ShowWindow(hwnd_, SW_HIDE);
    visible_ = false;
}

void ChoicePopup::Paint() {
    PAINTSTRUCT ps;
    HDC wdc = BeginPaint(hwnd_, &ps);
    RECT rc;
    GetClientRect(hwnd_, &rc);
    HDC dc = CreateCompatibleDC(wdc);
    HBITMAP bmp = CreateCompatibleBitmap(wdc, std::max(1L, rc.right), std::max(1L, rc.bottom));
    HGDIOBJ oldBmp = SelectObject(dc, bmp);
    FillRect(dc, &rc, theme_->panel);
    HBRUSH frame = CreateSolidBrush(Theme::kFaint);
    FrameRect(dc, &rc, frame);
    DeleteObject(frame);
    SetBkMode(dc, TRANSPARENT);
    const int rh = RowHeight();
    for (size_t i = 0; i < c_.words.size(); ++i) {
        RECT row{theme_->S(3), theme_->S(3) + static_cast<int>(i) * rh, rc.right - theme_->S(3),
                 theme_->S(3) + static_cast<int>(i + 1) * rh};
        const bool high = static_cast<int>(i) == c_.highlight;
        if (high || static_cast<int>(i) == hot_) {
            HBRUSH b = CreateSolidBrush(high ? Theme::kTabActive : Theme::kInputBg);
            FillRect(dc, &row, b);
            DeleteObject(b);
        }
        const bool typed = i == 0 && c_.firstIsTyped;
        // The word as typed stands in quotes: Space keeps it.
        const std::wstring shown = typed ? L"“" + c_.words[i] + L"”" : c_.words[i];
        SelectObject(dc, high ? theme_->fontUiBold : theme_->fontText);
        SetTextColor(dc, high ? Theme::kAccent : typed ? Theme::kMuted : Theme::kText);
        RECT tr = row;
        InflateRect(&tr, -theme_->S(8), 0);
        const bool rtl = IsRtlText(c_.words[i]);
        DrawTextW(dc, shown.c_str(), static_cast<int>(shown.size()), &tr,
                  DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS | (rtl ? DT_RTLREADING | DT_RIGHT : 0));
    }
    BitBlt(wdc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(dc);
    EndPaint(hwnd_, &ps);
}

LRESULT CALLBACK ChoicePopup::Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    ChoicePopup* self;
    if (msg == WM_NCCREATE) {
        self = static_cast<ChoicePopup*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = h;
    } else {
        self = reinterpret_cast<ChoicePopup*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    }
    return self ? self->Handle(msg, wp, lp) : DefWindowProcW(h, msg, wp, lp);
}

LRESULT ChoicePopup::Handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;  // typing goes on in the input
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            Paint();
            return 0;
        case WM_MOUSEMOVE: {
            const int row = RowAt(GET_Y_LPARAM(lp));
            if (row != hot_) {
                hot_ = row;
                InvalidateRect(hwnd_, nullptr, FALSE);
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
                TrackMouseEvent(&tme);
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            hot_ = -1;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case WM_LBUTTONUP: {
            const int row = RowAt(GET_Y_LPARAM(lp));
            if (row >= 0 && onPick_) onPick_(static_cast<size_t>(row));
            return 0;
        }
        case WM_NCDESTROY:
            SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
            break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

}  // namespace gct
