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
    bool fromModel = false;  // from the local model's table (language_catalog.inc): never sendable by itself, never
                             // offered by an online service – unproven there (Emi, 2026-11-25)
};

// The catalog languages a translator offers: a local model ("nllb", "m2m", "opus") those of its own table, an online
// service ("") the hand-kept catalog only (core/local_mt_protocol LocalModelHasLanguage).
bool OfferedBy(const std::string& model, const LangInfo& l);

const std::vector<LangInfo>& Languages();

// Exact code match first, then by primary language ("de-AT" -> DE, "EN" -> EN-GB).
const LangInfo* FindLanguage(const std::wstring& code);

// Our window's font draws it (Latin, Greek, Cyrillic; weDraw && !rtl).
bool DrawnInOurWindow(const LangInfo& l);

// GW2's chat shows it as written (gw2Shows && !rtl). Written in another, a message is translated first
// (core/send_route). An unknown code: as before, sendable.
bool SendableInGw2(const std::wstring& code);

// The online services, for what each is proven to take.
enum class MtService { DeepL, Google, Microsoft, Supertext, Libre, MyMemory };

// A catalog language a service is proven to take. Kurmanji (KMR) only at Google ("ku") and Microsoft ("kmr"): DeepL,
// MyMemory, LibreTranslate and Supertext refuse it before any request, as "pair not supported" – never sent as
// another language (Emi, 2026-11-25). Every other code: as before, true.
bool ServiceTakes(MtService service, const std::wstring& code);

// DeepL source languages are plain codes: "EN-GB" -> "EN", "ZH-HANS" -> "ZH".
std::wstring SourceCode(const std::wstring& code);

}  // namespace gct
