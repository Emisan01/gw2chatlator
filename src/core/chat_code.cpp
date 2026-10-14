#include "core/chat_code.hpp"

#include <vector>

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
