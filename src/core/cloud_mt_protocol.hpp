// cloud_mt_protocol.hpp — request/response format of Google Cloud Translation
// (v2 "basic", API key) and Microsoft Translator (v3, key + region).
// Pure data transformation; the HTTP calls live in win/online_translators.
//
// Protected segments: both services take HTML and leave
// <span translate="no" class="notranslate">…</span> untouched.
#pragma once

#include <string>
#include <vector>

#include "protect.hpp"

namespace gct {

struct CloudMtPayload {
    std::string json;
    bool html = false;  // the texts were sent as HTML (some segment is protected)
};

struct CloudMtParsed {
    bool ok = false;
    std::wstring text;
    std::wstring detectedSource;  // upper case: "DE"
    std::wstring error;
};

// Language codes of the services: "EN-GB" -> "en", "ZH-HANS" -> "zh-CN" (Google) / "zh-Hans" (Microsoft).
std::wstring GoogleLang(const std::wstring& code);
std::wstring MicrosoftLang(const std::wstring& code);

// POST https://translation.googleapis.com/language/translate/v2  (header X-Goog-Api-Key)
CloudMtPayload BuildGooglePayload(const std::vector<std::vector<Segment>>& items, const std::wstring& sourceLang,
                                  const std::wstring& targetLang);
std::vector<CloudMtParsed> ParseGoogleResponse(const std::string& body, bool html, size_t expected);

// POST https://api.cognitive.microsofttranslator.com/translate?api-version=3.0&to=..[&from=..][&textType=html]
CloudMtPayload BuildMicrosoftPayload(const std::vector<std::vector<Segment>>& items);
std::wstring MicrosoftQuery(const std::wstring& sourceLang, const std::wstring& targetLang, bool html);
std::vector<CloudMtParsed> ParseMicrosoftResponse(const std::string& body, bool html, size_t expected);

// LibreTranslate (own server or any instance): POST <url>/translate.
std::wstring LibreLang(const std::wstring& code);
CloudMtPayload BuildLibrePayload(const std::vector<std::vector<Segment>>& items, const std::wstring& sourceLang,
                                 const std::wstring& targetLang, const std::wstring& apiKey);
std::vector<CloudMtParsed> ParseLibreResponse(const std::string& body, bool html, size_t expected);
// "http://host:5000" or ".../translate" -> ".../translate"
std::wstring LibreTranslateUrl(const std::wstring& base);

// Drops the protecting <span> tags and decodes HTML entities (&amp; &#39; &#x27; …).
std::wstring RestoreFromHtml(const std::wstring& s);

}  // namespace gct
