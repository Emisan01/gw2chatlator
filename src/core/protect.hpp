// protect.hpp — splits a message into parts the translator may touch and
// parts it must leave alone.
#pragma once

#include <string>
#include <vector>

#include "glossary.hpp"
#include "text.hpp"

namespace gct {

struct Segment {
    std::wstring text;  // for protected segments: the final text to appear
    bool keep = false;  // true = must reach the output unchanged
};

struct ProtectedText {
    std::vector<Segment> segments;
    std::vector<GlossaryMatch> glossaryHits;  // official names that were substituted
};

// Protected, in this order of priority:
//   1. chat codes [&...]             -> unchanged
//   2. official game names            -> official name in the target language
//   3. keep-words (LFG, WvW, DPS ...) -> unchanged
ProtectedText ProtectForTranslation(const std::wstring& body, const Glossary* glossary, const WordSet* keepWords);

std::wstring JoinSegments(const std::vector<Segment>& segments);
bool HasProtected(const std::vector<Segment>& segments);

}  // namespace gct
