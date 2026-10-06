#pragma once
// User-interface language. All UI text in the code is written in English and
// wrapped in Tr(); German and Arabic come from the tables in i18n_de.cpp and
// i18n_ar.cpp (keyed by the English text). Missing entries fall back to
// English, so a new string never shows up empty.
//
//   SetStatus(Tr(L"Sent."), ...);
//   SetStatus(TrF(L"Translator: {1}", {name}), ...);
//
// tools/i18n_check.py lists every Tr()/TrF() text that has no German or
// Arabic entry yet. Portable: no windows.h. Tr() is safe from any thread.

#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

namespace gct {

enum class UiLang : uint8_t { En = 0, De = 1, Ar = 2 };

struct UiLangInfo {
    UiLang lang;
    const wchar_t* code;    // "en", "de", "ar" (ini value)
    const wchar_t* native;  // name in the language itself: "English", "Deutsch", "العربية"
    bool rtl;
};

const std::vector<UiLangInfo>& UiLanguages();

void SetUiLang(UiLang lang);
UiLang GetUiLang();
bool UiRtl();  // the current UI language is written right to left

// "de", "DE", "de-AT", "ar-SA" ... -> language; anything else -> fallback.
UiLang UiLangFromCode(const std::wstring& code, UiLang fallback = UiLang::En);
const wchar_t* UiLangCode(UiLang lang);

std::wstring Tr(const wchar_t* english);
// Tr() plus placeholders {1}, {2}, ... replaced by `args` in order.
std::wstring TrF(const wchar_t* english, std::initializer_list<std::wstring> args);
// Placeholder replacement without translation (for already translated text).
std::wstring FormatArgs(const std::wstring& pattern, std::initializer_list<std::wstring> args);

// Tests / tools.
struct I18nEntry {
    const wchar_t* en;
    const wchar_t* text;
};
const wchar_t* TranslationFor(UiLang lang, const wchar_t* english);  // nullptr if missing
size_t TranslationCount(UiLang lang);

}  // namespace gct
