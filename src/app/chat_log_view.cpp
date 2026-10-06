// chat_log_view.cpp
#include "core/i18n.hpp"
#include "chat_log_view.hpp"

#include <windowsx.h>

#include <algorithm>
#include <cstring>

#include "core/chat_stream.hpp"
#include "core/gw2_text.hpp"

namespace gct {
namespace {

constexpr wchar_t kClass[] = L"GW2ChatTranslatorLog";
constexpr UINT kTextFlags = DT_WORDBREAK | DT_EDITCONTROL | DT_NOPREFIX;

UINT DirFlags(const std::wstring& s) { return IsRtlText(s) ? (DT_RTLREADING | DT_RIGHT) : DT_LEFT; }

int MeasureText(HDC dc, HFONT font, const std::wstring& s, int width, UINT dir) {
    if (s.empty()) return 0;
    SelectObject(dc, font);
    RECT r{0, 0, width, 0};
    DrawTextW(dc, s.c_str(), static_cast<int>(s.size()), &r, kTextFlags | dir | DT_CALCRECT);
    return r.bottom;
}

COLORREF ToColorRef(Rgb c) { return RGB(c.r, c.g, c.b); }

const Channel kCalibratable[] = {Channel::Say,     Channel::Map,  Channel::Party, Channel::Squad,
                                 Channel::Team,    Channel::Whisper, Channel::Guild};

}  // namespace

bool ChatLogView::Register(HINSTANCE inst) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = Proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClass;
    return RegisterClassExW(&wc) != 0;
}

bool ChatLogView::Create(HWND parent, HINSTANCE inst, const Theme* theme, Callbacks cb) {
    theme_ = theme;
    cb_ = std::move(cb);
    palette_ = DefaultChannelColors();
    hwnd_ = CreateWindowExW(0, kClass, L"", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, parent, nullptr, inst, this);
    return hwnd_ != nullptr;
}

bool ChatLogView::Matches(const ChatEntry& e, ChannelMask channels, uint32_t tabId) {
    return (channels & ChannelBit(e.channel)) != 0 || (e.tabId != 0 && e.tabId == tabId);
}

bool ChatLogView::ShowsTranslation(const std::wstring& text) const {
    size_t looked = 0;
    for (auto it = entries_.rbegin(); it != entries_.rend() && looked < 80; ++it, ++looked) {
        if (it->state != ChatEntry::State::Translated || it->main.size() < 8) continue;
        if (DiceSimilarity(text, it->main) >= 0.9) return true;
    }
    return false;
}

uint64_t ChatLogView::Add(ChatEntry e) {
    e.id = nextId_++;
    const bool own = e.kind == ChatEntry::Kind::Outgoing;
    entries_.push_back(std::move(e));
    while (entries_.size() > kMaxEntries) entries_.pop_front();
    if (own) stickToBottom_ = true;  // your own message: jump to it
    layoutDirty_ = true;
    Relayout();
    InvalidateRect(hwnd_, nullptr, FALSE);
    return entries_.back().id;
}

void ChatLogView::Update(uint64_t id, const std::function<void(ChatEntry&)>& change) {
    // Ids grow monotonically: binary search.
    auto it = std::lower_bound(entries_.begin(), entries_.end(), id,
                               [](const ChatEntry& e, uint64_t v) { return e.id < v; });
    if (it == entries_.end() || it->id != id) return;
    change(*it);
    layoutDirty_ = true;
    Relayout();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void ChatLogView::Clear() {
    entries_.clear();
    scroll_ = 0;
    stickToBottom_ = true;
    layoutDirty_ = true;
    Relayout();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void ChatLogView::SetFilter(ChannelMask channels, uint32_t tabId) {
    if (channels == filterChannels_ && tabId == filterTab_) return;
    filterChannels_ = channels;
    filterTab_ = tabId;
    stickToBottom_ = true;
    layoutDirty_ = true;
    Relayout();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void ChatLogView::SetPalette(const std::vector<ChannelColor>& palette) {
    palette_ = palette;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void ChatLogView::ThemeChanged() {
    layoutDirty_ = true;
    Relayout();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void ChatLogView::SetEmptyHint(const std::wstring& text, bool clickable) {
    if (text == hint_ && clickable == hintClickable_) return;
    hint_ = text;
    hintClickable_ = clickable;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// ---------------------------------------------------------------------------
// Like the GW2 chat, but calmer: no timestamps, no channel tags. The channel
// shows only as the colour of the line.
std::wstring ChatLogView::MainLine(const ChatEntry& e) const {
    if (e.kind == ChatEntry::Kind::System) return e.main;
    std::wstring who;
    if (e.kind == ChatEntry::Kind::Outgoing) {
        who = Tr(L"You");
        if (e.channel == Channel::Whisper && !e.speaker.empty()) who += L" \u2192 " + e.speaker;
    } else {
        who = e.speaker;
        if (e.whisperOut && !e.speaker.empty()) who = Tr(L"You") + L" \u2192 " + e.speaker;
    }
    return who.empty() ? e.main : who + L": " + e.main;
}

std::wstring ChatLogView::SecondaryLine(const ChatEntry& e) const {
    if (e.state == ChatEntry::State::Pending) return Tr(L"translating \u2026");
    if (!e.note.empty()) return e.note;
    if (e.original.empty() || e.original == e.main) return {};
    if (e.kind == ChatEntry::Kind::Outgoing) return TrF(L"Original: {1}", {e.original});
    return e.lang.empty() ? e.original : e.lang + L": " + e.original;
}

COLORREF ChatLogView::MainColor(const ChatEntry& e) const {
    if (e.kind == ChatEntry::Kind::System) return Theme::kMuted;
    for (const ChannelColor& c : palette_)
        if (c.channel == e.channel) return ToColorRef(c.rgb);
    return Theme::kText;
}

void ChatLogView::Relayout() {
    RECT rc;
    GetClientRect(hwnd_, &rc);
    const int pad = theme_->S(10);
    const int width = std::max(10, static_cast<int>(rc.right) - 2 * pad);
    if (layoutDirty_ || width != layoutWidth_) {
        HDC dc = GetDC(hwnd_);
        rows_.clear();
        int y = pad;
        for (size_t i = 0; i < entries_.size(); ++i) {
            const ChatEntry& e = entries_[i];
            if (!Matches(e, filterChannels_, filterTab_)) continue;
            int h = MeasureText(dc, theme_->fontText, MainLine(e), width, DirFlags(e.main));
            const std::wstring sec = SecondaryLine(e);
            if (!sec.empty()) h += theme_->S(1) + MeasureText(dc, theme_->fontSmall, sec, width, DirFlags(sec));
            rows_.push_back({i, y, h});
            y += h + theme_->S(7);
        }
        total_ = y;
        layoutWidth_ = width;
        layoutDirty_ = false;
        ReleaseDC(hwnd_, dc);
    }
    ScrollTo(stickToBottom_ ? MaxScroll() : scroll_);
}

int ChatLogView::MaxScroll() const {
    RECT rc;
    GetClientRect(hwnd_, &rc);
    return std::max(0, total_ - static_cast<int>(rc.bottom));
}

void ChatLogView::ScrollTo(int y) {
    const int maxScroll = MaxScroll();
    scroll_ = std::clamp(y, 0, maxScroll);
    stickToBottom_ = scroll_ >= maxScroll;
}

int ChatLogView::RowAt(int clientY) const {
    const int y = clientY + scroll_;
    for (size_t i = 0; i < rows_.size(); ++i)
        if (y >= rows_[i].top - theme_->S(3) && y < rows_[i].top + rows_[i].height + theme_->S(4))
            return static_cast<int>(i);
    return -1;
}

void ChatLogView::Paint() {
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

    const int pad = theme_->S(10);
    hintRect_ = {};
    if (rows_.empty() && !hint_.empty()) {
        SelectObject(dc, theme_->fontUi);
        SetTextColor(dc, hintClickable_ ? Theme::kAccent : Theme::kFaint);
        RECT r = rc;
        InflateRect(&r, -pad * 2, -pad);
        RECT calc = r;
        const UINT rtl = UiRtl() ? DT_RTLREADING : 0;
        DrawTextW(dc, hint_.c_str(), -1, &calc, kTextFlags | DT_CENTER | DT_CALCRECT | rtl);
        const int h = calc.bottom - calc.top;
        r.top += std::max(0, static_cast<int>(r.bottom - r.top - h) / 2);
        r.bottom = r.top + h;
        DrawTextW(dc, hint_.c_str(), -1, &r, kTextFlags | DT_CENTER | rtl);
        hintRect_ = r;
    }

    for (const Row& row : rows_) {
        const int top = row.top - scroll_;
        if (top > rc.bottom || top + row.height < 0) continue;
        const ChatEntry& e = entries_[row.index];
        RECT r{pad, top, pad + layoutWidth_, top + row.height};

        const std::wstring main = MainLine(e);
        const UINT dir = DirFlags(e.main);
        SelectObject(dc, theme_->fontText);
        SetTextColor(dc, MainColor(e));
        RECT t = r;
        DrawTextW(dc, main.c_str(), static_cast<int>(main.size()), &t, kTextFlags | dir);
        r.top += MeasureText(dc, theme_->fontText, main, layoutWidth_, dir) + theme_->S(1);

        const std::wstring sec = SecondaryLine(e);
        if (!sec.empty()) {
            SelectObject(dc, theme_->fontSmall);
            SetTextColor(dc, e.state == ChatEntry::State::Failed ? Theme::kWarn : Theme::kMuted);
            DrawTextW(dc, sec.c_str(), static_cast<int>(sec.size()), &r, kTextFlags | DirFlags(sec));
        }
    }

    // Thin scroll indicator.
    const int maxScroll = MaxScroll();
    if (maxScroll > 0) {
        const int h = rc.bottom;
        const int barH = std::max(theme_->S(20), h * h / std::max(1, total_));
        const int barY = (h - barH) * scroll_ / maxScroll;
        RECT bar{rc.right - theme_->S(4), barY, rc.right - theme_->S(1), barY + barH};
        HBRUSH b = CreateSolidBrush(Theme::kFaint);
        FillRect(dc, &bar, b);
        DeleteObject(b);
    }

    BitBlt(wdc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldFont);
    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(dc);
    EndPaint(hwnd_, &ps);
}

void ChatLogView::CopyText(const std::wstring& s) {
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

void ChatLogView::ShowMenu(POINT screen) {
    POINT client = screen;
    ScreenToClient(hwnd_, &client);
    const int r = RowAt(client.y);
    const ChatEntry* e = r >= 0 ? &entries_[rows_[static_cast<size_t>(r)].index] : nullptr;

    enum : UINT { kCopyMain = 1, kCopyOriginal, kReply, kUseChannel, kClear, kCalibrateBase = 100 };
    HMENU menu = CreatePopupMenu();
    HMENU colors = nullptr;
    if (e) {
        const bool hasOriginal = !e->original.empty() && e->original != e->main;
        AppendMenuW(menu, MF_STRING, kCopyMain, (hasOriginal ? Tr(L"Copy the translation") : Tr(L"Copy the text")).c_str());
        if (hasOriginal) AppendMenuW(menu, MF_STRING, kCopyOriginal, Tr(L"Copy the original").c_str());
        if (e->kind == ChatEntry::Kind::Incoming && e->channel == Channel::Whisper && !e->whisperOut &&
            !e->speaker.empty()) {
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, kReply, TrF(L"Reply to {1}", {e->speaker}).c_str());
        } else if (e->kind == ChatEntry::Kind::Incoming && ChannelCommand(e->channel) &&
                   e->channel != Channel::Whisper) {
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, kUseChannel,
                        TrF(L"Answer in \u201c{1}\u201d", {ChannelLabel(e->channel)}).c_str());
        }
        if (e->kind == ChatEntry::Kind::Incoming && e->hasColor) {
            colors = CreatePopupMenu();
            for (size_t i = 0; i < std::size(kCalibratable); ++i) {
                const Channel ch = kCalibratable[i];
                AppendMenuW(colors, MF_STRING | (ch == e->channel ? MF_CHECKED : 0), kCalibrateBase + i,
                            ChannelLabel(ch).c_str());
            }
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(colors),
                        Tr(L"This line colour is").c_str());
        }
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    }
    AppendMenuW(menu, MF_STRING | (entries_.empty() ? MF_GRAYED : 0), kClear, Tr(L"Clear the history").c_str());

    const uint64_t id = e ? e->id : 0;
    const UINT cmd = static_cast<UINT>(
        TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY | (UiRtl() ? TPM_LAYOUTRTL : 0), screen.x,
                       screen.y, 0, hwnd_, nullptr));
    DestroyMenu(menu);  // also destroys the submenu

    // The entry may have been dropped while the menu was open: look it up again.
    const ChatEntry* now = nullptr;
    for (const ChatEntry& x : entries_)
        if (x.id == id) now = &x;
    if (cmd == kClear) {
        Clear();
        return;
    }
    if (!now) return;
    if (cmd == kCopyMain) CopyText(now->main);
    else if (cmd == kCopyOriginal) CopyText(now->original);
    else if (cmd == kReply && cb_.onReply) cb_.onReply(now->speaker);
    else if (cmd == kUseChannel && cb_.onUseChannel) cb_.onUseChannel(now->channel);
    else if (cmd >= kCalibrateBase && cmd - kCalibrateBase < std::size(kCalibratable) && cb_.onCalibrate)
        cb_.onCalibrate(kCalibratable[cmd - kCalibrateBase], now->color);
}

// ---------------------------------------------------------------------------
LRESULT CALLBACK ChatLogView::Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    ChatLogView* self;
    if (msg == WM_NCCREATE) {
        self = static_cast<ChatLogView*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = h;
    } else {
        self = reinterpret_cast<ChatLogView*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    }
    return self ? self->Handle(msg, wp, lp) : DefWindowProcW(h, msg, wp, lp);
}

LRESULT ChatLogView::Handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_SIZE:
            Relayout();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            Paint();
            return 0;
        case WM_MOUSEWHEEL: {
            const int delta = GET_WHEEL_DELTA_WPARAM(wp);
            ScrollTo(scroll_ - delta * 3 * theme_->textLineHeight / WHEEL_DELTA);
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        case WM_LBUTTONUP: {
            const POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            if (hintClickable_ && rows_.empty() && PtInRect(&hintRect_, pt) && cb_.onHintClick) cb_.onHintClick();
            return 0;
        }
        case WM_SETCURSOR: {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd_, &pt);
            if (LOWORD(lp) == HTCLIENT && hintClickable_ && rows_.empty() && PtInRect(&hintRect_, pt)) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            break;
        }
        case WM_CONTEXTMENU: {
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            if (pt.x == -1 && pt.y == -1) {
                pt = {theme_->S(20), theme_->S(20)};
                ClientToScreen(hwnd_, &pt);
            }
            ShowMenu(pt);
            return 0;
        }
        case WM_NCDESTROY:
            SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
            break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

}  // namespace gct
