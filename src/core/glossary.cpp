// glossary.cpp
#include "glossary.hpp"

#include <algorithm>
#include <map>
#include <mutex>
#include <unordered_set>

namespace gct {

namespace {

struct BuiltinName {
    const char* key;
    const char* names[4];  // en, de, fr, es
};
const BuiltinName kBuiltinNames[] = {
#include "gw2_names.inc"
};

// 0 en, 1 de, 2 fr, 3 es; -1 for a language the GW2 API has no names in.
int NameLang(const std::wstring& lang) {
    const std::wstring l = ToLowerAscii(lang.substr(0, 2));
    return l == L"en" ? 0 : l == L"de" ? 1 : l == L"fr" ? 2 : l == L"es" ? 3 : -1;
}

const NameTable& NamesAt(int i) {
    static const std::vector<NameTable> tables = [] {
        std::vector<NameTable> t(4);
        for (const BuiltinName& n : kBuiltinNames)
            for (int l = 0; l < 4; ++l) t[l][n.key] = FromUtf8(n.names[l]);
        return t;
    }();
    return tables[i];
}

// The usual words (data/gw2-words.tsv): forms by "|", the first is written; empty = the English word.
const wchar_t* const kWordLangs[] = {L"en", L"de", L"fr", L"es", L"it", L"pl", L"pt", L"ru", L"tr"};
struct BuiltinWord {
    const wchar_t* key;
    const wchar_t* forms[std::size(kWordLangs)];
};
const BuiltinWord kBuiltinWords[] = {
#include "gw2_words.inc"
};

int WordLang(const std::wstring& lang) {
    const std::wstring l = ToLowerAscii(lang.substr(0, 2));
    for (size_t i = 0; i < std::size(kWordLangs); ++i)
        if (l == kWordLangs[i]) return static_cast<int>(i);
    return -1;
}

std::vector<std::wstring> Forms(const wchar_t* cell) {
    std::vector<std::wstring> out;
    std::wstring f;
    for (const wchar_t* c = cell;; ++c) {
        if (*c == L'|' || *c == 0) {
            if (!f.empty()) out.push_back(f);
            f.clear();
            if (*c == 0) break;
        } else {
            f += *c;
        }
    }
    return out;
}

// Every form written in the source language → the target's word, under one key per form.
void AddWords(int s, int t, NameTable& source, NameTable& target) {
    for (const BuiltinWord& w : kBuiltinWords) {
        const std::vector<std::wstring> to = Forms(w.forms[t][0] ? w.forms[t] : w.forms[0]);
        const std::vector<std::wstring> from = Forms(w.forms[s]);
        for (size_t i = 0; i < from.size() && !to.empty(); ++i) {
            const std::string key = "word:" + ToUtf8(w.key) + ":" + std::to_string(i);
            source[key] = from[i];
            target[key] = to[0];
        }
    }
}

}  // namespace

const NameTable& BuiltinNames(const std::wstring& lang) {
    static const NameTable empty;
    const int i = NameLang(lang);
    return i < 0 ? empty : NamesAt(i);
}

const Glossary* BuiltinGlossary(const std::wstring& source, const std::wstring& target) {
    // No names or words in the target: the English ones.
    const int ns = NameLang(source), nt = (std::max)(NameLang(target), 0);
    const int ws = WordLang(source), wt = (std::max)(WordLang(target), 0);
    const bool names = ns >= 0 && ns != nt, words = ws >= 0 && ws != wt;
    if (!names && !words) return nullptr;
    static std::mutex mu;
    static std::map<int, Glossary> built;  // one per pair, built on first use
    std::lock_guard<std::mutex> lock(mu);
    const int key = ws * 16 + wt;
    auto it = built.find(key);
    if (it == built.end()) {
        NameTable src, tgt;
        if (names) src = NamesAt(ns), tgt = NamesAt(nt);
        if (words) AddWords(ws, wt, src, tgt);
        it = built.emplace(key, Glossary::Build(src, tgt)).first;
    }
    return &it->second;
}

std::string SerializeNameTable(const NameTable& names) {
    std::vector<std::pair<std::string, std::wstring>> rows(names.begin(), names.end());
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::string out = "# GW2 names, key<TAB>name\n";
    for (const auto& [key, name] : rows) {
        std::wstring clean = name;
        for (auto& c : clean)
            if (c == L'\t' || c == L'\n' || c == L'\r') c = L' ';
        out += key;
        out += '\t';
        out += ToUtf8(clean);
        out += '\n';
    }
    return out;
}

NameTable ParseNameTable(const std::string& utf8) {
    NameTable t;
    size_t pos = 0;
    while (pos < utf8.size()) {
        size_t eol = utf8.find('\n', pos);
        if (eol == std::string::npos) eol = utf8.size();
        std::string line = utf8.substr(pos, eol - pos);
        pos = eol + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const size_t tab = line.find('\t');
        if (tab == std::string::npos || tab == 0) continue;
        t[line.substr(0, tab)] = FromUtf8(line.substr(tab + 1));
    }
    return t;
}

void CollectNameWords(const NameTable& names, WordSet& out) {
    for (const auto& kv : names) {
        const std::wstring f = CaseFold(kv.second);
        for (const Span& w : WordSpans(f))
            if (w.length >= 2) out.insert(f.substr(w.start, w.length));
    }
}

Glossary Glossary::Build(const NameTable& source, const NameTable& target) {
    Glossary g;
    std::unordered_map<std::wstring, size_t> index;  // folded source -> term
    std::unordered_set<std::wstring> ambiguous;

    // Deterministic order regardless of hash-map iteration.
    std::vector<std::string> keys;
    keys.reserve(source.size());
    for (const auto& kv : source) keys.push_back(kv.first);
    std::sort(keys.begin(), keys.end());

    for (const auto& key : keys) {
        const auto tgt = target.find(key);
        if (tgt == target.end()) continue;
        const std::wstring s = SanitizeChatText(source.at(key));
        const std::wstring t = SanitizeChatText(tgt->second);
        if (CodePointCount(s) < 4 || t.empty()) continue;
        if (!IsWordChar(s.front()) || !IsWordChar(s.back())) continue;  // "(placeholder)" etc.

        const std::wstring f = CaseFold(s);
        if (ambiguous.count(f)) continue;
        const auto ex = index.find(f);
        if (ex != index.end()) {
            if (CaseFold(g.terms_[ex->second].target) != CaseFold(t)) ambiguous.insert(f);
            continue;
        }
        index.emplace(f, g.terms_.size());
        g.terms_.push_back({f, s, t});
    }

    g.terms_.erase(std::remove_if(g.terms_.begin(), g.terms_.end(),
                                  [&](const Term& t) { return ambiguous.count(t.folded) > 0; }),
                   g.terms_.end());
    std::sort(g.terms_.begin(), g.terms_.end(), [](const Term& a, const Term& b) {
        return a.folded.size() != b.folded.size() ? a.folded.size() > b.folded.size() : a.folded < b.folded;
    });
    for (size_t i = 0; i < g.terms_.size(); ++i) g.byFirst_[g.terms_[i].folded.front()].push_back(i);
    return g;
}

std::vector<GlossaryMatch> Glossary::FindAll(const std::wstring& text, const std::vector<Span>& blocked) const {
    std::vector<GlossaryMatch> out;
    if (terms_.empty()) return out;
    const std::wstring folded = CaseFold(text);  // same length as text

    size_t i = 0;
    while (i < folded.size()) {
        // A term can only start at a word boundary.
        if (i > 0 && IsWordChar(folded[i - 1]) && IsWordChar(folded[i])) {
            ++i;
            continue;
        }
        const auto bucket = byFirst_.find(folded[i]);
        bool matched = false;
        if (bucket != byFirst_.end()) {
            for (size_t idx : bucket->second) {  // longest first
                const Term& t = terms_[idx];
                const size_t n = t.folded.size();
                if (i + n > folded.size() || folded.compare(i, n, t.folded) != 0) continue;
                if (i + n < folded.size() && IsWordChar(folded[i + n])) continue;  // must end at a boundary
                const Span sp{i, n};
                if (std::any_of(blocked.begin(), blocked.end(), [&](const Span& b) { return b.Overlaps(sp); }))
                    continue;
                out.push_back({sp, t.source, t.target});
                i += n;
                matched = true;
                break;
            }
        }
        if (!matched) ++i;
    }
    return out;
}

void Glossary::CollectSourceWords(WordSet& out) const {
    for (const Term& t : terms_)
        for (const Span& w : WordSpans(t.folded)) out.insert(t.folded.substr(w.start, w.length));
}

}  // namespace gct
