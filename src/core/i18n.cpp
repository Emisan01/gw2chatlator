#include "core/i18n.hpp"

#include <atomic>
#include <cwctype>
#include <unordered_map>
#include <vector>

namespace gct {

// Defined in i18n_<code>.cpp, one table per language.
extern const I18nEntry kGermanStrings[];
extern const size_t kGermanCount;
extern const I18nEntry kFrenchStrings[];
extern const size_t kFrenchCount;
extern const I18nEntry kSpanishStrings[];
extern const size_t kSpanishCount;
extern const I18nEntry kItalianStrings[];
extern const size_t kItalianCount;
extern const I18nEntry kPolishStrings[];
extern const size_t kPolishCount;
extern const I18nEntry kPortugueseStrings[];
extern const size_t kPortugueseCount;
extern const I18nEntry kTurkishStrings[];
extern const size_t kTurkishCount;
extern const I18nEntry kRussianStrings[];
extern const size_t kRussianCount;

namespace {

std::atomic<uint8_t> g_lang{static_cast<uint8_t>(UiLang::En)};

using Table = std::unordered_map<std::wstring, const wchar_t*>;

const Table& TableFor(UiLang lang) {
    struct Source {
        UiLang lang;
        const I18nEntry* entries;
        const size_t* count;
    };
    static const std::vector<Table> tables = [] {  // all at once, the first time (a thread-safe static)
        const Source sources[] = {
            {UiLang::De, kGermanStrings, &kGermanCount},
            {UiLang::Fr, kFrenchStrings, &kFrenchCount},
            {UiLang::Es, kSpanishStrings, &kSpanishCount},
            {UiLang::It, kItalianStrings, &kItalianCount},
            {UiLang::Pl, kPolishStrings, &kPolishCount},
            {UiLang::Pt, kPortugueseStrings, &kPortugueseCount},
            {UiLang::Tr, kTurkishStrings, &kTurkishCount},
            {UiLang::Ru, kRussianStrings, &kRussianCount},
        };
        std::vector<Table> t(static_cast<size_t>(UiLang::Ru) + 1);  // English: the empty table
        for (const Source& s : sources)
            for (size_t i = 0; i < *s.count; ++i) t[static_cast<size_t>(s.lang)].emplace(s.entries[i].en, s.entries[i].text);
        return t;
    }();
    const size_t i = static_cast<size_t>(lang);
    return i < tables.size() ? tables[i] : tables[0];
}

}  // namespace

const std::vector<UiLangInfo>& UiLanguages() {
    static const std::vector<UiLangInfo> langs = {
        {UiLang::En, L"en", L"English"},
        {UiLang::De, L"de", L"Deutsch"},
        {UiLang::Fr, L"fr", L"Fran\u00e7ais"},
        {UiLang::Es, L"es", L"Espa\u00f1ol"},
        {UiLang::It, L"it", L"Italiano"},
        {UiLang::Pl, L"pl", L"Polski"},
        {UiLang::Pt, L"pt", L"Portugu\u00eas (Brasil)"},
        {UiLang::Tr, L"tr", L"T\u00fcrk\u00e7e"},
        {UiLang::Ru, L"ru", L"\u0420\u0443\u0441\u0441\u043a\u0438\u0439"},
    };
    return langs;
}

void SetUiLang(UiLang lang) { g_lang.store(static_cast<uint8_t>(lang)); }

UiLang GetUiLang() { return static_cast<UiLang>(g_lang.load()); }

UiLang UiLangFromCode(const std::wstring& code, UiLang fallback) {
    std::wstring c;
    for (wchar_t ch : code) {
        if (ch == L'-' || ch == L'_') break;
        if (!std::iswspace(ch)) c += static_cast<wchar_t>(std::towlower(ch));
    }
    for (const UiLangInfo& l : UiLanguages())
        if (c == l.code) return l.lang;
    return fallback;
}

const wchar_t* TranslationFor(UiLang lang, const wchar_t* english) {
    if (!english) return nullptr;
    const Table& t = TableFor(lang);
    auto it = t.find(english);
    return it == t.end() ? nullptr : it->second;
}

size_t TranslationCount(UiLang lang) { return TableFor(lang).size(); }

std::wstring Tr(const wchar_t* english) {
    if (!english) return {};
    const UiLang lang = GetUiLang();
    if (lang == UiLang::En) return english;
    const wchar_t* t = TranslationFor(lang, english);
    return t ? t : english;
}

std::wstring FormatArgs(const std::wstring& pattern, std::initializer_list<std::wstring> args) {
    std::wstring out;
    out.reserve(pattern.size() + 16);
    for (size_t i = 0; i < pattern.size(); ++i) {
        if (pattern[i] == L'{' && i + 2 < pattern.size() && pattern[i + 2] == L'}' && pattern[i + 1] >= L'1' &&
            pattern[i + 1] <= L'9') {
            const size_t idx = static_cast<size_t>(pattern[i + 1] - L'1');
            if (idx < args.size()) {
                out += *(args.begin() + idx);
                i += 2;
                continue;
            }
        }
        out += pattern[i];
    }
    return out;
}

std::wstring TrF(const wchar_t* english, std::initializer_list<std::wstring> args) {
    return FormatArgs(Tr(english), args);
}

}  // namespace gct
