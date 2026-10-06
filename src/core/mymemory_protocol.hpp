// mymemory_protocol.hpp — the free MyMemory translation API (no key needed;
// small daily quota, more with an e-mail address). The "basic" engine that
// works out of the box. Pure data.
#pragma once

#include <string>

namespace gct {

std::string UrlEncode(const std::string& utf8);

// DeepL-style code -> MyMemory/RFC 3066 code: "EN-GB" -> "en-GB", "ZH-HANS" -> "zh-CN", "DE" -> "de".
std::wstring MyMemoryLang(const std::wstring& code);

// Path + query for GET https://api.mymemory.translated.net
std::wstring BuildMyMemoryPath(const std::wstring& text, const std::wstring& sourceLang, const std::wstring& targetLang,
                               const std::wstring& email);

struct MyMemoryParsed {
    bool ok = false;
    bool quotaExceeded = false;
    std::wstring text;
    std::wstring error;
};
MyMemoryParsed ParseMyMemoryResponse(const std::string& body);

}  // namespace gct
