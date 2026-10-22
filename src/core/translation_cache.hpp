// translation_cache.hpp — the chat's translations of this session, in memory only (never on disk: invariant 6, the
// README's privacy promise): the same line from the same language into the same language by the same translator is
// asked once – "once" twice, a changed language and back, a repeated LFG line cost nothing more. Bounded: the least
// recently used goes first. Portable, no windows.h.
#pragma once

#include <cstddef>
#include <functional>
#include <list>
#include <map>
#include <string>
#include <tuple>

#include "core/translator.hpp"

namespace gct {

struct TranslationKey {
    std::wstring text;    // the line as it came (not normalised: "?" or a capital can change the translation)
    std::wstring source;  // what the translator is told it is ("" = it detects)
    std::wstring target;
    std::wstring by;      // the translator's name
    bool operator<(const TranslationKey& o) const {
        return std::tie(text, source, target, by) < std::tie(o.text, o.source, o.target, o.by);
    }
};

class TranslationCache {
public:
    static constexpr size_t kMax = 500;
    explicit TranslationCache(size_t max = kMax) : max_(max ? max : 1) {}

    const std::wstring* Find(const TranslationKey& k);  // a hit becomes the newest
    void Put(const TranslationKey& k, std::wstring text);
    size_t Size() const { return items_.size(); }

    // One text through the cache: a hit asks nobody; a miss asks `ask` once and keeps an answer that came back ok
    // and not empty. `asked`: whether `ask` ran.
    TranslateResult Through(const TranslationKey& k, const std::function<TranslateResult()>& ask, bool* asked = nullptr);

private:
    using Item = std::pair<TranslationKey, std::wstring>;
    size_t max_;
    std::list<Item> items_;  // newest first
    std::map<TranslationKey, std::list<Item>::iterator> index_;
};

}  // namespace gct
