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

// A guess from letters only one language (or script) uses: ı ğ ş -> TR,
// ñ ¿ ¡ -> ES, ą ę ł ż -> PL, ß/ä ö ü -> DE, ç œ ê -> FR, ã õ -> PT, ő ű -> HU,
// å ø æ -> DA/NO/SV family (NB), Cyrillic -> RU (і ї є -> UK), Greek -> EL,
// Arabic -> AR, Hebrew -> HE, Hangul -> KO, Kana -> JA, Han -> ZH, Thai -> TH.
// "" when the letters say nothing (plain a-z). For short chat lines the
// Windows language detection refuses.
std::wstring GuessLanguageByLetters(const std::wstring& text);

// The language of a chat line only when it is clear, else "" (then nothing is translated by itself; a click still
// can): another script (Arabic, Cyrillic, CJK …) decides at once; Latin lines need three words or more and the Windows
// detection `els` (passed in), or letters only one language uses (ñ, ç, ß …). Short Latin lines without such letters
// ("ok np", names, slang) stay unsure.
std::wstring SureLanguage(const std::wstring& text, const std::wstring& els);

// Spell-checker tags to try, best first. `lang` is a DeepL-style code
// ("DE", "EN-GB"), `userLocale` the Windows user locale ("de-AT").
std::vector<std::wstring> SpellTagCandidates(const std::wstring& lang, const std::wstring& userLocale);

}  // namespace gct
