// mymemory_protocol.cpp
#include "i18n.hpp"
#include "mymemory_protocol.hpp"

#include <cstdlib>

#include "json.hpp"
#include "langs.hpp"
#include "text.hpp"

namespace gct {

std::string UrlEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

std::wstring MyMemoryLang(const std::wstring& code) {
    const std::wstring c = ToUpperAscii(Trim(code));
    if (c == L"ZH-HANT" || c == L"ZH-TW") return L"zh-TW";
    if (PrimaryLang(c) == L"ZH") return L"zh-CN";
    if (c == L"NB") return L"no";
    const size_t dash = c.find(L'-');
    if (dash == std::wstring::npos) return ToLowerAscii(c);
    return ToLowerAscii(c.substr(0, dash)) + L"-" + c.substr(dash + 1);
}

std::wstring BuildMyMemoryPath(const std::wstring& text, const std::wstring& sourceLang, const std::wstring& targetLang,
                               const std::wstring& email) {
    std::string q = "/get?q=" + UrlEncode(ToUtf8(text));
    q += "&langpair=" + UrlEncode(ToUtf8(MyMemoryLang(sourceLang) + L"|" + MyMemoryLang(targetLang)));
    // Its machine translation only (onlyprivate without a key: no public memory). The public memory answered with the
    // translations of other sentences – misaligned corpora rated quality 0: "ah alles klar" -> "All these great people.",
    // "Wer kommt mit?" -> "Seen it?" (user, 2026-10-22).
    q += "&mt=1&onlyprivate=1";
    if (!Trim(email).empty()) q += "&de=" + UrlEncode(ToUtf8(Trim(email)));
    return FromUtf8(q);
}

MyMemoryParsed ParseMyMemoryResponse(const std::string& body) {
    MyMemoryParsed r;
    JsonValue root;
    if (!ParseJson(body, root) || !root.IsObject()) {
        r.error = Tr(L"Answer from MyMemory not readable");
        r.errorKind = TranslateErrorKind::BadOutput;
        return r;
    }
    int status = 0;
    if (const JsonValue* s = root.Get("responseStatus")) {
        if (s->type == JsonValue::Type::Number) status = static_cast<int>(s->n);
        else if (s->type == JsonValue::Type::String) status = std::atoi(s->s.c_str());
    }
    const JsonValue* data = root.Get("responseData");
    const std::wstring text = data ? FromUtf8(data->GetString("translatedText")) : std::wstring();
    const std::wstring details = FromUtf8(root.GetString("responseDetails"));
    const bool warning = text.rfind(L"MYMEMORY WARNING", 0) == 0 || details.rfind(L"MYMEMORY WARNING", 0) == 0;

    if (root.GetBool("quotaFinished") || status == 429 || warning) {
        r.quotaExceeded = true;
        r.error = Tr(L"MyMemory daily quota is used up");
        r.errorKind = TranslateErrorKind::Quota;
        return r;
    }
    if (status != 200 || text.empty()) {
        r.error = L"MyMemory: " + (details.empty() ? TrF(L"error {1}", {std::to_wstring(status)}) : details);
        if (status == 401 || status == 403) r.errorKind = TranslateErrorKind::Auth;
        else if (details.find(L"INVALID") != std::wstring::npos || details.find(L"PAIR") != std::wstring::npos ||
                 details.find(L"NOT SUPPORTED") != std::wstring::npos || details.find(L"not supported") != std::wstring::npos)
            r.errorKind = TranslateErrorKind::UnsupportedPair;
        else r.errorKind = TranslateErrorKind::Other;
        return r;
    }
    r.ok = true;
    r.text = text;
    r.errorKind = TranslateErrorKind::None;
    return r;
}

}  // namespace gct
