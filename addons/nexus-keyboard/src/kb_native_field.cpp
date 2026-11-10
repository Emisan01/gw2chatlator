// kb_native_field.cpp — native Windows RichEdit input field ("Fallen field") for external typing tools (Grammarly & co.).

#include "kb_shared.hpp"

#include <richedit.h>

namespace kb {

std::atomic<bool> g_nativeEnter{false};

namespace {

constexpr const wchar_t* kNativeFieldClass = L"GW2ChatlatorInputField";
constexpr UINT kMsgSync = WM_APP + 1;
constexpr UINT kMsgUnfocus = WM_APP + 2;

struct DesiredState {
    bool enabled = false;
    bool visible = false;
    int x = 0, y = 0, w = 0, h = 0;
    int fontPx = 16;
    COLORREF bgColor = RGB(24, 26, 30);
    COLORREF fgColor = RGB(235, 238, 245);
    bool wantFocus = false;
    bool hasPendingText = false;
    std::wstring pendingText;
};

std::mutex s_mu;        // leaf mutex for s_desired
std::mutex s_threadMu;  // protects s_thread start / join
DesiredState s_desired;
std::string s_syncedInput;  // protected by g_mu

std::thread s_thread;
std::atomic<bool> s_running{false};
std::atomic<HWND> s_win{nullptr};
std::atomic<bool> s_hasFocus{false};

// Field thread local state:
HWND s_edit = nullptr;
WNDPROC s_origEditProc = nullptr;
HFONT s_font = nullptr;
HBRUSH s_bgBrush = nullptr;
bool s_settingText = false;
int s_curX = -1, s_curY = -1, s_curW = -1, s_curH = -1;
int s_curFontPx = 0;
COLORREF s_curBg = 0xFFFFFFFF;
COLORREF s_curFg = 0xFFFFFFFF;
bool s_isShown = false;

std::wstring ReadEditText(HWND h) {
    if (!h) return {};
    const int len = GetWindowTextLengthW(h);
    if (len <= 0) return {};
    std::wstring wtext(static_cast<size_t>(len) + 1, L'\0');
    const int got = GetWindowTextW(h, wtext.data(), len + 1);
    wtext.resize(static_cast<size_t>(std::max(0, got)));
    return wtext;
}

void ApplyCharFormat(HWND edit, COLORREF fg) {
    if (!edit) return;
    CHARFORMAT2W cf{};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR;
    cf.crTextColor = fg;
    SendMessageW(edit, EM_SETCHARFORMAT, SCF_DEFAULT, reinterpret_cast<LPARAM>(&cf));
    SendMessageW(edit, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&cf));
}

void ApplyDesired(HWND win) {
    DesiredState d;
    {
        std::lock_guard<std::mutex> lock(s_mu);
        d = s_desired;
        s_desired.wantFocus = false;
        s_desired.hasPendingText = false;
    }
    if (!d.enabled) {
        DestroyWindow(win);
        return;
    }

    const int targetFontPx = std::clamp(d.fontPx, 10, 48);
    const bool fontChanged = (targetFontPx != s_curFontPx);
    if (fontChanged) {
        HFONT nf = CreateFontW(-targetFontPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        if (nf) {
            if (s_edit) SendMessageW(s_edit, WM_SETFONT, reinterpret_cast<WPARAM>(nf), TRUE);
            if (s_font) DeleteObject(s_font);
            s_font = nf;
            s_curFontPx = targetFontPx;
        }
    }

    if (d.bgColor != s_curBg) {
        HBRUSH nb = CreateSolidBrush(d.bgColor);
        if (nb) {
            if (s_bgBrush) DeleteObject(s_bgBrush);
            s_bgBrush = nb;
            s_curBg = d.bgColor;
        }
        if (s_edit) {
            SendMessageW(s_edit, EM_SETBKGNDCOLOR, 0, static_cast<LPARAM>(d.bgColor));
            InvalidateRect(s_edit, nullptr, TRUE);
        }
        InvalidateRect(win, nullptr, TRUE);
    }

    if (d.fgColor != s_curFg || fontChanged) {
        s_curFg = d.fgColor;
        ApplyCharFormat(s_edit, d.fgColor);
    }

    if (d.hasPendingText && s_edit) {
        const std::wstring cur = ReadEditText(s_edit);
        if (cur != d.pendingText) {
            s_settingText = true;
            SetWindowTextW(s_edit, d.pendingText.c_str());
            ApplyCharFormat(s_edit, s_curFg);
            const LPARAM endPos = static_cast<LPARAM>(d.pendingText.size());
            SendMessageW(s_edit, EM_SETSEL, endPos, endPos);
            s_settingText = false;
        }
    }

    if (d.w > 0 && d.h > 0 && (d.x != s_curX || d.y != s_curY || d.w != s_curW || d.h != s_curH)) {
        s_curX = d.x;
        s_curY = d.y;
        s_curW = d.w;
        s_curH = d.h;
        SetWindowPos(win, HWND_TOPMOST, d.x, d.y, d.w, d.h, SWP_NOACTIVATE);
        if (s_edit) {
            SetWindowPos(s_edit, nullptr, 4, 2, std::max(1, d.w - 8), std::max(1, d.h - 4),
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }

    bool fgOk = true;
    if (g_gameHwnd && !g_test.forceMap) {
        const HWND fg = GetForegroundWindow();
        fgOk = (fg == g_gameHwnd || fg == win || fg == s_edit || GetFocus() == s_edit);
    }
    const bool shouldShow = d.visible && fgOk && d.w > 0 && d.h > 0;
    if (shouldShow != s_isShown) {
        s_isShown = shouldShow;
        ShowWindow(win, shouldShow ? SW_SHOWNOACTIVATE : SW_HIDE);
    }

    if (d.wantFocus && shouldShow && s_edit) {
        SetForegroundWindow(win);
        SetFocus(s_edit);
        const int len = GetWindowTextLengthW(s_edit);
        SendMessageW(s_edit, EM_SETSEL, len, len);
        s_hasFocus.store(true);
    }
}

LRESULT CALLBACK EditSubProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_SETFOCUS) {
        s_hasFocus.store(true);
    } else if (msg == WM_KILLFOCUS) {
        s_hasFocus.store(false);
    } else if (msg == WM_KEYDOWN) {
        if (wp == VK_RETURN) {
            if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) {
                SendMessageW(h, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"\r\n"));
                return 0;
            }
            std::wstring wtext = ReadEditText(h);
            while (!wtext.empty() && (wtext.back() == L'\r' || wtext.back() == L'\n')) wtext.pop_back();
            s_settingText = true;
            SetWindowTextW(h, L"");
            s_settingText = false;
            if (!wtext.empty()) {
                const std::string u8 = U8(wtext);
                {
                    std::lock_guard<std::mutex> lock(g_mu);
                    CopyUtf8(g_input, u8);
                    g_flatInput = Flat(g_input);
                    g_wFlatInput = W(g_flatInput);
                    ++g_inputGen;
                    s_syncedInput.clear();
                    g_nativeEnter.store(true);
                }
                TechLog('O', L"native field: Enter, " + std::to_wstring(wtext.size()) + L" characters");
            }
            s_hasFocus.store(false);
            if (g_gameHwnd) {
                SetForegroundWindow(g_gameHwnd);
                SetFocus(g_gameHwnd);
            }
            return 0;
        }
        if (wp == VK_ESCAPE) {
            s_hasFocus.store(false);
            if (g_gameHwnd) {
                SetForegroundWindow(g_gameHwnd);
                SetFocus(g_gameHwnd);
            }
            return 0;
        }
    } else if (msg == WM_CHAR && (wp == '\r' || wp == 27)) {
        return 0;
    }
    return CallWindowProcW(s_origEditProc, h, msg, wp, lp);
}

LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_ERASEBKGND: {
            if (s_bgBrush) {
                RECT rc{};
                GetClientRect(h, &rc);
                FillRect(reinterpret_cast<HDC>(wp), &rc, s_bgBrush);
                return 1;
            }
            break;
        }
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC: {
            HDC hdc = reinterpret_cast<HDC>(wp);
            SetTextColor(hdc, s_curFg);
            SetBkColor(hdc, s_curBg);
            if (s_bgBrush) return reinterpret_cast<LRESULT>(s_bgBrush);
            break;
        }
        case WM_COMMAND: {
            if (HIWORD(wp) == EN_CHANGE && !s_settingText && s_edit) {
                std::wstring wtext = ReadEditText(s_edit);
                while (!wtext.empty() && (wtext.back() == L'\r' || wtext.back() == L'\n')) wtext.pop_back();
                const std::string u8 = U8(wtext);
                std::lock_guard<std::mutex> lock(g_mu);
                CopyUtf8(g_input, u8);
                s_syncedInput = g_input;
                return 0;
            }
            break;
        }
        case kMsgSync:
        case WM_TIMER: {
            ApplyDesired(h);
            return 0;
        }
        case kMsgUnfocus: {
            s_hasFocus.store(false);
            if (g_gameHwnd && (GetFocus() == s_edit || GetForegroundWindow() == h)) {
                SetForegroundWindow(g_gameHwnd);
                SetFocus(g_gameHwnd);
            }
            return 0;
        }
        case WM_CLOSE: {
            DestroyWindow(h);
            return 0;
        }
        case WM_DESTROY: {
            KillTimer(h, 1);
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void NativeFieldThreadMain() {
    Shielded([] {
        static HMODULE s_msftEdit = LoadLibraryW(L"Msftedit.dll");
        const wchar_t* editClass = s_msftEdit ? MSFTEDIT_CLASS : L"EDIT";

        s_edit = nullptr;
        s_origEditProc = nullptr;
        s_font = nullptr;
        s_bgBrush = nullptr;
        s_settingText = false;
        s_curX = s_curY = s_curW = s_curH = -1;
        s_curFontPx = 0;
        s_curBg = 0xFFFFFFFF;
        s_curFg = 0xFFFFFFFF;
        s_isShown = false;
        s_hasFocus.store(false);

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = WndProc;
        wc.hInstance = g_self;
        wc.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
        wc.lpszClassName = kNativeFieldClass;
        RegisterClassExW(&wc);

        HWND win = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kNativeFieldClass, L"",
                                   WS_POPUP | WS_CLIPCHILDREN, 0, 0, 100, 28, g_gameHwnd, nullptr, g_self, nullptr);
        if (win) {
            s_edit = CreateWindowExW(0, editClass, L"",
                                     WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
                                     4, 2, 92, 24, win, nullptr, g_self, nullptr);
            if (s_edit) {
                SendMessageW(s_edit, EM_SETEVENTMASK, 0, ENM_CHANGE);
                SendMessageW(s_edit, EM_EXLIMITTEXT, 0, sizeof(g_input) - 1);
                s_origEditProc = reinterpret_cast<WNDPROC>(
                    SetWindowLongPtrW(s_edit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(EditSubProc)));
            }
            s_win.store(win);
            SetTimer(win, 1, 30, nullptr);
            ApplyDesired(win);

            MSG msg;
            while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }

        s_win.store(nullptr);
        s_hasFocus.store(false);
        s_edit = nullptr;
        if (s_font) {
            DeleteObject(s_font);
            s_font = nullptr;
        }
        if (s_bgBrush) {
            DeleteObject(s_bgBrush);
            s_bgBrush = nullptr;
        }
        UnregisterClassW(kNativeFieldClass, g_self);
    });
    s_running.store(false);
}

void EnsureThreadStarted() {
    if (s_running.load()) return;
    std::lock_guard<std::mutex> lock(s_threadMu);
    if (s_running.load()) return;
    if (s_thread.joinable()) s_thread.join();
    s_running.store(true);
    s_thread = std::thread(NativeFieldThreadMain);
}

}  // namespace

void UpdateNativeField(int x, int y, int w, int h, bool visible, int fontPx, COLORREF bg, COLORREF fg, bool wantFocus) {
    // Called on the render thread with g_mu held.
    bool pushText = false;
    std::wstring textToPush;
    if (!s_running.load()) {
        s_syncedInput.clear();
    }
    if (s_syncedInput != g_input) {
        s_syncedInput = g_input;
        textToPush = W(Flat(g_input));
        pushText = true;
    }

    bool wake = false;
    {
        std::lock_guard<std::mutex> lock(s_mu);
        const bool changed = !s_desired.enabled || s_desired.visible != visible || s_desired.x != x ||
                             s_desired.y != y || s_desired.w != w || s_desired.h != h || s_desired.fontPx != fontPx ||
                             s_desired.bgColor != bg || s_desired.fgColor != fg || wantFocus || pushText;
        s_desired.enabled = true;
        s_desired.visible = visible;
        s_desired.x = x;
        s_desired.y = y;
        s_desired.w = w;
        s_desired.h = h;
        s_desired.fontPx = fontPx;
        s_desired.bgColor = bg;
        s_desired.fgColor = fg;
        if (wantFocus) s_desired.wantFocus = true;
        if (pushText) {
            s_desired.pendingText = std::move(textToPush);
            s_desired.hasPendingText = true;
        }
        wake = changed;
    }

    EnsureThreadStarted();
    if (wake) {
        if (HWND win = s_win.load()) PostMessageW(win, kMsgSync, 0, 0);
    }
}

void HideNativeField() {
    bool wake = false;
    {
        std::lock_guard<std::mutex> lock(s_mu);
        if (s_desired.visible) {
            s_desired.visible = false;
            wake = true;
        }
    }
    if (wake) {
        if (HWND win = s_win.load()) PostMessageW(win, kMsgSync, 0, 0);
    }
}

void UnfocusNativeField() {
    s_hasFocus.store(false);
    if (HWND win = s_win.load()) PostMessageW(win, kMsgUnfocus, 0, 0);
}

void StopNativeField() {
    // Must be called WITHOUT g_mu held if it joins s_thread.
    {
        std::lock_guard<std::mutex> lock(s_mu);
        s_desired.enabled = false;
        s_desired.visible = false;
    }
    if (HWND win = s_win.load()) {
        PostMessageW(win, WM_CLOSE, 0, 0);
    }
    std::lock_guard<std::mutex> lock(s_threadMu);
    if (s_thread.joinable()) {
        // In case s_win was not stored yet when StopNativeField was called, wait briefly and post WM_CLOSE once ready.
        while (s_running.load()) {
            if (HWND win = s_win.load()) PostMessageW(win, WM_CLOSE, 0, 0);
            Sleep(2);
        }
        s_thread.join();
    }
    s_hasFocus.store(false);
}

bool NativeFieldHasFocus() { return s_hasFocus.load(); }

}  // namespace kb

