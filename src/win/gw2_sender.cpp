// gw2_sender.cpp — clipboard round-trip + key injection into the GW2 chat.
#include "core/i18n.hpp"
#include "core/text.hpp"
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

// GW2 looks at the keyboard once per frame. A key pressed and released in the
// same batch can fall between two frames and is lost; for Ctrl+V the game may
// even see V after Ctrl is already up. So keys are held for a few frames.
void Tap(WORD vk, int holdMs) {
    Send({Key(vk, false)});
    Sleep(holdMs);
    Send({Key(vk, true)});
}

void CtrlTap(WORD vk, int holdMs) {
    Send({Key(VK_CONTROL, false)});
    Sleep(holdMs);
    Send({Key(vk, false)});
    Sleep(holdMs);
    Send({Key(vk, true)});
    Sleep(holdMs);
    Send({Key(VK_CONTROL, true)});
}

// Characters typed as keys (a chat command), each held a few frames like every key we send.
void TypeText(const std::wstring& s, int holdMs) {
    for (wchar_t c : s) {
        INPUT in{};
        in.type = INPUT_KEYBOARD;
        in.ki.wScan = c;
        in.ki.dwFlags = KEYEVENTF_UNICODE;
        SendInput(1, &in, sizeof(INPUT));
        Sleep(holdMs);
        in.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        SendInput(1, &in, sizeof(INPUT));
        Sleep(holdMs);
    }
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
        r.error = Tr(L"GW2 window not found");
        return r;
    }
    if (!WaitForKeysReleased(1500)) {
        r.error = Tr(L"A key is still held down – nothing sent");
        return r;
    }

    std::vector<SavedFormat> saved;
    if (!SaveClipboard(owner, saved)) {
        r.error = Tr(L"The clipboard is blocked");
        return r;
    }
    if (!SetClipboardText(owner, text)) {
        RestoreClipboard(owner, saved);
        r.error = Tr(L"The clipboard is blocked");
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
    auto finish = [&](const std::wstring& error) {
        const bool drained = !injected || WaitForTargetToDrain(gw2, 2000);
        if (drained) {
            if (injected) Sleep(opt.restoreDelayMs);
            r.clipboardRestored = RestoreClipboard(owner, saved);
        }
        r.ok = error.empty();
        if (!error.empty()) r.error = error;
        return r;
    };

    if (!BringToFront(gw2)) return finish(Tr(L"GW2 cannot be brought to the front"));
    Sleep(opt.stepDelayMs);

    // Never type into the wrong window: re-check focus before every step,
    // and let GW2 work through each step before the next one.
    if (GetForegroundWindow() != gw2) return finish(Tr(L"Focus lost – cancelled"));
    injected = true;
    const bool alreadyOpen = ms.live && mumble->Read().TextboxHasFocus();
    if (!alreadyOpen) {
        Tap(VK_RETURN, opt.keyHoldMs);  // open chat
        WaitForTargetToDrain(gw2, 1000);
        if (!textbox(true, 600)) Sleep(opt.stepDelayMs);
        else Sleep(opt.keyHoldMs);  // the line is open; one more frame before typing into it
    }

    // A chat command ("/w Name, text", "/p text") is typed the way a person does it: GW2 only switches the channel
    // when it sees the command typed – pasted, the whole line was sent as text. For a whisper the name is then the
    // address (confirmed with Tab), and only the message is pasted.
    std::wstring message = text;
    if (!text.empty() && text[0] == L'/') {
        const ChatSplit split = SplitChatCommand(text);
        const size_t sp = text.find(L' ');
        if (!split.prefix.empty() && sp != std::wstring::npos) {
            const std::wstring command = text.substr(0, sp + 1);  // "/w "
            std::wstring recipient = Trim(split.prefix.substr(command.size()));
            if (!recipient.empty() && recipient.back() == L',') recipient.pop_back();
            recipient = Trim(recipient);
            if (GetForegroundWindow() != gw2) return finish(Tr(L"Focus lost – cancelled"));
            TypeText(command, opt.keyHoldMs);
            WaitForTargetToDrain(gw2, 1000);
            Sleep(opt.stepDelayMs);
            if (!recipient.empty()) {
                if (GetForegroundWindow() != gw2 || !SetClipboardText(owner, recipient))
                    return finish(Tr(L"Focus lost – cancelled"));
                CtrlTap('V', opt.keyHoldMs);  // the address
                WaitForTargetToDrain(gw2, 1000);
                Sleep(opt.stepDelayMs);
                if (GetForegroundWindow() != gw2) return finish(Tr(L"Focus lost – cancelled"));
                Tap(VK_TAB, opt.keyHoldMs);  // confirm it: the cursor moves to the message
                WaitForTargetToDrain(gw2, 1000);
                Sleep(opt.stepDelayMs);
            }
            message = split.body;
            if (!SetClipboardText(owner, message)) return finish(Tr(L"The clipboard is blocked"));
        }
    }

    if (GetForegroundWindow() != gw2) return finish(Tr(L"Focus lost – cancelled"));
    CtrlTap('V', opt.keyHoldMs);  // paste
    WaitForTargetToDrain(gw2, 1000);
    Sleep(opt.stepDelayMs);  // the pasted text must be in the line before Enter

    if (GetForegroundWindow() != gw2) return finish(Tr(L"Focus lost – the text may still be in the chat line"));
    Tap(VK_RETURN, opt.keyHoldMs);  // send
    r.confirmedByGame = textbox(false, 1000);              // input closed = message went out
    if (!r.confirmedByGame) Sleep(opt.stepDelayMs);
    return finish(std::wstring());
}

}  // namespace gct
