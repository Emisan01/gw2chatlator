#pragma once
// Phone-keyboard style word knowledge: what you type often, which word tends
// to follow which, and which known word a typo most likely meant. Learns from
// the messages you send (per language, saved as learned_<lang>.txt).
// Portable, no windows.h.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace gct {

// Damerau-Levenshtein (optimal string alignment) on case-folded text.
// Returns limit + 1 as soon as the distance is known to exceed `limit`.
int EditDistance(const std::wstring& a, const std::wstring& b, int limit);

// `candidate` written the way `typed` was: "Hallo" + "hello" -> "Hello",
// "HALLO" + "hello" -> "HELLO" (only for words longer than one letter).
std::wstring MatchCase(const std::wstring& typed, const std::wstring& candidate);

class WordModel {
public:
    // Words and word pairs of a sent message (digits, chat codes, links and
    // one-letter words are skipped).
    void Learn(const std::wstring& text, double weight = 1.0);
    void AddWord(const std::wstring& word, double weight);
    // The user undid an autocorrection: `word` is meant as written.
    void Confirm(const std::wstring& word) { AddWord(word, 2.0); }

    double Count(const std::wstring& word) const;
    bool Knows(const std::wstring& word) const { return Count(word) >= 2.0; }

    // Words starting with `prefix` (longer than it), best first; a word that
    // often follows `prev` ranks higher. Case follows the typed prefix.
    std::vector<std::wstring> Complete(const std::wstring& prefix, const std::wstring& prev, size_t n) const;
    // Words that often follow `prev`.
    std::vector<std::wstring> Next(const std::wstring& prev, size_t n) const;
    // Known words one or two typos away from `word` (same first letter or the
    // first two swapped), closest and most used first.
    std::vector<std::wstring> Near(const std::wstring& word, size_t n) const;

    size_t Size() const { return words_.size(); }
    size_t PairCount() const { return pairs_.size(); }
    bool Dirty() const { return dirty_; }
    void ClearDirty() { dirty_ = false; }

    // UTF-8 text: "w\t<form>\t<count>" and "n\t<prev>\t<next>\t<count>" lines.
    std::string Serialize() const;
    void Parse(const std::string& utf8);

    static constexpr size_t kMaxWords = 20000;
    static constexpr size_t kMaxPairs = 60000;

private:
    struct Word {
        std::wstring form;  // as typed most recently
        double count = 0;
    };
    void AddPair(const std::wstring& prevKey, const std::wstring& nextKey, double weight);
    void Prune();

    std::unordered_map<std::wstring, Word> words_;                  // key: case-folded
    std::unordered_map<std::wstring, std::unordered_map<std::wstring, double>> pairs_;  // prev -> next -> count
    size_t pairTotal_ = 0;
    bool dirty_ = false;
};

// The correction a phone keyboard would apply to a just-finished word, or
// empty. Conservative: words of 4+ letters only, never all-caps (LFG, WvW),
// never a word you use, one typo up to 6 letters / two above, same first
// letter (or its first two letters swapped), single words only.
// `spell` are the dictionary's suggestions, best first.
std::wstring ChooseCorrection(const std::wstring& word, const std::vector<std::wstring>& spell,
                              const WordModel& model);

}  // namespace gct
