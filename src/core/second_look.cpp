// second_look.cpp
#include "second_look.hpp"

#include <algorithm>
#include <cstdlib>
#include <cwchar>
#include <vector>

#include "text.hpp"
#include "word_model.hpp"

namespace gct {

namespace {

bool IsLatinLetter(wchar_t c) {
    return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= 0x00C0 && c <= 0x024F && c != 0x00D7 && c != 0x00F7);
}

}  // namespace

std::wstring WordCore(const std::wstring& token, size_t* start) {
    size_t a = 0, b = token.size();
    while (a < b && !IsWordChar(token[a])) ++a;
    while (b > a && !IsWordChar(token[b - 1])) --b;
    if (start) *start = a;
    return token.substr(a, b - a);
}

bool WorthSecondLook(const std::wstring& core) {
    if (core.size() < 3 || core.size() > 24) return false;
    size_t upper = 0;
    for (wchar_t c : core) {
        if (c >= L'0' && c <= L'9') return false;  // levels, times, prices
        if (c == L'/' || c == L'.' || c == L'@' || c == L'&' || c == L'[') return false;  // links, codes
        if (c == L'\'' || c == L'-' || c == 0x2019) continue;                        // don't, Lion's, e-mail
        if (!IsLatinLetter(c)) return false;  // other scripts: the checker knows less there
        if (c >= L'A' && c <= L'Z') ++upper;
    }
    if (upper == core.size()) return false;  // LFG, ARAH: abbreviations
    if (upper >= 2 && core.size() <= 4) return false;  // WvW, PvP
    return true;
}

bool PlausibleRereading(const std::wstring& first, const std::wstring& second) {
    if (second.empty() || first == second) return false;
    // Short words: one character only ("tbe" -> "the"); "rnain" -> "main" is 2 on 5 letters; longer words up to 3
    // ("uvjrde" -> "würde": uv -> w, j -> ü). The second reading must also be a real word.
    const int allowed = first.size() <= 4 ? 1 : first.size() <= 5 ? 2 : 3;
    const int d = EditDistance(CaseFold(first), CaseFold(second), allowed);
    return d >= 1 && d <= allowed;
}

namespace {

bool IsDigit(wchar_t c) { return c >= L'0' && c <= L'9'; }
bool IsLetter(wchar_t c) { return IsWordChar(c) && !IsDigit(c) && c != L'_'; }
// Marks text recognition puts where a letter was.
bool IsStrayMark(wchar_t c) { return c != 0 && wcschr(L"!|><{}\\#$%§¦~*", c) != nullptr; }

struct Confusion {
    const wchar_t* from;
    const wchar_t* to;
};
// Marks and digits: in the order of likelihood (the first is what MarksToLetters uses).
const Confusion kMarks[] = {
    {L"!", L"t"}, {L"!", L"l"}, {L"!", L"i"}, {L"|", L"l"}, {L"|", L"I"}, {L"|", L"i"}, {L"0", L"o"}, {L"0", L"O"},
    {L"1", L"l"}, {L"1", L"i"}, {L"1", L"I"}, {L"5", L"s"}, {L"5", L"S"}, {L"9", L"g"}, {L"9", L"e"}, {L"9", L"q"},
    {L"8", L"B"}, {L"6", L"b"}, {L"6", L"G"}, {L"2", L"z"}, {L"3", L"e"}, {L"4", L"a"}, {L"7", L"t"}, {L"$", L"s"},
    {L"$", L"S"}, {L">", L""}, {L"<", L""}, {L"*", L""}, {L"~", L""}, {L"#", L"h"}, {L"%", L"x"}, {L"{", L"f"},
    {L"}", L"f"}, {L"\\", L"l"}, {L"§", L"s"}, {L"¦", L"l"},
};
// Letters text recognition mixes up (measured in GW2 and app fonts: "rnain", "uvjrde", "putput", "tbe").
const Confusion kLetters[] = {
    {L"rn", L"m"}, {L"m", L"rn"}, {L"vv", L"w"}, {L"uv", L"w"}, {L"VV", L"W"}, {L"cl", L"d"}, {L"ii", L"u"},
    {L"li", L"h"}, {L"p", L"o"}, {L"o", L"p"}, {L"c", L"e"}, {L"e", L"c"}, {L"l", L"i"}, {L"i", L"l"}, {L"I", L"l"},
    {L"l", L"I"}, {L"b", L"h"}, {L"h", L"b"}, {L"n", L"h"}, {L"h", L"n"}, {L"u", L"n"}, {L"n", L"u"}, {L"j", L"ü"},
    {L"u", L"ü"}, {L"a", L"ä"}, {L"o", L"ö"}, {L"g", L"q"}, {L"q", L"g"}, {L"f", L"t"}, {L"t", L"f"},
    {L"B", L"8"}, {L"O", L"Q"}, {L"Q", L"O"}, {L"v", L"y"}, {L"y", L"v"}, {L"d", L"cl"}, {L"w", L"vv"},
};

void AddUnique(std::vector<std::wstring>& out, const std::wstring& s, const std::wstring& self) {
    if (s.empty() || s == self) return;
    if (std::find(out.begin(), out.end(), s) == out.end()) out.push_back(s);
}

// Every way to apply one rule of `rules` once (each position).
template <size_t N>
void OneReplacement(const std::wstring& w, const Confusion (&rules)[N], std::vector<std::wstring>& out,
                    const std::wstring& self, size_t max) {
    for (const Confusion& r : rules) {
        const std::wstring from = r.from;
        for (size_t at = w.find(from); at != std::wstring::npos; at = w.find(from, at + 1)) {
            if (out.size() >= max) return;
            AddUnique(out, w.substr(0, at) + r.to + w.substr(at + from.size()), self);
        }
    }
}

}  // namespace

bool LooksGarbled(const std::wstring& token) {
    // Without punctuation around it (a mark in front stays: "!raining").
    size_t a = 0, b = token.size();
    while (a < b && !IsWordChar(token[a]) && !IsStrayMark(token[a])) ++a;
    while (b > a && !IsWordChar(token[b - 1])) --b;
    const std::wstring t = token.substr(a, b - a);
    size_t letters = 0;
    for (wchar_t c : t) {
        if (c == L'/' || c == L'@' || c == L'[' || c == L'&' || c == L'.' || c == L':' || c == L'=') return false;
        if (IsLetter(c)) ++letters;
    }
    if (letters < 2) return false;  // numbers, smileys, "<3"
    const size_t n = t.size();
    // A mark glued in front of a word: "!raining", "|ch".
    if (n >= 3 && IsStrayMark(t[0]) && IsLetter(t[1]) && letters + 1 == n) return true;
    for (size_t i = 1; i + 1 < n; ++i)
        if ((IsDigit(t[i]) || IsStrayMark(t[i])) && IsLetter(t[i - 1]) && IsLetter(t[i + 1])) return true;
    // Digits, then letters: only usual suffixes ("10er", "4k", "1st", "5min", "100x").
    size_t d = 0;
    while (d < n && IsDigit(t[d])) ++d;
    if (d > 0 && d < n) {
        size_t e = d;
        while (e < n && IsLetter(t[e])) ++e;
        if (e == n) {
            static const wchar_t* const kSuffixes[] = {L"er", L"ern", L"k", L"st", L"nd", L"rd", L"th", L"x", L"s",
                                                        L"h", L"m", L"min", L"ms", L"gb", L"mb", L"v", L"p", L"fps",
                                                        L"hz", L"e", L"te", L"ten", L"ter", L"en", L"g", L"d", L"w",
                                                        L"ers", L"km", L"kg", L"mio", L"sec"};
            const std::wstring tail = CaseFold(t.substr(d));
            bool usual = false;
            for (const wchar_t* s : kSuffixes) usual = usual || tail == s;
            if (!usual && tail.size() >= 2) return true;
        }
    }
    return false;
}

std::wstring GarbledCore(const std::wstring& token, size_t* start) {
    size_t at = 0;
    std::wstring core = WordCore(token, &at);
    if (at > 0 && IsStrayMark(token[at - 1]) && (at == 1 || !IsStrayMark(token[at - 2]))) {
        --at;
        core.insert(core.begin(), token[at]);
    }
    if (start) *start = at;
    return core;
}

std::vector<std::wstring> ConfusionCandidates(const std::wstring& core, size_t max) {
    std::vector<std::wstring> out;
    size_t marks = 0;
    for (wchar_t c : core) marks += IsDigit(c) || IsStrayMark(c);
    if (marks > 0) {
        // Every mark replaced at once by each of its readings (several marks: all combinations).
        std::vector<std::wstring> level{L""};
        for (wchar_t c : core) {
            std::vector<std::wstring> next;
            for (const std::wstring& w : level) {
                bool any = false;
                if (IsDigit(c) || IsStrayMark(c))
                    for (const Confusion& r : kMarks)
                        if (r.from[0] == c && r.from[1] == 0) {
                            next.push_back(w + r.to);
                            any = true;
                        }
                if (!any) next.push_back(w + c);
            }
            if (next.size() > max) next.resize(max);
            level = std::move(next);
        }
        for (const std::wstring& w : level) AddUnique(out, w, core);
        // A mark in front that is no letter at all: "!raining" -> "raining" as well.
        if (IsStrayMark(core[0])) AddUnique(out, core.substr(1), core);
        // Then one letter confusion on top of the most likely reading ("g9danken" -> "ggdanken" -> ...).
        if (out.size() < max) OneReplacement(MarksToLetters(core), kLetters, out, core, max);
        if (out.size() > max) out.resize(max);
        return out;
    }
    OneReplacement(core, kLetters, out, core, max);
    return out;
}

std::wstring MarksToLetters(const std::wstring& core) {
    std::wstring s;
    for (wchar_t c : core) {
        bool done = false;
        for (const Confusion& r : kMarks)
            if (r.from[0] == c && r.from[1] == 0) {
                s += r.to;
                done = true;
                break;
            }
        if (!done) s += c;
    }
    return s;
}

}  // namespace gct
