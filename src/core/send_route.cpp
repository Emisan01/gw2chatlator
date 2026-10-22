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
    if (voiceFirst) {  // what you said goes into a language – the first of yours when the footer says "as written"
        if (!s.tabLang.empty() && s.tabLang != L"=") r.lang = s.tabLang;
        else if (footerLang) r.lang = langs[static_cast<size_t>(s.sendIdx)];
        else if (!langs.empty()) r.lang = langs.front();
    } else if (!s.tabLang.empty()) {
        if (s.tabLang != L"=") r.lang = s.tabLang;
    } else if (footerLang) {
        r.lang = langs[static_cast<size_t>(s.sendIdx)];
    }
    const std::wstring& source = voiceFirst && !s.dictLang.empty() ? s.dictLang : s.typingLang;
    // Written (or said) in a script GW2's chat cannot show: never "as written" – into the first of yours (user,
    // 2026-10-22: Cyrillic or Greek to write in, translated into what GW2 shows).
    if (r.lang.empty() && !SendableInGw2(source)) r.lang = langs.empty() ? std::wstring(L"EN-GB") : langs.front();
    r.translating = !r.lang.empty() && PrimaryLang(r.lang) != PrimaryLang(source);
    return r;
}

}  // namespace gct
