#pragma once
// User-interface language. All UI text in the code is written in English and wrapped in Tr(); the other languages
// come from the tables in i18n_<code>.cpp (de, fr, es, it, pl, pt, tr, ru - keyed by the English text). Missing entries
// fall back to English, so a new string never shows up empty. Only languages our font draws left to right: Arabic
// was taken out (user, 2026-10-22 - ImGui 1.80 draws it unjoined, left to right).
//
//   SetStatus(Tr(L"Sent."), ...);
//   SetStatus(TrF(L"Translator: {1}", {name}), ...);
//
// tools/i18n_check.py lists every Tr()/TrF() text without an entry in a table. Portable: no windows.h. Tr() is
// safe from any thread.
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

namespace gct {

enum class UiLang : uint8_t { En = 0, De, Fr, Es, It, Pl, Pt, Tr, Ru };

struct UiLangInfo {
    UiLang lang;
    const wchar_t* code;    // "en", "de", "fr" ... (ini value)
    const wchar_t* native;  // name in the language itself: "English", "Deutsch", "Polski" ...
};

const std::vector<UiLangInfo>& UiLanguages();

void SetUiLang(UiLang lang);
UiLang GetUiLang();

// "de", "DE", "de-AT", "pt-BR" ... -> language; anything else -> fallback.
UiLang UiLangFromCode(const std::wstring& code, UiLang fallback = UiLang::En);

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
