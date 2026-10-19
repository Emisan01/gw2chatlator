// link_find.cpp — see link_find.hpp.

#include "core/link_find.hpp"

#include <algorithm>
#include <cstdlib>
#include <cwctype>

#include "core/text.hpp"
#include "core/word_model.hpp"

namespace gct {

namespace {

std::wstring Letters(const std::wstring& s) {  // case-folded letters and digits only: brackets, spaces, dashes go
    std::wstring out;
    for (wchar_t c : CaseFold(s))
        if (std::iswalnum(c)) out += c;
    return out;
}

int Tolerance(size_t n) { return n <= 6 ? 0 : n <= 14 ? 1 : 2; }  // misread letters allowed

// The best match of `target` (letters) in one row: the box and how many letters differ. In a row the closest match
// wins (an exact one over one that swallowed a neighbouring word).
bool MatchInRow(const TextRow& row, const std::wstring& target, TextBox* box, int* diff) {
    const int tol = Tolerance(target.size());
    bool found = false;
    for (size_t i = 0; i < row.words.size(); ++i) {
        std::wstring joined;
        int x0 = row.words[i].x, y0 = row.words[i].y, x1 = x0, y1 = y0;
        for (size_t j = i; j < row.words.size(); ++j) {
            const TextBox& w = row.words[j];
            joined += Letters(w.text);
            x0 = std::min(x0, w.x);
            y0 = std::min(y0, w.y);
            x1 = std::max(x1, w.x + w.w);
            y1 = std::max(y1, w.y + w.h);
            if (joined.size() > target.size() + static_cast<size_t>(tol)) break;
            if (joined.size() + tol < target.size()) continue;
            const int d = joined == target ? 0 : tol > 0 ? EditDistance(joined, target, tol + 1) : tol + 1;
            if (d <= tol && (!found || d < *diff)) {
                *box = TextBox{L"", x0, y0, x1 - x0, y1 - y0};
                *diff = d;
                found = true;
            }
            if (d == 0) break;
        }
    }
    return found;
}

size_t CommonInOrder(const std::wstring& a, const std::wstring& b) {  // longest common subsequence
    std::vector<size_t> prev(b.size() + 1, 0), cur(b.size() + 1, 0);
    for (size_t i = 1; i <= a.size(); ++i) {
        for (size_t j = 1; j <= b.size(); ++j)
            cur[j] = a[i - 1] == b[j - 1] ? prev[j - 1] + 1 : std::max(prev[j], cur[j - 1]);
        std::swap(prev, cur);
    }
    return prev[b.size()];
}

// The row's letters around the link (the link itself always fits – what is beside it decides), without a timestamp
// ("[12:01]": digits only) – GW2 may show one.
std::wstring RestLetters(const TextRow& row, const TextBox& link) {
    std::wstring out;
    for (const TextBox& w : row.words) {
        if (w.x >= link.x && w.x + w.w <= link.x + link.w) continue;  // a word of the link
        const std::wstring l = Letters(w.text);
        const bool digitsOnly = !l.empty() && std::all_of(l.begin(), l.end(), [](wchar_t c) { return std::iswdigit(c) != 0; });
        if (!(digitsOnly && w.text.find(L':') != std::wstring::npos)) out += l;
    }
    return out;
}

}  // namespace

bool FindLinkText(const std::vector<TextRow>& rows, const std::wstring& label, TextBox* hit) {
    const std::wstring target = Letters(label);
    if (target.size() < 3) return false;
    bool found = false;
    TextBox best;
    for (const TextRow& row : rows) {
        TextBox box;
        int d = 0;
        if (MatchInRow(row, target, &box, &d) && (!found || box.y > best.y)) {  // the lowest: the newest line
            best = box;
            found = true;
        }
    }
    if (found && hit) {
        *hit = best;
        hit->text = label;
    }
    return found;
}

bool FindChatLink(const std::vector<TextRow>& rows, const ChatLineHint& hint, TextBox* hit, double* fit) {
    const std::wstring target = Letters(hint.label);
    std::wstring expected = Letters(hint.speaker + L" " + hint.text);
    if (target.size() < 3 || expected.empty()) return false;
    if (const size_t at = expected.find(target); at != std::wstring::npos) expected.erase(at, target.size());  // its rest
    bool found = false;
    double bestFit = 0;
    TextBox best;
    for (const TextRow& row : rows) {
        TextBox box;
        int d = 0;
        if (!MatchInRow(row, target, &box, &d)) continue;
        const std::wstring letters = RestLetters(row, box);
        // How much of the row beside the link belongs to our line, in order (a wrapped part of it counts fully). A
        // row with (almost) nothing beside the link proves little: just above the minimum, so a richer row wins.
        const double f = letters.size() < 4 ? kMinFit + 0.01
                                            : static_cast<double>(CommonInOrder(letters, expected)) / static_cast<double>(letters.size());
        const bool better = !found || f > bestFit + 0.05 || (f > bestFit - 0.05 && box.y > best.y);
        if (better) {
            best = box;
            bestFit = f;
            found = true;
        }
    }
    if (fit) *fit = found ? bestFit : 0;
    if (!found || bestFit < kMinFit) return false;
    if (hit) {
        *hit = best;
        hit->text = hint.label;
    }
    return true;
}

}  // namespace gct
