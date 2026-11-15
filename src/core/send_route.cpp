// send_route.cpp — see send_route.hpp.

#include "core/send_route.hpp"

#include "core/langs.hpp"
#include "core/languages.hpp"

namespace gct {

SendRoute RouteMessage(const SendState& s) {
    static const std::vector<std::wstring> kNone;
    const std::vector<std::wstring>& langs = s.writeLangs ? *s.writeLangs : kNone;
    const bool footerLang = s.sendIdx >= 0 && s.sendIdx < static_cast<int>(langs.size());
    const bool voiceFirst = s.spoken && s.voiceOn && s.voiceTranslate;
    SendRoute r;
    if (!s.tabLang.empty()) {
        if (s.tabLang != L"=") r.lang = s.tabLang;
    } else if (footerLang) {
        r.lang = langs[static_cast<size_t>(s.sendIdx)];
    }
    if (!r.lang.empty() && !SendableInGw2(r.lang)) r.lang.clear();
    const std::wstring& source = voiceFirst && !s.dictLang.empty() ? s.dictLang : s.typingLang;
    // Written (or said) in a script GW2's chat cannot show: never "as written" – into the first sendable of yours
    // (or EN-GB). Cyrillic/Greek can be written, Chinese/Latin can go out into GW2.
    if (r.lang.empty() && !SendableInGw2(source)) {
        for (const std::wstring& l : langs) {
            if (SendableInGw2(l)) {
                r.lang = l;
                break;
            }
        }
        if (r.lang.empty()) r.lang = L"EN-GB";
    }
    r.translating = !r.lang.empty() && PrimaryLang(r.lang) != PrimaryLang(source);
    return r;
}

}  // namespace gct
