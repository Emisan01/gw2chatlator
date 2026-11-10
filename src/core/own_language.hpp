// own_language.hpp — which language a text of your own is in, before it is translated. Windows' detection takes a
// whole text and guesses among every language it knows: a short German line with an English word in it came back as
// Afrikaans, and the local model has no Afrikaans. You write in few languages, so only those can be the answer:
//   1. another script (Cyrillic, Greek, Arabic, CJK ...) decides at once, as before – yours or not;
//   2. Windows' guess counts only when it is one of your own languages;
//   3. telltale letters and words (ñ, ç, ß, "merci") count only for one of your own languages;
//   4. else unsure – the caller takes the typing language.
// Measured with detect_bench (2026-11-10, typing German, 42 made-up lines, every prefix): results outside your
// languages 31 -> 0, changes of mind while typing 43 -> 25, wrong prefixes 235 -> 220 of 1620, German lines 14 -> 1.
// Tried and dropped: "every word is in the typing language's dictionary -> the typing language". Windows' German
// dictionary accepts "be right back in five" and "merci pour le groupe"; it cost 40 prefixes in English and French
// lines for one in German. Portable (Windows' guess is passed in), unit-tested.
#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "langs.hpp"
#include "text.hpp"

namespace gct {

inline bool OwnLanguageHas(const std::vector<std::wstring>& own, const std::wstring& lang) {
    const std::wstring p = PrimaryLang(lang);
    return !p.empty() && std::any_of(own.begin(), own.end(), [&](const std::wstring& o) { return PrimaryLang(o) == p; });
}

struct OwnLanguageAnswer {
    std::wstring lang;  // "" unsure
    int rule = 4;       // which of the rules above decided
};

// `own`: your languages – the typing language, the send list, the ones you understand, English. `windows`: Windows'
// guess for the whole text ("" none).
inline OwnLanguageAnswer OwnLanguage(const std::wstring& text, const std::vector<std::wstring>& own, const std::wstring& windows) {
    OwnLanguageAnswer out;
    for (wchar_t c : text) {
        const unsigned u = static_cast<unsigned>(c);
        if (u >= 0x0370 && !(u >= 0x1E00 && u <= 0x1EFF) && IsWordChar(c)) {
            out.lang = PrimaryLang(SureLanguage(text, windows));
            out.rule = 1;
            return out;
        }
    }
    const bool windowsOwn = OwnLanguageHas(own, windows);
    const std::wstring sure = PrimaryLang(SureLanguage(text, windowsOwn ? windows : std::wstring()));
    if (!sure.empty() && OwnLanguageHas(own, sure)) {
        out.lang = sure;
        out.rule = windowsOwn && sure == PrimaryLang(windows) ? 2 : 3;
    }
    return out;
}

}  // namespace gct
