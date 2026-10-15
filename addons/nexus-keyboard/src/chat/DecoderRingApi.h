// ============================================================================
// Decoder Ring — public ABI for consumer addons.
// ----------------------------------------------------------------------------
// This is the ONE file a consumer copies to integrate. It is pure: depends only
// on <cstdint> — no Nexus, no Win32, no STL containers across the boundary.
//
// Delivery is two coupled halves, both keyed by the (linkType, id) correlation
// tuple:
//   1. Warm query  — DataLink_Get(DECODER_RING_DATALINK) yields a DecoderRingApi*;
//      call Resolve(): warm -> Resolved+record, cold -> NotReady (fetch kicked).
//   2. Miss event  — subscribe EV_DECODER_RING_RESOLVED; payload is DecoderRecord*
//      (delivered synchronously — copy what you need during the handler).
//
// VERSIONING: schemaVersion is the FIRST field of DecoderRecord; apiVersion is
// the FIRST field of DecoderRingApi. A consumer MUST check both before use.
// ============================================================================
#pragma once
#include <cstdint>

// Bump on ANY change to DecoderRingApi or DecoderRecord layout/semantics.
// v2: added DecoderRecord::rarity (item links) — see DecoderRarity below.
// v3: description[] + facts[] now also carry full item-tooltip data (flavour text
//     + pre-formatted stat/meta lines) for item links, not just skills. Layout is
//     unchanged (a capability signal only); gate item tooltips on schemaVersion>=3.
// v4: added recipe links (0x09) support — see Recipe section below.
// v5: added DecoderRingApi::GetActiveLanguage() — the language DR is currently
//     resolving names in. Layout-extending only (a trailing function pointer), so a
//     pre-v5 consumer is unaffected and a v5 service is compatible with a v4 consumer.
//     Gate GetActiveLanguage() calls on apiVersion >= 5.
#define DECODER_RING_API_VERSION   5u
// DataLink identifier the service publishes the DecoderRingApi struct under.
#define DECODER_RING_DATALINK      "DECODER_RING_API"
// Service announces its API is live (raised on load + in reply to a ping).
#define EV_DECODER_RING_READY      "EV_DECODER_RING_READY"
// Service announces it is unloading. Consumers MUST drop any cached DecoderRingApi*
// on receipt and treat the service as absent until the next EV_DECODER_RING_READY.
// The DataLink block is Nexus-owned and survives the unload, but its function
// pointers are zeroed before this fires — calling them after unload is a crash.
#define EV_DECODER_RING_UNLOADING  "EV_DECODER_RING_UNLOADING"
// A consumer pings to ask the service to (re-)announce readiness.
#define EV_DECODER_RING_PING       "EV_DECODER_RING_PING"
// A background resolution landed. Payload: DecoderRecord* (synchronous).
#define EV_DECODER_RING_RESOLVED   "EV_DECODER_RING_RESOLVED"
// The effective resolution language changed (auto-switch or override). Payload: a
// const char* immortal lang literal ("en"|"de"|"fr"|"es"), valid for the duration
// of the handler — copy out. Consumers may instead just poll GetActiveLanguage().
#define EV_DECODER_RING_LANGUAGE_CHANGED "EV_DECODER_RING_LANGUAGE_CHANGED"

// Resolution status of a record / query result.
enum DecoderStatus : uint8_t {
    DR_NotReady = 0,  // not in warm cache; a background fetch was kicked (watch the event)
    DR_Resolved = 1,  // fully resolved; fields below are valid for this linkType
    DR_Failed   = 2,  // last fetch failed and is in cooldown; retryable on a later query
};

// Bound status for item links (mirrors GW2 item flags, first-match-wins order).
enum DecoderBound : uint8_t {
    DB_None = 0, DB_AccountOnAcquire = 1, DB_SoulOnAcquire = 2,
    DB_AccountOnUse = 3, DB_SoulOnUse = 4,
};

// POI kind for waypoint/map links.
enum DecoderPoiKind : uint8_t { DP_Waypoint = 0, DP_PointOfInterest = 1, DP_Vista = 2 };

// Item rarity (mirrors the GW2 /v2/items "rarity" string set). DR_RarityUnknown is
// the not-yet-resolved value and what an old disk-cache entry (lacking the field)
// reads back as — consumers must treat it as "unknown", never as a real tier.
enum DecoderRarity : uint8_t {
    DR_RarityUnknown = 0,  // unresolved, or read from a pre-v2 cache lacking rarity
    DR_Junk       = 1, DR_Basic    = 2, DR_Fine     = 3, DR_Masterwork = 4,
    DR_Rare       = 5, DR_Exotic   = 6, DR_Ascended = 7, DR_Legendary  = 8,
};

// One pre-formatted tooltip fact: a render-service icon URL + a label. Used by
// both skill links (e.g. "Range: 1200") and item links (e.g. "Defense: 381",
// "+85 Power"). Item facts carry no icon (icon == "").
struct DecoderFact {
    char icon[128];   // render-service icon URL ("" if none)
    char text[160];   // pre-formatted label, e.g. "Range: 1200", "Defense: 381"
};

// Versioned, fixed-size POD metadata record. Trivially copyable; safe in shared
// memory and event payloads. Variable-length data is bounded (facts[16]).
struct DecoderRecord {
    uint16_t schemaVersion;   // == DECODER_RING_API_VERSION. FIRST FIELD. Always check.
    uint8_t  linkType;        // correlation key part 1: ChatLinkType byte (0x02 item, ...)
    uint8_t  status;          // DecoderStatus
    uint32_t id;              // correlation key part 2: resolved id

    char     name[128];       // display name / build label / waypoint name; "" if unresolved
    char     iconUrl[256];    // render-service icon URL; may be "" (consumer downloads it)

    // --- Item (linkType == 0x02) ---
    uint8_t  bound;           // DecoderBound
    uint8_t  noSell;          // 1 = cannot vendor to an NPC (does NOT imply untradeable)
    uint8_t  tradeable;       // 1 = eligible for the trading post
    uint8_t  rarity;          // DecoderRarity; DR_RarityUnknown if unresolved / pre-v2 cache
    int32_t  vendorValue;     // item (0x02): vendor sale value, copper.
                              // recipe (0x09): holds the output item's id (resolve it as
                              // a 0x02 link to render the output item's full tooltip).

    // --- Skill (0x06) AND Item (0x02) [description/facts: schemaVersion >= 3] ---
    // Shared by both link types: skills fill these with their description + fact
    // lines; items (v3+) fill them with flavour text + tooltip lines (defense,
    // attributes, infusion slots, rune bonuses, required level, weight, subtype).
    char     description[512]; // flavour / description text
    uint8_t  factCount;        // number of valid entries in facts[]
    uint8_t  _pad1[3];
    DecoderFact facts[16];     // pre-formatted facts; entries past 16 are dropped

    // --- Recipe (linkType == 0x09) ---
    // name = "Recipe: <output> [(Rarity)]"
    // iconUrl = output item's icon
    // facts[] = ingredient lines + "Required Rating: N" (entry count in factCount)
    // vendorValue = output item id (resolve as 0x02 item link for full tooltip)

    // --- Waypoint / POI (linkType == 0x04) ---
    char     mapName[96];      // map the POI sits on
    uint8_t  poiType;          // DecoderPoiKind
    uint8_t  _pad2[3];

    // Build/AE2 (linkType == 0x0D): the spec label (e.g. "Mirage Build") is in name[].
};

// Volatile trading-post price. In-memory-only on the service, ~5-min TTL, never
// disk. Queried separately — NOT part of the durable DecoderRecord.
struct DecoderPrice {
    int32_t buy;   // highest buy order, copper; -1 = no listings that side
    int32_t sell;  // lowest sell listing, copper; -1 = no listings that side
};

// Exported function table, published in shared memory under DECODER_RING_DATALINK.
struct DecoderRingApi {
    uint32_t apiVersion;   // == DECODER_RING_API_VERSION. FIRST FIELD. Check before calling.
    // Warm resolve. Returns immediately; never blocks on the network.
    //   DR_Resolved -> *out filled from warm cache.
    //   DR_NotReady -> background fetch kicked (or already in flight); watch the event.
    //   DR_Failed   -> last fetch failed, still in cooldown (retryable later).
    // chatCode: the full "[&...]" chat link. REQUIRED for build links (LINK_BUILD,
    // which carry no integer id); may be NULL for item/skin/skill/waypoint where
    // (linkType,id) fully identifies the target. The returned record (even when
    // NotReady) carries the (linkType,id) correlation key to match the event by.
    DecoderStatus (*Resolve)(uint8_t linkType, uint32_t id, const char* chatCode, DecoderRecord* out);
    // Volatile price. DR_Resolved with *out filled, or DR_NotReady (fetch kicked).
    DecoderStatus (*QueryPrice)(uint32_t itemId, DecoderPrice* out);
    // Returns the language DR is CURRENTLY resolving names in, as a stable, immortal
    // literal: "en" | "de" | "fr" | "es". This is the RESOLVED language — after applying
    // the auto/override selection and the en-fallback for unsupported locales — i.e. the
    // language item/skin/skill/recipe names actually come back in. Never null; never blocks.
    // Added in apiVersion 5; gate calls on apiVersion >= 5.
    const char* (*GetActiveLanguage)();
};
