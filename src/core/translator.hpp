// translator.hpp — interface every translation backend implements
// (DeepL, an OpenAI-compatible LLM endpoint — cloud or local — and later
// a local classic MT model).
#pragma once

#include <string>
#include <vector>

#include "protect.hpp"

namespace gct {

enum class TranslateErrorKind {
    None,
    UnsupportedPair,
    HelperMissing,
    HelperCrashed,
    BadOutput,
    Network,
    Timeout,
    Auth,
    Quota,
    Other
};

inline const wchar_t* TranslateErrorKindName(TranslateErrorKind k) {
    switch (k) {
        case TranslateErrorKind::UnsupportedPair: return L"UnsupportedPair";
        case TranslateErrorKind::HelperMissing:   return L"HelperMissing";
        case TranslateErrorKind::HelperCrashed:   return L"HelperCrashed";
        case TranslateErrorKind::BadOutput:       return L"BadOutput";
        case TranslateErrorKind::Network:         return L"Network";
        case TranslateErrorKind::Timeout:         return L"Timeout";
        case TranslateErrorKind::Auth:            return L"Auth";
        case TranslateErrorKind::Quota:           return L"Quota";
        case TranslateErrorKind::Other:           return L"Other";
        default:                                  return L"None";
    }
}

// What a local model did for one message, for the live view (no text).
struct TranslateRun {
    int requests = 0;   // to the model: one per sentence, more with extra tries
    int beam = 0;       // the highest beam it really ran
    int threads = 0;    // its CPU threads ...
    int cores = 0;      // ... on this many processors
    int tokensIn = 0, tokensOut = 0;
    int modelMs = 0;    // inside the model, summed
    int loadMs = 0;     // the model was loaded during this message
    int sentencesKept = 0;  // sentences that came from the sentence cache, without the model
    void Add(const TranslateRun& o) {
        requests += o.requests;
        beam = beam > o.beam ? beam : o.beam;
        threads = o.threads ? o.threads : threads;
        cores = o.cores ? o.cores : cores;
        tokensIn += o.tokensIn;
        tokensOut += o.tokensOut;
        modelMs += o.modelMs;
        loadMs = o.loadMs ? o.loadMs : loadMs;
        sentencesKept += o.sentencesKept;
    }
};

struct TranslateResult {
    bool ok = false;
    std::wstring text;            // translated text, protected segments restored
    std::wstring detectedSource;  // e.g. "DE", if the backend reports it
    std::wstring error;           // human readable, empty when ok
    bool quotaExceeded = false;   // free contingent used up: retrying soon is pointless
    TranslateErrorKind errorKind = TranslateErrorKind::None;
    // A local model's certainty: its lowest mean log probability per token over all requests (0 best, below
    // kUnsureScore it guessed); `scored` only when the helper reports it.
    double score = 0;
    bool scored = false;
    TranslateRun run;
    std::vector<std::wstring> variants;  // other versions of `text`, the likeliest first (a local model, when asked for)
};

constexpr double kUnsureScore = -1.2;  // translation_bench 2026-10-27: a made-up greeting it got wrong scored -1.39, every good line -1.04 or better

class Translator {
public:
    virtual ~Translator() = default;

    // Blocking. Called from worker threads — implementations must be
    // thread-safe. Segments with keep=true must come back unchanged.
    virtual TranslateResult Translate(const std::vector<Segment>& segments,
                                      const std::wstring& sourceLang,  // empty = auto-detect
                                      const std::wstring& targetLang) = 0;

    // Several independent texts at once (incoming chat). Default: one by
    // one; once the contingent is used up the rest fails without a request.
    virtual std::vector<TranslateResult> TranslateBatch(const std::vector<std::vector<Segment>>& items,
                                                        const std::wstring& sourceLang,
                                                        const std::wstring& targetLang) {
        std::vector<TranslateResult> out;
        out.reserve(items.size());
        for (const auto& item : items) {
            if (!out.empty() && out.back().quotaExceeded) {
                out.push_back(out.back());
                continue;
            }
            out.push_back(Translate(item, sourceLang, targetLang));
        }
        return out;
    }

    virtual std::wstring Name() const = 0;
};

}  // namespace gct
