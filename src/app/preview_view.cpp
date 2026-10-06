// preview_view.cpp
#include "core/i18n.hpp"
#include "preview_view.hpp"

#include <windowsx.h>

#include <algorithm>
#include <cstring>

#include "core/gw2_text.hpp"

namespace gct {
namespace {

constexpr wchar_t kClass[] = L"GW2ChatTranslatorPreview";

UINT DirFlags(const std::wstring& s) { return IsRtlText(s) ? (DT_RTLREADING | DT_RIGHT) : DT_LEFT; }

}  // namespace

bool PreviewView::Register(HINSTANCE inst) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = Proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
    wc.lpszClassName = kClass;
    return RegisterClassExW(&wc) != 0;
}

bool PreviewView::Create(HWND parent, HINSTANCE inst, const Theme* theme, std::function<void()> onClick) {
    theme_ = theme;
    onClick_ = std::move(onClick);
    hwnd_ = CreateWindowExW(0, kClass, L"", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, parent, nullptr, inst, this);
    return hwnd_ != nullptr;
}

void PreviewView::Set(Content c) {
    c_ = std::move(c);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

int PreviewView::PreferredHeight() const {
    return theme_->textLineHeight * 2 + theme_->smallLineHeight * 2 + theme_->S(14);
}

void PreviewView::Paint() {
    PAINTSTRUCT ps;
    HDC wdc = BeginPaint(hwnd_, &ps);
    RECT rc;
    GetClientRect(hwnd_, &rc);
    HDC dc = CreateCompatibleDC(wdc);
    HBITMAP bmp = CreateCompatibleBitmap(wdc, std::max(1L, rc.right), std::max(1L, rc.bottom));
    HGDIOBJ oldBmp = SelectObject(dc, bmp);
    HGDIOBJ oldFont = SelectObject(dc, theme_->fontText);
    FillRect(dc, &rc, theme_->panel);
    SetBkMode(dc, TRANSPARENT);

    RECT r = rc;
    InflateRect(&r, -theme_->S(8), -theme_->S(6));
    const int smallBlock =
        (c_.back.empty() ? 0 : theme_->smallLineHeight) + (c_.note.empty() ? 0 : theme_->smallLineHeight);
    RECT text = r;
    text.bottom = std::max(static_cast<int>(text.top) + theme_->textLineHeight, static_cast<int>(r.bottom) - smallBlock);

    if (c_.text.empty()) {
        SelectObject(dc, theme_->fontUi);
        SetTextColor(dc, Theme::kFaint);
        DrawTextW(dc, c_.placeholder.c_str(), -1, &text, DT_WORDBREAK | DT_NOPREFIX | DT_END_ELLIPSIS);
    } else {
        SelectObject(dc, theme_->fontText);
        SetTextColor(dc, c_.current ? Theme::kPreviewText : Theme::kMuted);
        DrawTextW(dc, c_.text.c_str(), static_cast<int>(c_.text.size()), &text,
                  DT_WORDBREAK | DT_EDITCONTROL | DT_NOPREFIX | DT_END_ELLIPSIS | DirFlags(c_.text));
    }

    int y = text.bottom;
    SelectObject(dc, theme_->fontSmall);
    if (!c_.back.empty()) {
        const std::wstring s = L"\u2248 " + c_.back;
        RECT b{r.left, y, r.right, y + theme_->smallLineHeight};
        SetTextColor(dc, c_.current ? Theme::kMuted : Theme::kFaint);
        DrawTextW(dc, s.c_str(), static_cast<int>(s.size()), &b,
                  DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS | DirFlags(c_.back));
        y += theme_->smallLineHeight;
    }
    if (!c_.note.empty()) {
        RECT n{r.left, y, r.right, y + theme_->smallLineHeight};
        SetTextColor(dc, c_.warn ? Theme::kWarn : Theme::kMuted);
        DrawTextW(dc, c_.note.c_str(), static_cast<int>(c_.note.size()), &n,
                  DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    }

    BitBlt(wdc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldFont);
    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(dc);
    EndPaint(hwnd_, &ps);
}

void PreviewView::CopyText(const std::wstring& s) {
    if (!OpenClipboard(hwnd_)) return;
    EmptyClipboard();
    const size_t bytes = (s.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        if (void* p = GlobalLock(g)) {
            std::memcpy(p, s.c_str(), bytes);
            GlobalUnlock(g);
            if (!SetClipboardData(CF_UNICODETEXT, g)) GlobalFree(g);
        } else {
            GlobalFree(g);
        }
    }
    CloseClipboard();
}

LRESULT CALLBACK PreviewView::Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    PreviewView* self;
    if (msg == WM_NCCREATE) {
        self = static_cast<PreviewView*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = h;
    } else {
        self = reinterpret_cast<PreviewView*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    }
    return self ? self->Handle(msg, wp, lp) : DefWindowProcW(h, msg, wp, lp);
}

LRESULT PreviewView::Handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            Paint();
            return 0;
        case WM_LBUTTONUP:
            if (onClick_) onClick_();
            return 0;
        case WM_CONTEXTMENU: {
            if (c_.text.empty()) return 0;
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            if (pt.x == -1 && pt.y == -1) {
                pt = {theme_->S(20), theme_->S(10)};
                ClientToScreen(hwnd_, &pt);
            }
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, 1, Tr(L"Copy the preview").c_str());
            if (!c_.back.empty()) AppendMenuW(menu, MF_STRING, 2, Tr(L"Copy the back-translation").c_str());
            const UINT cmd = static_cast<UINT>(
                TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd_, nullptr));
            DestroyMenu(menu);
            if (cmd == 1) CopyText(c_.text);
            else if (cmd == 2) CopyText(c_.back);
            return 0;
        }
        case WM_NCDESTROY:
            SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
            break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

}  // namespace gct
