#pragma once
// LanguageTool (https://languagetool.org) — optional grammar and spelling
// check while typing. Works with the public API (free, about 20 requests per
// minute, 20 KB per request) or a LanguageTool server you run yourself
// (unlimited, private). Portable request/response code; the HTTP call is in
// win/online_translators.

#include <set>
#include <string>
#include <vector>

#include "core/text.hpp"

namespace gct {

struct LtMatch {
    Span span;                              // in the checked text (wchar_t units)
    std::wstring message;                   // explanation (in the checked language)
    std::wstring shortMessage;
    std::vector<std::wstring> replacements;  // best first
    std::wstring ruleId;                    // "MORFOLOGIK_RULE_DE_DE", "PERS_PRONOUN_AGREEMENT" ...
    std::wstring issueType;                 // "misspelling", "grammar", "typographical", ...
    std::wstring categoryId;                // "TYPOS", "GRAMMAR", "CONFUSED_WORDS", ...
    bool IsGrammar() const {
        return issueType == L"grammar" || categoryId == L"GRAMMAR" || categoryId == L"CONFUSED_WORDS";
    }
    bool Spelling() const { return issueType == L"misspelling" || categoryId == L"TYPOS"; }
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

// Returns true if cached grammar marks from `checkedText` are still valid for `newText`.
// Valid when newText begins with checkedText (user continuing to type at the end) or is identical.
bool CanKeepGrammarMarks(const std::wstring& checkedText, const std::wstring& newText);

// Merges spelling marks from local checker and grammar marks from LanguageTool.
// Grammar marks from `cachedMarks` are retained if CanKeepGrammarMarks(cachedText, text).
// Overlapping spans: grammar marks take priority, keeping grammar message and explanation.
std::vector<WordMark> MergeGrammarMarks(const std::vector<WordMark>& spellMarks,
                                       const std::wstring& text,
                                       const std::wstring& cachedText,
                                       const std::vector<WordMark>& cachedMarks);

struct LtApplyResult {
    std::wstring text;
    std::vector<WordMark> marks;
    size_t replacedCount = 0;
};

// Applies LanguageTool matches to `currentText` according to `mode`.
// Matches are applied from back to front so indices remain valid.
// `revertedWords` and `revertedRules` prevent re-applying changes the user undid.
LtApplyResult ApplyLanguageToolMatches(const std::wstring& currentText,
                                      const std::wstring& checkedText,
                                      const std::vector<LtMatch>& matches,
                                      AutoCorrectMode mode,
                                      const std::set<std::wstring>& revertedWords,
                                      const std::set<std::wstring>& revertedRules);

// The public server allows 20 requests per minute per IP; this keeps us below.
class RateLimiter {
public:
    RateLimiter(int perMinute) : perMinute_(perMinute) {}
    // True if a request may go out at time `nowMs` (and records it).
    bool Allow(unsigned long long nowMs);
    // Milliseconds to wait until the next request is allowed (0 if allowed now).
    unsigned long long WaitMs(unsigned long long nowMs);

private:
    int perMinute_;
    std::vector<unsigned long long> sent_;
};

}  // namespace gct

