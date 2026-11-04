// whisper_watch.cpp — see whisper_watch.hpp.
#include "core/whisper_watch.hpp"

#include <algorithm>

#include "core/text.hpp"

namespace gct {

void WhisperWatch::Sent(const std::wstring& text, uint64_t nowMs) {
    const std::wstring key = NormalizeForCompare(text);
    if (key.empty()) return;
    waiting_.push_back({key, nowMs});
    if (waiting_.size() > kMaxWaiting) waiting_.pop_front();
}

void WhisperWatch::Forget(const std::wstring& text) {
    const std::wstring key = NormalizeForCompare(text);
    const auto it = std::find_if(waiting_.begin(), waiting_.end(), [&](const Waiting& w) { return w.text == key; });
    if (it != waiting_.end()) waiting_.erase(it);
}

bool WhisperWatch::WentElsewhere(const std::wstring& text, bool isWhisper, uint64_t nowMs) {
    while (!waiting_.empty() && nowMs - waiting_.front().at > kWindowMs) waiting_.pop_front();
    const std::wstring key = NormalizeForCompare(text);
    const auto it = std::find_if(waiting_.begin(), waiting_.end(), [&](const Waiting& w) { return w.text == key; });
    if (it == waiting_.end()) return false;
    waiting_.erase(it);
    return !isWhisper;
}

}  // namespace gct
