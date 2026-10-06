// deepl_protocol.cpp
#include "deepl_protocol.hpp"

#include "json.hpp"
#include "text.hpp"

namespace gct {

namespace {

std::wstring XmlEscape(const std::wstring& s) {
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

}  // namespace

namespace {

std::wstring EncodeSegments(const std::vector<Segment>& segments, bool xmlMode) {
    std::wstring text;
    for (const Segment& s : segments) {
        if (!xmlMode) text += s.text;
        else if (s.keep) text += L"<keep>" + XmlEscape(s.text) + L"</keep>";
        else text += XmlEscape(s.text);
    }
    return text;
}

}  // namespace

DeepLPayload BuildDeepLPayloadBatch(const std::vector<std::vector<Segment>>& items, const std::wstring& sourceLang,
                                    const std::wstring& targetLang) {
    DeepLPayload p;
    for (const auto& item : items) p.xmlMode = p.xmlMode || HasProtected(item);

    std::string j = "{\"text\":[";
    for (size_t i = 0; i < items.size(); ++i) {
        if (i) j += ",";
        j += "\"" + JsonEscape(ToUtf8(EncodeSegments(items[i], p.xmlMode))) + "\"";
    }
    j += "]";
    j += ",\"target_lang\":\"" + JsonEscape(ToUtf8(ToUpperAscii(Trim(targetLang)))) + "\"";
    const std::wstring src = ToUpperAscii(Trim(sourceLang));
    if (!src.empty()) j += ",\"source_lang\":\"" + JsonEscape(ToUtf8(src)) + "\"";
    if (p.xmlMode) j += ",\"tag_handling\":\"xml\",\"ignore_tags\":[\"keep\"]";
    j += "}";
    p.json = std::move(j);
    return p;
}

DeepLPayload BuildDeepLPayload(const std::vector<Segment>& segments, const std::wstring& sourceLang,
                               const std::wstring& targetLang) {
    return BuildDeepLPayloadBatch({segments}, sourceLang, targetLang);
}

std::wstring RestoreFromXml(const std::wstring& s) {
    std::wstring t;
    t.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        if (s.compare(i, 6, L"<keep>") == 0) { i += 6; continue; }
        if (s.compare(i, 7, L"</keep>") == 0) { i += 7; continue; }
        if (s.compare(i, 7, L"<keep/>") == 0) { i += 7; continue; }
        if (s[i] == L'&') {
            struct Ent { const wchar_t* name; size_t len; wchar_t ch; };
            static const Ent ents[] = {{L"&amp;", 5, L'&'},  {L"&lt;", 4, L'<'},   {L"&gt;", 4, L'>'},
                                       {L"&quot;", 6, L'"'}, {L"&apos;", 6, L'\''}};
            bool hit = false;
            for (const auto& e : ents) {
                if (s.compare(i, e.len, e.name) == 0) {
                    t += e.ch;
                    i += e.len;
                    hit = true;
                    break;
                }
            }
            if (hit) continue;
        }
        t += s[i++];
    }
    return t;
}

std::vector<DeepLParsed> ParseDeepLResponseBatch(const std::string& body, bool xmlMode, size_t expected) {
    std::vector<DeepLParsed> out(expected);
    auto failAll = [&](const std::wstring& error) {
        for (auto& r : out) r.error = error;
        return out;
    };
    JsonValue root;
    if (!ParseJson(body, root)) return failAll(L"Antwort von DeepL nicht lesbar");
    const JsonValue* tr = root.Get("translations");
    if (!tr || !tr->IsArray() || tr->arr.size() != expected) {
        const std::string msg = root.GetString("message");
        return failAll(msg.empty() ? L"Unerwartete Antwort von DeepL" : FromUtf8(msg));
    }
    for (size_t i = 0; i < expected; ++i) {
        const JsonValue* text = tr->arr[i].Get("text");
        if (!text || text->type != JsonValue::Type::String) {
            out[i].error = L"Unerwartete Antwort von DeepL";
            continue;
        }
        out[i].ok = true;
        out[i].text = FromUtf8(text->s);
        if (xmlMode) out[i].text = RestoreFromXml(out[i].text);
        out[i].detectedSource = FromUtf8(tr->arr[i].GetString("detected_source_language"));
    }
    return out;
}

DeepLParsed ParseDeepLResponse(const std::string& body, bool xmlMode) {
    JsonValue root;
    if (ParseJson(body, root)) {
        if (const JsonValue* tr = root.Get("translations"); tr && tr->IsArray() && !tr->arr.empty())
            return ParseDeepLResponseBatch(body, xmlMode, tr->arr.size())[0];
    }
    return ParseDeepLResponseBatch(body, xmlMode, 1)[0];
}

bool IsDeepLFreeKey(const std::wstring& key) {
    const std::wstring k = Trim(key);
    return k.size() >= 3 && k.compare(k.size() - 3, 3, L":fx") == 0;
}

}  // namespace gct
