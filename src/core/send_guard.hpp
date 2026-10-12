// send_guard.hpp — keeps the tool from being used as a chat bot or spam macro (invariant 1).
// Every Enter that would put a line into the game is checked: it must come from this PC's keyboard (not injected by
// another program), the text must have been typed, pasted or dictated here (not written into the box from outside),
// and it must not look like spam (the same line again and again, more lines than a person writes and checks). A hit
// blocks that line; the second hit switches sending off for the rest of the session. Nothing is stored: the tool is
// open source, a lock that survives a restart would only hurt honest players after a false alarm.
#pragma once

#include <cstdint>
#include <deque>
#include <string>

namespace gct {

class SendGuard {
public:
    enum class Reason {
        None,
        Injected,  // the Enter came from software (SendInput / a posted message), not from the keyboard
        Foreign,   // the text was written into the box by another program
        Repeat,    // the same line too often in a short time
        Rate,      // too many lines per minute
        Locked,    // sending is off for this session after earlier hits
    };
    struct Verdict {
        bool allow = true;
        Reason reason = Reason::None;
        bool nowLocked = false;  // this hit switched sending off
    };

    static constexpr int kRepeatMax = 4;            // the same line at most 4 times ...
    static constexpr uint64_t kRepeatWindowMs = 120000;  // ... in 2 minutes (the 5th is blocked)
    static constexpr int kRateMax = 20;             // at most 20 lines ...
    static constexpr uint64_t kRateWindowMs = 60000;     // ... per minute
    static constexpr int kStrikesToLock = 2;

    // One Enter that would send `text` (as it goes to the game) at `nowMs` (a steady clock). An allowed line is
    // counted for the spam rules.
    Verdict Check(const std::wstring& text, bool injectedEnter, bool foreignText, uint64_t nowMs);

    bool Locked() const { return locked_; }
    int Strikes() const { return strikes_; }
    Reason LastReason() const { return last_; }

private:
    struct Sent {
        uint64_t ms;
        std::wstring key;
    };
    static std::wstring Key(const std::wstring& text);  // case- and spacing-insensitive, end punctuation ignored
    Verdict Strike(Reason r);

    std::deque<Sent> sent_;
    int strikes_ = 0;
    bool locked_ = false;
    Reason last_ = Reason::None;
};

}  // namespace gct
