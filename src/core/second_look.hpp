// second_look.hpp — the rules of the "second look" at words that make no
// sense after text recognition (like a person reading twice).
//
// A word the dictionary does not know is read once more, enlarged more and
// with a little room around it. The new reading is taken only if it is a
// real word and close to the first one (one or two characters differ: "*ain"
// -> "main"); otherwise the first reading stays – people do write odd words.
// The decision for the dictionary itself lives in the app (Windows spell checker).
#pragma once

#include <string>
#include <vector>

namespace gct {

// The word without punctuation around it: "(main)," -> "main"; `start` gets its position in `token`.
std::wstring WordCore(const std::wstring& token, size_t* start = nullptr);

// Worth a dictionary check at all: Latin letters, 3+ long, no digits, no abbreviation in capitals (LFG, WvW
// stays: mixed case with 3 capitals counts as one), no link, chat code or smiley.
bool WorthSecondLook(const std::wstring& core);

// The second reading may replace the first: different, close (1 edit up to 4 letters, 2 for 5, 3 above).
bool PlausibleRereading(const std::wstring& first, const std::wstring& second);

// A token no person types – a recognition artifact: a digit or a mark (! | > $ ...) between letters ("syn!ax",
// "g9danken", "Plövdsi0Q"), "!" or "|" glued before a word ("!raining"), digits before letters
// with a capital among small letters ("9QEine"; "10er", "4k", "2nd", "2day" are fine). Times, levels, "gw2", links,
// chat codes, account names, emotes ("*grins*") are not.
bool LooksGarbled(const std::wstring& token);

// The part of a garbled token to repair: like WordCore, but a single mark in front of the letters belongs to it
// ("!raining," -> "!raining").
std::wstring GarbledCore(const std::wstring& token, size_t* start = nullptr);

// What a word may really have said, for a dictionary to pick from: characters text recognition confuses replaced
// (! -> t/l/i, 9 -> g/e, 0 -> o, 1 -> l/i, 5 -> s, rn <-> m, vv -> w, cl -> d, p <-> o, c <-> e, l <-> i ...). Marks and
// digits are replaced first (a garbled word gets up to two of them), letters one at a time. Never the word itself.
std::vector<std::wstring> ConfusionCandidates(const std::wstring& core, size_t max = 80);

// `core` with every mark and digit replaced by its most likely letter ("g9danken" -> "ggdanken"), for suggestions.
std::wstring MarksToLetters(const std::wstring& core);

}  // namespace gct
