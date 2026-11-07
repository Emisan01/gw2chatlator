// online_translators.hpp — the backends next to DeepL: MyMemory (free, no key; an e-mail address in the INI raises its
// daily quota), Google, Microsoft, Supertext and LibreTranslate.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/translator.hpp"

namespace gct {

std::shared_ptr<Translator> MakeMyMemoryTranslator(const std::wstring& email);
// Google Cloud Translation (key from console.cloud.google.com) and Microsoft Translator (key + Azure region).
std::shared_ptr<Translator> MakeGoogleTranslator(const std::wstring& apiKey);
std::shared_ptr<Translator> MakeMicrosoftTranslator(const std::wstring& apiKey, const std::wstring& region);
std::shared_ptr<Translator> MakeSupertextTranslator(const std::wstring& apiKey);
// Any LibreTranslate-compatible server ("http://localhost:5000", a public instance ...).
std::shared_ptr<Translator> MakeLibreTranslator(const std::wstring& url, const std::wstring& apiKey);

}  // namespace gct
