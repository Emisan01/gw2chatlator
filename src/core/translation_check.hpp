// translation_check.hpp — what every translation is checked for, whichever translator made it: nothing lost (each
// sentence arrived, the protected parts are there) and nothing cut short. The local models translate sentence by
// sentence with it (NLLB drops sentences when it gets several at once – a Spanish guild ad lost two of three, user,
// 2026-10-27). Portable, no windows.h.
#pragma once

#include <string>
#include <vector>

#include "protect.hpp"

namespace gct {

// A text in its sentences, each with the spaces behind it: joining text + gap of all gives the text back exactly.
// A sentence ends after . ! ? … (one or more, closing quotes and brackets included) followed by a space; not after an
// abbreviation with a dot inside ("z.B.", "e.g.") or a single letter ("J. R. R.").
struct Sentence {
    std::wstring text, gap;
};
std::vector<Sentence> SplitSentences(const std::wstring& text);
size_t CountSentences(const std::wstring& text);  // those with a word in them

// The protected text of a message in its sentences: each sentence with its own segments (a protected part is never cut
// – no sentence ends inside it) and the spaces behind it. Joining all segments and gaps gives the message back.
struct SentenceSegments {
    std::vector<Segment> segments;
    std::wstring gap;
};
std::vector<SentenceSegments> SegmentsBySentence(const std::vector<Segment>& segments);

// The protected parts that must reach the output (keep segments with a letter, digit or symbol in them) and are not
// in `output`; a keep-word in another case counts as there ("LFG" → "lfg").
std::vector<std::wstring> MissingProtected(const std::vector<Segment>& segments, const std::wstring& output);

enum class TranslationIssue { None, LostProtected, LostSentence, TooShort };
struct TranslationCheck {
    TranslationIssue issue = TranslationIssue::None;
    std::wstring detail;  // the missing part, or "1/3" sentences
    bool ok() const { return issue == TranslationIssue::None; }
};

// `segments` went to the translator, `output` came back, in `targetLang` ("DE", "zh-Hans" …). Too short is only judged
// for alphabetic targets (Chinese, Japanese, Korean write much shorter) and texts of 20 letters and more.
TranslationCheck CheckTranslation(const std::vector<Segment>& segments, const std::wstring& output,
                                  const std::wstring& targetLang);

}  // namespace gct
