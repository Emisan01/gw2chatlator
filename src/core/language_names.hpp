// language_names.hpp — a language's name in the window's language (Unicode CLDR via tools/gen_language_names.py,
// data/language-names.tsv) and the order to list names in. Portable, no windows.h.
#pragma once

#include <string>
#include <vector>

#include "i18n.hpp"

namespace gct {

constexpr size_t kUiLangCount = static_cast<size_t>(UiLang::Ru) + 1;

struct LanguageNameRow {
    const wchar_t* code;                  // the catalog's code: "NB", "ZH-HANS", "KMR"
    const wchar_t* names[kUiLangCount];   // in the order of UiLang
};

// The generated table, one row per catalog language (src/core/language_names.inc).
const std::vector<LanguageNameRow>& LanguageNameTable();

// The name of a catalog language in a window language: ("NB", De) -> "Norwegisch", ("de-AT", Fr) -> "Allemand".
// Empty when the catalog does not know the code – the caller shows the English name or the code then, never "?".
std::wstring LanguageNameIn(const std::wstring& code, UiLang ui);

// Names in a list's order: case and accents left out ("Čeština" and "Çekçe" among the C's, "Ö" among the O's,
// "ı" and "İ" as i, "Ё" as "е"); equal so, the written names decide.
std::wstring SortFold(const std::wstring& name);
bool NameLess(const std::wstring& a, const std::wstring& b);

}  // namespace gct
