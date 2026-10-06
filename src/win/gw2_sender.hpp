// gw2_sender.hpp — puts a line into the GW2 chat via the clipboard.
//
// Sequence: save clipboard -> put text on clipboard -> focus GW2 ->
// Enter (open chat) -> Ctrl+V -> Enter (send) -> restore clipboard.
// Pasting instead of typing means special characters (! + ^ # { }) and any
// keyboard layout arrive exactly as written.
#pragma once

#include <windows.h>

#include <string>

#include "mumble_link.hpp"

namespace gct {

struct SendOptions {
    int stepDelayMs = 80;      // pause between the key steps (raise for low FPS)
    int keyHoldMs = 30;        // each key is held this long: GW2 reads the keyboard once per frame
    int restoreDelayMs = 250;  // extra wait before the old clipboard is restored
};

struct SendOutcome {
    bool ok = false;
    bool clipboardRestored = false;  // false: GW2 did not confirm, translation stays on the clipboard
    bool confirmedByGame = false;    // MumbleLink saw the chat input close after sending
    std::wstring error;              // German, for the status line
};

// `processId` from MumbleLink when known (exact), else by window class/title.
HWND FindGw2Window(DWORD processId = 0);

// Blocks for roughly 3 * stepDelayMs + restoreDelayMs (longer if GW2 is
// slow). Call from the UI thread; `owner` must be a window of this process
// (clipboard ownership). The old clipboard is only restored after GW2 has
// confirmed it worked through the injected keys.
// `mumble` (optional): waits on GW2's "chat input open" flag instead of
// fixed delays and confirms the message went out.
SendOutcome SendToGw2Chat(HWND owner, const std::wstring& text, const SendOptions& opt, MumbleLink* mumble = nullptr);

// SetForegroundWindow with a fallback for the foreground lock (a synthetic
// Alt tap, delivered to whichever window is in front — never needed while
// this process is in front). `allowAltTap = false` never synthesizes input.
// Returns true once `hwnd` really is the foreground window.
bool BringToFront(HWND hwnd, bool allowAltTap = true);

// Plain copy, for the "copy only" mode: no keys are sent anywhere.
bool CopyTextToClipboard(HWND owner, const std::wstring& text);

}  // namespace gct
