// cloud_mt_protocol.cpp
#include "cloud_mt_protocol.hpp"

#include <cwchar>

#include "i18n.hpp"
#include "json.hpp"
#include "languages.hpp"
#include "text.hpp"

namespace gct {

namespace {

constexpr wchar_t kOpen[] = L"<span translate=\"no\" class=\"notranslate\">";
constexpr wchar_t kClose[] = L"</span>";

std::wstring HtmlEscape(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size());
    for (wchar_t c : s) {
        switch (c) {
            case L'&': out += L"&amp;"; break;
            case L'<': out += L"&lt;"; break;
            case L'>': out += L"&gt;"; break;
            default: out += c;
        }
    }
    return out;
}

std::wstring Encode(const std::vector<Segment>& segments, bool html) {
    std::wstring t;
    for (const Segment& s : segments) {
        if (!html) t += s.text;
        else if (s.keep) t += kOpen + HtmlEscape(s.text) + kClose;
        else t += HtmlEscape(s.text);
    }
    return t;
}

bool AnyProtected(const std::vector<std::vector<Segment>>& items) {
    for (const auto& item : items)
        if (HasProtected(item)) return true;
    return false;
}

std::wstring Primary(const std::wstring& code) {
    const std::wstring c = ToUpperAscii(Trim(code));
    const size_t dash = c.find(L'-');
    return dash == std::wstring::npos ? c : c.substr(0, dash);
}

std::wstring Lower(std::wstring s) {
    for (wchar_t& c : s)
        if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c - L'A' + L'a');
    return s;
}

}  // namespace

std::wstring GoogleLang(const std::wstring& code) {
    const std::wstring c = ToUpperAscii(Trim(code));
    if (c.empty()) return L"";
    if (c == L"ZH-HANT" || c == L"ZH-TW" || c == L"ZH-HK") return L"zh-TW";
    if (Primary(c) == L"ZH") return L"zh-CN";
    if (c == L"PT-PT") return L"pt-PT";
    if (Primary(c) == L"NB" || Primary(c) == L"NN") return L"no";
    return Lower(Primary(c));
}

std::wstring MicrosoftLang(const std::wstring& code) {
    const std::wstring c = ToUpperAscii(Trim(code));
    if (c.empty()) return L"";
    if (c == L"ZH-HANT" || c == L"ZH-TW" || c == L"ZH-HK") return L"zh-Hant";
    if (Primary(c) == L"ZH") return L"zh-Hans";
    if (c == L"PT-PT") return L"pt-PT";
    if (Primary(c) == L"NO" || Primary(c) == L"NN") return L"nb";
    return Lower(Primary(c));
}

CloudMtPayload BuildGooglePayload(const std::vector<std::vector<Segment>>& items, const std::wstring& sourceLang,
                                  const std::wstring& targetLang) {
    CloudMtPayload p;
    p.html = AnyProtected(items);
    std::string j = "{\"q\":[";
    for (size_t i = 0; i < items.size(); ++i) {
        if (i) j += ",";
        j += "\"" + JsonEscape(ToUtf8(Encode(items[i], p.html))) + "\"";
    }
    j += "],\"target\":\"" + JsonEscape(ToUtf8(GoogleLang(targetLang))) + "\"";
    const std::wstring src = GoogleLang(sourceLang);
    if (!src.empty()) j += ",\"source\":\"" + JsonEscape(ToUtf8(src)) + "\"";
    j += p.html ? ",\"format\":\"html\"}" : ",\"format\":\"text\"}";
    p.json = std::move(j);
    return p;
}

std::vector<CloudMtParsed> ParseGoogleResponse(const std::string& body, bool html, size_t expected) {
    std::vector<CloudMtParsed> out(expected);
    auto failAll = [&](const std::wstring& e) {
        for (auto& r : out) r.error = e;
        return out;
    };
    JsonValue root;
    if (!ParseJson(body, root)) return failAll(Tr(L"Answer from Google not readable"));
    const JsonValue* data = root.Get("data");
    const JsonValue* tr = data ? data->Get("translations") : nullptr;
    if (!tr || !tr->IsArray() || tr->arr.size() != expected) {
        const JsonValue* err = root.Get("error");
        const std::string msg = err ? err->GetString("message") : std::string();
        return failAll(msg.empty() ? Tr(L"Unexpected answer from Google") : L"Google: " + FromUtf8(msg));
    }
    for (size_t i = 0; i < expected; ++i) {
        const JsonValue* t = tr->arr[i].Get("translatedText");
        if (!t || t->type != JsonValue::Type::String) {
            out[i].error = Tr(L"Unexpected answer from Google");
            continue;
        }
        out[i].ok = true;
        // Google returns HTML entities even for plain text sometimes (&#39;).
        out[i].text = RestoreFromHtml(FromUtf8(t->s));
        (void)html;
        out[i].detectedSource = ToUpperAscii(FromUtf8(tr->arr[i].GetString("detectedSourceLanguage")));
    }
    return out;
}

CloudMtPayload BuildMicrosoftPayload(const std::vector<std::vector<Segment>>& items) {
    CloudMtPayload p;
    p.html = AnyProtected(items);
    std::string j = "[";
    for (size_t i = 0; i < items.size(); ++i) {
        if (i) j += ",";
        j += "{\"Text\":\"" + JsonEscape(ToUtf8(Encode(items[i], p.html))) + "\"}";
    }
    j += "]";
    p.json = std::move(j);
    return p;
}

std::wstring MicrosoftQuery(const std::wstring& sourceLang, const std::wstring& targetLang, bool html) {
    std::wstring q = L"/translate?api-version=3.0&to=" + MicrosoftLang(targetLang);
    const std::wstring src = MicrosoftLang(sourceLang);
    if (!src.empty()) q += L"&from=" + src;
    if (html) q += L"&textType=html";
    return q;
}

std::vector<CloudMtParsed> ParseMicrosoftResponse(const std::string& body, bool html, size_t expected) {
    std::vector<CloudMtParsed> out(expected);
    auto failAll = [&](const std::wstring& e) {
        for (auto& r : out) r.error = e;
        return out;
    };
    JsonValue root;
    if (!ParseJson(body, root)) return failAll(Tr(L"Answer from Microsoft not readable"));
    if (!root.IsArray() || root.arr.size() != expected) {
        const JsonValue* err = root.Get("error");
        const std::string msg = err ? err->GetString("message") : std::string();
        return failAll(msg.empty() ? Tr(L"Unexpected answer from Microsoft") : L"Microsoft: " + FromUtf8(msg));
    }
    for (size_t i = 0; i < expected; ++i) {
        const JsonValue* tr = root.arr[i].Get("translations");
        const JsonValue* t = tr && tr->IsArray() && !tr->arr.empty() ? tr->arr[0].Get("text") : nullptr;
        if (!t || t->type != JsonValue::Type::String) {
            out[i].error = Tr(L"Unexpected answer from Microsoft");
            continue;
        }
        out[i].ok = true;
        out[i].text = html ? RestoreFromHtml(FromUtf8(t->s)) : FromUtf8(t->s);
        if (const JsonValue* d = root.arr[i].Get("detectedLanguage"))
            out[i].detectedSource = ToUpperAscii(FromUtf8(d->GetString("language")));
    }
    return out;
}

std::wstring LibreLang(const std::wstring& code) {
    const std::wstring c = ToUpperAscii(Trim(code));
    if (c.empty()) return L"auto";
    if (c == L"ZH-HANT" || c == L"ZH-TW" || c == L"ZH-HK") return L"zt";
    if (Primary(c) == L"NO" || Primary(c) == L"NN") return L"nb";
    return Lower(Primary(c));
}

CloudMtPayload BuildLibrePayload(const std::vector<std::vector<Segment>>& items, const std::wstring& sourceLang,
                                 const std::wstring& targetLang, const std::wstring& apiKey) {
    CloudMtPayload p;
    p.html = AnyProtected(items);
    std::string j = "{\"q\":[";
    for (size_t i = 0; i < items.size(); ++i) {
        if (i) j += ",";
        j += "\"" + JsonEscape(ToUtf8(Encode(items[i], p.html))) + "\"";
    }
    j += "],\"source\":\"" + JsonEscape(ToUtf8(LibreLang(sourceLang))) + "\"";
    std::wstring target = LibreLang(targetLang);
    if (target == L"auto") target = L"en";
    j += ",\"target\":\"" + JsonEscape(ToUtf8(target)) + "\"";
    j += p.html ? ",\"format\":\"html\"" : ",\"format\":\"text\"";
    if (!Trim(apiKey).empty()) j += ",\"api_key\":\"" + JsonEscape(ToUtf8(Trim(apiKey))) + "\"";
    j += "}";
    p.json = std::move(j);
    return p;
}

std::vector<CloudMtParsed> ParseLibreResponse(const std::string& body, bool html, size_t expected) {
    std::vector<CloudMtParsed> out(expected);
    auto failAll = [&](const std::wstring& e) {
        for (auto& r : out) r.error = e;
        return out;
    };
    JsonValue root;
    if (!ParseJson(body, root)) return failAll(Tr(L"Answer from the translation server not readable"));
    const JsonValue* t = root.Get("translatedText");
    if (!t || !t->IsArray() || t->arr.size() != expected) {
        const std::string msg = root.GetString("error");
        return failAll(msg.empty() ? Tr(L"Unexpected answer from the translation server") : FromUtf8(msg));
    }
    const JsonValue* det = root.Get("detectedLanguage");
    for (size_t i = 0; i < expected; ++i) {
        if (t->arr[i].type != JsonValue::Type::String) {
            out[i].error = Tr(L"Unexpected answer from the translation server");
            continue;
        }
        out[i].ok = true;
        out[i].text = html ? RestoreFromHtml(FromUtf8(t->arr[i].s)) : FromUtf8(t->arr[i].s);
        if (det && det->IsArray() && i < det->arr.size())
            out[i].detectedSource = ToUpperAscii(FromUtf8(det->arr[i].GetString("language")));
    }
    return out;
}

std::wstring LibreTranslateUrl(const std::wstring& base) {
    std::wstring u = Trim(base);
    while (!u.empty() && u.back() == L'/') u.pop_back();
    if (u.size() >= 10 && u.compare(u.size() - 10, 10, L"/translate") == 0) return u;
    return u + L"/translate";
}

std::wstring RestoreFromHtml(const std::wstring& s) {
    std::wstring t;
    t.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] == L'<') {
            // Only our own span tags are removed (any attribute order the service may produce).
            const size_t end = s.find(L'>', i);
            if (end != std::wstring::npos) {
                const std::wstring tag = s.substr(i, end - i + 1);
                if (tag.compare(0, 5, L"<span") == 0 || tag == kClose) {
                    i = end + 1;
                    continue;
                }
            }
        }
        if (s[i] == L'&') {
            const size_t semi = s.find(L';', i);
            if (semi != std::wstring::npos && semi - i <= 8) {
                const std::wstring name = s.substr(i + 1, semi - i - 1);
                wchar_t ch = 0;
                if (name == L"amp") ch = L'&';
                else if (name == L"lt") ch = L'<';
                else if (name == L"gt") ch = L'>';
                else if (name == L"quot") ch = L'"';
                else if (name == L"apos") ch = L'\'';
                else if (name == L"nbsp") ch = L' ';
                else if (name.size() > 1 && name[0] == L'#') {
                    const bool hex = name[1] == L'x' || name[1] == L'X';
                    const unsigned long v = std::wcstoul(name.c_str() + (hex ? 2 : 1), nullptr, hex ? 16 : 10);
                    if (v > 0 && v < 0xFFFF) ch = static_cast<wchar_t>(v);
                }
                if (ch) {
                    t += ch;
                    i = semi + 1;
                    continue;
                }
            }
        }
        t += s[i++];
    }
    return t;
}

}  // namespace gct
