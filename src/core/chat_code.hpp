// chat_code.hpp — GW2 chat links ("[&AgGvWwEA]"): what they point at, so the chat can show the item's name the way
// the game does. Portable (no windows.h); the names come from the public GW2 API (win side).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gct {

enum class ChatCodeKind { Unknown, Coin, Item, Text, Poi, Skill, Trait, Recipe, Skin, Outfit, Objective, Build, Achievement };

struct ChatCode {
    ChatCodeKind kind = ChatCodeKind::Unknown;
    uint32_t id = 0;     // item / skill / trait / recipe / skin / outfit / achievement / PoI id; coins: copper
    int count = 1;       // items: the stack size
};

// "[&AgGvWwEA]" (with or without the brackets) -> what it links; kind Unknown when it cannot be read.
ChatCode DecodeChatCode(const std::wstring& code);

// The GW2 API path for its name ("/v2/items?ids=23471"), empty when there is none (coins, PoIs, builds ...).
std::wstring ChatCodeApiPath(const ChatCode& c);

// Asking the GW2 API for several names at once: "/v2/items?lang=de&ids=1,2" (`what`: items, skills, traits ...).
std::wstring ChatCodeNamesPath(const wchar_t* what, const std::vector<uint32_t>& ids, const std::wstring& lang);
// The language the API knows for a reading language ("DE", "zh-Hans"): de, fr, es, zh, otherwise en.
std::wstring ChatCodeApiLang(const std::wstring& readLang);
// WvW objectives are asked by "map-id" ("95-34").
std::wstring ChatCodeObjectivesPath(const std::vector<std::wstring>& ids, const std::wstring& lang);
// What it answers. Entries without a name and anything that is not such a list give nothing.
struct ChatCodeName {
    uint32_t id = 0;
    uint32_t mapId = 0;  // a WvW objective's map
    std::string name;    // UTF-8; a recipe has none, only what it makes
    std::string rarity;  // items only
    std::string type, description;
    int level = -1, rating = -1;  // an item's level, what a recipe needs of the crafter
    uint32_t outputItem = 0;      // a recipe: what it makes
};
std::vector<ChatCodeName> ParseChatCodeNames(const std::string& body);
// An API description for a tooltip: the game's colour tags gone, one line, at most about 400 bytes.
std::string CleanChatCodeText(const std::string& text);
// One line of the waypoint table (tools/gen_pois.py): id, type, name, map, continent, floor, x, y, split by tabs.
struct ChatCodePoi {
    uint32_t id = 0;
    std::string type, name, map;
    int continent = 1, floor = 1;
    float x = 0, y = 0;
};
bool ParseChatCodePoi(const std::string& line, ChatCodePoi* out);
// Text holds a chat code ("[&BCkEAAA=]", also among other words).
bool HoldsChatCode(const std::wstring& text);
// Names the place of a link in GW2's chat: it moves with your character's name, the window's size and GW2's interface size.
std::wstring ChatCodePlaceKey(const std::wstring& name, int width, int height, int uiSize);

// The chat text of Events: Chat escapes brackets ("\[&AgGvWwEA\]"): back to what the game shows.
std::wstring UnescapeChatText(const std::wstring& s);

// GW2's colour of an item rarity ("Exotic" -> 0xFFA405), 0 when unknown.
uint32_t RarityColor(const std::string& rarity);

}  // namespace gct
