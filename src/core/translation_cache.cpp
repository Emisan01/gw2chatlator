// translation_cache.cpp — see translation_cache.hpp.

#include "core/translation_cache.hpp"

namespace gct {

const std::wstring* TranslationCache::Find(const TranslationKey& k) {
    const auto it = index_.find(k);
    if (it == index_.end()) return nullptr;
    items_.splice(items_.begin(), items_, it->second);
    return &it->second->second;
}

void TranslationCache::Put(const TranslationKey& k, std::wstring text) {
    if (const auto it = index_.find(k); it != index_.end()) {
        it->second->second = std::move(text);
        items_.splice(items_.begin(), items_, it->second);
        return;
    }
    items_.emplace_front(k, std::move(text));
    index_[k] = items_.begin();
    while (items_.size() > max_) {
        index_.erase(items_.back().first);
        items_.pop_back();
    }
}

TranslateResult TranslationCache::Through(const TranslationKey& k, const std::function<TranslateResult()>& ask, bool* asked) {
    if (asked) *asked = false;
    if (const std::wstring* hit = Find(k)) {
        TranslateResult r;
        r.ok = true;
        r.text = *hit;
        return r;
    }
    if (asked) *asked = true;
    TranslateResult r = ask();
    if (r.ok && !r.text.empty()) Put(k, r.text);
    return r;
}

}  // namespace gct
