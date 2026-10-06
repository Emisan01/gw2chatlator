// suggestion_bar.cpp
#include "suggestion_bar.hpp"

#include <windowsx.h>

#include <algorithm>

#include "core/gw2_text.hpp"
#include "core/i18n.hpp"

namespace gct {
namespace {
constexpr wchar_t kClass[] = L"GW2ChatTranslatorWordBar";
}

bool SuggestionBar::Register(HINSTANCE inst) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = Proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_HAND);
    wc.lpszClassName = kClass;
    return RegisterClassExW(&wc) != 0;
}

bool SuggestionBar::Create(HWND parent, HINSTANCE inst, const Theme* theme, std::function<void(size_t)> onPick) {
    theme_ = theme;
    onPick_ = std::move(onPick);
    hwnd_ = CreateWindowExW(0, kClass, L"", WS_CHILD, 0, 0, 10, 10, parent, nullptr, inst, this);
    return hwnd_ != nullptr;
}

int SuggestionBar::PreferredHeight() const { return theme_->textLineHeight + theme_->S(8); }

void SuggestionBar::Set(const WordSuggestions& s) {
    s_ = s;
    hot_ = -1;
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

int SuggestionBar::SlotAt(int x) const {
    RECT rc;
    GetClientRect(hwnd_, &rc);
    const int w = std::max(1, static_cast<int>(rc.right) / 3);
    const int slot = std::clamp(x / w, 0, 2);
    return slot < static_cast<int>(s_.words.size()) ? slot : -1;
}

void SuggestionBar::Paint() {
    PAINTSTRUCT ps;
    HDC wdc = BeginPaint(hwnd_, &ps);
    RECT rc;
    GetClientRect(hwnd_, &rc);
    HDC dc = CreateCompatibleDC(wdc);
    HBITMAP bmp = CreateCompatibleBitmap(wdc, std::max(1L, rc.right), std::max(1L, rc.bottom));
    HGDIOBJ oldBmp = SelectObject(dc, bmp);
    FillRect(dc, &rc, theme_->bg);
    SetBkMode(dc, TRANSPARENT);
    const int w = std::max(1, static_cast<int>(rc.right) / 3);
    for (int i = 0; i < 3; ++i) {
        RECT cell{i * w, 0, i == 2 ? rc.right : (i + 1) * w, rc.bottom};
        if (i > 0) {  // thin divider
            RECT div{cell.left, cell.top + theme_->S(6), cell.left + 1, cell.bottom - theme_->S(6)};
            HBRUSH b = CreateSolidBrush(Theme::kFaint);
            FillRect(dc, &div, b);
            DeleteObject(b);
        }
        if (i >= static_cast<int>(s_.words.size())) continue;
        const bool highlighted = i == s_.autoIndex;
        if (i == hot_ || highlighted) {
            RECT bgR = cell;
            InflateRect(&bgR, -theme_->S(3), -theme_->S(2));
            HBRUSH b = CreateSolidBrush(i == hot_ ? Theme::kTabActive : Theme::kInputBg);
            FillRect(dc, &bgR, b);
            DeleteObject(b);
        }
        const std::wstring& word = s_.words[static_cast<size_t>(i)];
        COLORREF color = Theme::kText;
        if (highlighted) color = Theme::kAccent;
        else if (s_.kind == WordSuggestions::Kind::Next) color = Theme::kMuted;
        SelectObject(dc, highlighted ? theme_->fontUiBold : theme_->fontUi);
        SetTextColor(dc, color);
        RECT tr = cell;
        InflateRect(&tr, -theme_->S(6), 0);
        DrawTextW(dc, word.c_str(), static_cast<int>(word.size()), &tr,
                  DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX |
                      (IsRtlText(word) ? DT_RTLREADING : 0));
    }
    BitBlt(wdc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(dc);
    EndPaint(hwnd_, &ps);
}

LRESULT CALLBACK SuggestionBar::Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    SuggestionBar* self;
    if (msg == WM_NCCREATE) {
        self = static_cast<SuggestionBar*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = h;
    } else {
        self = reinterpret_cast<SuggestionBar*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    }
    return self ? self->Handle(msg, wp, lp) : DefWindowProcW(h, msg, wp, lp);
}

LRESULT SuggestionBar::Handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;  // keep typing in the input
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            Paint();
            return 0;
        case WM_MOUSEMOVE: {
            const int slot = SlotAt(GET_X_LPARAM(lp));
            if (slot != hot_) {
                hot_ = slot;
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
            const int slot = SlotAt(GET_X_LPARAM(lp));
            if (slot >= 0 && onPick_) onPick_(static_cast<size_t>(slot));
            return 0;
        }
        case WM_RBUTTONUP: {
            const int slot = SlotAt(GET_X_LPARAM(lp));
            if (slot < 0 || !onForget_) return 0;
            const std::wstring word = s_.words[static_cast<size_t>(slot)];
            if (isLearned_ && !isLearned_(word)) return 0;
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ClientToScreen(hwnd_, &pt);
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, 1, TrF(L"Forget “{1}”", {word}).c_str());
            const UINT cmd = static_cast<UINT>(TrackPopupMenu(
                menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY | (UiRtl() ? TPM_LAYOUTRTL : 0), pt.x, pt.y, 0,
                hwnd_, nullptr));
            DestroyMenu(menu);
            if (cmd == 1) onForget_(word);
            return 0;
        }
        case WM_NCDESTROY:
            SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
            break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

}  // namespace gct
