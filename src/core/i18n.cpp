#include "core/i18n.hpp"

#include <atomic>
#include <cwctype>
#include <unordered_map>

namespace gct {

// Defined in i18n_de.cpp / i18n_ar.cpp.
extern const I18nEntry kGermanStrings[];
extern const size_t kGermanCount;
extern const I18nEntry kArabicStrings[];
extern const size_t kArabicCount;

namespace {

std::atomic<uint8_t> g_lang{static_cast<uint8_t>(UiLang::En)};

using Table = std::unordered_map<std::wstring, const wchar_t*>;

const Table& TableFor(UiLang lang) {
    static const Table de = [] {
        Table t;
        for (size_t i = 0; i < kGermanCount; ++i) t.emplace(kGermanStrings[i].en, kGermanStrings[i].text);
        return t;
    }();
    static const Table ar = [] {
        Table t;
        for (size_t i = 0; i < kArabicCount; ++i) t.emplace(kArabicStrings[i].en, kArabicStrings[i].text);
        return t;
    }();
    static const Table none;
    switch (lang) {
        case UiLang::De: return de;
        case UiLang::Ar: return ar;
        default: return none;
    }
}

}  // namespace

const std::vector<UiLangInfo>& UiLanguages() {
    static const std::vector<UiLangInfo> langs = {
        {UiLang::En, L"en", L"English", false},
        {UiLang::De, L"de", L"Deutsch", false},
        {UiLang::Ar, L"ar", L"العربية", true},
    };
    return langs;
}

void SetUiLang(UiLang lang) { g_lang.store(static_cast<uint8_t>(lang)); }

UiLang GetUiLang() { return static_cast<UiLang>(g_lang.load()); }

bool UiRtl() { return GetUiLang() == UiLang::Ar; }

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

const wchar_t* UiLangCode(UiLang lang) {
    for (const UiLangInfo& l : UiLanguages())
        if (l.lang == lang) return l.code;
    return L"en";
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
