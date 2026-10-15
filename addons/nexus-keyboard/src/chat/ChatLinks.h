// ============================================================================
// ChatLinks -- GW2 chat-link codec (PURE, vendorable)
// ----------------------------------------------------------------------------
// Encodes/decodes GW2 chat links (item / skin / skill / trait / map-waypoint /
// build / wardrobe), classifies a link's type, parses Alter Ego v2 (AE2) codes,
// and segments a UTF-8 chat line into ordered interactive spans.
//
// PURITY: depends ONLY on the C++ standard library. No Nexus, ImGui, Win32,
// config, logging, or network. The single project header it pulls in
// (chat/SpecData.h) is itself a pure, header-only <cstdint> name table and is
// considered PART OF THIS UNIT.
//
// VENDORING: this unit is shared by COPY. To reuse it in another addon, copy
// THREE files: ChatLinks.h, ChatLinks.cpp, and SpecData.h. See
// ChatLinks.README.md for the procedure and the no-fork discipline. Resolution
// (IDs -> names/icons via /v2) is intentionally NOT part of this unit; it stops
// at structure (type + raw IDs + spans). The build-link chip label is the one
// exception: a static, compile-time profession/spec name from SpecData.h.
//
// LINEAGE: ported from alter_ego/ChatLink + Tyrian IM (TIM) ChatLinks.
//
// THREAD/STATE: no global mutable state. All lookup tables are const/static and
// read-only, so this unit is safe to call from any thread and (being compiled
// into each addon's own DLL) carries per-DLL statics -- fine to vendor.
// ============================================================================
#pragma once
//
// Pie UI chat-link codec + line segmenter (PURE LOGIC, host-tested).
//
// Ported from the sibling references (read-only): the GW2 chat-link codec from
// alter_ego/src/ChatLink.{h,cpp} (canonical) and the AE2 partial-decode + line
// segmenter from tyrian_instant_messaging/src/ChatLinks.cpp. No game calls, no
// memory RE, no ImGui/Win32 -- just bytes in, structured spans out.
//
#include <string>
#include <vector>
#include <cstdint>

namespace PieUI {
namespace ChatLinks {

// Chat-link type bytes (first decoded byte of a [&base64] payload).
enum ChatLinkType : uint8_t {
    LINK_NONE          = 0x00,
    LINK_COIN          = 0x01,
    LINK_ITEM          = 0x02,
    LINK_NPC_TEXT      = 0x03,
    LINK_MAP           = 0x04,  // waypoint / PoI
    LINK_PVP_GAME      = 0x05,
    LINK_SKILL         = 0x06,
    LINK_TRAIT         = 0x07,
    LINK_USER          = 0x08,
    LINK_RECIPE        = 0x09,
    LINK_SKIN          = 0x0A,
    LINK_OUTFIT        = 0x0B,
    LINK_WVW_OBJECTIVE = 0x0C,
    LINK_BUILD         = 0x0D,
    LINK_ACHIEVEMENT   = 0x0E,
    LINK_WARDROBE_TMPL = 0x0F,
};

// Item-link flags (byte after the item id).
enum ItemLinkFlags : uint8_t {
    ITEM_FLAG_SKINNED  = 0x80,
    ITEM_FLAG_UPGRADE1 = 0x40,
    ITEM_FLAG_UPGRADE2 = 0x20,
    ITEM_FLAG_NAME_KEY = 0x10,
    ITEM_FLAG_DESC_KEY = 0x08,
};

// Profession codes used in build templates / AE2 codes.
enum ProfessionCode : uint8_t {
    PROF_GUARDIAN     = 1,
    PROF_WARRIOR      = 2,
    PROF_ENGINEER     = 3,
    PROF_RANGER       = 4,
    PROF_THIEF        = 5,
    PROF_ELEMENTALIST = 6,
    PROF_MESMER       = 7,
    PROF_NECROMANCER  = 8,
    PROF_REVENANT     = 9,
};

struct DecodedItemLink {
    uint32_t item_id = 0;
    int      count = 1;
    uint32_t skin_id = 0;      // 0 = no skin override
    uint32_t upgrade1_id = 0;  // 0 = no upgrade
    uint32_t upgrade2_id = 0;
};

struct DecodedBuildLink {
    uint8_t profession = 0;
    struct SpecChoice {
        uint8_t spec_id = 0;
        uint8_t traits[3] = {0, 0, 0};  // 0=none, 1=top, 2=mid, 3=bottom
    } specs[3];
    uint16_t terrestrial_skills[5] = {0};  // heal, util1-3, elite (palette IDs)
    uint16_t aquatic_skills[5] = {0};
    uint8_t  pets[4] = {0};    // ranger
    uint8_t  legends[4] = {0}; // revenant
    uint16_t inactive_rev_skills[6] = {0};
    std::vector<uint16_t> weapons;          // SotO+
    std::vector<uint32_t> skill_overrides;  // Weaponmaster
};

struct DecodedWardrobeLink {
    uint16_t aquabreather_skin = 0;
    uint16_t backpack_skin = 0;   uint16_t backpack_dyes[4] = {0};
    uint16_t chest_skin = 0;      uint16_t chest_dyes[4] = {0};
    uint16_t boots_skin = 0;      uint16_t boots_dyes[4] = {0};
    uint16_t gloves_skin = 0;     uint16_t gloves_dyes[4] = {0};
    uint16_t helm_skin = 0;       uint16_t helm_dyes[4] = {0};
    uint16_t leggings_skin = 0;   uint16_t leggings_dyes[4] = {0};
    uint16_t shoulders_skin = 0;  uint16_t shoulders_dyes[4] = {0};
    uint16_t outfit_id = 0;       uint16_t outfit_dyes[4] = {0};
    uint16_t aquatic_weapon_a = 0;
    uint16_t aquatic_weapon_b = 0;
    uint16_t weapon_a_main = 0;
    uint16_t weapon_a_off = 0;
    uint16_t weapon_b_main = 0;
    uint16_t weapon_b_off = 0;
    uint16_t visibility_flags = 0;
};

// --- Base64 ---
// Encode raw bytes to a Base64 string (standard alphabet, with padding).
std::string          Base64Encode(const uint8_t* data, size_t len);
// Decode a Base64 string to raw bytes; stops at '=' padding or non-alphabet chars.
std::vector<uint8_t> Base64Decode(const std::string& encoded);

// --- Payload / type ---
// Extract the base64 between [& and ] (tolerates the \] escape GW2 uses internally).
bool          ExtractPayload(const std::string& link, std::string& payload);
// Decode the first payload byte of a [&...] link to its ChatLinkType; returns LINK_NONE on failure.
ChatLinkType  DetectType(const std::string& link);

// --- Decode ---
// Decode a [&...] item link into its constituent IDs, count, skin, and upgrade slots; returns false on type mismatch or truncation.
bool DecodeItem(const std::string& link, DecodedItemLink& out);
// Decode a [&...] build-template link into profession, specs/traits, skills, pets, legends, and SotO weapon/override extensions; returns false on type mismatch or truncation.
bool DecodeBuild(const std::string& link, DecodedBuildLink& out);
// Decode a [&...] wardrobe-template link into per-slot skin and dye IDs plus weapon and visibility data; returns false on type mismatch or truncation.
bool DecodeWardrobe(const std::string& link, DecodedWardrobeLink& out);

// --- Encode ---
// Encode an item link; count is clamped to 250, skin/upgrade IDs are omitted when zero.
std::string EncodeItem(uint32_t item_id, int count = 1, uint32_t skin_id = 0,
                       uint32_t upgrade1 = 0, uint32_t upgrade2 = 0);
// Encode a skill palette ID as a [&...] skill link.
std::string EncodeSkill(uint32_t skill_id);
// Encode a trait ID as a [&...] trait link.
std::string EncodeTrait(uint32_t trait_id);
// Encode a skin ID as a [&...] skin link.
std::string EncodeSkin(uint32_t skin_id);
std::string EncodeMap(uint32_t poi_id);   // waypoint / PoI link
// Encode a DecodedBuildLink (including SotO weapon and Weaponmaster override extensions) as a [&...] build link.
std::string EncodeBuild(const DecodedBuildLink& build);

// --- Names ---
// Return the profession name string for a ProfessionCode; returns nullptr for unknown codes.
const char* ProfessionName(uint8_t code);
const char* LinkTypeLabel(ChatLinkType type);  // "[Item]", "[Waypoint]", ...

// ---------------------------------------------------------------------------
// Line segmenter -- splits a raw chat message into ordered interactive spans.
// ---------------------------------------------------------------------------

enum class SegmentKind {
    Plain,  // ordinary text
    Url,    // http:// or https:// link
    Link,   // GW2 [&base64] chat link (waypoint / item / skin / skill / ...)
    Ae2,    // Alter Ego AE2:... build code
};

struct LinkSegment {
    SegmentKind kind = SegmentKind::Plain;
    std::string display;  // text to render (plain text, domain, or a label)
    std::string raw;      // source slice / canonical [&..] / full URL

    // URL spans
    std::string url;      // full URL (== raw for URLs)
    std::string domain;   // host portion (anti-spoof display)

    // GW2 link spans (kind == Link)
    ChatLinkType linkType = LINK_NONE;
    uint32_t primaryId = 0;  // item id / skin id / waypoint PoI id / skill id ...
    int      count = 1;      // item quantity (1 otherwise)
    uint32_t secondaryId = 0; // WvW objective mapId (LINK_WVW_OBJECTIVE 0x0C)

    // AE2 spans (kind == Ae2)
    std::string ae2Profession;          // "" if it couldn't be identified
    uint8_t     ae2GameMode = 0;        // flags & 0x07
    std::string ae2ChatLink;            // embedded [&...] build link
};

// Undo GW2's chat wire-escaping in plain text: "\\"->"\", "\]"->"]", "\["->"[".
std::string Unescape(const std::string& s);

// Split a UTF-8 chat line into an ORDERED, GAP-FREE list of LinkSegment spans.
// Walking the returned vector front-to-back and emitting each segment's `display`
// reproduces the original line's visible text in order. Each segment is one of
// SegmentKind: Plain (plain run; render `display` literally), Link (a decoded
// chat link; `linkType` + raw ids populated, `display` is its label/text),
// Url (a hyperlink; `url`/`domain` set, `domain` is the anti-spoof display), or
// Ae2 (an Alter Ego v2 build code; `ae2Profession`/`ae2ChatLink` set). Consumers
// render Plain inline and may make Link/Url/Ae2 interactive (click/tooltip). The
// codec performs NO id->name/icon resolution -- the consumer brings its own.
std::vector<LinkSegment> SegmentLine(const std::string& utf8);

// Decode a single AE2:... code into an Ae2 segment (exposed for testing/reuse).
LinkSegment ParseAE2(const std::string& raw);

} // namespace ChatLinks
} // namespace PieUI
