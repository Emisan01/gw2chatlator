// deepl_translator.cpp — POST /v2/translate (single texts and batches).
#include "deepl_translator.hpp"

#include "core/deepl_protocol.hpp"
#include "core/text.hpp"
#include "http.hpp"

namespace gct {
namespace {

class DeepLTranslator final : public Translator {
public:
    explicit DeepLTranslator(std::wstring key) : key_(Trim(key)) {}

    std::wstring Name() const override { return L"DeepL"; }

    TranslateResult Translate(const std::vector<Segment>& segments, const std::wstring& sourceLang,
                              const std::wstring& targetLang) override {
        return TranslateBatch({segments}, sourceLang, targetLang)[0];
    }

    std::vector<TranslateResult> TranslateBatch(const std::vector<std::vector<Segment>>& items,
                                                const std::wstring& sourceLang,
                                                const std::wstring& targetLang) override {
        std::vector<TranslateResult> out(items.size());
        if (items.empty()) return out;
        auto failAll = [&](const std::wstring& error) {
            for (auto& r : out) r.error = error;
            return out;
        };

        const DeepLPayload payload = BuildDeepLPayloadBatch(items, SourceOnly(sourceLang), targetLang);
        const std::wstring host = IsDeepLFreeKey(key_) ? L"api-free.deepl.com" : L"api.deepl.com";
        const std::wstring headers =
            L"Authorization: DeepL-Auth-Key " + key_ + L"\r\nContent-Type: application/json\r\n";

        const HttpResponse http = HttpsRequest(L"POST", host, L"/v2/translate", headers, payload.json);
        if (!http.transportOk) return failAll(L"DeepL: " + http.error);

        const auto parsed = ParseDeepLResponseBatch(http.body, payload.xmlMode, items.size());
        if (http.status == 200) {
            for (size_t i = 0; i < items.size(); ++i) {
                out[i].ok = parsed[i].ok;
                out[i].text = parsed[i].text;
                out[i].detectedSource = parsed[i].detectedSource;
                out[i].error = parsed[i].error;
            }
            return out;
        }

        const std::wstring msg = parsed.empty() ? std::wstring() : parsed[0].error;
        const std::wstring code = L" (HTTP " + std::to_wstring(http.status) + L")";
        switch (http.status) {
            case 401:
            case 403: return failAll(L"DeepL-Key ung\u00fcltig");
            case 456:
                for (auto& r : out) r.quotaExceeded = true;
                return failAll(L"DeepL-Kontingent f\u00fcr diesen Monat aufgebraucht");
            case 429: return failAll(L"DeepL: zu viele Anfragen \u2013 kurz warten");
            case 400: return failAll(L"DeepL lehnt ab: " + msg);
            default:
                return failAll(http.status >= 500 ? L"DeepL gerade gest\u00f6rt" + code : L"DeepL: " + msg + code);
        }
    }

private:
    // DeepL wants plain codes as source ("EN", not "EN-GB").
    static std::wstring SourceOnly(const std::wstring& code) {
        const std::wstring c = ToUpperAscii(Trim(code));
        const size_t dash = c.find(L'-');
        return dash == std::wstring::npos ? c : c.substr(0, dash);
    }

    const std::wstring key_;
};

}  // namespace

std::shared_ptr<Translator> MakeDeepLTranslator(const std::wstring& apiKey) {
    return std::make_shared<DeepLTranslator>(apiKey);
}

}  // namespace gct
