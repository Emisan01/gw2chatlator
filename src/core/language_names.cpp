// language_names.cpp — see language_names.hpp.

#include "language_names.hpp"

#include "languages.hpp"
#include "text.hpp"

namespace gct {

const std::vector<LanguageNameRow>& LanguageNameTable() {
    static const std::vector<LanguageNameRow> table = {
#include "language_names.inc"
    };
    return table;
}

std::wstring LanguageNameIn(const std::wstring& code, UiLang ui) {
    const LangInfo* l = FindLanguage(code);
    if (!l) return {};
    const size_t col = static_cast<size_t>(ui);
    if (col >= kUiLangCount) return {};
    const std::wstring key = l->code;
    for (const LanguageNameRow& row : LanguageNameTable())
        if (key == row.code) return row.names[col];
    return {};
}

namespace {
// The base letter of U+00C0..U+017F ('\0': the character stays): Latin-1 and Latin Extended-A.
wchar_t LatinBase(wchar_t c) {
    static const char kLatin1[] =  // U+00C0..U+00FF; × and ÷ stay
        "aaaaaaaceeeeiiiidnooooo\0ouuuuyts"
        "aaaaaaaceeeeiiiidnooooo\0ouuuuyty";
    struct Run {
        int count;
        char base;
    };
    static const Run kExtA[] = {  // U+0100..U+017F in runs of one base letter
        {6, 'a'}, {8, 'c'}, {4, 'd'}, {10, 'e'}, {8, 'g'}, {4, 'h'}, {12, 'i'}, {2, 'j'}, {3, 'k'}, {10, 'l'},
        {9, 'n'}, {8, 'o'}, {6, 'r'}, {8, 's'}, {6, 't'}, {12, 'u'}, {2, 'w'}, {3, 'y'}, {6, 'z'}, {1, 's'}};
    if (c >= 0x00C0 && c <= 0x00FF) return static_cast<wchar_t>(kLatin1[c - 0x00C0]);
    if (c >= 0x0100 && c <= 0x017F) {
        int at = c - 0x0100;
        for (const Run& r : kExtA) {
            if (at < r.count) return static_cast<wchar_t>(r.base);
            at -= r.count;
        }
    }
    if (c >= 0x0218 && c <= 0x021B) return c < 0x021A ? L's' : L't';  // Romanian Ș ș Ț ț
    return 0;
}
}  // namespace

std::wstring SortFold(const std::wstring& name) {
    std::wstring out;
    out.reserve(name.size());
    for (wchar_t c : name) {
        if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c + 32);
        else if (c >= 0x0410 && c <= 0x042F) c = static_cast<wchar_t>(c + 0x20);  // Cyrillic А-Я
        else if (c == 0x0401 || c == 0x0451) c = 0x0435;                           // Ё ё as е
        else if (const wchar_t base = LatinBase(c)) c = base;
        out += c;
    }
    return out;
}

bool NameLess(const std::wstring& a, const std::wstring& b) {
    const std::wstring fa = SortFold(a), fb = SortFold(b);
    return fa != fb ? fa < fb : a < b;
}

}  // namespace gct
