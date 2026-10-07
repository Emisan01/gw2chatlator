#pragma once
// Phone-keyboard style word knowledge: what you type often, which word tends
// to follow which, and which known word a typo most likely meant. Learns from
// the messages you send (per language, saved as learned_<lang>.txt).
// Portable, no windows.h.

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace gct {

// How words are compared: case-folded, and Arabic spelling variants that
// typists mix up count as the same letter (أ إ آ ٱ -> ا, ى -> ي, ة -> ه,
// ؤ -> و, ئ -> ي, Persian ک/ی); tatweel and harakat are dropped.
std::wstring WordKey(const std::wstring& word);

// True if two letters sit next to each other on the active keyboard layout.
using KeyNeighbors = std::function<bool(wchar_t, wchar_t)>;

// Where the letters sit on the keyboard: three rows of keys, each row shifted
// a bit to the right like on a real keyboard. Letters are WordKey'd.
class KeyLayout {
public:
    void Set(wchar_t c, int row, float x);
    bool Empty() const { return pos_.empty(); }
    bool Neighbors(wchar_t a, wchar_t b) const;
    // The letter one key to the left (dx = -1) or right (+1) in the same row; 0 if none.
    wchar_t Shifted(wchar_t c, int dx) const;
    KeyNeighbors AsNeighbors() const;

private:
    struct Pos {
        int row;
        float x;
    };
    std::unordered_map<wchar_t, Pos> pos_;
    std::unordered_map<int, wchar_t> at_;  // row * 1000 + column -> letter
};

// The word typed with the whole hand one key off ("jsööp" for "hallo" on
// QWERTZ): the word moved back left and right. Only complete moves count.
std::vector<std::wstring> HandShiftVariants(const std::wstring& word, const KeyLayout& layout);

// Edit distance for typing slips: a wrong key next door costs 0.5, two
// swapped letters 0.7, anything else 1. Returns limit + 1 once above `limit`.
double SlipDistance(const std::wstring& a, const std::wstring& b, const KeyLayout& layout, double limit);

// Damerau-Levenshtein (optimal string alignment) on WordKey text.
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
    // You took `word` from the suggestions after `prev2 prev`: the word, its pair and its triple get stronger, so it
    // ranks higher there next time (what you did not take falls back by itself).
    void Chose(const std::wstring& prev2, const std::wstring& prev, const std::wstring& word, double weight);
    // The user undid an autocorrection: `word` is meant as written.
    void Confirm(const std::wstring& word) { AddWord(word, 2.0); }
    // Removes a word and every pair it is part of. False if it was unknown.
    bool Forget(const std::wstring& word);
    void Clear();

    double Count(const std::wstring& word) const;
    bool Knows(const std::wstring& word) const { return Count(word) >= 2.0; }

    // Words starting with `prefix` (longer than it), best first; a word that
    // often follows `prev` ranks higher, one that follows `prev2 prev` higher still.
    // Case follows the typed prefix.
    std::vector<std::wstring> Complete(const std::wstring& prefix, const std::wstring& prev, size_t n,
                                       const std::wstring& prev2 = std::wstring()) const;
    // Like Complete, but the typed prefix may hold one typo ("helo" -> "hello",
    // "komt" -> "kommt"): 3+ letters, same first letter (or the first two
    // swapped), only words used at least twice. A typo on a neighbouring key
    // ranks higher. Exact completions are not repeated here.
    std::vector<std::wstring> CompleteFuzzy(const std::wstring& prefix, const std::wstring& prev, size_t n,
                                            const KeyNeighbors& neighbors = nullptr) const;
    // Words that often follow `prev2 prev` ("kommst du" -> "mit"), then those that follow `prev`.
    std::vector<std::wstring> Next(const std::wstring& prev, size_t n, const std::wstring& prev2 = std::wstring()) const;
    // The next word when it is (almost) always the same: seen 3+ times there and at least 60 % of what followed –
    // worth offering before you type a letter. Else "".
    std::wstring NextSure(const std::wstring& prev, const std::wstring& prev2 = std::wstring()) const;
    // Known words one or two typos away from `word` (same first letter or the
    // first two swapped), closest and most used first.
    std::vector<std::wstring> Near(const std::wstring& word, size_t n) const;
    // Known words a typing slip away (SlipDistance: up to 1 for short words,
    // 1.5 up to 7 letters, 2.5 above), closest and most used first.
    std::vector<std::wstring> NearSlip(const std::wstring& word, const KeyLayout& layout, size_t n) const;

    size_t Size() const { return words_.size(); }
    size_t PairCount() const { return pairs_.size(); }
    bool Dirty() const { return dirty_; }
    void ClearDirty() { dirty_ = false; }

    // UTF-8 text: "w\t<form>\t<count>" and "n\t<prev>\t<next>\t<count>" lines; a word triple is a pair whose
    // <prev> is "<prev2>\x1f<prev>" (older files without triples load unchanged).
    std::string Serialize() const;
    void Parse(const std::string& utf8);

    static constexpr size_t kMaxWords = 20000;
    static constexpr size_t kMaxPairs = 60000;
    // Forgetting by use (like the forgetting curve of Android's keyboard, but counted in messages, not days – nothing
    // fades while you do not write): every kDecayEvery learned messages all counts shrink by kDecay. Old habits make
    // room for new ones; a word stays "known" (count 2) once you used it twice.
    static constexpr int kDecayEvery = 200;
    static constexpr double kDecay = 0.9;
    void Decay();

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
    int learnedSinceDecay_ = 0;
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
