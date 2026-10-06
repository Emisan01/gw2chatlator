// names.hpp — the names of the people in the chat, so a translator leaves
// them alone ("helo du med kiro" -> "Mad Kiro" is a name, not "mad"). Fed
// with the speakers the reader saw; local only, kept in memory.
// Portable, no windows.h.
#pragma once

#include <string>
#include <vector>

#include "text.hpp"

namespace gct {

class NameList {
public:
    // A speaker as read from the chat ("Mad Kiro", "B E L A"). The most
    // recent kMax names are kept.
    void Add(const std::wstring& name);
    size_t Size() const { return names_.size(); }

    // Where known names stand in `text`, also with one typo per word of 3+
    // letters ("med kiro" for "Mad Kiro"; at least one word must be exact).
    // A one-word name needs 3+ letters; a typo in it only counts from 6 letters.
    std::vector<Span> Find(const std::wstring& text) const;

    static constexpr size_t kMax = 300;

private:
    std::vector<std::vector<std::wstring>> names_;  // words of each name (WordKey), oldest first
};

}  // namespace gct
