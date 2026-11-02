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
    // Delete and its neighbours share their scan code with the number pad: without the extended flag Delete is the
    // pad's "," while Num Lock is on
    const bool extended = vk == VK_DELETE || vk == VK_INSERT || vk == VK_HOME || vk == VK_END || vk == VK_PRIOR ||
                          vk == VK_NEXT || vk == VK_LEFT || vk == VK_RIGHT || vk == VK_UP || vk == VK_DOWN;
    in.ki.dwFlags = KEYEVENTF_SCANCODE | (extended ? KEYEVENTF_EXTENDEDKEY : 0) | (up ? KEYEVENTF_KEYUP : 0);
    in.ki.dwExtraInfo = kGw2SenderExtraInfo;
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
        in.ki.dwExtraInfo = kGw2SenderExtraInfo;
        SendInput(1, &in, sizeof(INPUT));
        Sleep(holdMs);
        in.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        in.ki.dwExtraInfo = kGw2SenderExtraInfo;
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
    if (processId) {  // exactly that process (several clients, renamed windows) – never another GW2 instead
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
        return search.found;  // the add-on asks for its own process: a test host has no GW2 window, and must not get one
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

SendOutcome SendToGw2Chat(HWND owner, const std::wstring& text, const SendOptions& opt, MumbleLink* mumble,
                          bool (*keysReachGame)()) {
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

    // The steps for Options > Technical: each key and whether GW2's chat line was open right after it ("open"/"shut";
    // "?" without MumbleLink) – where a send went wrong shows here. Never the text or the name.
    const ULONGLONG tStart = GetTickCount64();
    auto note = [&](const wchar_t* step) {
        const MumbleState s = mumble ? mumble->Read() : MumbleState{};
        r.trace += (r.trace.empty() ? L"" : L" → ") + std::wstring(step) + L" " +
                   (!s.live ? L"?" : s.TextboxHasFocus() ? L"open" : L"shut") + L" " +
                   std::to_wstring(GetTickCount64() - tStart);
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

    const bool wasAlreadyForeground = (GetForegroundWindow() == gw2);
    if (!wasAlreadyForeground) {
        if (!BringToFront(gw2)) return finish(Tr(L"GW2 cannot be brought to the front"));
        Sleep(opt.stepDelayMs);
    }

    // Every message starts from the same state, whatever GW2's own chat was doing – shut or open, any channel, a draft,
    // a whisper already addressed, any of its chat tabs: its line shut → Enter opens it → what it still holds goes → the
    // channel command typed → the message pasted → Enter. Before every key the state is read again (MumbleLink is
    // GW2's shared memory, written every frame: a change shows within one frame): GW2 in front, our window not holding
    // the keyboard, and once the line is open, GW2's line still has the keyboard. Anything else and not one key more:
    // a key outside the line is a game action, and a Tab outside the whisper name switches GW2's own chat tab, whose
    // empty line then sends the message in its channel (live 2026-11-02: a whisper went out in Say).
    // What MumbleLink cannot tell: the channel, and whether the whisper name field has the cursor. That is why the line
    // is emptied before the command – a command typed behind other text is no command.
    const bool watched = ms.live;  // without MumbleLink: the window focus only, and the set pauses
    auto lineHasKeys = [&] {
        const MumbleState s = mumble->Read();
        return s.live && s.TextboxHasFocus();
    };
    // the reason the next key would not land in GW2's chat line; empty: go on
    auto lost = [&](bool needLine) -> std::wstring {
        if (GetForegroundWindow() != gw2) return Tr(L"Focus lost – cancelled");
        if (keysReachGame && !keysReachGame()) return Tr(L"Our window took the keyboard – cancelled");
        if (needLine && watched && !lineHasKeys()) return Tr(L"GW2's chat line closed – cancelled");
        return std::wstring();
    };
    // after a key: the pause between keys (Send timing: automatic one frame + 10 ms, or yours) – and at least two more
    // frames of GW2 (MumbleLink's frame counter). The counter alone was too short: GW2 bumps it more often than it
    // switches its line into the whisper mode, the name came before the name field and the Tab switched GW2's chat tab
    // (live 2026-11-02); the pause had held since 2.7.1 (2026-10-29: without it name and text swapped or cut).
    auto settle = [&] {
        WaitForTargetToDrain(gw2, 1000);
        const uint32_t from = watched ? mumble->Read().uiTick : 0;
        Sleep(opt.stepDelayMs);
        if (!watched) return;
        for (int waited = 0; waited < 1000 && mumble->Read().uiTick - from < 2; waited += 5) Sleep(5);
    };
    std::wstring why;
    if (!(why = lost(false)).empty()) return finish(why);
    injected = true;
    note(L"start");

    // 1. GW2's line shut. Esc only while MumbleLink says a text box of GW2 has the keyboard: then Esc closes that and
    //    nothing else (never the game menu).
    for (int i = 0; i < 2 && watched && lineHasKeys(); ++i) {
        if (!(why = lost(false)).empty()) return finish(why);
        Tap(VK_ESCAPE, opt.keyHoldMs);
        WaitForTargetToDrain(gw2, 1000);
        textbox(false, std::max(600, 8 * opt.keyHoldMs));
        note(L"Esc");
    }
    if (watched && lineHasKeys()) return finish(Tr(L"GW2's chat line does not close – nothing sent"));

    // 2. Enter opens it fresh – typed on only once GW2 says the line has the keyboard.
    if (!(why = lost(false)).empty()) return finish(why);
    const ULONGLONG t0 = GetTickCount64();
    Tap(VK_RETURN, opt.keyHoldMs);
    WaitForTargetToDrain(gw2, 1000);
    if (watched) {
        const bool opened = textbox(true, std::max(600, 8 * opt.keyHoldMs));  // ~8 frames: also a game at a few fps
        note(L"Enter");
        if (!opened) return finish(Tr(L"GW2's chat line did not open – nothing sent"));
        r.openMs = static_cast<int>(GetTickCount64() - t0);
    } else {
        note(L"Enter");
    }
    settle();

    // 3. No Ctrl+A to empty the line: GW2's chat line does not select with it – a control character stayed in front,
    //    "/w " behind it was no command, and the Tab meant for the name switched GW2's chat tab (live 2026-11-02, a
    //    whisper in Say from a shut line in Say). How GW2 empties a draft is open (Esc above closes the line).

    // 4. Any other command ("/wiki Tequatl", "/dance", "/sqjoin Name") is typed whole: GW2 runs a typed command, a
    //    pasted one lost its command word.
    if (IsPlainCommand(text)) {
        if (!(why = lost(true)).empty()) return finish(why);
        TypeText(text, opt.keyHoldMs);
        note(L"command");
        settle();
        if (GetForegroundWindow() != gw2) return finish(Tr(L"Focus lost – the text may still be in the chat line"));
        if (!(why = lost(true)).empty()) return finish(why);
        const ULONGLONG tCommand = GetTickCount64();
        Tap(VK_RETURN, opt.keyHoldMs);
        r.confirmedByGame = textbox(false, std::max(1000, 8 * opt.keyHoldMs));
        note(L"Enter");
        if (r.confirmedByGame) r.confirmMs = static_cast<int>(GetTickCount64() - tCommand);
        if (!r.confirmedByGame) Sleep(opt.stepDelayMs);
        return finish(std::wstring());
    }
    // A channel command ("/w Name, text", "/p text") is typed the way a person does it: GW2 only switches the channel
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
            if (!(why = lost(true)).empty()) return finish(why);
            TypeText(command, opt.keyHoldMs);
            note(Trim(command).c_str());
            settle();  // GW2 switches the line into the whisper mode after "/w "
            if (!recipient.empty()) {
                // The address is pasted whole, not typed: GW2's name field offers a partner you whispered before from the
                // first letters on and marks the rest of the name – typed letter by letter, the space deleted that mark
                // and the name came out wrong, and the Tab then switched GW2's chat tab instead of taking the name (live
                // 2026-11-02, "Ted Manara"). Pasted it stands as one piece; Tab takes it and moves on to the text. The
                // clipboard gets the message only after GW2 took the name (the pause and two frames below: without
                // pauses name and text once came swapped, 2026-10-29).
                if (!SetClipboardText(owner, recipient)) return finish(Tr(L"The clipboard is blocked"));
                if (!(why = lost(true)).empty()) return finish(why);
                CtrlTap('V', opt.keyHoldMs);
                note(L"name");
                settle();
                if (!(why = lost(true)).empty()) return finish(why);
                Tap(VK_TAB, opt.keyHoldMs);  // confirm it: the cursor moves to the message
                note(L"Tab");
                settle();
            }
            message = split.body;
            if (!SetClipboardText(owner, message)) return finish(Tr(L"The clipboard is blocked"));
        }
    }

    // 5. The message pasted, then sent.
    if (!(why = lost(true)).empty()) return finish(why);
    CtrlTap('V', opt.keyHoldMs);
    note(L"Ctrl+V");
    settle();  // the pasted text must be in the line before Enter

    if (GetForegroundWindow() != gw2) return finish(Tr(L"Focus lost – the text may still be in the chat line"));
    if (!(why = lost(true)).empty()) return finish(why);
    const ULONGLONG tSend = GetTickCount64();
    Tap(VK_RETURN, opt.keyHoldMs);
    r.confirmedByGame = textbox(false, std::max(1000, 8 * opt.keyHoldMs));  // the line shut = the message went out
    note(L"Enter");
    if (r.confirmedByGame) r.confirmMs = static_cast<int>(GetTickCount64() - tSend);
    if (!r.confirmedByGame) Sleep(opt.stepDelayMs);
    return finish(std::wstring());
}

}  // namespace gct
