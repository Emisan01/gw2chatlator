// names.cpp
#include "names.hpp"

#include <algorithm>

#include "word_model.hpp"

namespace gct {

void NameList::Add(const std::wstring& name) {
    std::vector<std::wstring> words;
    for (const Span& s : WordSpans(name)) words.push_back(WordKey(name.substr(s.start, s.length)));
    if (words.empty() || (words.size() == 1 && words[0].size() < 3)) return;
    names_.erase(std::remove(names_.begin(), names_.end(), words), names_.end());
    names_.push_back(std::move(words));
    if (names_.size() > kMax) names_.erase(names_.begin());
}

std::vector<Span> NameList::Find(const std::wstring& text) const {
    std::vector<Span> out;
    const std::vector<Span> spans = WordSpans(text);
    std::vector<std::wstring> keys;
    for (const Span& s : spans) keys.push_back(WordKey(text.substr(s.start, s.length)));

    // 0 = no, 1 = one typo, 2 = exact
    auto match = [](const std::wstring& t, const std::wstring& n, size_t minFuzzy) {
        if (t == n) return 2;
        if (n.size() >= minFuzzy && !t.empty() && t[0] == n[0] && EditDistance(t, n, 1) <= 1) return 1;
        return 0;
    };
    for (size_t i = 0; i < keys.size();) {
        size_t best = 0;  // words of the longest name found at i
        for (auto it = names_.rbegin(); it != names_.rend(); ++it) {
            const std::vector<std::wstring>& n = *it;
            if (n.size() <= best || i + n.size() > keys.size()) continue;
            bool all = true, anyExact = false;
            for (size_t k = 0; k < n.size() && all; ++k) {
                const int m = match(keys[i + k], n[k], n.size() == 1 ? 6 : 3);
                all = m > 0;
                anyExact = anyExact || m == 2;
            }
            if (all && (anyExact || n.size() == 1)) best = n.size();
        }
        if (best > 0) {
            out.push_back({spans[i].start, spans[i + best - 1].end() - spans[i].start});
            i += best;
        } else {
            ++i;
        }
    }
    return out;
}

}  // namespace gct
