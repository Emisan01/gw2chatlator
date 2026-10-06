// gw2_text.hpp — what the GW2 chat can take: scripts, length, direction.
#pragma once

#include <string>
#include <vector>

namespace gct {

// Name of the first script in `text` the GW2 chat font most likely cannot
// show (German, for the status line), or empty. Latin incl. accents is fine.
std::wstring UnsupportedScript(const std::wstring& text);

// True if the first strongly directional character is right-to-left
// (Arabic, Hebrew ...). Used to align lines and the input field.
bool IsRtlText(const std::wstring& text);

// Splits a message into chat lines of at most `maxCodePoints`, preferring
// sentence ends, then word boundaries. `prefix` (e.g. "/p ") is repeated in
// front of every part and counted.
std::vector<std::wstring> SplitForChat(const std::wstring& text, const std::wstring& prefix, size_t maxCodePoints);

}  // namespace gct
