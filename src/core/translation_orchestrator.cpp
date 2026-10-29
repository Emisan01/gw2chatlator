// translation_orchestrator.cpp — Smart-Sieve-Manager implementation.
#include "core/translation_orchestrator.hpp"

#include <algorithm>
#include <unordered_set>

#include "core/text.hpp"

namespace gct {
namespace {

const WordSet& KnownGamerInterjections() {
    static const WordSet s = {
        // match & gameplay
        L"gg", L"wp", L"ggwp", L"gj", L"nice", L"gw", L"gratz", L"gz",
        // laughter & reactions
        L"lol", L"mdr", L"ptdr", L"xptdr", L"rofl", L"lmao", L"haha", L"hehe", L"xd", L"dx",
        // courtesy
        L"ty", L"thx", L"tyvm", L"thanks", L"danke", L"merci", L"np", L"yw", L"pls", L"plz", L"sry", L"mb",
        // audience address in interjections ("gg all", "thx guys", "hi all")
        L"all", L"guys", L"alle", L"tous",
        // status & callouts
        L"afk", L"brb", L"wb", L"rdy", L"ready", L"inc", L"dc", L"rc", L"o7", L"hf", L"gl", L"glhf", L"omg", L"wtf",
        // greetings & farewells
        L"hi", L"hey", L"hallo", L"hello", L"salut", L"slt", L"cc", L"coucou", L"re", L"bb", L"bye", L"cu", L"gn", L"gn8", L"gm",
        // short affirmations / negations
        L"ok", L"kk", L"k", L"yes", L"ja", L"oui", L"no", L"nein", L"non", L"yep", L"nope",
        // game callouts
        L"wipe", L"res", L"rez", L"dps", L"cc", L"wvw", L"pvp", L"pve", L"lfg", L"lfm"
    };
    return s;
}

std::wstring CacheKey(const std::wstring& text, const std::wstring& targetLang) {
    return NormalizeForCompare(text) + L"\t" + ToLowerAscii(targetLang);
}

}  // namespace

bool IsGamerInterjection(const std::wstring& text) {
    const std::wstring trimmed = Trim(text);
    if (trimmed.empty()) return true;

    const std::vector<Span> spans = WordSpans(trimmed);
    if (spans.empty()) {
        // Pure numbers, emojis, punctuation (e.g. "1", "+1", "++", "??", ":)", "^^")
        return true;
    }

    const WordSet& interjections = KnownGamerInterjections();
    for (const Span& span : spans) {
        const std::wstring word = CaseFold(trimmed.substr(span.start, span.length));
        if (std::all_of(word.begin(), word.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; })) {
            continue;
        }
        if (interjections.find(word) == interjections.end()) {
            return false;  // Contains a word that is not a known gaming interjection
        }
    }
    return true;
}

RepetitionCache::RepetitionCache(size_t capacity) : capacity_(capacity ? capacity : 1) {}

void RepetitionCache::RebuildIndex() {
    index_.clear();
    for (size_t i = 0; i < entries_.size(); ++i) {
        index_[CacheKey(entries_[i].text, entries_[i].targetLang)] = i;
    }
}

bool RepetitionCache::Lookup(const std::wstring& text, const std::wstring& targetLang,
                             std::wstring& outTranslation, std::wstring& outBy) const {
    const std::wstring key = CacheKey(text, targetLang);
    const auto it = index_.find(key);
    if (it == index_.end() || it->second >= entries_.size()) return false;
    const Entry& e = entries_[it->second];
    outTranslation = e.translation;
    outBy = e.by;
    return true;
}

void RepetitionCache::Store(const std::wstring& text, const std::wstring& targetLang,
                            const std::wstring& translation, const std::wstring& by) {
    if (text.empty() || translation.empty()) return;
    const std::wstring key = CacheKey(text, targetLang);
    const auto it = index_.find(key);
    if (it != index_.end() && it->second < entries_.size()) {
        entries_[it->second].translation = translation;
        entries_[it->second].by = by;
        return;
    }

    if (entries_.size() >= capacity_) {
        entries_.pop_front();
    }
    entries_.push_back({text, targetLang, translation, by});
    RebuildIndex();
}

void RepetitionCache::Clear() {
    entries_.clear();
    index_.clear();
}

size_t RepetitionCache::Size() const {
    return entries_.size();
}

TranslationOrchestrator::TranslationOrchestrator() : repCache_(64) {}

ChatPriority TranslationOrchestrator::SieveMessage(const std::wstring& text,
                                                  const std::wstring& targetLang,
                                                  bool isWindowVisible,
                                                  bool isChannelVisibleInTab,
                                                  bool isInViewport,
                                                  bool isRecentTail,
                                                  std::wstring& outTranslation,
                                                  std::wstring& outBy,
                                                  const std::wstring& currentBy) {
    if (paused_) return ChatPriority::Skip;

    // Filter 1: Spam-Sieve (Gamer interjections & shorthand)
    if (sieve_ && IsGamerInterjection(text)) {
        ++slangFiltered_;
        return ChatPriority::Skip;
    }

    // Filter 1: Repetition-Hash check
    // (a translation by the translator used before a switch is not taken: it would show the old one's wording)
    if (repCache_.Lookup(text, targetLang, outTranslation, outBy) && (currentBy.empty() || outBy == currentBy)) {
        ++cacheHits_;
        return ChatPriority::Immediate;
    }

    // Filter 2: Occlusion culling / Viewport visibility
    if (!isWindowVisible || !isChannelVisibleInTab) {
        ++culledLines_;
        return ChatPriority::Low;
    }

    if (isInViewport) return ChatPriority::High;
    if (isRecentTail) return ChatPriority::Medium;

    return ChatPriority::Low;
}

void TranslationOrchestrator::RecordTranslation(const std::wstring& text,
                                                const std::wstring& targetLang,
                                                const std::wstring& translation,
                                                const std::wstring& by) {
    repCache_.Store(text, targetLang, translation, by);
}

int TranslationOrchestrator::ComputeBeamSize(QualityMode userMode, size_t queueDepth) const {
    const int baseBeam = static_cast<int>(userMode);
    if (baseBeam <= 1) return 1;
    if (queueDepth > 2) return 2;  // under queue pressure a smaller beam, never greedy (beam 1: "dance" -> "TANCE")
    return baseBeam;
}

}  // namespace gct
