// llm_protocol.hpp — translation through an OpenAI-compatible chat endpoint
// (OpenAI, OpenRouter, a local Ollama or LM Studio ...). Pure data.
//
// The model only ever gets text and returns text. Chat lines from other
// players are framed as data in the system prompt; nothing the model says is
// executed or sent anywhere except into the translation view.
#pragma once

#include <string>
#include <vector>

#include "protect.hpp"

namespace gct {

std::string BuildLlmRequest(const std::vector<std::vector<Segment>>& items,
                            const std::wstring& targetLangName,  // "German", "Chinese (Simplified)"
                            const std::wstring& model);

// Same text in Latin letters as people write it in chat (Arabizi, Pinyin,
// Romaji ...), not translated — for scripts the GW2 chat cannot display.
std::string BuildLlmRomanizeRequest(const std::wstring& text, const std::wstring& model);

struct LlmParsed {
    bool ok = false;
    std::vector<std::wstring> texts;
    std::wstring error;
    bool formatError = false;  // the model answered, but not in the requested shape
};

// Accepts plain JSON arrays, arrays inside ```json fences, and strips
// <think>…</think> blocks of reasoning models. For a single item a plain
// text answer is accepted as well.
LlmParsed ParseLlmResponse(const std::string& body, size_t expected);

}  // namespace gct
