// fake_spellcheck.cpp — deterministic stand-in for win/spellcheck.cpp so the
// spell UI (marks, menu, autocorrection) can be exercised where the Windows
// spell checker is missing (Wine, CI). Linked into GW2ChatTranslator_fakespell.
//
//   komtm   -> suggestions "kommt", "komm"
//   dsa     -> safe autocorrection to "das"
//   fraktal -> flagged, but on the GW2 word list (must NOT show up)
//   a word repeated right after itself -> "delete" issue
#include "win/spellcheck.hpp"

namespace gct {

SpellChecker::~SpellChecker() = default;

bool SpellChecker::Init(const std::vector<std::wstring>&) {
    tag_ = L"de-DE (fake)";
    checker_ = reinterpret_cast<ISpellChecker*>(this);  // any non-null marker; never dereferenced
    return true;
}

std::vector<SpellIssue> SpellChecker::Check(const std::wstring& text) const {
    std::vector<SpellIssue> out;
    const std::vector<Span> words = WordSpans(text);
    for (size_t i = 0; i < words.size(); ++i) {
        const std::wstring w = CaseFold(text.substr(words[i].start, words[i].length));
        SpellIssue issue;
        issue.span = words[i];
        if (w == L"komtm" || w == L"fraktal") {
            out.push_back(issue);
        } else if (w == L"dsa") {
            issue.kind = SpellIssue::Kind::Replace;
            issue.replacement = L"das";
            out.push_back(issue);
        } else if (i > 0 && w == CaseFold(text.substr(words[i - 1].start, words[i - 1].length))) {
            issue.kind = SpellIssue::Kind::Delete;
            out.push_back(issue);
        }
    }
    return out;
}

std::vector<std::wstring> SpellChecker::Suggest(const std::wstring& word, size_t) const {
    if (CaseFold(word) == L"komtm") return {L"kommt", L"komm"};
    return {};
}

}  // namespace gct
