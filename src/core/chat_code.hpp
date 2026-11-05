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
// What it answers. Entries without a name and anything that is not such a list give nothing.
struct ChatCodeName {
    uint32_t id = 0;
    std::string name;    // UTF-8
    std::string rarity;  // items only
};
std::vector<ChatCodeName> ParseChatCodeNames(const std::string& body);

// The chat text of Events: Chat escapes brackets ("\[&AgGvWwEA\]"): back to what the game shows.
std::wstring UnescapeChatText(const std::wstring& s);

// GW2's colour of an item rarity ("Exotic" -> 0xFFA405), 0 when unknown.
uint32_t RarityColor(const std::string& rarity);

}  // namespace gct
