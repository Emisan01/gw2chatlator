// translator.hpp — interface every translation backend implements
// (DeepL, an OpenAI-compatible LLM endpoint — cloud or local — and later
// a local classic MT model).
#pragma once

#include <string>
#include <vector>

#include "protect.hpp"

namespace gct {

struct TranslateResult {
    bool ok = false;
    std::wstring text;            // translated text, protected segments restored
    std::wstring detectedSource;  // e.g. "DE", if the backend reports it
    std::wstring error;           // human readable (German), empty when ok
    bool quotaExceeded = false;   // free contingent used up: retrying soon is pointless
};

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
