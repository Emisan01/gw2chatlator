// voice_learn.hpp — words the voice input misheard, learned from your own corrections (GW2 Keyboard).
//
// Like the typing side it learns only from what you send: the text the recognizer put into the input box is compared
// word by word with the message you finally sent. A few words swapped for words that sound alike ("Red" -> "Raid",
// "Fracktale" -> "Fraktale", "Löwen Stein" -> "Löwenstein") are a correction and are remembered; a rewritten message,
// added or removed words, plurals and other endings are not. Later dictations get the learned words right away
// (Apply), and the meant words steer the recognizer's spelling (Words, whisper's initial prompt).
// Stored as "heard = meant" per line (<data>\voice-fixes.txt), editable in Settings -> Voice. Portable, no windows.h.
#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace gct {

class VoiceFixes {
public:
    using Pair = std::pair<std::wstring, std::wstring>;  // heard, meant
    static constexpr size_t kMaxPairs = 500;

    // One "heard = meant" per line; '#' starts a comment line; empty sides are skipped. Later lines win.
    void Parse(const std::wstring& text);
    std::wstring Serialize() const;  // "heard = meant\r\n" per pair, oldest first

    // `dictated`: the input text right after the voice input; `sent`: the text that was sent. Returns the pairs it
    // learned (none for a rewrite). Newest wins: a heard phrase learned again gets the new meaning.
    std::vector<Pair> Learn(const std::wstring& dictated, const std::wstring& sent);

    // `text` with every learned heard phrase (whole words, case-insensitive) replaced by its meaning, written as
    // learned; capitalized at the start of a sentence.
    std::wstring Apply(const std::wstring& text) const;

    // The meant words, newest first, each once (for the recognizer's prompt).
    std::vector<std::wstring> Words() const;

    const std::vector<Pair>& Entries() const { return pairs_; }
    size_t Size() const { return pairs_.size(); }
    void Clear() { pairs_.clear(); }

    // A heard -> meant pair that may be learned: different, plausibly sounding alike, not just another ending.
    static bool SoundsAlike(const std::wstring& heard, const std::wstring& meant);

private:
    void Add(const std::wstring& heard, const std::wstring& meant);
    std::vector<Pair> pairs_;  // oldest first
};

}  // namespace gct
