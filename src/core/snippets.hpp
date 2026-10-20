// snippets.hpp — your text blocks: saved from the input, put back into it with a click (menu ≡ → Text blocks).
//
// Only your own text, one block per line in <data>\snippets.txt (a line break inside a block is stored as "\n", a
// backslash as "\\"). At most kMax blocks of at most kMaxChars characters (three chat lines). Portable, no windows.h.
#pragma once

#include <string>
#include <vector>

namespace gct {

class Snippets {
public:
    static constexpr size_t kMax = 30;
    static constexpr size_t kMaxChars = 600;

    enum class Added { Yes, Empty, Twice, Full };

    void Parse(const std::wstring& text);
    std::wstring Serialize() const;

    // Trimmed, cut to kMaxChars (never inside a surrogate pair); refused when empty, already there or kMax are saved.
    Added Add(const std::wstring& text);
    bool Remove(size_t index);

    const std::vector<std::wstring>& Items() const { return items_; }

private:
    std::vector<std::wstring> items_;
};

}  // namespace gct
