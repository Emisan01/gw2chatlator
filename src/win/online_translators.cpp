// online_translators.cpp
#include "core/i18n.hpp"
#include "online_translators.hpp"

#include <algorithm>

#include "core/cloud_mt_protocol.hpp"
#include "core/json.hpp"
#include "core/langs.hpp"
#include "core/languages.hpp"
#include "core/languagetool_protocol.hpp"
#include "core/mymemory_protocol.hpp"
#include "core/text.hpp"
#include "core/translation_check.hpp"
#include "els.hpp"
#include "http.hpp"

#include <windows.h>

namespace gct {

// ===========================================================================
// MyMemory
// ===========================================================================
namespace {

class MyMemoryTranslator final : public Translator {
public:
    explicit MyMemoryTranslator(std::wstring email) : email_(Trim(email)) {}

    std::wstring Name() const override { return Tr(L"MyMemory (free)"); }  // as the translator lists call it

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

        // MyMemory has no way to leave parts alone: the whole text first (the sentence as context); when it changed a
        // protected part ("LFG", a name, a chat code), only the parts around them go – costs more of the contingent,
        // so only then.
        std::wstring whole;
        if (!TranslateText(text, source, targetLang, whole, r)) return r;
        if (HasProtected(segments) && !MissingProtected(segments, whole).empty()) {
            std::wstring rebuilt;
            for (const Segment& s : segments) {
                if (s.keep || std::none_of(s.text.begin(), s.text.end(), IsWordChar)) {
                    rebuilt += s.text;
                    continue;
                }
                const size_t b = s.text.find_first_not_of(L' '), e = s.text.find_last_not_of(L' ');
                std::wstring part;
                if (!TranslateText(s.text.substr(b, e - b + 1), source, targetLang, part, r)) return r;
                rebuilt += s.text.substr(0, b) + part + s.text.substr(e + 1);
            }
            whole = std::move(rebuilt);
        } else if (HasProtected(segments)) {
            whole = RestoreKeepSpelling(std::move(whole), segments);
        }
        r.text = std::move(whole);
        r.ok = true;
        r.detectedSource = source;
        r.errorKind = TranslateErrorKind::None;
        return r;
    }

private:
    // One text through MyMemory: at most 500 bytes per query, so longer texts go in whole sentences that fit (a word
    // boundary only for a single sentence longer than that).
    bool TranslateText(const std::wstring& text, const std::wstring& source, const std::wstring& target, std::wstring& out,
                       TranslateResult& r) {
        std::vector<std::wstring> chunks;
        std::wstring current;
        const auto add = [&](const std::wstring& piece) {
            if (!current.empty() && ToUtf8(current + piece).size() > 440) {
                chunks.push_back(current);
                current.clear();
            }
            current += piece;
        };
        for (const Sentence& s : SplitSentences(text)) {
            const std::wstring sentence = s.text + s.gap;
            if (ToUtf8(sentence).size() <= 440) {
                add(sentence);
                continue;
            }
            for (const Span& w : SplitKeepingSpaces(sentence)) add(sentence.substr(w.start, w.length));
        }
        if (!current.empty()) chunks.push_back(current);

        out.clear();
        for (const std::wstring& chunk : chunks) {
            const HttpResponse http = HttpsRequest(L"GET", L"api.mymemory.translated.net",
                                                   BuildMyMemoryPath(Trim(chunk), source, target, email_), L"", "");
            if (!http.transportOk) {
                r.error = L"MyMemory: " + http.error;
                r.errorKind = (http.status == 408 || http.status == 504) ? TranslateErrorKind::Timeout : TranslateErrorKind::Network;
                return false;
            }
            const MyMemoryParsed p = ParseMyMemoryResponse(http.body);
            if (!p.ok) {
                r.error = p.quotaExceeded ? p.error + L" – " + Tr(L"choose another translator in the settings (Google, Microsoft, DeepL, AI model)") : p.error;
                r.quotaExceeded = p.quotaExceeded;
                r.errorKind = p.errorKind;
                return false;
            }
            if (!out.empty()) out += L" ";
            out += p.text;
        }
        return true;
    }

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
// Google Cloud Translation (v2, API key) and Microsoft Translator (v3)
// ===========================================================================
namespace {

TranslateResult FromParsed(const CloudMtParsed& p) {
    TranslateResult r;
    r.ok = p.ok;
    r.text = p.text;
    r.detectedSource = p.detectedSource;
    r.error = p.error;
    r.errorKind = p.errorKind;
    return r;
}

// Shared HTTP status handling of both services.
std::vector<TranslateResult> CloudFailure(const wchar_t* service, int status, const std::wstring& parsedError,
                                          size_t n) {
    std::vector<TranslateResult> out(n);
    std::wstring e;
    TranslateErrorKind k = TranslateErrorKind::Other;
    if (status == 400 && parsedError.find(L"key") != std::wstring::npos) {
        e = TrF(L"{1}: API key is invalid", {service});
        k = TranslateErrorKind::Auth;
    } else if (status == 401 || status == 403) {
        e = TrF(L"{1}: API key is invalid", {service});
        k = TranslateErrorKind::Auth;
    } else if (status == 429) {
        e = TrF(L"{1}: free contingent used up or too many requests", {service});
        k = TranslateErrorKind::Quota;
    } else if (status == 408 || status == 504) {
        e = std::wstring(service) + L": timeout";
        k = TranslateErrorKind::Timeout;
    } else if (status == 0) {
        e = std::wstring(service) + L": network error";
        k = TranslateErrorKind::Network;
    } else {
        e = parsedError.empty() ? std::wstring(service) + L": HTTP " + std::to_wstring(status) : parsedError;
        k = TranslateErrorKind::Other;
    }
    for (auto& r : out) {
        r.error = e;
        r.quotaExceeded = (status == 429);
        r.errorKind = k;
    }
    return out;
}

class GoogleTranslator final : public Translator {
public:
    explicit GoogleTranslator(std::wstring key) : key_(Trim(key)) {}
    std::wstring Name() const override { return L"Google"; }

    TranslateResult Translate(const std::vector<Segment>& segments, const std::wstring& sourceLang,
                              const std::wstring& targetLang) override {
        return TranslateBatch({segments}, sourceLang, targetLang)[0];
    }

    std::vector<TranslateResult> TranslateBatch(const std::vector<std::vector<Segment>>& items,
                                                const std::wstring& sourceLang,
                                                const std::wstring& targetLang) override {
        if (items.empty()) return {};
        const CloudMtPayload p = BuildGooglePayload(items, sourceLang, targetLang);
        // The key goes in a header, never into the address.
        const std::wstring headers = L"X-Goog-Api-Key: " + key_ + L"\r\nContent-Type: application/json\r\n";
        const HttpResponse http =
            HttpsRequest(L"POST", L"translation.googleapis.com", L"/language/translate/v2", headers, p.json);
        if (!http.transportOk) return CloudFailure(L"Google", 0, L"Google: " + http.error, items.size());
        const auto parsed = ParseGoogleResponse(http.body, p.html, items.size());
        if (http.status != 200) return CloudFailure(L"Google", http.status, parsed[0].error, items.size());
        std::vector<TranslateResult> out;
        for (const auto& x : parsed) out.push_back(FromParsed(x));
        return out;
    }

private:
    const std::wstring key_;
};

class MicrosoftTranslator final : public Translator {
public:
    MicrosoftTranslator(std::wstring key, std::wstring region) : key_(Trim(key)), region_(Trim(region)) {}
    std::wstring Name() const override { return L"Microsoft"; }

    TranslateResult Translate(const std::vector<Segment>& segments, const std::wstring& sourceLang,
                              const std::wstring& targetLang) override {
        return TranslateBatch({segments}, sourceLang, targetLang)[0];
    }

    std::vector<TranslateResult> TranslateBatch(const std::vector<std::vector<Segment>>& items,
                                                const std::wstring& sourceLang,
                                                const std::wstring& targetLang) override {
        if (items.empty()) return {};
        const CloudMtPayload p = BuildMicrosoftPayload(items);
        std::wstring headers = L"Ocp-Apim-Subscription-Key: " + key_ + L"\r\nContent-Type: application/json\r\n";
        if (!region_.empty()) headers += L"Ocp-Apim-Subscription-Region: " + region_ + L"\r\n";
        const HttpResponse http = HttpsRequest(L"POST", L"api.cognitive.microsofttranslator.com",
                                               MicrosoftQuery(sourceLang, targetLang, p.html), headers, p.json);
        if (!http.transportOk) return CloudFailure(L"Microsoft", 0, L"Microsoft: " + http.error, items.size());
        const auto parsed = ParseMicrosoftResponse(http.body, p.html, items.size());
        if (http.status != 200) return CloudFailure(L"Microsoft", http.status, parsed[0].error, items.size());
        std::vector<TranslateResult> out;
        for (const auto& x : parsed) out.push_back(FromParsed(x));
        return out;
    }

private:
    const std::wstring key_, region_;
};

class SupertextTranslator final : public Translator {
public:
    explicit SupertextTranslator(std::wstring key) : key_(Trim(key)) {}
    std::wstring Name() const override { return L"Supertext"; }

    TranslateResult Translate(const std::vector<Segment>& segments, const std::wstring& sourceLang,
                              const std::wstring& targetLang) override {
        return TranslateBatch({segments}, sourceLang, targetLang)[0];
    }

    std::vector<TranslateResult> TranslateBatch(const std::vector<std::vector<Segment>>& items,
                                                const std::wstring& sourceLang,
                                                const std::wstring& targetLang) override {
        if (items.empty()) return {};
        const CloudMtPayload p = BuildSupertextPayload(items, sourceLang, targetLang);
        const std::wstring headers = L"Authorization: Supertext-Auth-Key " + key_ + L"\r\nContent-Type: application/json\r\n";
        const HttpResponse http = HttpsRequest(L"POST", L"api.supertext.com", L"/v1/translate/ai/text", headers, p.json);
        if (!http.transportOk) return CloudFailure(L"Supertext", 0, L"Supertext: " + http.error, items.size());
        const auto parsed = ParseSupertextResponse(http.body, p.html, items.size());
        if (http.status != 200) {
            auto failed = CloudFailure(L"Supertext", http.status, parsed[0].error, items.size());
            if (parsed[0].errorKind == TranslateErrorKind::UnsupportedPair)
                for (auto& r : failed) r.errorKind = TranslateErrorKind::UnsupportedPair;
            return failed;
        }
        std::vector<TranslateResult> out;
        for (const auto& x : parsed) out.push_back(FromParsed(x));
        return out;
    }

private:
    const std::wstring key_;
};

class LibreTranslator final : public Translator {
public:
    LibreTranslator(std::wstring url, std::wstring key) : url_(LibreTranslateUrl(url)), key_(Trim(key)) {}
    std::wstring Name() const override { return L"LibreTranslate"; }

    TranslateResult Translate(const std::vector<Segment>& segments, const std::wstring& sourceLang,
                              const std::wstring& targetLang) override {
        return TranslateBatch({segments}, sourceLang, targetLang)[0];
    }

    std::vector<TranslateResult> TranslateBatch(const std::vector<std::vector<Segment>>& items,
                                                const std::wstring& sourceLang,
                                                const std::wstring& targetLang) override {
        if (items.empty()) return {};
        const CloudMtPayload p = BuildLibrePayload(items, sourceLang, targetLang, key_);
        const HttpResponse http = HttpRequestUrl(L"POST", url_, L"Content-Type: application/json\r\n", p.json, 20000);
        if (!http.transportOk)
            return CloudFailure(L"LibreTranslate", 0, TrF(L"Server not reachable ({1}): {2}", {url_, http.error}),
                                items.size());
        const auto parsed = ParseLibreResponse(http.body, p.html, items.size());
        if (http.status != 200) return CloudFailure(L"LibreTranslate", http.status, parsed[0].error, items.size());
        std::vector<TranslateResult> out;
        for (const auto& x : parsed) out.push_back(FromParsed(x));
        return out;
    }

private:
    const std::wstring url_, key_;
};

}  // namespace

std::shared_ptr<Translator> MakeLibreTranslator(const std::wstring& url, const std::wstring& apiKey) {
    return std::make_shared<LibreTranslator>(url, apiKey);
}

std::shared_ptr<Translator> MakeGoogleTranslator(const std::wstring& apiKey) {
    return std::make_shared<GoogleTranslator>(apiKey);
}

std::shared_ptr<Translator> MakeSupertextTranslator(const std::wstring& apiKey) {
    return std::make_shared<SupertextTranslator>(apiKey);
}

std::shared_ptr<Translator> MakeMicrosoftTranslator(const std::wstring& apiKey, const std::wstring& region) {
    return std::make_shared<MicrosoftTranslator>(apiKey, region);
}

LtResult CheckWithLanguageTool(const std::wstring& serverUrl, const std::wstring& text, const std::wstring& lang,
                               const std::wstring& motherTongue) {
    const HttpResponse http =
        HttpRequestUrl(L"POST", NormalizeLanguageToolUrl(serverUrl),
                       L"Content-Type: application/x-www-form-urlencoded\r\nAccept: application/json\r\n",
                       BuildLanguageToolForm(text, lang, motherTongue), 3000);
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
