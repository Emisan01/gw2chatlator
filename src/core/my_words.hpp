// my_words.hpp — your own words and what they mean ("finds = finde es").
//
// Slang, abbreviations and mixed language a translator cannot know. Before a
// text is translated, each of your words is replaced by its meaning (whole
// words only, case of the first letter kept), so every translator gets plain
// language; what you wrote stays visible as written. The words also count as
// correct for spell checking and the second look.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace gct {

class MyWords {
public:
    // One "word = meaning" per line; '#' starts a comment. A line without
    // '=' only marks the word as correct (a name, a GW2 term).
    void Parse(const std::wstring& text);
    std::wstring Serialize() const;

    void Set(const std::wstring& word, const std::wstring& meaning);
    bool Remove(const std::wstring& word);
    // The meaning of `word` (empty if none or unknown).
    std::wstring MeaningOf(const std::wstring& word) const;
    bool Knows(const std::wstring& word) const;

    // `text` with every word that has a meaning replaced by it.
    std::wstring Expand(const std::wstring& text) const;

    size_t Size() const { return entries_.size(); }
    const std::vector<std::pair<std::wstring, std::wstring>>& Entries() const { return entries_; }

private:
    std::vector<std::pair<std::wstring, std::wstring>> entries_;  // word, meaning (may be empty)
};

}  // namespace gct
