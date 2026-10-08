// typo_memory.cpp
#include "typo_memory.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "core/text.hpp"
#include "core/word_model.hpp"

namespace gct {

void TypoMemory::Add(const std::wstring& typo, const std::wstring& fix, double weight) {
    const std::wstring k = WordKey(typo);
    if (k.empty() || fix.empty() || k == WordKey(fix) || weight <= 0) return;
    if (!map_.count(k) && map_.size() >= kMaxEntries) {
        // Full: the weakest entry makes room.
        auto weakest = map_.end();
        double low = 1e300;
        for (auto it = map_.begin(); it != map_.end(); ++it) {
            double w = 0;
            for (const Fix& f : it->second) w += f.weight;
            if (w < low) {
                low = w;
                weakest = it;
            }
        }
        if (weakest != map_.end()) map_.erase(weakest);
    }
    std::vector<Fix>& fixes = map_[k];
    const std::wstring fk = WordKey(fix);
    auto it = std::find_if(fixes.begin(), fixes.end(), [&](const Fix& f) { return WordKey(f.word) == fk; });
    if (it == fixes.end()) {
        fixes.push_back({fix, weight});
    } else {
        it->weight += weight;
        it->word = fix;
    }
    dirty_ = true;
}

bool TypoMemory::Forget(const std::wstring& typo) {
    if (map_.erase(WordKey(typo)) == 0) return false;
    dirty_ = true;
    return true;
}

void TypoMemory::ForgetFix(const std::wstring& fix) {
    const std::wstring fk = WordKey(fix);
    for (auto it = map_.begin(); it != map_.end();) {
        auto& fixes = it->second;
        const size_t before = fixes.size();
        fixes.erase(std::remove_if(fixes.begin(), fixes.end(), [&](const Fix& f) { return WordKey(f.word) == fk; }),
                    fixes.end());
        if (fixes.size() != before) dirty_ = true;
        it = fixes.empty() ? map_.erase(it) : std::next(it);
    }
}

std::wstring TypoMemory::FixFor(const std::wstring& typo) const {
    auto it = map_.find(WordKey(typo));
    if (it == map_.end()) return {};
    double total = 0;
    const Fix* best = nullptr;
    for (const Fix& f : it->second) {
        total += f.weight;
        if (!best || f.weight > best->weight) best = &f;
    }
    if (!best || best->weight * 3 < total * 2) return {};
    return best->word;
}

void TypoMemory::Clear() {
    if (!map_.empty()) dirty_ = true;
    map_.clear();
}

std::string TypoMemory::Serialize() const {
    std::string out = "# typing mistakes and what was meant: typo<TAB>fix<TAB>weight\n";
    std::vector<std::wstring> keys;
    for (const auto& kv : map_) keys.push_back(kv.first);
    std::sort(keys.begin(), keys.end());
    for (const std::wstring& k : keys)
        for (const Fix& f : map_.at(k)) {
            char w[32];
            std::snprintf(w, sizeof w, "%.2f", f.weight);
            out += ToUtf8(k) + "\t" + ToUtf8(f.word) + "\t" + w + "\n";
        }
    return out;
}

void TypoMemory::Parse(const std::string& data) {
    map_.clear();
    size_t pos = 0;
    while (pos < data.size()) {
        size_t eol = data.find('\n', pos);
        if (eol == std::string::npos) eol = data.size();
        std::string line = data.substr(pos, eol - pos);
        pos = eol + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const size_t t1 = line.find('\t');
        const size_t t2 = t1 == std::string::npos ? t1 : line.find('\t', t1 + 1);
        if (t2 == std::string::npos) continue;
        const double w = std::atof(line.c_str() + t2 + 1);
        Add(FromUtf8(line.substr(0, t1)), FromUtf8(line.substr(t1 + 1, t2 - t1 - 1)), w > 0 ? w : 1.0);
    }
    dirty_ = false;
}

}  // namespace gct
