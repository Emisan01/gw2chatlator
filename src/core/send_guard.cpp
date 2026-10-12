// send_guard.cpp
#include "core/send_guard.hpp"

#include <cwctype>

#include "core/text.hpp"

namespace gct {

std::wstring SendGuard::Key(const std::wstring& text) {
    std::wstring k;
    bool space = false;
    for (wchar_t c : CaseFold(text)) {
        if (std::iswspace(c)) {
            space = !k.empty();
            continue;
        }
        if (space) k += L' ';
        space = false;
        k += c;
    }
    while (!k.empty() && (k.back() == L'.' || k.back() == L'!' || k.back() == L'?')) k.pop_back();
    return k;
}

SendGuard::Verdict SendGuard::Strike(Reason r) {
    Verdict v;
    v.allow = false;
    v.reason = r;
    last_ = r;
    if (++strikes_ >= kStrikesToLock && !locked_) {
        locked_ = true;
        v.nowLocked = true;
    }
    return v;
}

SendGuard::Verdict SendGuard::Check(const std::wstring& text, bool injectedEnter, bool foreignText, uint64_t nowMs) {
    if (locked_) {
        Verdict v;
        v.allow = false;
        v.reason = Reason::Locked;
        return v;
    }
    if (injectedEnter) return Strike(Reason::Injected);
    if (foreignText) return Strike(Reason::Foreign);

    const uint64_t keep = kRepeatWindowMs > kRateWindowMs ? kRepeatWindowMs : kRateWindowMs;
    while (!sent_.empty() && nowMs - sent_.front().ms > keep) sent_.pop_front();
    const std::wstring key = Key(text);
    int same = 0, recent = 0;
    for (const Sent& s : sent_) {
        if (nowMs - s.ms <= kRepeatWindowMs && s.key == key) ++same;
        if (nowMs - s.ms <= kRateWindowMs) ++recent;
    }
    if (!key.empty() && same >= kRepeatMax) return Strike(Reason::Repeat);
    if (recent >= kRateMax) return Strike(Reason::Rate);
    sent_.push_back({nowMs, key});
    return {};
}

}  // namespace gct
