// online_translators.hpp — the two other backends next to DeepL:
//
//  * MyMemory: free, no key — the "basic" engine that works out of the box
//    (small daily quota; an e-mail address in the INI raises it).
//  * LLM: any OpenAI-compatible chat endpoint — a local Ollama / LM Studio
//    (free, private, unlimited) or a cloud provider with your own key.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/languagetool_protocol.hpp"
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
    // Chat lines read from the screen: translated with obvious OCR errors repaired.
    virtual std::vector<TranslateResult> TranslateOcrBatch(const std::vector<std::vector<Segment>>& items,
                                                           const std::wstring& targetLang) = 0;
};

std::shared_ptr<LlmTranslator> MakeLlmTranslator(const LlmSettings& settings);

// "http://localhost:11434" or ".../v1" -> full chat-completions URL.
std::wstring NormalizeLlmUrl(const std::wstring& url);

// True for an LLM on this PC or in the home network (Ollama, LM Studio), false for cloud APIs.
bool IsLocalLlmUrl(const std::wstring& url);

// Models the server offers: tries <base>/v1/models, then Ollama's /api/tags.
// Blocking (call from a worker thread).
std::vector<std::wstring> FetchLlmModels(const std::wstring& url, const std::wstring& apiKey, std::wstring* error);

// Local models offered with an install button (Ollama names), smallest first.
struct LocalModelOffer {
    const wchar_t* id;       // "gemma3:1b"
    const wchar_t* summary;  // English UI text (translated with Tr): size, hardware, strengths
};
const std::vector<LocalModelOffer>& LocalModelOffers();

// True if an Ollama server answers at the host of `llmUrl` (default localhost:11434).
bool OllamaReachable(const std::wstring& llmUrl);
// Ollama downloads `model` (blocking: minutes for gigabytes; call from a worker thread).
bool PullOllamaModel(const std::wstring& llmUrl, const std::wstring& model, std::wstring* error);

// LanguageTool check (public API or own server); blocking.
LtResult CheckWithLanguageTool(const std::wstring& serverUrl, const std::wstring& text, const std::wstring& lang,
                               const std::wstring& motherTongue);

}  // namespace gct
