// deepl_protocol.hpp — request/response format of DeepL /v2/translate.
// Pure data transformation; the HTTP call lives in win/deepl_translator.
#pragma once

#include <string>
#include <vector>

#include "protect.hpp"

namespace gct {

// If any segment is protected, the payload switches to DeepL's XML mode:
// protected text is wrapped in <keep>…</keep>, which is an ignore tag.
struct DeepLPayload {
    std::string json;
    bool xmlMode = false;
};
DeepLPayload BuildDeepLPayload(const std::vector<Segment>& segments,
                               const std::wstring& sourceLang,  // empty = auto
                               const std::wstring& targetLang);

// Several texts in one request (DeepL accepts a "text" array).
DeepLPayload BuildDeepLPayloadBatch(const std::vector<std::vector<Segment>>& items, const std::wstring& sourceLang,
                                    const std::wstring& targetLang);

struct DeepLParsed {
    bool ok = false;
    std::wstring text;
    std::wstring detectedSource;
    std::wstring error;  // DeepL "message" or a parse problem
};
DeepLParsed ParseDeepLResponse(const std::string& body, bool xmlMode);

// One result per requested text; on a top-level error every entry carries it.
std::vector<DeepLParsed> ParseDeepLResponseBatch(const std::string& body, bool xmlMode, size_t expected);

// Undo the XML wrapping: drops <keep> tags and decodes the five XML entities.
std::wstring RestoreFromXml(const std::wstring& s);

// Free keys end with ":fx" and must use api-free.deepl.com.
bool IsDeepLFreeKey(const std::wstring& key);

}  // namespace gct
