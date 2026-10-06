// online_translators.cpp
#include "online_translators.hpp"

#include <algorithm>

#include "core/json.hpp"
#include "core/langs.hpp"
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

    std::wstring Name() const override { return L"MyMemory (Basis)"; }

    TranslateResult Translate(const std::vector<Segment>& segments, const std::wstring& sourceLang,
                              const std::wstring& targetLang) override {
        TranslateResult r;
        const std::wstring text = JoinSegments(segments);
        std::wstring source = sourceLang.empty() ? DetectLanguage(text) : PrimaryLang(sourceLang);
        if (source.empty()) source = L"EN";  // most common in GW2 chat
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
                r.error = p.quotaExceeded ? p.error + L" \u2013 DeepL-Key oder LLM in der ini eintragen" : p.error;
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
        std::vector<TranslateResult> out(items.size());
        constexpr size_t kChunk = 16;  // keep prompts small for local models
        for (size_t begin = 0; begin < items.size(); begin += kChunk) {
            const size_t end = std::min(items.size(), begin + kChunk);
            const std::vector<std::vector<Segment>> part(items.begin() + begin, items.begin() + end);
            const LlmParsed p = Call(BuildLlmRequest(part, LanguageEnglishName(targetLang), s_.model), part.size());
            if (!p.ok && p.formatError && part.size() > 1) {
                // Small local models sometimes merge or drop lines in a batch:
                // ask once per line instead of losing the whole batch.
                for (size_t i = begin; i < end; ++i) {
                    const LlmParsed one =
                        Call(BuildLlmRequest({items[i]}, LanguageEnglishName(targetLang), s_.model), 1);
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
            p.error = L"LLM nicht erreichbar (" + s_.url + L"): " + http.error;
            return p;
        }
        p = ParseLlmResponse(http.body, expected);
        if (http.status != 200 && p.error.empty()) {
            p.ok = false;
            p.error = L"LLM: HTTP " + std::to_wstring(http.status);
        }
        if (http.status == 401 || http.status == 403) p.error = L"LLM: API-Key ung\u00fcltig";
        if (http.status == 404 && p.error.find(L"model") == std::wstring::npos)
            p.error = L"LLM: Adresse oder Modell \u201e" + s_.model + L"\u201c nicht gefunden";
        return p;
    }

    LlmSettings s_;
};

}  // namespace

std::shared_ptr<LlmTranslator> MakeLlmTranslator(const LlmSettings& settings) {
    return std::make_shared<OpenAiCompatibleTranslator>(settings);
}

}  // namespace gct
