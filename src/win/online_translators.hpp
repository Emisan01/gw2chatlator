// online_translators.hpp — the two other backends next to DeepL:
//
//  * MyMemory: free, no key — the "basic" engine that works out of the box
//    (small daily quota; an e-mail address in the INI raises it).
//  * LLM: any OpenAI-compatible chat endpoint — a local Ollama / LM Studio
//    (free, private, unlimited) or a cloud provider with your own key.
#pragma once

#include <memory>
#include <string>

#include "core/translator.hpp"

namespace gct {

std::shared_ptr<Translator> MakeMyMemoryTranslator(const std::wstring& email);

struct LlmSettings {
    std::wstring url = L"http://localhost:11434/v1/chat/completions";  // Ollama default
    std::wstring model;
    std::wstring apiKey;  // empty for local servers
    int timeoutMs = 60000;
};

class LlmTranslator : public Translator {
public:
    // Same text in Latin letters (Arabizi, Pinyin ...), not translated.
    virtual TranslateResult Romanize(const std::wstring& text) = 0;
};

std::shared_ptr<LlmTranslator> MakeLlmTranslator(const LlmSettings& settings);

// "http://localhost:11434" or ".../v1" -> full chat-completions URL.
std::wstring NormalizeLlmUrl(const std::wstring& url);

}  // namespace gct
