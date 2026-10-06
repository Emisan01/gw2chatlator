// gw2_sender.cpp — clipboard round-trip + key injection into the GW2 chat.
#include "gw2_sender.hpp"

#include <cstring>
#include <vector>

namespace gct {
namespace {

// ---------------------------------------------------------------------------
// Clipboard snapshot. Every HGLOBAL-backed format is copied byte for byte, so
// text, rich text, HTML, file lists and DIB images all come back. GDI handle
// formats (CF_BITMAP etc.) are skipped; Windows re-synthesises CF_BITMAP from
// the saved CF_DIB.
// ---------------------------------------------------------------------------
struct SavedFormat {
    UINT format = 0;
    std::vector<unsigned char> data;
};

bool IsHandleFormat(UINT f) {
    switch (f) {
        case CF_BITMAP:
        case CF_METAFILEPICT:
        case CF_PALETTE:
        case CF_ENHMETAFILE:
        case CF_OWNERDISPLAY:
        case CF_DSPBITMAP:
        case CF_DSPMETAFILEPICT:
        case CF_DSPENHMETAFILE:
            return true;
        default:
            return (f >= CF_PRIVATEFIRST && f <= CF_PRIVATELAST) || (f >= CF_GDIOBJFIRST && f <= CF_GDIOBJLAST);
    }
}

bool OpenClipboardRetry(HWND owner) {
    for (int i = 0; i < 20; ++i) {
        if (OpenClipboard(owner)) return true;
        Sleep(15);  // another app holds it for a moment
    }
    return false;
}

bool SaveClipboard(HWND owner, std::vector<SavedFormat>& out) {
    if (!OpenClipboardRetry(owner)) return false;
    constexpr SIZE_T kMaxFormatBytes = 64u << 20;
    UINT f = 0;
    while ((f = EnumClipboardFormats(f)) != 0) {
        if (IsHandleFormat(f)) continue;
        HANDLE h = GetClipboardData(f);
        if (!h) continue;
        SIZE_T size = GlobalSize(h);
        if (size == 0 || size > kMaxFormatBytes) continue;
        const void* p = GlobalLock(h);
        if (!p) continue;
        SavedFormat s;
        s.format = f;
        s.data.assign(static_cast<const unsigned char*>(p), static_cast<const unsigned char*>(p) + size);
        GlobalUnlock(h);
        out.push_back(std::move(s));
    }
    CloseClipboard();
    return true;
}

bool PutGlobal(UINT format, const void* data, SIZE_T size) {
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!g) return false;
    void* p = GlobalLock(g);
    if (!p) { GlobalFree(g); return false; }
    std::memcpy(p, data, size);
    GlobalUnlock(g);
    if (!SetClipboardData(format, g)) { GlobalFree(g); return false; }
    return true;  // clipboard owns g now
}

bool RestoreClipboard(HWND owner, const std::vector<SavedFormat>& saved) {
    if (!OpenClipboardRetry(owner)) return false;
    EmptyClipboard();
    for (const auto& s : saved) PutGlobal(s.format, s.data.data(), s.data.size());
    CloseClipboard();
    return true;
}

bool SetClipboardText(HWND owner, const std::wstring& text) {
    if (!OpenClipboardRetry(owner)) return false;
    EmptyClipboard();
    bool ok = PutGlobal(CF_UNICODETEXT, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
    CloseClipboard();
    return ok;
}

// ---------------------------------------------------------------------------
// Keyboard
// ---------------------------------------------------------------------------
INPUT Key(WORD vk, bool up) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.wScan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
    in.ki.dwFlags = KEYEVENTF_SCANCODE | (up ? KEYEVENTF_KEYUP : 0);
    return in;
}

bool Send(std::initializer_list<INPUT> keys) {
    std::vector<INPUT> v(keys);
    return SendInput(static_cast<UINT>(v.size()), v.data(), sizeof(INPUT)) == v.size();
}

bool AnyKeyHeld() {
    static const int keys[] = {VK_RETURN, VK_CONTROL, VK_SHIFT, VK_MENU, VK_LWIN, VK_RWIN};
    for (int k : keys)
        if (GetAsyncKeyState(k) & 0x8000) return true;
    return false;
}

// The user triggers sending with Enter / Ctrl+Enter. If those keys were still
// physically down when we inject, GW2 would see e.g. Ctrl+Enter.
bool WaitForKeysReleased(int timeoutMs) {
    for (int waited = 0; waited < timeoutMs; waited += 10) {
        if (!AnyKeyHeld()) return true;
        Sleep(10);
    }
    return !AnyKeyHeld();
}

// SendMessageTimeout returns once the target thread runs its message loop.
// Windows hands out sent messages before queued input, so a single ping can
// still overtake our keystrokes; after the second ping the loop has been
// through at least one full pass with our input in the queue.
bool WaitForTargetToDrain(HWND target, UINT timeoutMs) {
    for (int i = 0; i < 2; ++i) {
        DWORD_PTR result = 0;
        if (!SendMessageTimeoutW(target, WM_NULL, 0, 0, SMTO_NORMAL | SMTO_ABORTIFHUNG, timeoutMs, &result))
            return false;
        Sleep(10);
    }
    return true;
}

}  // namespace

HWND FindGw2Window(DWORD processId) {
    if (processId) {  // exact match through MumbleLink (several clients, renamed windows)
        struct Search {
            DWORD pid;
            HWND found;
        } search{processId, nullptr};
        EnumWindows(
            [](HWND h, LPARAM lp) -> BOOL {
                auto* s = reinterpret_cast<Search*>(lp);
                DWORD pid = 0;
                GetWindowThreadProcessId(h, &pid);
                if (pid != s->pid || !IsWindowVisible(h) || GetWindow(h, GW_OWNER)) return TRUE;
                s->found = h;
                return FALSE;
            },
            reinterpret_cast<LPARAM>(&search));
        if (search.found) return search.found;
    }
    HWND h = FindWindowW(L"ArenaNet_Gr_Window_Class", nullptr);
    if (!h) h = FindWindowW(nullptr, L"Guild Wars 2");
    return (h && IsWindowVisible(h)) ? h : nullptr;
}

bool CopyTextToClipboard(HWND owner, const std::wstring& text) { return SetClipboardText(owner, text); }

bool BringToFront(HWND hwnd, bool allowAltTap) {
    if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
    SetForegroundWindow(hwnd);
    for (int i = 0; i < 15; ++i) {
        if (GetForegroundWindow() == hwnd) return true;
        Sleep(20);
    }
    if (!allowAltTap) return false;
    // Foreground lock: a synthetic Alt tap counts as input from our process
    // and lifts it. Only used as a fallback.
    Send({Key(VK_MENU, false), Key(VK_MENU, true)});
    SetForegroundWindow(hwnd);
    for (int i = 0; i < 15; ++i) {
        if (GetForegroundWindow() == hwnd) return true;
        Sleep(20);
    }
    return false;
}

SendOutcome SendToGw2Chat(HWND owner, const std::wstring& text, const SendOptions& opt, MumbleLink* mumble) {
    SendOutcome r;
    MumbleState ms = mumble ? mumble->Read() : MumbleState{};
    HWND gw2 = FindGw2Window(ms.live ? ms.processId : 0);
    if (!gw2) {
        r.error = L"GW2-Fenster nicht gefunden";
        return r;
    }
    if (!WaitForKeysReleased(1500)) {
        r.error = L"Taste noch gedr\u00fcckt \u2013 nichts gesendet";
        return r;
    }

    std::vector<SavedFormat> saved;
    if (!SaveClipboard(owner, saved)) {
        r.error = L"Zwischenablage ist blockiert";
        return r;
    }
    if (!SetClipboardText(owner, text)) {
        RestoreClipboard(owner, saved);
        r.error = L"Zwischenablage ist blockiert";
        return r;
    }

    // MumbleLink tells us whether GW2's chat input is open. With it, each
    // step waits for the game instead of guessing with sleeps.
    auto textbox = [&](bool wantOpen, int timeoutMs) {
        if (!mumble) return false;
        for (int waited = 0; waited <= timeoutMs; waited += 10) {
            const MumbleState s = mumble->Read();
            if (!s.live) return false;
            if (s.TextboxHasFocus() == wantOpen) return true;
            Sleep(10);
        }
        return false;
    };

    // The old clipboard may only come back once GW2 has really processed the
    // paste — otherwise a slow frame would paste the *old* content (anything
    // the user had copied) into the chat. If GW2 does not confirm, the
    // translation simply stays on the clipboard.
    bool injected = false;
    auto finish = [&](const wchar_t* error) {
        const bool drained = !injected || WaitForTargetToDrain(gw2, 2000);
        if (drained) {
            if (injected) Sleep(opt.restoreDelayMs);
            r.clipboardRestored = RestoreClipboard(owner, saved);
        }
        r.ok = (error == nullptr);
        if (error) r.error = error;
        return r;
    };

    if (!BringToFront(gw2)) return finish(L"GW2 l\u00e4sst sich nicht in den Vordergrund holen");
    Sleep(opt.stepDelayMs);

    // Never type into the wrong window: re-check focus before every step,
    // and let GW2 work through each step before the next one.
    if (GetForegroundWindow() != gw2) return finish(L"Fokus verloren \u2013 abgebrochen");
    injected = true;
    const bool alreadyOpen = ms.live && mumble->Read().TextboxHasFocus();
    if (!alreadyOpen) {
        Send({Key(VK_RETURN, false), Key(VK_RETURN, true)});  // open chat
        WaitForTargetToDrain(gw2, 1000);
        if (!textbox(true, 600)) Sleep(opt.stepDelayMs);
    }

    if (GetForegroundWindow() != gw2) return finish(L"Fokus verloren \u2013 abgebrochen");
    Send({Key(VK_CONTROL, false), Key('V', false), Key('V', true), Key(VK_CONTROL, true)});  // paste
    WaitForTargetToDrain(gw2, 1000);
    Sleep(opt.stepDelayMs);

    if (GetForegroundWindow() != gw2) return finish(L"Fokus verloren \u2013 Text steht evtl. noch im Chatfeld");
    Send({Key(VK_RETURN, false), Key(VK_RETURN, true)});  // send
    r.confirmedByGame = textbox(false, 1000);              // input closed = message went out
    if (!r.confirmedByGame) Sleep(opt.stepDelayMs);
    return finish(nullptr);
}

}  // namespace gct
