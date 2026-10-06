// chat_stream.hpp — which lines of a capture are new?
//
// The chat is read about once a second; most lines are already known. OCR
// reads the same line slightly differently from frame to frame, so lines are
// compared fuzzily (character-bigram Dice similarity), not exactly.
#pragma once

#include <cstdint>
#include <deque>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>

#include "chat_line.hpp"

namespace gct {

// Case-folded word characters only — the form used for comparisons.
std::wstring NormalizeForCompare(const std::wstring& s);

// 0..1, 1 = identical (Dice coefficient over character bigrams).
double DiceSimilarity(const std::wstring& a, const std::wstring& b);

class ChatStream {
public:
    explicit ChatStream(size_t memory = 150) : memory_(memory) {}

    // Messages of `snapshot` that were not seen recently, in order.
    // `confirm` (the double scan): a new message only counts once the next
    // snapshot shows it again; things seen only once (half drawn while
    // scrolling, a tooltip, a fading animation) never get through.
    std::vector<ChatMessage> Feed(const std::vector<ChatMessage>& snapshot, bool confirm = false);
    // New messages are waiting for their second look: read again soon.
    bool HasPending() const { return !pending_.empty(); }
    void Reset() {
        recent_.clear();
        pending_.clear();
    }

private:
    struct Seen {
        std::wstring key;
        std::vector<uint32_t> grams;
    };
    static bool Similar(const Seen& a, const Seen& b);
    bool IsKnown(const Seen& s) const;

    size_t memory_;
    std::deque<Seen> recent_;
    std::vector<Seen> pending_;  // seen once in the last snapshot
};

// Small LRU cache: normalized source text + target language -> translation.
class TranslationCache {
public:
    explicit TranslationCache(size_t capacity = 600) : capacity_(capacity) {}
    bool Get(const std::wstring& text, const std::wstring& lang, std::wstring& out);
    void Put(const std::wstring& text, const std::wstring& lang, const std::wstring& translation);

private:
    using Entry = std::pair<std::wstring, std::wstring>;  // key, translation
    size_t capacity_;
    std::list<Entry> order_;
    std::unordered_map<std::wstring, std::list<Entry>::iterator> map_;
};

}  // namespace gct
