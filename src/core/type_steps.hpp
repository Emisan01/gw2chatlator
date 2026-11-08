// type_steps.hpp — a chat command typed letter by letter (gw2_sender), with the state read before every letter: the
// moment a key would land anywhere but GW2's chat line, nothing more goes out. Apart from SendInput, so a test can
// count what went out.
#pragma once

#include <cstddef>
#include <string>

namespace gct {

// Types `s` through type(c) while lost() returns empty. Returns empty when every letter went out, else lost()'s reason;
// `sent` is how many letters went out before it stopped.
template <class Lost, class Type>
std::wstring TypeWhileOk(const std::wstring& s, Lost&& lost, Type&& type, size_t& sent) {
    sent = 0;
    for (wchar_t c : s) {
        if (std::wstring why = lost(); !why.empty()) return why;
        type(c);
        ++sent;
    }
    return std::wstring();
}

}  // namespace gct
