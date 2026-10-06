// text.hpp — string helpers shared by every layer. Portable, no Win32.
#pragma once

#include <cstddef>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace gct {

// A half-open range [start, start + length) in UTF-16/wchar_t units.
struct Span {
    size_t start = 0;
    size_t length = 0;
    size_t end() const { return start + length; }
    bool Overlaps(const Span& o) const { return start < o.end() && o.start < end(); }
};

// Set of case-folded words (see CaseFold).
using WordSet = std::unordered_set<std::wstring>;

// ---- UTF conversion (works with 16-bit and 32-bit wchar_t) ----------------
std::string ToUtf8(const std::wstring& w);
std::wstring FromUtf8(const std::string& s);

// Number of Unicode code points — what the GW2 chat box counts.
size_t CodePointCount(const std::wstring& w);

// ---- Basic helpers ---------------------------------------------------------
std::wstring Trim(const std::wstring& s);
std::wstring ToUpperAscii(std::wstring s);
std::wstring ToLowerAscii(std::wstring s);

// For ini values (the ini is ASCII): non-ASCII as \uXXXX (UTF-16 units),
// backslash as \\. Unescape accepts exactly what Escape produces and keeps
// anything else as it is.
std::wstring AsciiEscape(const std::wstring& s);
std::wstring AsciiUnescape(const std::wstring& s);

// Case folding for matching: ASCII, Latin-1 and Latin Extended-A.
// (Not called FoldString: that is a Win32 macro.)
// Enough for DE/EN/FR/ES game text; CJK has no case.
wchar_t CaseFoldChar(wchar_t c);
std::wstring CaseFold(const std::wstring& s);

// Letters/digits of Latin scripts (incl. umlauts, accents) and anything
// above U+0250 except common punctuation. Used for whole-word matching.
bool IsWordChar(wchar_t c);

// Words of a text as spans (runs of IsWordChar, apostrophes inside a word
// are kept: "Lion's").
std::vector<Span> WordSpans(const std::wstring& s);

// GW2 chat is single-line: newlines/tabs become spaces, runs collapse, trimmed.
std::wstring SanitizeChatText(const std::wstring& s);

// Comma separated language list ("en-gb, fr ,ZH-HANS") -> {"EN-GB","FR","ZH-HANS"}
std::vector<std::wstring> ParseLangList(const std::wstring& s);

// ---- GW2 chat specifics ----------------------------------------------------
// Splits a leading chat command so it is never sent to the translator.
//   "/p hallo"              -> prefix "/p ",              body "hallo"
//   "/w Emi.1234 hi"        -> prefix "/w Emi.1234 ",     body "hi"
//   "/w Some Name, hallo"   -> prefix "/w Some Name, ",   body "hallo"
//   "/wave"                 -> prefix "/wave",            body ""
struct ChatSplit {
    std::wstring prefix;
    std::wstring body;
};
ChatSplit SplitChatCommand(const std::wstring& text);

// Chat links such as [&AgH1WQAA] (waypoints, items, skills ...).
std::vector<Span> FindChatCodes(const std::wstring& s);

// Account names like "Emi.1234".
bool IsAccountName(const std::wstring& token);

}  // namespace gct
