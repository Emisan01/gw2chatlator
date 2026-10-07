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

namespace gct {

// The word without punctuation around it: "(main)," -> "main"; `start` gets its position in `token`.
std::wstring WordCore(const std::wstring& token, size_t* start = nullptr);

// Worth a dictionary check at all: Latin letters, 3+ long, no digits, no abbreviation in capitals (LFG, WvW
// stays: mixed case with 3 capitals counts as one), no link, chat code or smiley.
bool WorthSecondLook(const std::wstring& core);

// The second reading may replace the first: different, close (1 edit up to 4 letters, 2 for 5, 3 above).
bool PlausibleRereading(const std::wstring& first, const std::wstring& second);

}  // namespace gct
