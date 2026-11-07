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

static uint32_t ReadU24LE(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
}
static uint32_t ReadU32LE(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
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

// ---------------------------------------------------------------------------
// Encoders
// ---------------------------------------------------------------------------

static std::string EncodeSimpleLink(uint8_t type, uint32_t id) {
    uint8_t buf[5] = {0};
    buf[0] = type;
    buf[1] = (uint8_t)(id & 0xFF);
    buf[2] = (uint8_t)((id >> 8) & 0xFF);
    buf[3] = (uint8_t)((id >> 16) & 0xFF);
    buf[4] = 0;
    return "[&" + Base64Encode(buf, 5) + "]";
}

std::string EncodeMap(uint32_t poi_id)     { return EncodeSimpleLink(LINK_MAP, poi_id); }

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
