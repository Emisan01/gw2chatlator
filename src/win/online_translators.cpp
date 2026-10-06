// online_translators.cpp
#include "core/i18n.hpp"
#include "online_translators.hpp"

#include <algorithm>

#include "core/json.hpp"
#include "core/langs.hpp"
#include "core/languagetool_protocol.hpp"
#include "core/languages.hpp"
#include "core/llm_protocol.hpp"
#include "core/mymemory_protocol.hpp"
#include "core/text.hpp"
#include "els.hpp"
#include "http.hpp"

namespace gct {

// ===========================================================================
// MyMemory
// ===========================================================================
namespace {

class MyMemoryTranslator final : public Translator {
public:
    explicit MyMemoryTranslator(std::wstring email) : email_(Trim(email)) {}

    std::wstring Name() const override { return L"MyMemory (" + Tr(L"basic") + L")"; }

    TranslateResult Translate(const std::vector<Segment>& segments, const std::wstring& sourceLang,
                              const std::wstring& targetLang) override {
        TranslateResult r;
        const std::wstring text = JoinSegments(segments);
        std::wstring source = sourceLang.empty() ? DetectLanguage(text) : PrimaryLang(sourceLang);
        // Windows refuses short lines: then telltale letters (ı ğ ş -> Turkish ...),
        // and only without any hint English, the most common in GW2 chat.
        if (source.empty()) source = GuessLanguageByLetters(text);
        if (source.empty()) source = L"EN";
        if (source == PrimaryLang(targetLang)) {  // nothing to do
            r.ok = true;
            r.text = text;
            r.detectedSource = source;
            return r;
        }

        // MyMemory takes at most 500 bytes per query: split long texts at spaces.
        std::vector<std::wstring> chunks;
        std::wstring current;
        for (const Span& w : SplitKeepingSpaces(text)) {
            const std::wstring piece = text.substr(w.start, w.length);
            if (!current.empty() && ToUtf8(current + piece).size() > 440) {
                chunks.push_back(current);
                current.clear();
            }
            current += piece;
        }
        if (!current.empty()) chunks.push_back(current);

        for (const std::wstring& chunk : chunks) {
            const HttpResponse http = HttpsRequest(L"GET", L"api.mymemory.translated.net",
                                                   BuildMyMemoryPath(Trim(chunk), source, targetLang, email_), L"", "");
            if (!http.transportOk) {
                r.error = L"MyMemory: " + http.error;
                return r;
            }
            const MyMemoryParsed p = ParseMyMemoryResponse(http.body);
            if (!p.ok) {
                r.error = p.quotaExceeded ? p.error + L" – " + Tr(L"add a DeepL key or an LLM in the settings") : p.error;
                r.quotaExceeded = p.quotaExceeded;
                return r;
            }
            if (!r.text.empty()) r.text += L" ";
            r.text += p.text;
        }
        r.ok = true;
        r.detectedSource = source;
        return r;
    }

private:
    // Tokens of the text, each word together with the spaces after it.
    static std::vector<Span> SplitKeepingSpaces(const std::wstring& s) {
        std::vector<Span> out;
        size_t i = 0;
        while (i < s.size()) {
            const size_t start = i;
            while (i < s.size() && s[i] != L' ') ++i;
            while (i < s.size() && s[i] == L' ') ++i;
            out.push_back({start, i - start});
        }
        return out;
    }

    const std::wstring email_;
};

}  // namespace

std::shared_ptr<Translator> MakeMyMemoryTranslator(const std::wstring& email) {
    return std::make_shared<MyMemoryTranslator>(email);
}

// ===========================================================================
// LLM (OpenAI-compatible)
// ===========================================================================
std::wstring NormalizeLlmUrl(const std::wstring& in) {
    std::wstring u = Trim(in);
    if (u.empty()) return L"http://localhost:11434/v1/chat/completions";
    while (!u.empty() && u.back() == L'/') u.pop_back();
    if (u.find(L"/chat/completions") != std::wstring::npos) return u;
    const size_t scheme = u.find(L"://");
    const size_t pathStart = scheme == std::wstring::npos ? std::wstring::npos : u.find(L'/', scheme + 3);
    if (pathStart == std::wstring::npos) return u + L"/v1/chat/completions";  // bare host:port
    return u + L"/chat/completions";                                           // ".../v1"
}

namespace {

class OpenAiCompatibleTranslator final : public LlmTranslator {
public:
    explicit OpenAiCompatibleTranslator(LlmSettings s) : s_(std::move(s)) { s_.url = NormalizeLlmUrl(s_.url); }

    std::wstring Name() const override { return L"LLM (" + s_.model + L")"; }

    TranslateResult Translate(const std::vector<Segment>& segments, const std::wstring& sourceLang,
                              const std::wstring& targetLang) override {
        return TranslateBatch({segments}, sourceLang, targetLang)[0];
    }

    std::vector<TranslateResult> TranslateBatch(const std::vector<std::vector<Segment>>& items, const std::wstring&,
                                                const std::wstring& targetLang) override {
        return Batch(items, targetLang, false);
    }

    std::vector<TranslateResult> TranslateOcrBatch(const std::vector<std::vector<Segment>>& items,
                                                   const std::wstring& targetLang) override {
        return Batch(items, targetLang, true);
    }

    std::vector<TranslateResult> Batch(const std::vector<std::vector<Segment>>& items, const std::wstring& targetLang,
                                       bool fromOcr) {
        std::vector<TranslateResult> out(items.size());
        constexpr size_t kChunk = 16;  // keep prompts small for local models
        for (size_t begin = 0; begin < items.size(); begin += kChunk) {
            const size_t end = std::min(items.size(), begin + kChunk);
            const std::vector<std::vector<Segment>> part(items.begin() + begin, items.begin() + end);
            const LlmParsed p =
                Call(BuildLlmRequest(part, LanguageEnglishName(targetLang), s_.model, fromOcr), part.size());
            if (!p.ok && p.formatError && part.size() > 1) {
                // Small local models sometimes merge or drop lines in a batch:
                // ask once per line instead of losing the whole batch.
                for (size_t i = begin; i < end; ++i) {
                    const LlmParsed one =
                        Call(BuildLlmRequest({items[i]}, LanguageEnglishName(targetLang), s_.model, fromOcr), 1);
                    out[i].ok = one.ok;
                    if (one.ok) out[i].text = one.texts[0];
                    else out[i].error = one.error;
                }
                continue;
            }
            for (size_t i = begin; i < end; ++i) {
                out[i].ok = p.ok;
                if (p.ok) out[i].text = p.texts[i - begin];
                else out[i].error = p.error;
            }
        }
        return out;
    }

    TranslateResult Romanize(const std::wstring& text) override {
        TranslateResult r;
        const LlmParsed p = Call(BuildLlmRomanizeRequest(text, s_.model), 1);
        r.ok = p.ok;
        if (p.ok) r.text = p.texts[0];
        else r.error = p.error;
        return r;
    }

private:
    LlmParsed Call(const std::string& body, size_t expected) {
        std::wstring headers = L"Content-Type: application/json\r\n";
        if (!Trim(s_.apiKey).empty()) headers += L"Authorization: Bearer " + Trim(s_.apiKey) + L"\r\n";
        const HttpResponse http = HttpRequestUrl(L"POST", s_.url, headers, body, s_.timeoutMs);
        LlmParsed p;
        if (!http.transportOk) {
            p.error = TrF(L"LLM not reachable ({1}): {2}", {s_.url, http.error});
            return p;
        }
        p = ParseLlmResponse(http.body, expected);
        if (http.status != 200 && p.error.empty()) {
            p.ok = false;
            p.error = L"LLM: HTTP " + std::to_wstring(http.status);
        }
        if (http.status == 401 || http.status == 403) p.error = L"LLM: " + Tr(L"API key is invalid");
        if (http.status == 404 && p.error.find(L"model") == std::wstring::npos)
            p.error = L"LLM: " + TrF(L"address or model “{1}” not found", {s_.model});
        return p;
    }

    LlmSettings s_;
};

}  // namespace

std::shared_ptr<LlmTranslator> MakeLlmTranslator(const LlmSettings& settings) {
    return std::make_shared<OpenAiCompatibleTranslator>(settings);
}

namespace {

// "http://host:port/v1/chat/completions" -> "http://host:port"
std::wstring HostRoot(const std::wstring& url) {
    const size_t scheme = url.find(L"://");
    const size_t path = scheme == std::wstring::npos ? std::wstring::npos : url.find(L'/', scheme + 3);
    return path == std::wstring::npos ? url : url.substr(0, path);
}

}  // namespace

std::vector<std::wstring> FetchLlmModels(const std::wstring& url, const std::wstring& apiKey, std::wstring* error) {
    const std::wstring full = NormalizeLlmUrl(url);
    std::wstring base = full.substr(0, full.rfind(L"/chat/completions"));  // ".../v1"
    std::wstring headers;
    if (!Trim(apiKey).empty()) headers = L"Authorization: Bearer " + Trim(apiKey) + L"\r\n";
    std::wstring lastError;
    for (const std::wstring& u : {base + L"/models", HostRoot(full) + L"/api/tags"}) {
        const HttpResponse http = HttpRequestUrl(L"GET", u, headers, "", 8000);
        if (!http.transportOk) {
            lastError = http.error;
            continue;
        }
        if (http.status == 401 || http.status == 403) {
            lastError = Tr(L"API key is invalid");
            continue;
        }
        if (http.status != 200) {
            lastError = L"HTTP " + std::to_wstring(http.status);
            continue;
        }
        std::vector<std::wstring> models = ParseModelList(http.body);
        if (!models.empty()) return models;
        lastError = Tr(L"the server offers no models");
    }
    if (error) *error = lastError.empty() ? Tr(L"not reachable") : lastError;
    return {};
}

// Sizes are what Ollama downloads; memory needs are rough (the model plus its
// working memory) and the game wants the graphics card too.
const std::vector<LocalModelOffer>& LocalModelOffers() {
    static const std::vector<LocalModelOffer> offers = {
        {L"gemma3:1b", L"Very fast · 0.8 GB download · runs on the CPU or with ~2 GB VRAM · good for short chat lines"},
        {L"gemma3:4b", L"Balanced · 3.3 GB download · ~4–6 GB VRAM · 140 languages"},
        {L"aya-expanse:8b", L"Best translations · 5 GB download · 8 GB VRAM recommended · made for translating, "
                            L"23 languages incl. Arabic, Turkish, Russian"},
    };
    return offers;
}

static std::wstring OllamaRoot(const std::wstring& llmUrl) {
    return Trim(llmUrl).empty() ? std::wstring(L"http://localhost:11434") : HostRoot(NormalizeLlmUrl(llmUrl));
}

bool OllamaReachable(const std::wstring& llmUrl) {
    const HttpResponse http = HttpRequestUrl(L"GET", OllamaRoot(llmUrl) + L"/api/version", L"", "", 3000);
    return http.transportOk && http.status == 200;
}

bool PullOllamaModel(const std::wstring& llmUrl, const std::wstring& model, std::wstring* error) {
    const std::string body = "{\"model\":\"" + ToUtf8(model) + "\",\"stream\":false}";
    const HttpResponse http = HttpRequestUrl(L"POST", OllamaRoot(llmUrl) + L"/api/pull",
                                             L"Content-Type: application/json\r\n", body, 60 * 60 * 1000);
    if (!http.transportOk) {
        if (error) *error = http.error;
        return false;
    }
    if (http.status != 200 || http.body.find("success") == std::string::npos) {
        if (error) *error = L"HTTP " + std::to_wstring(http.status) + L": " + FromUtf8(http.body.substr(0, 200));
        return false;
    }
    return true;
}

LtResult CheckWithLanguageTool(const std::wstring& serverUrl, const std::wstring& text, const std::wstring& lang,
                               const std::wstring& motherTongue) {
    const HttpResponse http =
        HttpRequestUrl(L"POST", NormalizeLanguageToolUrl(serverUrl),
                       L"Content-Type: application/x-www-form-urlencoded\r\nAccept: application/json\r\n",
                       BuildLanguageToolForm(text, lang, motherTongue), 8000);
    LtResult r;
    if (!http.transportOk) {
        r.error = L"LanguageTool: " + http.error;
        return r;
    }
    if (http.status == 429) {
        r.error = L"LanguageTool: " + Tr(L"too many requests – the public server allows 20 per minute");
        return r;
    }
    if (http.status != 200) {
        r.error = L"LanguageTool: HTTP " + std::to_wstring(http.status);
        return r;
    }
    r = ParseLanguageToolResponse(http.body, text);
    if (!r.ok) r.error = L"LanguageTool: " + r.error;
    return r;
}

}  // namespace gct
