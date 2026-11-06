// spellcheck.hpp — thin wrapper around the Windows Spell Checking API
// (Windows 8+, offline, uses the installed language packs).
#pragma once

#include <string>
#include <vector>

#include "core/text.hpp"

struct ISpellChecker;  // from <spellcheck.h>; kept out of this header

namespace gct {

struct SpellIssue {
    enum class Kind { Suggest, Replace, Delete };
    Span span;
    Kind kind = Kind::Suggest;
    std::wstring replacement;  // Kind::Replace: Windows' safe autocorrection
};

class SpellChecker {
public:
    SpellChecker() = default;
    ~SpellChecker();
    SpellChecker(const SpellChecker&) = delete;
    SpellChecker& operator=(const SpellChecker&) = delete;

    // Tries the tags in order ("de-AT", "de-DE", "de" ...). COM must be
    // initialised (apartment-threaded) on the calling thread; use the
    // checker only from that thread.
    bool Init(const std::vector<std::wstring>& tags);

    bool Ready() const { return checker_ != nullptr; }
    const std::wstring& Tag() const { return tag_; }

    std::vector<SpellIssue> Check(const std::wstring& text) const;
    std::vector<std::wstring> Suggest(const std::wstring& word, size_t max = 6) const;

private:
    ISpellChecker* checker_ = nullptr;
    std::wstring tag_;
};

}  // namespace gct
