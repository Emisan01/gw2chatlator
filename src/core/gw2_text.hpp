// gw2_text.hpp — what the GW2 chat can take: scripts, length, direction.
#pragma once

#include <string>
#include <vector>

namespace gct {

// Name of the first script in `text` the GW2 chat font most likely cannot
// show, or empty. Latin incl. accents is fine.
std::wstring UnsupportedScript(const std::wstring& text);

// Name of the first script in `text` tested to render blank in GW2 chat (e.g. Cyrillic), or empty.
std::wstring WarnUnsupportedInGw2(const std::wstring& text);

// True if the first strongly directional character is right-to-left
// (Arabic, Hebrew ...). Used to align lines and the input field.
bool IsRtlText(const std::wstring& text);

// True if our ImGui chat/preview font ranges (kb_theme kRanges) can draw `codePoint`.
bool OurFontCanDraw(uint32_t codePoint);

// If `text` contains any non-whitespace character the active font (`canDraw`, default OurFontCanDraw)
// cannot draw (e.g. Chinese/Japanese/Arabic), returns a readable notice like "Chinese – 12 characters"
// instead of boxes; otherwise returns `text` unchanged.
std::wstring DisplayForOurFont(const std::wstring& text, const std::wstring& langHint = L"",
                               bool (*canDraw)(uint32_t) = nullptr);

// Splits a message into chat lines of at most `maxCodePoints`, preferring
// sentence ends, then word boundaries. `prefix` (e.g. "/p ") is repeated in
// front of every part and counted.
std::vector<std::wstring> SplitForChat(const std::wstring& text, const std::wstring& prefix, size_t maxCodePoints);

}  // namespace gct
