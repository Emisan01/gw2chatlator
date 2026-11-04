// whisper_watch.hpp — a whisper that came back in another channel. Sending into GW2 is blind (MumbleLink does not say
// the channel), but your line comes back through the chat reading with its channel: a whisper that went to Say or Party
// is said at once. Your own text only, for seconds, in memory.
#pragma once

#include <cstdint>
#include <deque>
#include <string>

namespace gct {

class WhisperWatch {
public:
    static constexpr uint64_t kWindowMs = 15000;  // a line comes back well within this
    static constexpr size_t kMaxWaiting = 8;

    void Sent(const std::wstring& text, uint64_t nowMs);    // before the keys go out: the line can be back at once
    void Forget(const std::wstring& text);                  // the send failed: it will not come back
    // Your own line arrived. True: it is a whisper just sent, and it came back in another channel.
    bool WentElsewhere(const std::wstring& text, bool isWhisper, uint64_t nowMs);

private:
    struct Waiting {
        std::wstring text;  // as NormalizeForCompare has it
        uint64_t at = 0;
    };
    std::deque<Waiting> waiting_;
};

}  // namespace gct
