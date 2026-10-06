// mymemory_protocol.cpp
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
    q += "&mt=1";
    if (!Trim(email).empty()) q += "&de=" + UrlEncode(ToUtf8(Trim(email)));
    return FromUtf8(q);
}

MyMemoryParsed ParseMyMemoryResponse(const std::string& body) {
    MyMemoryParsed r;
    JsonValue root;
    if (!ParseJson(body, root) || !root.IsObject()) {
        r.error = L"Antwort von MyMemory nicht lesbar";
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
        r.error = L"Tageskontingent von MyMemory aufgebraucht";
        return r;
    }
    if (status != 200 || text.empty()) {
        r.error = L"MyMemory: " + (details.empty() ? L"Fehler " + std::to_wstring(status) : details);
        return r;
    }
    r.ok = true;
    r.text = text;
    return r;
}

}  // namespace gct
