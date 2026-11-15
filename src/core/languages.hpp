// languages.hpp — the languages offered in the menus.
// Codes are DeepL target codes.
#pragma once

#include <string>
#include <vector>

namespace gct {

struct LangInfo {
    const wchar_t* code;     // "EN-GB", "ZH-HANS", "AR" ...
    const wchar_t* native;   // "English (UK)", "中文（简体）", "العربية"
    const wchar_t* english;  // "English (British)"
    bool gw2Shows;           // GW2 chat can display it
    bool weDraw;             // our window's font draws it
    bool rtl;                // right-to-left script (stays outside everywhere)
};

const std::vector<LangInfo>& Languages();

// Exact code match first, then by primary language ("de-AT" -> DE, "EN" -> EN-GB).
const LangInfo* FindLanguage(const std::wstring& code);

// Our window's font draws it (Latin, Greek, Cyrillic; weDraw && !rtl).
bool DrawnInOurWindow(const LangInfo& l);

// GW2's chat shows it as written (gw2Shows && !rtl). Written in another, a message is translated first
// (core/send_route). An unknown code: as before, sendable.
bool SendableInGw2(const std::wstring& code);



// DeepL source languages are plain codes: "EN-GB" -> "EN", "ZH-HANS" -> "ZH".
std::wstring SourceCode(const std::wstring& code);

}  // namespace gct
