// chat_stream.cpp
#include "chat_stream.hpp"

#include <algorithm>

#include "text.hpp"

namespace gct {

std::wstring NormalizeForCompare(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size());
    for (wchar_t c : s)
        if (IsWordChar(c)) out += CaseFoldChar(c);
    return out;
}

static std::vector<uint32_t> Bigrams(const std::wstring& n) {
    std::vector<uint32_t> g;
    if (n.size() < 2) {
        if (!n.empty()) g.push_back(static_cast<uint32_t>(n[0]));
        return g;
    }
    g.reserve(n.size() - 1);
    for (size_t i = 0; i + 1 < n.size(); ++i)
        g.push_back((static_cast<uint32_t>(n[i]) << 16) ^ static_cast<uint32_t>(n[i + 1]));
    std::sort(g.begin(), g.end());
    return g;
}

static double DiceSorted(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b) {
    if (a.empty() && b.empty()) return 1.0;
    if (a.empty() || b.empty()) return 0.0;
    size_t i = 0, j = 0, common = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i] == b[j]) { ++common; ++i; ++j; }
        else if (a[i] < b[j]) ++i;
        else ++j;
    }
    return 2.0 * common / static_cast<double>(a.size() + b.size());
}

double DiceSimilarity(const std::wstring& a, const std::wstring& b) {
    return DiceSorted(Bigrams(NormalizeForCompare(a)), Bigrams(NormalizeForCompare(b)));
}

bool ChatStream::IsKnown(const Seen& s) const {
    for (const Seen& r : recent_) {
        if (r.key == s.key) return true;
        const size_t la = r.key.size(), lb = s.key.size();
        if (std::max(la, lb) > 0 && std::min(la, lb) * 10 < std::max(la, lb) * 7) continue;  // lengths too different
        if (DiceSorted(r.grams, s.grams) >= 0.82) return true;
    }
    return false;
}

std::vector<ChatMessage> ChatStream::Feed(const std::vector<ChatMessage>& snapshot) {
    std::vector<ChatMessage> fresh;
    for (const ChatMessage& m : snapshot) {
        Seen s;
        s.key = NormalizeForCompare(m.speaker + L":" + m.text);
        if (s.key.empty()) continue;
        s.grams = Bigrams(s.key);
        if (IsKnown(s)) continue;
        fresh.push_back(m);
        recent_.push_back(std::move(s));
        while (recent_.size() > memory_) recent_.pop_front();
    }
    return fresh;
}

bool TranslationCache::Get(const std::wstring& text, const std::wstring& lang, std::wstring& out) {
    const std::wstring key = lang + L"|" + NormalizeForCompare(text);
    const auto it = map_.find(key);
    if (it == map_.end()) return false;
    order_.splice(order_.begin(), order_, it->second);  // most recent first
    out = it->second->second;
    return true;
}

void TranslationCache::Put(const std::wstring& text, const std::wstring& lang, const std::wstring& translation) {
    const std::wstring key = lang + L"|" + NormalizeForCompare(text);
    if (const auto it = map_.find(key); it != map_.end()) {
        it->second->second = translation;
        order_.splice(order_.begin(), order_, it->second);
        return;
    }
    order_.emplace_front(key, translation);
    map_[key] = order_.begin();
    while (order_.size() > capacity_) {
        map_.erase(order_.back().first);
        order_.pop_back();
    }
}

}  // namespace gct
