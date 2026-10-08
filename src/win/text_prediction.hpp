// text_prediction.hpp — Windows' own word prediction (Windows.Data.Text.TextPredictionGenerator, the touch
// keyboard's suggestions): offline, for every installed input language, no download. The typing help's basic
// vocabulary before it has learned your words ("Sch" -> "schon", "schön"; "wie g" -> "wie geht's").
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace gct {

class TextPrediction {
public:
    TextPrediction();
    ~TextPrediction();
    // "de-DE", "en-US" … False when Windows has no prediction for it (input language not installed).
    bool Init(const std::wstring& languageTag);
    bool Ready() const;
    // Candidates for the text typed so far (the last word, or the last few for phrases), best first. Blocks at most
    // ~150 ms; answers are cached per input.
    const std::vector<std::wstring>& Candidates(const std::wstring& input) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    mutable std::unordered_map<std::wstring, std::vector<std::wstring>> cache_;
};

}  // namespace gct
