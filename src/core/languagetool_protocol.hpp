#pragma once
// LanguageTool (https://languagetool.org) — optional grammar and spelling
// check while typing. Works with the public API (free, about 20 requests per
// minute, 20 KB per request) or a LanguageTool server you run yourself
// (unlimited, private). Portable request/response code; the HTTP call is in
// win/languagetool_client.

#include <string>
#include <vector>

#include "core/text.hpp"

namespace gct {

struct LtMatch {
    Span span;                              // in the checked text (wchar_t units)
    std::wstring message;                   // explanation (in the checked language)
    std::wstring shortMessage;
    std::vector<std::wstring> replacements;  // best first
    std::wstring ruleId;                    // "MORFOLOGIK_RULE_DE_DE", "GERMAN_SPELLER_RULE" ...
    std::wstring issueType;                 // "misspelling", "grammar", "typographical", ...
    bool Spelling() const { return issueType == L"misspelling"; }
};

struct LtResult {
    bool ok = false;
    std::wstring error;
    std::wstring language;  // detected / used language code ("de-DE")
    std::vector<LtMatch> matches;
};

// "de-DE" stays, "DE" -> "de-DE", "EN-GB" -> "en-GB", "AR" -> "ar", empty -> "auto".
std::wstring LanguageToolLang(const std::wstring& code);

// application/x-www-form-urlencoded body for POST /v2/check.
std::string BuildLanguageToolForm(const std::wstring& text, const std::wstring& lang, const std::wstring& motherTongue);

// Parses the JSON answer. Offsets in the answer count UTF-16 units; they are
// mapped onto `text` (which is what was sent).
LtResult ParseLanguageToolResponse(const std::string& body, const std::wstring& text);

// "https://api.languagetool.org" or "http://localhost:8081" (with or without
// "/v2" or "/v2/check") -> full check URL.
std::wstring NormalizeLanguageToolUrl(const std::wstring& base);

// The public server allows 20 requests per minute per IP; this keeps us below.
class RateLimiter {
public:
    RateLimiter(int perMinute) : perMinute_(perMinute) {}
    // True if a request may go out at time `nowMs` (and records it).
    bool Allow(unsigned long long nowMs);

private:
    int perMinute_;
    std::vector<unsigned long long> sent_;
};

}  // namespace gct
