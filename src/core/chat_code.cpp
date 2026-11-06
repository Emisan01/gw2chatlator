#include "core/chat_code.hpp"

#include <cstdlib>
#include <vector>

#include "core/json.hpp"

namespace gct {
namespace {

int Base64Value(wchar_t c) {
    if (c >= L'A' && c <= L'Z') return c - L'A';
    if (c >= L'a' && c <= L'z') return c - L'a' + 26;
    if (c >= L'0' && c <= L'9') return c - L'0' + 52;
    if (c == L'+') return 62;
    if (c == L'/') return 63;
    return -1;
}

bool Base64Decode(const std::wstring& s, std::vector<uint8_t>* out) {
    uint32_t acc = 0;
    int bits = 0;
    for (wchar_t c : s) {
        if (c == L'=') break;
        const int v = Base64Value(c);
        if (v < 0) return false;
        acc = (acc << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out->push_back(static_cast<uint8_t>((acc >> bits) & 0xFF));
        }
    }
    return !out->empty();
}

uint32_t Le(const std::vector<uint8_t>& b, size_t at, size_t n) {
    uint32_t v = 0;
    for (size_t i = 0; i < n && at + i < b.size(); ++i) v |= static_cast<uint32_t>(b[at + i]) << (8 * i);
    return v;
}

}  // namespace

ChatCode DecodeChatCode(const std::wstring& code) {
    ChatCode c;
    std::wstring body = code;
    if (body.size() >= 3 && body.compare(0, 2, L"[&") == 0 && body.back() == L']') body = body.substr(2, body.size() - 3);
    std::vector<uint8_t> b;
    if (!Base64Decode(body, &b) || b.size() < 2) return c;
    switch (b[0]) {
        case 0x01:
            if (b.size() >= 5) c = {ChatCodeKind::Coin, Le(b, 1, 4), 1};
            break;
        case 0x02:  // item: count, 3-byte id, flags (+ skin / upgrades)
            if (b.size() >= 5) c = {ChatCodeKind::Item, Le(b, 2, 3), b[1]};
            break;
        case 0x03: c = {ChatCodeKind::Text, Le(b, 1, 4), 1}; break;
        case 0x04: c = {ChatCodeKind::Poi, Le(b, 1, 4), 1}; break;
        case 0x06: c = {ChatCodeKind::Skill, Le(b, 1, 4), 1}; break;
        case 0x07: c = {ChatCodeKind::Trait, Le(b, 1, 4), 1}; break;
        case 0x09: c = {ChatCodeKind::Recipe, Le(b, 1, 4), 1}; break;
        case 0x0A: c = {ChatCodeKind::Skin, Le(b, 1, 4), 1}; break;
        case 0x0B: c = {ChatCodeKind::Outfit, Le(b, 1, 4), 1}; break;
        case 0x0C: c = {ChatCodeKind::Objective, Le(b, 1, 4), 1}; break;
        case 0x0D: c = {ChatCodeKind::Build, 0, 1}; break;
        case 0x0E: c = {ChatCodeKind::Achievement, Le(b, 1, 4), 1}; break;
        default: break;
    }
    if (c.kind != ChatCodeKind::Unknown && c.kind != ChatCodeKind::Build && c.id == 0) c.kind = ChatCodeKind::Unknown;
    return c;
}

std::wstring ChatCodeApiPath(const ChatCode& c) {
    const wchar_t* what = nullptr;
    switch (c.kind) {
        case ChatCodeKind::Item: what = L"items"; break;
        case ChatCodeKind::Skill: what = L"skills"; break;
        case ChatCodeKind::Trait: what = L"traits"; break;
        case ChatCodeKind::Recipe: what = L"recipes"; break;
        case ChatCodeKind::Skin: what = L"skins"; break;
        case ChatCodeKind::Outfit: what = L"outfits"; break;
        case ChatCodeKind::Achievement: what = L"achievements"; break;
        default: return {};
    }
    return std::wstring(L"/v2/") + what + L"?ids=" + std::to_wstring(c.id);
}

std::wstring ChatCodeNamesPath(const wchar_t* what, const std::vector<uint32_t>& ids, const std::wstring& lang) {
    std::wstring path = std::wstring(L"/v2/") + what + L"?lang=" + lang + L"&ids=";
    for (size_t i = 0; i < ids.size(); ++i) {
        if (i) path += L',';
        path += std::to_wstring(ids[i]);
    }
    return path;
}

std::wstring ChatCodeObjectivesPath(const std::vector<std::wstring>& ids, const std::wstring& lang) {
    std::wstring path = L"/v2/wvw/objectives?lang=" + lang + L"&ids=";
    for (size_t i = 0; i < ids.size(); ++i) {
        if (i) path += L',';
        path += ids[i];
    }
    return path;
}

std::wstring ChatCodeApiLang(const std::wstring& readLang) {
    std::wstring two;
    for (size_t i = 0; i < readLang.size() && i < 2; ++i)
        two += readLang[i] >= L'A' && readLang[i] <= L'Z' ? static_cast<wchar_t>(readLang[i] + 32) : readLang[i];
    return two == L"de" || two == L"fr" || two == L"es" || two == L"zh" ? two : L"en";
}

std::vector<ChatCodeName> ParseChatCodeNames(const std::string& body) {
    std::vector<ChatCodeName> names;
    JsonValue doc;
    if (!ParseJson(body, doc) || !doc.IsArray()) return names;
    for (const JsonValue& entry : doc.arr) {
        const JsonValue* id = entry.IsObject() ? entry.Get("id") : nullptr;
        if (!id) continue;
        ChatCodeName name;
        if (id->type == JsonValue::Type::Number && id->n > 0) {
            name.id = static_cast<uint32_t>(id->n);
        } else if (id->type == JsonValue::Type::String) {  // a WvW objective: "95-34"
            const size_t dash = id->s.find('-');
            if (dash == std::string::npos || dash == 0 || dash + 1 >= id->s.size()) continue;
            name.mapId = static_cast<uint32_t>(std::strtoul(id->s.c_str(), nullptr, 10));
            name.id = static_cast<uint32_t>(std::strtoul(id->s.c_str() + dash + 1, nullptr, 10));
            if (!name.id) continue;
        } else {
            continue;
        }
        name.name = entry.GetString("name");
        name.rarity = entry.GetString("rarity");
        name.type = entry.GetString("type");
        name.description = entry.GetString("description");
        if (const JsonValue* v = entry.Get("level"); v && v->type == JsonValue::Type::Number) name.level = static_cast<int>(v->n);
        if (const JsonValue* v = entry.Get("min_rating"); v && v->type == JsonValue::Type::Number) name.rating = static_cast<int>(v->n);
        if (const JsonValue* v = entry.Get("output_item_id"); v && v->type == JsonValue::Type::Number && v->n > 0) name.outputItem = static_cast<uint32_t>(v->n);
        if (!name.name.empty() || name.outputItem) names.push_back(std::move(name));
    }
    return names;
}

std::string CleanChatCodeText(const std::string& text) {
    std::string out;
    bool space = false;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '<') {  // <c=@flavor>, </c>, <br>
            const size_t end = text.find('>', i);
            if (end != std::string::npos) {
                i = end;
                continue;
            }
        }
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
            space = !out.empty();
            continue;
        }
        if (space) out += ' ';
        space = false;
        out += c;
    }
    constexpr size_t kMax = 400;
    if (out.size() > kMax) {
        size_t cut = kMax;
        while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) --cut;  // never inside a letter
        out.resize(cut);
        out += "...";
    }
    return out;
}

bool ParseChatCodePoi(const std::string& line, ChatCodePoi* out) {
    const size_t a = line.find('\t');
    const size_t b = a == std::string::npos ? a : line.find('\t', a + 1);
    const size_t c = b == std::string::npos ? b : line.find('\t', b + 1);
    if (c == std::string::npos) return false;
    const unsigned long id = std::strtoul(line.c_str(), nullptr, 10);
    if (!id || c == b + 1) return false;
    out->id = static_cast<uint32_t>(id);
    out->type = line.substr(a + 1, b - a - 1);
    out->name = line.substr(b + 1, c - b - 1);
    const size_t d = line.find('\t', c + 1);
    out->map = line.substr(c + 1, d == std::string::npos ? d : d - c - 1);
    if (!out->map.empty() && out->map.back() == '\r') out->map.pop_back();
    out->continent = out->floor = 1;
    out->x = out->y = 0;
    if (d != std::string::npos) {
        char* at = nullptr;
        out->continent = static_cast<int>(std::strtol(line.c_str() + d + 1, &at, 10));
        out->floor = static_cast<int>(std::strtol(at, &at, 10));
        out->x = std::strtof(at, &at);
        out->y = std::strtof(at, &at);
    }
    return true;
}

std::wstring UnescapeChatText(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'\\' && i + 1 < s.size() && (s[i + 1] == L'[' || s[i + 1] == L']' || s[i + 1] == L'\\')) ++i;
        out += s[i];
    }
    return out;
}

uint32_t RarityColor(const std::string& rarity) {
    static const struct {
        const char* name;
        uint32_t rgb;
    } kRarity[] = {{"Junk", 0xAAAAAA},   {"Basic", 0xFFFFFF},  {"Fine", 0x62A4DA},     {"Masterwork", 0x1A9306},
                   {"Rare", 0xFCD00B},   {"Exotic", 0xFFA405}, {"Ascended", 0xFB3E8D}, {"Legendary", 0xA02EF8}};
    for (const auto& r : kRarity)
        if (rarity == r.name) return r.rgb;
    return 0;
}

}  // namespace gct
