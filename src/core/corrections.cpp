// corrections.cpp
#include "corrections.hpp"

#include <algorithm>

#include "text.hpp"

namespace gct {

namespace {

std::wstring LangKey(const std::wstring& lang) {
    std::wstring l = ToUpperAscii(Trim(lang));
    const size_t dash = l.find(L'-');
    return dash == std::wstring::npos ? l : l.substr(0, dash);
}

// Case folded, runs of spaces collapsed, trimmed.
std::wstring Folded(const std::wstring& s) {
    std::wstring out;
    bool space = false;
    for (wchar_t c : CaseFold(Trim(s))) {
        if (c == L' ' || c == L'\t' || c == L'\n' || c == L'\r') {
            space = true;
            continue;
        }
        if (space && !out.empty()) out += L' ';
        space = false;
        out += c;
    }
    return out;
}

std::vector<std::wstring> Words(const std::wstring& s) {
    std::vector<std::wstring> w;
    std::wstring cur;
    for (wchar_t c : s) {
        if (c == L' ' || c == L'\t' || c == L'\n' || c == L'\r') {
            if (!cur.empty()) w.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) w.push_back(cur);
    return w;
}

std::wstring Join(const std::vector<std::wstring>& w, size_t a, size_t b) {
    std::wstring s;
    for (size_t i = a; i < b; ++i) s += (s.empty() ? L"" : L" ") + w[i];
    return s;
}

std::wstring Escape(const std::wstring& s) {
    std::wstring o;
    for (wchar_t c : s) {
        if (c == L'\\') o += L"\\\\";
        else if (c == L'\t') o += L"\\t";
        else if (c == L'\n') o += L"\\n";
        else if (c == L'\r') continue;
        else o += c;
    }
    return o;
}

std::wstring Unescape(const std::wstring& s) {
    std::wstring o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'\\' && i + 1 < s.size()) {
            const wchar_t n = s[++i];
            o += n == L't' ? L'\t' : n == L'n' ? L'\n' : n;
        } else {
            o += s[i];
        }
    }
    return o;
}

size_t Letters(const std::wstring& s) {
    size_t n = 0;
    for (wchar_t c : s)
        if (IsWordChar(c)) ++n;
    return n;
}

}  // namespace

std::wstring CorrectionMemory::Key(const std::wstring& source, const std::wstring& lang) {
    return LangKey(lang) + L'\x1f' + Folded(source);
}

bool CorrectionMemory::Lookup(const std::wstring& source, const std::wstring& lang, std::wstring* out) const {
    const auto it = lines_.find(Key(source, lang));
    if (it == lines_.end()) return false;
    *out = it->second;
    return true;
}

std::wstring CorrectionMemory::Apply(const std::wstring& translation, const std::wstring& lang) const {
    const std::wstring l = LangKey(lang);
    std::wstring text = translation;
    for (const Phrase& p : phrases_) {
        if (p.lang != l || p.from.empty()) continue;
        const std::wstring needle = CaseFold(p.from);
        std::wstring folded = CaseFold(text);
        size_t pos = 0;
        while ((pos = folded.find(needle, pos)) != std::wstring::npos) {
            const size_t end = pos + needle.size();
            const bool startOk = pos == 0 || !IsWordChar(text[pos - 1]);
            const bool endOk = end >= text.size() || !IsWordChar(text[end]);
            if (startOk && endOk) {
                text.replace(pos, needle.size(), p.to);
                folded = CaseFold(text);
                pos += p.to.size();
            } else {
                pos = end;
            }
        }
    }
    return text;
}

bool CorrectionMemory::ChangedPhrase(const std::wstring& before, const std::wstring& after, std::wstring* from,
                                     std::wstring* to) {
    const std::vector<std::wstring> a = Words(before), b = Words(after);
    size_t head = 0;
    while (head < a.size() && head < b.size() && a[head] == b[head]) ++head;
    size_t tail = 0;
    while (tail < a.size() - head && tail < b.size() - head && a[a.size() - 1 - tail] == b[b.size() - 1 - tail]) ++tail;
    const size_t aEnd = a.size() - tail, bEnd = b.size() - tail;
    if (aEnd <= head || bEnd <= head) return false;           // pure insertion or deletion: too vague
    if (aEnd - head > 4 || bEnd - head > 6) return false;    // most of the sentence changed: keep the line only
    if (aEnd - head == a.size()) return false;                // nothing stayed the same
    *from = Join(a, head, aEnd);
    *to = Join(b, head, bEnd);
    // Punctuation glued to the words at the edges stays where it is.
    while (!from->empty() && !to->empty() && !IsWordChar(from->back()) && from->back() == to->back()) {
        from->pop_back();
        to->pop_back();
    }
    return Letters(*from) >= 3 && Folded(*from) != Folded(*to);
}

void CorrectionMemory::AddPhrase(const std::wstring& lang, const std::wstring& from, const std::wstring& to) {
    phrases_.erase(std::remove_if(phrases_.begin(), phrases_.end(),
                                  [&](const Phrase& p) { return p.lang == lang && Folded(p.from) == Folded(from); }),
                   phrases_.end());
    // A phrase that undoes an older one replaces it, it does not chain.
    phrases_.erase(std::remove_if(phrases_.begin(), phrases_.end(),
                                  [&](const Phrase& p) { return p.lang == lang && Folded(p.to) == Folded(from); }),
                   phrases_.end());
    phrases_.push_back({lang, from, to});
    if (phrases_.size() > kMaxPhrases) phrases_.erase(phrases_.begin());
}

void CorrectionMemory::Add(const std::wstring& source, const std::wstring& lang, const std::wstring& wrong,
                           const std::wstring& right) {
    const std::wstring r = Trim(right);
    if (Trim(source).empty() || r.empty()) return;
    const std::wstring key = Key(source, lang);
    if (lines_.find(key) == lines_.end()) lineOrder_.push_back(key);
    lines_[key] = r;
    while (lines_.size() > kMaxLines && !lineOrder_.empty()) {
        lines_.erase(lineOrder_.front());
        lineOrder_.erase(lineOrder_.begin());
    }
    std::wstring from, to;
    if (ChangedPhrase(Trim(wrong), r, &from, &to)) AddPhrase(LangKey(lang), from, to);
}

void CorrectionMemory::Clear() {
    lines_.clear();
    lineOrder_.clear();
    phrases_.clear();
}

std::string CorrectionMemory::Serialize() const {
    std::wstring s = L"# GW2 Chat Translator: your corrected translations (L = whole line, P = phrase)\n";
    for (const std::wstring& key : lineOrder_) {
        const auto it = lines_.find(key);
        if (it == lines_.end()) continue;
        const size_t sep = key.find(L'\x1f');
        s += L"L\t" + key.substr(0, sep) + L"\t" + Escape(key.substr(sep + 1)) + L"\t" + Escape(it->second) + L"\n";
    }
    for (const Phrase& p : phrases_) s += L"P\t" + p.lang + L"\t" + Escape(p.from) + L"\t" + Escape(p.to) + L"\n";
    return ToUtf8(s);
}

void CorrectionMemory::Parse(const std::string& utf8) {
    Clear();
    const std::wstring all = FromUtf8(utf8);
    size_t start = 0;
    while (start < all.size()) {
        size_t end = all.find(L'\n', start);
        if (end == std::wstring::npos) end = all.size();
        std::wstring line = all.substr(start, end - start);
        start = end + 1;
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        if (line.size() < 3 || line[0] == L'#') continue;
        std::vector<std::wstring> f;
        size_t a = 0;
        for (int i = 0; i < 3; ++i) {
            const size_t t = line.find(L'\t', a);
            if (t == std::wstring::npos) break;
            f.push_back(line.substr(a, t - a));
            a = t + 1;
        }
        if (f.size() != 3) continue;
        f.push_back(line.substr(a));
        const std::wstring lang = LangKey(f[1]), x = Unescape(f[2]), y = Unescape(f[3]);
        if (lang.empty() || x.empty() || y.empty()) continue;
        if (f[0] == L"L") {
            const std::wstring key = Key(x, lang);
            if (lines_.find(key) == lines_.end()) lineOrder_.push_back(key);
            lines_[key] = y;
        } else if (f[0] == L"P") {
            AddPhrase(lang, x, y);
        }
    }
}

size_t CorrectionMemory::Merge(const std::string& utf8) {
    CorrectionMemory other;
    other.Parse(utf8);
    size_t added = 0;
    for (const std::wstring& key : other.lineOrder_) {
        if (lines_.count(key)) continue;
        lines_[key] = other.lines_[key];
        lineOrder_.push_back(key);
        ++added;
    }
    for (const Phrase& p : other.phrases_) {
        bool have = false;
        for (const Phrase& q : phrases_) have = have || (q.lang == p.lang && Folded(q.from) == Folded(p.from));
        if (!have) {
            phrases_.push_back(p);
            ++added;
        }
    }
    while (lines_.size() > kMaxLines && !lineOrder_.empty()) {
        lines_.erase(lineOrder_.front());
        lineOrder_.erase(lineOrder_.begin());
    }
    while (phrases_.size() > kMaxPhrases) phrases_.erase(phrases_.begin());
    return added;
}

}  // namespace gct
