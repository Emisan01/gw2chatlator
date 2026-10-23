// translation_orchestrator.hpp — Smart-Sieve-Manager: spam filter, repetition cache,
// occlusion culling / priority queueing, and adaptive beam throttling.
#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

namespace gct {

// Priority levels for translation scheduling
enum class ChatPriority {
    Skip,       // Gamer interjection / slang, or no translatable text (CPU: 0)
    Immediate,  // Found in repetition cache -> copied immediately (CPU: 0)
    High,       // Visible in the active ImGui chat viewport
    Medium,     // Recent tail (active message feed)
    Low         // Scrolled away, collapsed, or hidden in background tab
};

// User quality setting
enum class QualityMode {
    Fast = 1,      // Beam size 1 (greedy search) — lowest latency & CPU
    Balanced = 2,  // Beam size 2 (default) — sweet spot for accuracy
    Quality = 4    // Beam size 4 — maximum nuance
};

// Filter 1: Spam-Sieve
// Returns true if the message is purely international gamer shorthand / slang
// (e.g. "gg", "wp", "lol", "mdr", "ty", "np", "afk", "o7", "++", "1", "?!?!")
// without translatable sentences.
bool IsGamerInterjection(const std::wstring& text);

// Filter 1 Part A: Repetition Cache (Fast ring buffer / map for recent duplicate messages)
class RepetitionCache {
public:
    explicit RepetitionCache(size_t capacity = 64);

    // Returns true if a valid recent translation exists for this text & target language
    bool Lookup(const std::wstring& text, const std::wstring& targetLang,
                std::wstring& outTranslation, std::wstring& outBy) const;

    // Stores a completed translation
    void Store(const std::wstring& text, const std::wstring& targetLang,
               const std::wstring& translation, const std::wstring& by);

    void Clear();
    size_t Size() const;

private:
    size_t capacity_;
    struct Entry {
        std::wstring text;
        std::wstring targetLang;
        std::wstring translation;
        std::wstring by;
    };
    std::deque<Entry> entries_;
    std::unordered_map<std::wstring, size_t> index_;  // key: text + L"\t" + targetLang -> index in entries_
    void RebuildIndex();
};

// Filter 2 & 3: TranslationOrchestrator
class TranslationOrchestrator {
public:
    TranslationOrchestrator();
    TranslationOrchestrator(const TranslationOrchestrator&) = delete;
    TranslationOrchestrator& operator=(const TranslationOrchestrator&) = delete;

    // Sieve incoming line:
    // - checks pause switch
    // - checks gamer interjections
    // - checks repetition cache
    // - checks window / viewport visibility
    ChatPriority SieveMessage(const std::wstring& text,
                              const std::wstring& targetLang,
                              bool isWindowVisible,
                              bool isChannelVisibleInTab,
                              bool isInViewport,
                              bool isRecentTail,
                              std::wstring& outTranslation,
                              std::wstring& outBy);

    // Records an inferred translation into the repetition cache
    void RecordTranslation(const std::wstring& text,
                           const std::wstring& targetLang,
                           const std::wstring& translation,
                           const std::wstring& by);

    // Dynamic Throttle: computes effective beam size based on user quality setting and current queue depth.
    // If queue is congested (> 2 items), drops to beam 1 (greedy) to flush backlog quickly.
    int ComputeBeamSize(QualityMode userMode, size_t queueDepth) const;

    // Panic-Switch / Pause Toggle
    void SetPaused(bool paused) { paused_ = paused; }
    bool IsPaused() const { return paused_; }
    void TogglePaused() { paused_ = !paused_; }

    void ClearCache() { repCache_.Clear(); }

    // Telemetry accessors
    uint64_t SlangFilteredCount() const { return slangFiltered_.load(); }
    uint64_t CacheHitCount() const { return cacheHits_.load(); }
    uint64_t CulledCount() const { return culledLines_.load(); }
    size_t CacheSize() const { return repCache_.Size(); }

private:
    RepetitionCache repCache_;
    bool paused_ = false;
    std::atomic<uint64_t> slangFiltered_{0};
    std::atomic<uint64_t> cacheHits_{0};
    std::atomic<uint64_t> culledLines_{0};
};

}  // namespace gct
