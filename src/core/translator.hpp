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

struct TranslateResult {
    bool ok = false;
    std::wstring text;            // translated text, protected segments restored
    std::wstring detectedSource;  // e.g. "DE", if the backend reports it
    std::wstring error;           // human readable, empty when ok
    bool quotaExceeded = false;   // free contingent used up: retrying soon is pointless
    TranslateErrorKind errorKind = TranslateErrorKind::None;
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
