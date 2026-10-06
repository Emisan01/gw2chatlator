#include "core/word_model.hpp"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <memory>

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

// True if `typed` is the start of `key` with exactly one typo: a wrong, an
// extra, a missing or two swapped letters. The first letter must be right,
// unless it was swapped with the second. No allocations: runs for every
// learned word on every key press.
bool OneTypoPrefix(const std::wstring& key, const std::wstring& typed) {
    const size_t m = typed.size(), n = key.size();
    size_t i = 0;
    while (i < m && i < n && key[i] == typed[i]) ++i;
    if (i == m) return false;  // no typo: an exact prefix
    auto same = [&](size_t keyAt, size_t typedAt) {  // rest of `typed` from typedAt matches key from keyAt
        const size_t len = m - typedAt;
        return keyAt + len <= n && key.compare(keyAt, len, typed, typedAt, len) == 0;
    };
    if (i + 1 < m && i + 1 < n && key[i] == typed[i + 1] && key[i + 1] == typed[i] && same(i + 2, i + 2))
        return true;  // swapped
    if (i == 0) return false;
    return (i < n && same(i + 1, i + 1))  // wrong letter
           || same(i, i + 1)              // extra letter typed
           || (i < n && same(i + 1, i));  // letter missed
}

}  // namespace

// ---------------------------------------------------------------------------
void KeyLayout::Set(wchar_t c, int row, float x) {
    pos_[c] = {row, x};
    at_[row * 1000 + static_cast<int>(std::lround(x * 4))] = c;
}

bool KeyLayout::Neighbors(wchar_t a, wchar_t b) const {
    if (a == b) return false;
    const auto ia = pos_.find(a), ib = pos_.find(b);
    if (ia == pos_.end() || ib == pos_.end()) return false;
    const int dy = std::abs(ia->second.row - ib->second.row);
    const float dx = std::fabs(ia->second.x - ib->second.x);
    return dy == 0 ? dx <= 1.01f : dy == 1 && dx <= 1.01f;
}

wchar_t KeyLayout::Shifted(wchar_t c, int dx) const {
    const auto it = pos_.find(c);
    if (it == pos_.end()) return 0;
    const auto at = at_.find(it->second.row * 1000 + static_cast<int>(std::lround((it->second.x + dx) * 4)));
    return at == at_.end() ? 0 : at->second;
}

KeyNeighbors KeyLayout::AsNeighbors() const {
    if (Empty()) return nullptr;
    auto copy = std::make_shared<KeyLayout>(*this);
    return [copy](wchar_t a, wchar_t b) { return copy->Neighbors(a, b); };
}

std::vector<std::wstring> HandShiftVariants(const std::wstring& word, const KeyLayout& layout) {
    std::vector<std::wstring> out;
    const std::wstring key = WordKey(word);
    if (key.size() < 3 || layout.Empty()) return out;
    for (int dx : {-1, 1}) {
        std::wstring v;
        for (wchar_t c : key) {
            const wchar_t s = layout.Shifted(c, dx);
            if (!s) break;
            v += s;
        }
        if (v.size() == key.size()) out.push_back(v);
    }
    return out;
}

double SlipDistance(const std::wstring& a0, const std::wstring& b0, const KeyLayout& layout, double limit) {
    const std::wstring a = WordKey(a0), b = WordKey(b0);
    const size_t n = a.size(), m = b.size();
    if ((n > m ? n - m : m - n) > limit) return limit + 1;
    std::vector<double> prev2(m + 1), prev(m + 1), cur(m + 1);
    for (size_t j = 0; j <= m; ++j) prev[j] = static_cast<double>(j);
    for (size_t i = 1; i <= n; ++i) {
        cur[0] = static_cast<double>(i);
        double rowMin = cur[0];
        for (size_t j = 1; j <= m; ++j) {
            const double sub = a[i - 1] == b[j - 1] ? 0.0 : layout.Neighbors(a[i - 1], b[j - 1]) ? 0.5 : 1.0;
            double v = std::min({prev[j] + 1.0, cur[j - 1] + 1.0, prev[j - 1] + sub});
            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1]) v = std::min(v, prev2[j - 2] + 0.7);
            cur[j] = v;
            rowMin = std::min(rowMin, v);
        }
        if (rowMin > limit) return limit + 1;
        prev2.swap(prev);
        prev.swap(cur);
    }
    return std::min(prev[m], limit + 1);
}

std::wstring WordKey(const std::wstring& word) {
    std::wstring out;
    out.reserve(word.size());
    for (wchar_t c : word) {
        const uint32_t u = static_cast<uint32_t>(c);
        if (u >= 0x0600 && u <= 0x06FF) {
            if (u == 0x0640 || (u >= 0x064B && u <= 0x065F) || u == 0x0670 || (u >= 0x06D6 && u <= 0x06ED))
                continue;  // tatweel, harakat, Quranic marks
            switch (u) {
                case 0x0622: case 0x0623: case 0x0625: case 0x0671: c = 0x0627; break;  // alef forms
                case 0x0649: case 0x0626: case 0x06CC: c = 0x064A; break;              // alef maqsura, yeh forms
                case 0x0629: c = 0x0647; break;                                        // teh marbuta
                case 0x0624: c = 0x0648; break;                                        // waw with hamza
                case 0x06A9: c = 0x0643; break;                                        // Persian keheh
                default: break;
            }
            out += c;
            continue;
        }
        out += CaseFoldChar(c);
    }
    return out;
}

int EditDistance(const std::wstring& a0, const std::wstring& b0, int limit) {
    const std::wstring a = WordKey(a0), b = WordKey(b0);
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
    const std::wstring key = WordKey(word);
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
            const std::wstring key = WordKey(w);
            auto it = words_.find(key);
            form = it != words_.end() ? it->second.form : std::wstring(1, CaseFoldChar(w[0])) + w.substr(1);
        }
        AddWord(form, weight);
        const std::wstring key = WordKey(w);
        if (!prevKey.empty()) AddPair(prevKey, key, weight);
        prevKey = key;
    }
}

double WordModel::Count(const std::wstring& word) const {
    auto it = words_.find(WordKey(word));
    return it == words_.end() ? 0.0 : it->second.count;
}

std::vector<std::wstring> WordModel::Complete(const std::wstring& prefix, const std::wstring& prev, size_t n) const {
    std::vector<std::wstring> out;
    if (prefix.empty() || n == 0) return out;
    const std::wstring p = WordKey(prefix);
    const auto pit = prev.empty() ? pairs_.end() : pairs_.find(WordKey(prev));
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
    const auto top = scored.begin() + static_cast<std::ptrdiff_t>(std::min(n, scored.size()));
    std::partial_sort(scored.begin(), top, scored.end(), [](const auto& a, const auto& b) {
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

std::vector<std::wstring> WordModel::CompleteFuzzy(const std::wstring& prefix, const std::wstring& prev, size_t n,
                                                   const KeyNeighbors& neighbors) const {
    std::vector<std::wstring> out;
    const std::wstring p = WordKey(prefix);
    if (p.size() < 3 || n == 0 || std::any_of(p.begin(), p.end(), IsDigitChar)) return out;
    const auto pit = prev.empty() ? pairs_.end() : pairs_.find(WordKey(prev));
    const size_t m = p.size();
    std::vector<std::pair<double, const Word*>> scored;
    for (const auto& [key, w] : words_) {
        if (w.count < 2.0 || key.size() < m || key == p) continue;
        if (!OneTypoPrefix(key, p)) continue;
        double score = w.count;
        if (pit != pairs_.end()) {
            auto nit = pit->second.find(key);
            if (nit != pit->second.end()) score += nit->second * 6.0;
        }
        if (neighbors) {  // a slip to the key next door is the most likely typo
            size_t diff = 0, at = 0;
            for (size_t i = 0; i < m; ++i)
                if (key[i] != p[i]) {
                    ++diff;
                    at = i;
                }
            if (diff == 1 && neighbors(key[at], p[at])) score *= 2.0;
        }
        scored.push_back({score, &w});
    }
    const auto top = scored.begin() + static_cast<std::ptrdiff_t>(std::min(n, scored.size()));
    std::partial_sort(scored.begin(), top, scored.end(), [](const auto& a, const auto& b) {
        if (a.first != b.first) return a.first > b.first;
        return a.second->form < b.second->form;
    });
    for (size_t i = 0; i < scored.size() && out.size() < n; ++i) {
        std::wstring form = scored[i].second->form;
        if (IsUpper(prefix[0])) form = MatchCase(prefix, form);
        out.push_back(form);
    }
    return out;
}

bool WordModel::Forget(const std::wstring& word) {
    const std::wstring key = WordKey(word);
    bool found = words_.erase(key) > 0;
    if (auto pit = pairs_.find(key); pit != pairs_.end()) {
        pairTotal_ -= pit->second.size();
        pairs_.erase(pit);
        found = true;
    }
    for (auto pit = pairs_.begin(); pit != pairs_.end();) {
        if (pit->second.erase(key)) {
            --pairTotal_;
            found = true;
        }
        if (pit->second.empty()) pit = pairs_.erase(pit);
        else ++pit;
    }
    if (found) dirty_ = true;
    return found;
}

void WordModel::Clear() {
    const bool had = !words_.empty() || !pairs_.empty();
    words_.clear();
    pairs_.clear();
    pairTotal_ = 0;
    if (had) dirty_ = true;
}

std::vector<std::wstring> WordModel::Next(const std::wstring& prev, size_t n) const {
    std::vector<std::wstring> out;
    auto pit = pairs_.find(WordKey(prev));
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
    const std::wstring key = WordKey(word);
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

std::vector<std::wstring> WordModel::NearSlip(const std::wstring& word, const KeyLayout& layout, size_t n) const {
    std::vector<std::wstring> out;
    const std::wstring key = WordKey(word);
    if (key.size() < 3 || n == 0) return out;
    const double limit = key.size() <= 4 ? 1.0 : key.size() <= 7 ? 1.5 : 2.5;
    struct Hit {
        double dist;
        double count;
        const std::wstring* form;
    };
    std::vector<Hit> hits;
    for (const auto& [k, w] : words_) {
        if (k == key || w.count < 2.0) continue;
        // The first letter right, on the key next door, or swapped with the second.
        if (k[0] != key[0] && !layout.Neighbors(k[0], key[0]) && !FirstTwoSwapped(k, key)) continue;
        const double d = SlipDistance(k, key, layout, limit);
        if (d > limit) continue;
        hits.push_back({d, w.count, &w.form});
    }
    const auto top = hits.begin() + static_cast<std::ptrdiff_t>(std::min(n, hits.size()));
    std::partial_sort(hits.begin(), top, hits.end(), [](const Hit& a, const Hit& b) {
        if (a.dist != b.dist) return a.dist < b.dist;
        if (a.count != b.count) return a.count > b.count;
        return *a.form < *b.form;
    });
    for (auto it = hits.begin(); it != top; ++it) out.push_back(MatchCase(word, *it->form));
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
            Word& w = words_[WordKey(form)];
            w.form = form;
            w.count += count(f[2]);
        } else if (f[0] == "n" && f.size() == 4) {
            const double c = count(f[3]);
            if (c <= 0) continue;
            auto [it, inserted] = pairs_[WordKey(FromUtf8(f[1]))].emplace(WordKey(FromUtf8(f[2])), 0.0);
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
    const std::wstring key = WordKey(word);

    struct Cand {
        std::wstring text;
        int dist;
        int rank;      // position in the dictionary's list (lower = better), 99 = only learned
        double count;  // how often you used it
    };
    std::vector<Cand> cands;
    auto consider = [&](const std::wstring& c, int rank) {
        if (c.empty() || c.find(L' ') != std::wstring::npos || c.find(L'-') != std::wstring::npos) return;
        const std::wstring ck = WordKey(c);
        if (ck == key) return;
        if (ck[0] != key[0] && !FirstTwoSwapped(ck, key)) return;
        const int d = EditDistance(ck, key, allowed);
        if (d > allowed) return;
        for (Cand& x : cands)
            if (WordKey(x.text) == ck) {
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
