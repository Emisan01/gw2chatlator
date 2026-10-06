// langs.hpp — mapping between DeepL codes, GW2 API languages and the
// BCP-47 tags the Windows spell checker wants.
#pragma once

#include <string>
#include <vector>

namespace gct {

// "EN-GB" -> "EN", "zh-hans" -> "ZH", "de" -> "DE"
std::wstring PrimaryLang(const std::wstring& code);

// DeepL code -> GW2 API language ("en", "de", "fr", "es", "zh"), or "" when
// the game has no such language (then there is no glossary for it).
std::string Gw2ApiLang(const std::wstring& deeplCode);

// Spell-checker tags to try, best first. `lang` is a DeepL-style code
// ("DE", "EN-GB"), `userLocale` the Windows user locale ("de-AT").
std::vector<std::wstring> SpellTagCandidates(const std::wstring& lang, const std::wstring& userLocale);

}  // namespace gct
