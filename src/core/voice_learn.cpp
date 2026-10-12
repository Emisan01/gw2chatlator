// voice_learn.cpp — learning misheard words from your corrections (see voice_learn.hpp).
#include "core/voice_learn.hpp"

#include <algorithm>
#include <cwctype>
#include <unordered_set>

#include "core/text.hpp"

namespace gct {
namespace {

struct Word {
    std::wstring text;  // as written
    std::wstring key;   // case-folded, without apostrophes
    size_t start = 0, end = 0;
};

bool IsApostrophe(wchar_t c) { return c == L'\'' || c == 0x2018 || c == 0x2019 || c == 0x02BC; }

std::wstring KeyOf(const std::wstring& word) {
    std::wstring k;
    for (wchar_t c : CaseFold(word))
        if (!IsApostrophe(c)) k += c;
    return k;
}

std::vector<Word> SplitWords(const std::wstring& s) {
    std::vector<Word> out;
    for (const Span& sp : WordSpans(s)) {
        Word w;
        w.text = s.substr(sp.start, sp.length);
        w.key = KeyOf(w.text);
        w.start = sp.start;
        w.end = sp.end();
        if (!w.key.empty()) out.push_back(std::move(w));
    }
    return out;
}

// The word keys joined by single spaces: "Löwen, Stein!" -> "löwen stein".
std::wstring PhraseKey(const std::wstring& phrase) {
    std::wstring k;
    for (const Word& w : SplitWords(phrase)) k += (k.empty() ? L"" : L" ") + w.key;
    return k;
}

// Only the folded word characters: "Löwen Stein" -> "löwenstein".
std::wstring Letters(const std::wstring& s) {
    std::wstring out;
    for (wchar_t c : CaseFold(s))
        if (IsWordChar(c) && !IsApostrophe(c)) out += c;
    return out;
}

size_t EditDistance(const std::wstring& a, const std::wstring& b) {
    std::vector<size_t> prev(b.size() + 1), cur(b.size() + 1);
    for (size_t j = 0; j <= b.size(); ++j) prev[j] = j;
    for (size_t i = 1; i <= a.size(); ++i) {
        cur[0] = i;
        for (size_t j = 1; j <= b.size(); ++j)
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)});
        std::swap(prev, cur);
    }
    return prev[b.size()];
}

std::wstring JoinWords(const std::vector<Word>& w, size_t from, size_t to) {
    std::wstring s;
    for (size_t i = from; i < to; ++i) s += (s.empty() ? L"" : L" ") + w[i].text;
    return s;
}

// Upper case of a lower-case letter (ASCII, Latin-1, Latin Extended-A like CaseFold; else the C library's).
wchar_t UpperChar(wchar_t c) {
    if (c >= L'a' && c <= L'z') return static_cast<wchar_t>(c - 32);
    if (c >= 0xE0 && c <= 0xFE && c != 0xF7) return static_cast<wchar_t>(c - 0x20);
    if (c > 0x100 && c <= 0x17F) {
        const wchar_t up = static_cast<wchar_t>(c - 1);
        return CaseFoldChar(up) == c ? up : c;
    }
    return static_cast<wchar_t>(std::towupper(c));
}

// The word at `pos` begins a sentence: nothing but spaces before it, or the end of a sentence.
bool AtSentenceStart(const std::wstring& text, size_t pos) {
    while (pos > 0 && std::iswspace(text[pos - 1])) --pos;
    if (pos == 0) return true;
    const wchar_t c = text[pos - 1];
    return c == L'.' || c == L'!' || c == L'?' || c == 0x2026 || c == 0x061F || c == 0x06D4 || c == 0x00A1 ||
           c == 0x00BF;
}

}  // namespace

bool VoiceFixes::SoundsAlike(const std::wstring& heard, const std::wstring& meant) {
    const std::wstring a = Letters(heard), b = Letters(meant);
    if (a.size() < 3 || b.size() < 3) return false;  // "in" -> "im": too common to learn
    if (a == b) return SplitWords(heard).size() != SplitWords(meant).size();  // only the spaces: "Löwen Stein"
    const size_t shorter = std::min(a.size(), b.size()), longer = std::max(a.size(), b.size());
    if (a.compare(0, shorter, b, 0, shorter) == 0) return false;   // another ending: "Raid" -> "Raids"
    if (longer - shorter > std::max<size_t>(2, longer / 3)) return false;  // "gehen" -> "gegangen"
    const double ratio = static_cast<double>(EditDistance(a, b)) / static_cast<double>(longer);
    if (ratio <= 0.6) return true;               // "Red" -> "Raid", "Fracktale" -> "Fraktale"
    return a[0] == b[0] && ratio <= 0.75;        // same first sound, a little further apart
}

void VoiceFixes::Add(const std::wstring& heard, const std::wstring& meant) {
    const std::wstring hk = PhraseKey(heard), mk = PhraseKey(meant);
    if (hk.empty() || mk.empty() || hk == mk) return;
    pairs_.erase(std::remove_if(pairs_.begin(), pairs_.end(),
                                [&](const Pair& p) {
                                    const std::wstring k = PhraseKey(p.first);
                                    return k == hk || (k == mk && PhraseKey(p.second) == hk);  // same, or the reverse
                                }),
                 pairs_.end());
    pairs_.emplace_back(Trim(heard), Trim(meant));
    if (pairs_.size() > kMaxPairs) pairs_.erase(pairs_.begin(), pairs_.end() - static_cast<ptrdiff_t>(kMaxPairs));
}

void VoiceFixes::Parse(const std::wstring& text) {
    pairs_.clear();
    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find(L'\n', pos);
        if (nl == std::wstring::npos) nl = text.size();
        const std::wstring line = Trim(text.substr(pos, nl - pos));
        pos = nl + 1;
        if (line.empty() || line[0] == L'#') continue;
        const size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        const std::wstring heard = Trim(line.substr(0, eq)), meant = Trim(line.substr(eq + 1));
        if (!heard.empty() && !meant.empty()) Add(heard, meant);
    }
}

std::wstring VoiceFixes::Serialize() const {
    std::wstring s;
    for (const Pair& p : pairs_) s += p.first + L" = " + p.second + L"\r\n";
    return s;
}

std::vector<VoiceFixes::Pair> VoiceFixes::Learn(const std::wstring& dictated, const std::wstring& sent) {
    std::vector<Pair> learned;
    const std::vector<Word> d = SplitWords(dictated), s = SplitWords(sent);
    const size_t n = d.size(), m = s.size();
    if (n == 0 || m == 0 || n > 300 || m > 300) return learned;

    // Longest common subsequence of the word keys, from the back: L[i][j] = LCS of d[i..] and s[j..].
    std::vector<unsigned short> L((n + 1) * (m + 1), 0);
    auto at = [&](size_t i, size_t j) -> unsigned short& { return L[i * (m + 1) + j]; };
    for (size_t i = n; i-- > 0;)
        for (size_t j = m; j-- > 0;)
            at(i, j) = d[i].key == s[j].key ? static_cast<unsigned short>(at(i + 1, j + 1) + 1)
                                            : std::max(at(i + 1, j), at(i, j + 1));
    const size_t changed = n - at(0, 0);  // dictated words that did not stay
    if (changed == 0) return learned;
    if (changed > 1 && changed * 5 > n * 2) return learned;  // more than 40 % changed: a rewrite, not a correction

    // The blocks between kept words: dictated [i0, i1) became sent [j0, j1).
    struct Block {
        size_t i0, i1, j0, j1;
    };
    std::vector<Block> blocks;
    size_t i = 0, j = 0, i0 = 0, j0 = 0;
    while (i < n || j < m) {
        if (i < n && j < m && d[i].key == s[j].key && at(i, j) == at(i + 1, j + 1) + 1) {
            if (i > i0 || j > j0) blocks.push_back({i0, i, j0, j});
            i0 = ++i;
            j0 = ++j;
        } else if (j >= m || (i < n && at(i + 1, j) >= at(i, j + 1))) {
            ++i;
        } else {
            ++j;
        }
    }
    if (i0 < n || j0 < m) blocks.push_back({i0, n, j0, m});

    for (const Block& b : blocks) {
        const size_t dn = b.i1 - b.i0, sn = b.j1 - b.j0;
        if (dn == 0 || sn == 0 || dn > 3 || sn > 3) continue;  // added / removed words, or rewritten
        if (dn == sn) {  // word for word: each pair on its own
            for (size_t k = 0; k < dn; ++k) {
                const std::wstring& heard = d[b.i0 + k].text;
                const std::wstring& meant = s[b.j0 + k].text;
                if (!SoundsAlike(heard, meant)) continue;
                Add(heard, meant);
                learned.emplace_back(heard, meant);
            }
        } else {  // split or joined: the phrase as a whole ("Löwen Stein" -> "Löwenstein")
            const std::wstring heard = JoinWords(d, b.i0, b.i1), meant = JoinWords(s, b.j0, b.j1);
            if (!SoundsAlike(heard, meant)) continue;
            Add(heard, meant);
            learned.emplace_back(heard, meant);
        }
    }
    return learned;
}

std::wstring VoiceFixes::Apply(const std::wstring& text) const {
    if (pairs_.empty() || text.empty()) return text;
    const std::vector<Word> w = SplitWords(text);
    struct Entry {
        std::vector<std::wstring> keys;
        const std::wstring* meant;
    };
    std::vector<Entry> entries;
    for (const Pair& p : pairs_) {
        Entry e;
        for (const Word& x : SplitWords(p.first)) e.keys.push_back(x.key);
        e.meant = &p.second;
        if (!e.keys.empty()) entries.push_back(std::move(e));
    }
    std::wstring out;
    size_t copied = 0, i = 0;
    while (i < w.size()) {
        const Entry* best = nullptr;
        for (const Entry& e : entries) {
            const size_t k = e.keys.size();
            if ((best && k <= best->keys.size()) || i + k > w.size()) continue;
            bool ok = true;
            for (size_t t = 0; t < k && ok; ++t) {
                ok = w[i + t].key == e.keys[t];
                if (ok && t > 0)  // the words of a phrase stand apart only by spaces
                    for (size_t c = w[i + t - 1].end; c < w[i + t].start && ok; ++c) ok = std::iswspace(text[c]) != 0;
            }
            if (ok) best = &e;
        }
        if (!best) {
            ++i;
            continue;
        }
        out += text.substr(copied, w[i].start - copied);
        std::wstring rep = *best->meant;
        if (!rep.empty() && AtSentenceStart(text, w[i].start)) rep[0] = UpperChar(rep[0]);
        out += rep;
        copied = w[i + best->keys.size() - 1].end;
        i += best->keys.size();
    }
    out += text.substr(copied);
    return out;
}

std::vector<std::wstring> VoiceFixes::Words() const {
    std::vector<std::wstring> out;
    std::unordered_set<std::wstring> seen;
    for (auto it = pairs_.rbegin(); it != pairs_.rend(); ++it)
        if (seen.insert(PhraseKey(it->second)).second) out.push_back(it->second);
    return out;
}

}  // namespace gct
