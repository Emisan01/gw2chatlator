#include "chat/ChatLinks.h"
#include "chat/SpecData.h"   // host-safe elite-spec / core-profession names (for build labels)
#include <cstring>
#include <algorithm>
#include <cctype>

namespace PieUI {
namespace ChatLinks {

// ---------------------------------------------------------------------------
// Base64 (ported from alter_ego/src/ChatLink.cpp)
// ---------------------------------------------------------------------------

static const char kB64Table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string Base64Encode(const uint8_t* data, size_t len) {
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        unsigned int n = ((unsigned int)data[i]) << 16;
        if (i + 1 < len) n |= ((unsigned int)data[i + 1]) << 8;
        if (i + 2 < len) n |= (unsigned int)data[i + 2];
        out += kB64Table[(n >> 18) & 0x3F];
        out += kB64Table[(n >> 12) & 0x3F];
        out += (i + 1 < len) ? kB64Table[(n >> 6) & 0x3F] : '=';
        out += (i + 2 < len) ? kB64Table[n & 0x3F] : '=';
    }
    return out;
}

static int B64DecodeChar(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

std::vector<uint8_t> Base64Decode(const std::string& encoded) {
    std::vector<uint8_t> out;
    if (encoded.empty()) return out;
    out.reserve((encoded.size() / 4) * 3);
    int val = 0, bits = -8;
    for (char c : encoded) {
        if (c == '=') break;
        int v = B64DecodeChar(c);
        if (v < 0) continue;  // skip stray chars (whitespace etc.)
        val = (val << 6) | v;
        bits += 6;
        if (bits >= 0) {
            out.push_back((uint8_t)((val >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Little-endian readers / writers
// ---------------------------------------------------------------------------

static uint16_t ReadU16LE(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static uint32_t ReadU24LE(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
}
static uint32_t ReadU32LE(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void WriteU16LE(std::vector<uint8_t>& buf, uint16_t v) {
    buf.push_back((uint8_t)(v & 0xFF));
    buf.push_back((uint8_t)((v >> 8) & 0xFF));
}
static void WriteU32LE(std::vector<uint8_t>& buf, uint32_t v) {
    buf.push_back((uint8_t)(v & 0xFF));
    buf.push_back((uint8_t)((v >> 8) & 0xFF));
    buf.push_back((uint8_t)((v >> 16) & 0xFF));
    buf.push_back((uint8_t)((v >> 24) & 0xFF));
}

// ---------------------------------------------------------------------------
// Plain-text un-escape
// ---------------------------------------------------------------------------

std::string Unescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size() &&
            (s[i+1] == '\\' || s[i+1] == ']' || s[i+1] == '[')) {
            out.push_back(s[i+1]);
            ++i;
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Payload extraction / type detection
// ---------------------------------------------------------------------------

bool ExtractPayload(const std::string& link, std::string& payload) {
    size_t start = link.find("[&");
    if (start == std::string::npos) return false;
    size_t end = link.find(']', start);
    if (end == std::string::npos) return false;
    payload = link.substr(start + 2, end - start - 2);
    // GW2's internal chat format escapes ] as \] — drop a trailing backslash.
    if (!payload.empty() && payload.back() == '\\') payload.pop_back();
    return !payload.empty();
}

ChatLinkType DetectType(const std::string& link) {
    std::string payload;
    if (!ExtractPayload(link, payload)) return LINK_NONE;
    auto bytes = Base64Decode(payload);
    if (bytes.empty()) return LINK_NONE;
    return (ChatLinkType)bytes[0];
}

// ---------------------------------------------------------------------------
// Encoders
// ---------------------------------------------------------------------------

std::string EncodeItem(uint32_t item_id, int count, uint32_t skin_id,
                       uint32_t upgrade1, uint32_t upgrade2) {
    std::vector<uint8_t> buf;
    buf.push_back(LINK_ITEM);
    buf.push_back((uint8_t)std::min(count, 250));
    buf.push_back((uint8_t)(item_id & 0xFF));
    buf.push_back((uint8_t)((item_id >> 8) & 0xFF));
    buf.push_back((uint8_t)((item_id >> 16) & 0xFF));

    uint8_t flags = 0;
    if (skin_id)  flags |= ITEM_FLAG_SKINNED;
    if (upgrade1) flags |= ITEM_FLAG_UPGRADE1;
    if (upgrade2) flags |= ITEM_FLAG_UPGRADE2;
    buf.push_back(flags);

    auto pushU24 = [&](uint32_t v) {
        buf.push_back((uint8_t)(v & 0xFF));
        buf.push_back((uint8_t)((v >> 8) & 0xFF));
        buf.push_back((uint8_t)((v >> 16) & 0xFF));
        buf.push_back(0);
    };
    if (skin_id)  pushU24(skin_id);
    if (upgrade1) pushU24(upgrade1);
    if (upgrade2) pushU24(upgrade2);

    return "[&" + Base64Encode(buf.data(), buf.size()) + "]";
}

static std::string EncodeSimpleLink(uint8_t type, uint32_t id) {
    uint8_t buf[5] = {0};
    buf[0] = type;
    buf[1] = (uint8_t)(id & 0xFF);
    buf[2] = (uint8_t)((id >> 8) & 0xFF);
    buf[3] = (uint8_t)((id >> 16) & 0xFF);
    buf[4] = 0;
    return "[&" + Base64Encode(buf, 5) + "]";
}

std::string EncodeSkill(uint32_t skill_id) { return EncodeSimpleLink(LINK_SKILL, skill_id); }
std::string EncodeTrait(uint32_t trait_id) { return EncodeSimpleLink(LINK_TRAIT, trait_id); }
std::string EncodeSkin(uint32_t skin_id)   { return EncodeSimpleLink(LINK_SKIN, skin_id); }
std::string EncodeMap(uint32_t poi_id)     { return EncodeSimpleLink(LINK_MAP, poi_id); }

std::string EncodeBuild(const DecodedBuildLink& build) {
    std::vector<uint8_t> buf;
    buf.push_back(LINK_BUILD);
    buf.push_back(build.profession);

    for (int i = 0; i < 3; i++) {
        buf.push_back(build.specs[i].spec_id);
        uint8_t tb = 0;
        tb |= (build.specs[i].traits[0] & 0x03);
        tb |= (build.specs[i].traits[1] & 0x03) << 2;
        tb |= (build.specs[i].traits[2] & 0x03) << 4;
        buf.push_back(tb);
    }

    for (int i = 0; i < 5; i++) {
        WriteU16LE(buf, build.terrestrial_skills[i]);
        WriteU16LE(buf, build.aquatic_skills[i]);
    }

    if (build.profession == PROF_RANGER) {
        for (int i = 0; i < 4; i++) buf.push_back(build.pets[i]);
        for (int i = 0; i < 12; i++) buf.push_back(0);
    } else if (build.profession == PROF_REVENANT) {
        for (int i = 0; i < 4; i++) buf.push_back(build.legends[i]);
        for (int i = 0; i < 6; i++) WriteU16LE(buf, build.inactive_rev_skills[i]);
    } else {
        for (int i = 0; i < 16; i++) buf.push_back(0);
    }

    buf.push_back((uint8_t)build.weapons.size());
    for (auto w : build.weapons) WriteU16LE(buf, w);

    buf.push_back((uint8_t)build.skill_overrides.size());
    for (auto s : build.skill_overrides) WriteU32LE(buf, s);

    return "[&" + Base64Encode(buf.data(), buf.size()) + "]";
}

// ---------------------------------------------------------------------------
// Decoders
// ---------------------------------------------------------------------------

bool DecodeItem(const std::string& link, DecodedItemLink& out) {
    std::string payload;
    if (!ExtractPayload(link, payload)) return false;
    auto bytes = Base64Decode(payload);
    if (bytes.size() < 6 || bytes[0] != LINK_ITEM) return false;

    out.count   = bytes[1];
    out.item_id = ReadU24LE(&bytes[2]);

    uint8_t flags = bytes[5];
    size_t offset = 6;
    if (flags & ITEM_FLAG_SKINNED) {
        if (offset + 4 > bytes.size()) return false;
        out.skin_id = ReadU24LE(&bytes[offset]); offset += 4;
    }
    if (flags & ITEM_FLAG_UPGRADE1) {
        if (offset + 4 > bytes.size()) return false;
        out.upgrade1_id = ReadU24LE(&bytes[offset]); offset += 4;
    }
    if (flags & ITEM_FLAG_UPGRADE2) {
        if (offset + 4 > bytes.size()) return false;
        out.upgrade2_id = ReadU24LE(&bytes[offset]); offset += 4;
    }
    return true;
}

bool DecodeBuild(const std::string& link, DecodedBuildLink& out) {
    std::string payload;
    if (!ExtractPayload(link, payload)) return false;
    auto bytes = Base64Decode(payload);
    if (bytes.size() < 2 || bytes[0] != LINK_BUILD) return false;

    out = DecodedBuildLink{};
    out.profession = bytes[1];

    size_t offset = 2;
    for (int i = 0; i < 3; i++) {
        if (offset + 2 > bytes.size()) return true;  // partial decode OK
        out.specs[i].spec_id = bytes[offset];
        uint8_t tb = bytes[offset + 1];
        out.specs[i].traits[0] = tb & 0x03;
        out.specs[i].traits[1] = (tb >> 2) & 0x03;
        out.specs[i].traits[2] = (tb >> 4) & 0x03;
        offset += 2;
    }

    if (offset + 20 > bytes.size()) return true;
    for (int i = 0; i < 5; i++) {
        out.terrestrial_skills[i] = ReadU16LE(&bytes[offset]); offset += 2;
        out.aquatic_skills[i]     = ReadU16LE(&bytes[offset]); offset += 2;
    }

    if (out.profession == PROF_RANGER) {
        if (offset + 4 <= bytes.size())
            for (int i = 0; i < 4; i++) out.pets[i] = bytes[offset + i];
        offset += 16;
    } else if (out.profession == PROF_REVENANT) {
        if (offset + 4 <= bytes.size())
            for (int i = 0; i < 4; i++) out.legends[i] = bytes[offset + i];
        offset += 4;
        for (int i = 0; i < 6; i++) {
            if (offset + 2 <= bytes.size())
                out.inactive_rev_skills[i] = ReadU16LE(&bytes[offset]);
            offset += 2;
        }
    } else {
        offset += 16;
    }

    if (offset < bytes.size()) {
        uint8_t weapon_count = bytes[offset++];
        for (int i = 0; i < weapon_count && offset + 2 <= bytes.size(); i++) {
            out.weapons.push_back(ReadU16LE(&bytes[offset])); offset += 2;
        }
    }
    if (offset < bytes.size()) {
        uint8_t override_count = bytes[offset++];
        for (int i = 0; i < override_count && offset + 4 <= bytes.size(); i++) {
            out.skill_overrides.push_back(ReadU32LE(&bytes[offset])); offset += 4;
        }
    }
    return true;
}

bool DecodeWardrobe(const std::string& link, DecodedWardrobeLink& out) {
    std::string payload;
    if (!ExtractPayload(link, payload)) return false;
    auto bytes = Base64Decode(payload);
    if (bytes.size() < 2 || bytes[0] != LINK_WARDROBE_TMPL) return false;

    out = DecodedWardrobeLink{};
    size_t offset = 1;
    auto read16 = [&]() -> uint16_t {
        if (offset + 2 > bytes.size()) return 0;
        uint16_t v = ReadU16LE(&bytes[offset]); offset += 2; return v;
    };

    out.aquabreather_skin = read16();
    out.backpack_skin = read16(); for (int i = 0; i < 4; i++) out.backpack_dyes[i] = read16();
    out.chest_skin = read16();    for (int i = 0; i < 4; i++) out.chest_dyes[i] = read16();
    out.boots_skin = read16();    for (int i = 0; i < 4; i++) out.boots_dyes[i] = read16();
    out.gloves_skin = read16();   for (int i = 0; i < 4; i++) out.gloves_dyes[i] = read16();
    out.helm_skin = read16();     for (int i = 0; i < 4; i++) out.helm_dyes[i] = read16();
    out.leggings_skin = read16(); for (int i = 0; i < 4; i++) out.leggings_dyes[i] = read16();
    out.shoulders_skin = read16();for (int i = 0; i < 4; i++) out.shoulders_dyes[i] = read16();
    out.outfit_id = read16();     for (int i = 0; i < 4; i++) out.outfit_dyes[i] = read16();
    out.aquatic_weapon_a = read16();
    out.aquatic_weapon_b = read16();
    out.weapon_a_main = read16();
    out.weapon_a_off  = read16();
    out.weapon_b_main = read16();
    out.weapon_b_off  = read16();
    out.visibility_flags = read16();
    return true;
}

// ---------------------------------------------------------------------------
// Name lookups
// ---------------------------------------------------------------------------

const char* ProfessionName(uint8_t code) {
    switch (code) {
        case PROF_GUARDIAN:     return "Guardian";
        case PROF_WARRIOR:      return "Warrior";
        case PROF_ENGINEER:     return "Engineer";
        case PROF_RANGER:       return "Ranger";
        case PROF_THIEF:        return "Thief";
        case PROF_ELEMENTALIST: return "Elementalist";
        case PROF_MESMER:       return "Mesmer";
        case PROF_NECROMANCER:  return "Necromancer";
        case PROF_REVENANT:     return "Revenant";
        default:                return "";
    }
}

const char* LinkTypeLabel(ChatLinkType type) {
    switch (type) {
        case LINK_COIN:          return "[Coin]";
        case LINK_ITEM:          return "[Item]";
        case LINK_NPC_TEXT:      return "[Text]";
        case LINK_MAP:           return "[Waypoint]";
        case LINK_PVP_GAME:      return "[PvP]";
        case LINK_SKILL:         return "[Skill]";
        case LINK_TRAIT:         return "[Trait]";
        case LINK_USER:          return "[User]";
        case LINK_RECIPE:        return "[Recipe]";
        case LINK_SKIN:          return "[Skin]";
        case LINK_OUTFIT:        return "[Outfit]";
        case LINK_WVW_OBJECTIVE: return "[WvW]";
        case LINK_BUILD:         return "[Build]";
        case LINK_ACHIEVEMENT:   return "[Achievement]";
        case LINK_WARDROBE_TMPL: return "[Wardrobe]";
        default:                 return "[Link]";
    }
}

// ---------------------------------------------------------------------------
// AE2 partial decode (ported from tyrian_instant_messaging/src/ChatLinks.cpp)
// ---------------------------------------------------------------------------

LinkSegment ParseAE2(const std::string& raw) {
    LinkSegment seg;
    seg.kind    = SegmentKind::Ae2;
    seg.raw     = raw;
    seg.display = "[Build]";

    if (raw.size() < 5) return seg;
    auto data = Base64Decode(raw.substr(4));  // strip "AE2:"
    if (data.size() < 4) return seg;

    size_t pos = 0;
    uint8_t version = data[pos++];
    if (version != 2 && version != 3) return seg;

    uint8_t flags = data[pos++];
    seg.ae2GameMode = flags & 0x07;

    uint8_t linkLen = data[pos++];
    if (pos + linkLen > data.size()) return seg;

    std::vector<uint8_t> linkBytes(data.begin() + pos, data.begin() + pos + linkLen);
    seg.ae2ChatLink = "[&" + Base64Encode(linkBytes.data(), linkBytes.size()) + "]";

    // Profession lives in linkBytes[1] (the build-link's profession byte).
    if (linkBytes.size() >= 2) {
        const char* prof = ProfessionName(linkBytes[1]);
        if (prof && prof[0]) {
            seg.ae2Profession = prof;
            seg.display = std::string("Alter Ego ") + prof + " Build Code";
        }
    }
    return seg;
}

// ---------------------------------------------------------------------------
// GW2 [&base64] link → segment
// ---------------------------------------------------------------------------

// Build a spec-aware label for a build-template link, e.g. "[Mechanist Build]" or
// "[Engineer Build]". bytes = the raw decoded link ([0]=0x0D, [1]=profession, then 3x
// {specId,traits}). The elite spec sits in the 3rd trait line; we scan all three and use
// the one that resolves to an elite-spec name, else the core profession. Falls back to
// "[Build]" if the profession is unknown.
static std::string BuildSpecLabel(const std::vector<uint8_t>& bytes) {
    if (bytes.size() < 2) return "[Build]";
    const char* name = nullptr;
    for (int i = 0; i < 3; i++) {
        size_t off = 2 + (size_t)i * 2;
        if (off >= bytes.size()) break;
        const char* es = SpecData::GetEliteSpecName(bytes[off]);
        if (es && es[0]) { name = es; break; }
    }
    if (!name) name = SpecData::GetProfessionName(bytes[1]);
    if (!name || !name[0]) return "[Build]";
    return std::string("[") + name + " Build]";
}

static LinkSegment DecodeGW2Link(const std::string& raw) {
    LinkSegment seg;
    seg.kind = SegmentKind::Link;
    seg.raw  = raw;

    std::string payload;
    if (!ExtractPayload(raw, payload)) { seg.display = "[Link]"; return seg; }
    auto bytes = Base64Decode(payload);
    if (bytes.empty()) { seg.display = "[Link]"; return seg; }

    // Canonicalise raw to [&payload] so a clipboard paste works in-game even if
    // the source used the \[&..\] escaped form.
    seg.raw = "[&" + payload + "]";

    seg.linkType = (ChatLinkType)bytes[0];
    if (seg.linkType == LINK_ITEM) {
        if (bytes.size() >= 5) seg.primaryId = ReadU24LE(&bytes[2]);
        if (bytes.size() >= 2) seg.count = bytes[1];
    } else {
        if (bytes.size() >= 5) seg.primaryId = ReadU32LE(&bytes[1]);
        // WvW objective link (0x0C) carries a second u32: the mapId (objective ids
        // repeat across maps, so both are needed to resolve the objective name).
        if (seg.linkType == LINK_WVW_OBJECTIVE && bytes.size() >= 9)
            seg.secondaryId = ReadU32LE(&bytes[5]);
    }
    seg.display = (seg.linkType == LINK_BUILD) ? BuildSpecLabel(bytes)
                                               : LinkTypeLabel(seg.linkType);
    return seg;
}

// ---------------------------------------------------------------------------
// SegmentLine — the public scanner
// ---------------------------------------------------------------------------

static bool IsBase64Char(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=';
}

std::vector<LinkSegment> SegmentLine(const std::string& utf8) {
    std::vector<LinkSegment> result;
    std::string plain;

    auto flushPlain = [&]() {
        if (!plain.empty()) {
            LinkSegment s;
            s.kind = SegmentKind::Plain;
            s.display = Unescape(plain);
            s.raw = plain;
            plain.clear();
            result.push_back(std::move(s));
        }
    };
    auto pushSeg = [&](LinkSegment s) {
        flushPlain();
        result.push_back(std::move(s));
    };

    const std::string& text = utf8;
    size_t i = 0;
    while (i < text.size()) {
        // --- HTTP/HTTPS URL ---
        bool is_http  = i + 7 <= text.size() && text.compare(i, 7, "http://")  == 0;
        bool is_https = i + 8 <= text.size() && text.compare(i, 8, "https://") == 0;
        if (is_http || is_https) {
            size_t j = i;
            while (j < text.size() && !std::isspace((unsigned char)text[j])) j++;
            constexpr size_t kMaxUrlLen = 2048;
            if (j - i > kMaxUrlLen) { plain += text[i++]; continue; }
            std::string url = text.substr(i, j - i);
            size_t proto_end = url.find("://");
            std::string domain;
            if (proto_end != std::string::npos) {
                size_t dom_start = proto_end + 3;
                size_t dom_end   = url.find('/', dom_start);
                domain = url.substr(dom_start,
                    dom_end == std::string::npos ? std::string::npos : dom_end - dom_start);
            } else {
                domain = url;
            }
            if (domain.empty()) { plain += text[i++]; continue; }
            LinkSegment seg;
            seg.kind    = SegmentKind::Url;
            seg.raw     = url;
            seg.url     = url;
            seg.domain  = domain;
            seg.display = domain;
            pushSeg(std::move(seg));
            i = j;
            continue;
        }

        // --- AE2 build code ---
        if (i + 4 <= text.size() &&
            text[i] == 'A' && text[i+1] == 'E' && text[i+2] == '2' && text[i+3] == ':') {
            size_t j = i + 4;
            while (j < text.size() && IsBase64Char(text[j])) j++;
            if (j > i + 4) {
                pushSeg(ParseAE2(text.substr(i, j - i)));
                i = j;
                continue;
            }
        }

        // --- GW2 chat link: [&...] or \[&...\] ---
        bool escaped = false;
        bool is_link_start = false;
        size_t raw_start = i;
        if (text[i] == '[' && i + 1 < text.size() && text[i+1] == '&') {
            is_link_start = true;
        } else if (text[i] == '\\' && i + 1 < text.size() && text[i+1] == '[' &&
                   i + 2 < text.size() && text[i+2] == '&') {
            is_link_start = true;
            escaped = true;
        }
        if (is_link_start) {
            size_t open = i + (escaped ? 1 : 0);  // position of '['
            size_t close = std::string::npos;
            for (size_t j = open + 2; j < text.size(); j++) {
                if (!escaped && text[j] == ']') { close = j; break; }
                if (escaped && text[j] == '\\' && j + 1 < text.size() && text[j+1] == ']') {
                    close = j; break;
                }
            }
            if (close != std::string::npos) {
                size_t raw_end = close + (escaped ? 2 : 1);
                pushSeg(DecodeGW2Link(text.substr(raw_start, raw_end - raw_start)));
                i = raw_end;
                continue;
            }
        }

        plain += text[i++];
    }

    flushPlain();
    return result;
}

} // namespace ChatLinks
} // namespace PieUI
