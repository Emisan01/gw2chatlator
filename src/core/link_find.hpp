// link_find.hpp — finds a chat link's text ("[Lion's Arch Waypoint]") among recognized text lines (OCR word boxes),
// for the waypoint click in GW2's own chat. Brackets, case and a few misread letters do not matter; of several
// matches the lowest wins (GW2's chat has its newest line at the bottom). Portable, unit-tested.
#pragma once

#include <string>
#include <vector>

namespace gct {

struct TextBox {
    std::wstring text;
    int x = 0, y = 0, w = 0, h = 0;
};

struct TextRow {
    std::vector<TextBox> words;
};

// `label` as the game shows it, with or without its brackets. `hit` gets the box around the matched words.
bool FindLinkText(const std::vector<TextRow>& rows, const std::wstring& label, TextBox* hit);

// What Events: Chat told us about the line the link stands in.
struct ChatLineHint {
    std::wstring speaker;  // "Kai Example" ("" = none, e.g. a commander's squad message)
    std::wstring text;     // the message as GW2 shows it – chat codes as their "[Name]"
    std::wstring label;    // the link: "[Lion's Arch Waypoint]"
};

// The row in GW2's chat that is this very line, and the link's box in it. A row must hold the link's name; then it
// scores by how much of it belongs to this line (the speaker and the message, a wrapped part counts as well); the best
// wins, a tie the lowest (the newest). Below `kMinFit` nothing is taken – better no click than a wrong one.
bool FindChatLink(const std::vector<TextRow>& rows, const ChatLineHint& hint, TextBox* hit, double* fit = nullptr);
constexpr double kMinFit = 0.7;

}  // namespace gct
