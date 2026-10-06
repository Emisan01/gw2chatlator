#include "core/word_model.hpp"

#include <algorithm>
#include <cwctype>

#include "core/text.hpp"

namespace gct {
namespace {

bool IsDigitChar(wchar_t c) { return c >= L'0' && c <= L'9'; }

bool IsUpper(wchar_t c) { return CaseFoldChar(c) != c; }

bool AllUpper(const std::wstring& w) {
    size_t letters = 0, upper = 0;
    for (wchar_t c : w) {
        if (CaseFoldChar(c) != c || static_cast<wchar_t>(std::towupper(c)) != c) {
            ++letters;
            if (IsUpper(c)) ++upper;
        }
    }
    return letters >= 2 && upper == letters;
}

size_t Letters(const std::wstring& w) {
    size_t n = 0;
    for (wchar_t c : w)
        if (IsWordChar(c) && !IsDigitChar(c) && c != L'\'') ++n;
    return n;
}

bool Learnable(const std::wstring& w) {
    if (w.size() < 2 || w.size() > 40) return false;
    if (std::any_of(w.begin(), w.end(), IsDigitChar)) return false;
    return Letters(w) >= 2;
}

// Words of a text for learning; a chat command prefix and chat codes are skipped.
std::vector<std::wstring> Tokens(const std::wstring& text) {
    std::wstring t = text;
    const ChatSplit split = SplitChatCommand(SanitizeChatText(t));
    t = split.prefix.empty() ? SanitizeChatText(t) : split.body;
    for (const Span& s : FindChatCodes(t))
        for (size_t i = s.start; i < s.end() && i < t.size(); ++i) t[i] = L' ';
    std::vector<std::wstring> out;
    for (const Span& s : WordSpans(t)) {
        std::wstring w = t.substr(s.start, s.length);
        // Links: "http", "www" and the rest of the URL are not words worth learning.
        if (w == L"http" || w == L"https" || w == L"www") return out;
        out.push_back(std::move(w));
    }
    return out;
}

// True if `b` begins with the first two letters of `a` swapped ("hlalo"/"hallo").
bool FirstTwoSwapped(const std::wstring& a, const std::wstring& b) {
    return a.size() >= 2 && b.size() >= 2 && a[0] == b[1] && a[1] == b[0];
}

int AllowedDistance(size_t letters) { return letters <= 6 ? 1 : 2; }

}  // namespace

int EditDistance(const std::wstring& a0, const std::wstring& b0, int limit) {
    const std::wstring a = CaseFold(a0), b = CaseFold(b0);
    const int n = static_cast<int>(a.size()), m = static_cast<int>(b.size());
    if (std::abs(n - m) > limit) return limit + 1;
    std::vector<int> prev2(m + 1), prev(m + 1), cur(m + 1);
    for (int j = 0; j <= m; ++j) prev[j] = j;
    for (int i = 1; i <= n; ++i) {
        cur[0] = i;
        int rowMin = cur[0];
        for (int j = 1; j <= m; ++j) {
            const int cost = a[i - 1] == b[j - 1] ? 0 : 1;
            int v = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost});
            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1]) v = std::min(v, prev2[j - 2] + 1);
            cur[j] = v;
            rowMin = std::min(rowMin, v);
        }
        if (rowMin > limit) return limit + 1;
        prev2.swap(prev);
        prev.swap(cur);
    }
    return std::min(prev[m], limit + 1);
}

std::wstring MatchCase(const std::wstring& typed, const std::wstring& candidate) {
    if (typed.empty() || candidate.empty()) return candidate;
    if (AllUpper(typed) && candidate.size() > 1) {
        std::wstring out = candidate;
        for (auto& c : out) c = static_cast<wchar_t>(std::towupper(c));
        return out;
    }
    std::wstring out = candidate;
    if (IsUpper(typed[0])) out[0] = static_cast<wchar_t>(std::towupper(out[0]));
    return out;
}

// ---------------------------------------------------------------------------
void WordModel::AddWord(const std::wstring& word, double weight) {
    if (!Learnable(word)) return;
    const std::wstring key = CaseFold(word);
    Word& w = words_[key];
    w.form = word;  // Learn() passes the mid-sentence form for a message's first word
    w.count += weight;
    dirty_ = true;
    if (words_.size() > kMaxWords) Prune();
}

void WordModel::AddPair(const std::wstring& prevKey, const std::wstring& nextKey, double weight) {
    auto& next = pairs_[prevKey];
    auto [it, inserted] = next.emplace(nextKey, 0.0);
    it->second += weight;
    if (inserted) ++pairTotal_;
    dirty_ = true;
    if (pairTotal_ > kMaxPairs) Prune();
}

void WordModel::Learn(const std::wstring& text, double weight) {
    const std::vector<std::wstring> tokens = Tokens(text);
    std::wstring prevKey;
    for (size_t i = 0; i < tokens.size(); ++i) {
        const std::wstring& w = tokens[i];
        if (!Learnable(w)) {
            prevKey.clear();
            continue;
        }
        // First word of a message: do not learn its sentence capitalisation as the form.
        std::wstring form = w;
        if (i == 0 && w.size() > 1 && IsUpper(w[0]) && !IsUpper(w[1])) {
            const std::wstring key = CaseFold(w);
            auto it = words_.find(key);
            form = it != words_.end() ? it->second.form : std::wstring(1, CaseFoldChar(w[0])) + w.substr(1);
        }
        AddWord(form, weight);
        const std::wstring key = CaseFold(w);
        if (!prevKey.empty()) AddPair(prevKey, key, weight);
        prevKey = key;
    }
}

double WordModel::Count(const std::wstring& word) const {
    auto it = words_.find(CaseFold(word));
    return it == words_.end() ? 0.0 : it->second.count;
}

std::vector<std::wstring> WordModel::Complete(const std::wstring& prefix, const std::wstring& prev, size_t n) const {
    std::vector<std::wstring> out;
    if (prefix.empty() || n == 0) return out;
    const std::wstring p = CaseFold(prefix);
    const auto pit = prev.empty() ? pairs_.end() : pairs_.find(CaseFold(prev));
    std::vector<std::pair<double, const Word*>> scored;
    for (const auto& [key, w] : words_) {
        if (key.size() <= p.size() || key.compare(0, p.size(), p) != 0) continue;
        double score = w.count;
        if (pit != pairs_.end()) {
            auto nit = pit->second.find(key);
            if (nit != pit->second.end()) score += nit->second * 6.0;
        }
        scored.push_back({score, &w});
    }
    std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) {
        if (a.first != b.first) return a.first > b.first;
        return a.second->form < b.second->form;
    });
    for (size_t i = 0; i < scored.size() && out.size() < n; ++i) {
        // Keep the learned form ("Lion's Arch"), but follow an upper-case first letter you typed.
        std::wstring form = scored[i].second->form;
        if (IsUpper(prefix[0])) form = MatchCase(prefix, form);
        out.push_back(form);
    }
    return out;
}

std::vector<std::wstring> WordModel::Next(const std::wstring& prev, size_t n) const {
    std::vector<std::wstring> out;
    auto pit = pairs_.find(CaseFold(prev));
    if (pit == pairs_.end() || n == 0) return out;
    std::vector<std::pair<double, std::wstring>> scored;
    for (const auto& [key, count] : pit->second) {
        if (count < 1.0) continue;
        auto wit = words_.find(key);
        scored.push_back({count, wit != words_.end() ? wit->second.form : key});
    }
    std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) {
        if (a.first != b.first) return a.first > b.first;
        return a.second < b.second;
    });
    for (size_t i = 0; i < scored.size() && out.size() < n; ++i) out.push_back(scored[i].second);
    return out;
}

std::vector<std::wstring> WordModel::Near(const std::wstring& word, size_t n) const {
    std::vector<std::wstring> out;
    const std::wstring key = CaseFold(word);
    if (key.size() < 2 || n == 0) return out;
    const int allowed = AllowedDistance(Letters(word));
    struct Hit {
        int dist;
        double count;
        const std::wstring* form;
    };
    std::vector<Hit> hits;
    for (const auto& [k, w] : words_) {
        if (k == key || w.count < 2.0) continue;
        if (k[0] != key[0] && !FirstTwoSwapped(k, key)) continue;
        const int d = EditDistance(k, key, allowed);
        if (d > allowed) continue;
        hits.push_back({d, w.count, &w.form});
    }
    std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
        if (a.dist != b.dist) return a.dist < b.dist;
        if (a.count != b.count) return a.count > b.count;
        return *a.form < *b.form;
    });
    for (size_t i = 0; i < hits.size() && out.size() < n; ++i) out.push_back(MatchCase(word, *hits[i].form));
    return out;
}

void WordModel::Prune() {
    // Drop the rarest words (and pairs) down to 90 % of the limits.
    if (words_.size() > kMaxWords) {
        std::vector<std::pair<double, std::wstring>> byCount;
        byCount.reserve(words_.size());
        for (const auto& [k, w] : words_) byCount.push_back({w.count, k});
        std::sort(byCount.begin(), byCount.end());
        const size_t drop = words_.size() - kMaxWords * 9 / 10;
        for (size_t i = 0; i < drop && i < byCount.size(); ++i) {
            words_.erase(byCount[i].second);
            auto pit = pairs_.find(byCount[i].second);
            if (pit != pairs_.end()) {
                pairTotal_ -= pit->second.size();
                pairs_.erase(pit);
            }
        }
    }
    if (pairTotal_ > kMaxPairs) {
        std::vector<double> counts;
        counts.reserve(pairTotal_);
        for (const auto& [p, next] : pairs_)
            for (const auto& [k, c] : next) counts.push_back(c);
        std::sort(counts.begin(), counts.end());
        const size_t keep = kMaxPairs * 9 / 10;
        const double cut = counts[counts.size() - keep];
        pairTotal_ = 0;
        for (auto pit = pairs_.begin(); pit != pairs_.end();) {
            for (auto it = pit->second.begin(); it != pit->second.end();) {
                if (it->second < cut) it = pit->second.erase(it);
                else ++it;
            }
            pairTotal_ += pit->second.size();
            if (pit->second.empty()) pit = pairs_.erase(pit);
            else ++pit;
        }
    }
    dirty_ = true;
}

std::string WordModel::Serialize() const {
    std::string out = "# GW2 Chat Translator - learned words (w=word, n=word pair). Safe to delete.\n";
    auto num = [](double v) {
        const long long whole = static_cast<long long>(v * 100 + 0.5);
        return std::to_string(whole / 100) + "." + (whole % 100 < 10 ? "0" : "") + std::to_string(whole % 100);
    };
    std::vector<const std::pair<const std::wstring, Word>*> sorted;
    for (const auto& kv : words_) sorted.push_back(&kv);
    std::sort(sorted.begin(), sorted.end(), [](const auto* a, const auto* b) { return a->first < b->first; });
    for (const auto* kv : sorted) out += "w\t" + ToUtf8(kv->second.form) + "\t" + num(kv->second.count) + "\n";
    std::vector<std::pair<std::wstring, std::wstring>> keys;
    for (const auto& [p, next] : pairs_)
        for (const auto& [k, c] : next) keys.push_back({p, k});
    std::sort(keys.begin(), keys.end());
    for (const auto& [p, k] : keys)
        out += "n\t" + ToUtf8(p) + "\t" + ToUtf8(k) + "\t" + num(pairs_.at(p).at(k)) + "\n";
    return out;
}

void WordModel::Parse(const std::string& utf8) {
    words_.clear();
    pairs_.clear();
    pairTotal_ = 0;
    size_t pos = 0;
    while (pos < utf8.size()) {
        size_t eol = utf8.find('\n', pos);
        if (eol == std::string::npos) eol = utf8.size();
        std::string line = utf8.substr(pos, eol - pos);
        pos = eol + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() < 3 || line[0] == '#') continue;
        std::vector<std::string> f;
        size_t s = 0;
        for (;;) {
            size_t tab = line.find('\t', s);
            f.push_back(line.substr(s, tab == std::string::npos ? std::string::npos : tab - s));
            if (tab == std::string::npos) break;
            s = tab + 1;
        }
        auto count = [](const std::string& v) {
            try {
                return std::max(0.0, std::stod(v));
            } catch (...) {
                return 0.0;
            }
        };
        if (f[0] == "w" && f.size() == 3) {
            const std::wstring form = FromUtf8(f[1]);
            if (!Learnable(form)) continue;
            Word& w = words_[CaseFold(form)];
            w.form = form;
            w.count += count(f[2]);
        } else if (f[0] == "n" && f.size() == 4) {
            const double c = count(f[3]);
            if (c <= 0) continue;
            auto [it, inserted] = pairs_[CaseFold(FromUtf8(f[1]))].emplace(CaseFold(FromUtf8(f[2])), 0.0);
            it->second += c;
            if (inserted) ++pairTotal_;
        }
    }
    if (words_.size() > kMaxWords || pairTotal_ > kMaxPairs) Prune();
    dirty_ = false;
}

// ---------------------------------------------------------------------------
std::wstring ChooseCorrection(const std::wstring& word, const std::vector<std::wstring>& spell, const WordModel& model) {
    const size_t letters = Letters(word);
    if (letters < 4 || word.size() > 40) return {};
    if (AllUpper(word) || model.Knows(word)) return {};
    if (std::any_of(word.begin(), word.end(), IsDigitChar)) return {};
    const int allowed = AllowedDistance(letters);
    const std::wstring key = CaseFold(word);

    struct Cand {
        std::wstring text;
        int dist;
        int rank;      // position in the dictionary's list (lower = better), 99 = only learned
        double count;  // how often you used it
    };
    std::vector<Cand> cands;
    auto consider = [&](const std::wstring& c, int rank) {
        if (c.empty() || c.find(L' ') != std::wstring::npos || c.find(L'-') != std::wstring::npos) return;
        const std::wstring ck = CaseFold(c);
        if (ck == key) return;
        if (ck[0] != key[0] && !FirstTwoSwapped(ck, key)) return;
        const int d = EditDistance(ck, key, allowed);
        if (d > allowed) return;
        for (Cand& x : cands)
            if (CaseFold(x.text) == ck) {
                x.rank = std::min(x.rank, rank);
                return;
            }
        cands.push_back({c, d, rank, model.Count(c)});
    };
    for (size_t i = 0; i < spell.size() && i < 8; ++i) consider(spell[i], static_cast<int>(i));
    for (const std::wstring& c : model.Near(word, 5)) consider(c, 99);
    if (cands.empty()) return {};
    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
        if (a.dist != b.dist) return a.dist < b.dist;
        // A word you use a lot beats the dictionary's order.
        const bool aUsed = a.count >= 3, bUsed = b.count >= 3;
        if (aUsed != bUsed) return aUsed;
        if (a.rank != b.rank) return a.rank < b.rank;
        return a.count > b.count;
    });
    return MatchCase(word, cands[0].text);
}

}  // namespace gct
