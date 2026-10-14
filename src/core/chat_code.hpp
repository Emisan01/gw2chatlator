// chat_code.hpp — GW2 chat links ("[&AgGvWwEA]"): what they point at, so the chat can show the item's name the way
// the game does. Portable (no windows.h); the names come from the public GW2 API (win side).
#pragma once

#include <cstdint>
#include <string>

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

// The chat text of Events: Chat escapes brackets ("\[&AgGvWwEA\]"): back to what the game shows.
std::wstring UnescapeChatText(const std::wstring& s);

// GW2's colour of an item rarity ("Exotic" -> 0xFFA405), 0 when unknown.
uint32_t RarityColor(const std::string& rarity);

}  // namespace gct
