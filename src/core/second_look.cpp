// second_look.cpp
#include "second_look.hpp"

#include <algorithm>
#include <cstdlib>
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

}  // namespace gct
