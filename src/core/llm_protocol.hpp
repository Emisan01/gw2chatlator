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

// `fromOcr`: the lines were read from the screen and may contain recognition
// errors ("Mnuten", "Haåwick"); the model is asked to repair obvious ones
// while translating, never to invent content.
// `temperature0`: ask for temperature 0. Local models like it; current cloud
// reasoning models (Claude, GPT-5 …) reject any temperature, so cloud calls omit it.
std::string BuildLlmRequest(const std::vector<std::vector<Segment>>& items,
                            const std::wstring& targetLangName,  // "German", "Chinese (Simplified)"
                            const std::wstring& model, bool fromOcr = false, bool temperature0 = true);

// Same text in Latin letters as people write it in chat (Arabizi, Pinyin,
// Romaji ...), not translated — for scripts the GW2 chat cannot display.
std::string BuildLlmRomanizeRequest(const std::wstring& text, const std::wstring& model, bool temperature0 = true);

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

// Model names from GET <base>/v1/models (OpenAI style: data[].id) or Ollama's
// GET /api/tags (models[].name). Sorted, without duplicates.
std::vector<std::wstring> ParseModelList(const std::string& body);

}  // namespace gct
