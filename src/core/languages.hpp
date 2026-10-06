// languages.hpp — the languages offered in the menus.
// Codes are DeepL target codes; the LLM backend uses the English name.
#pragma once

#include <string>
#include <vector>

namespace gct {

struct LangInfo {
    const wchar_t* code;     // "EN-GB", "ZH-HANS", "AR" ...
    const wchar_t* native;   // "English (UK)", "中文（简体）", "العربية"
    const wchar_t* english;  // "English (British)", for LLM prompts
    bool latinScript;        // GW2 chat can display it
};

const std::vector<LangInfo>& Languages();

// Exact code match first, then by primary language ("de-AT" -> DE, "EN" -> EN-GB).
const LangInfo* FindLanguage(const std::wstring& code);

// Human name for menus/labels, falls back to the code itself.
std::wstring LanguageLabel(const std::wstring& code);

// Name for an LLM prompt ("German"); falls back to the code.
std::wstring LanguageEnglishName(const std::wstring& code);

// DeepL source languages are plain codes: "EN-GB" -> "EN", "ZH-HANS" -> "ZH".
std::wstring SourceCode(const std::wstring& code);

}  // namespace gct
