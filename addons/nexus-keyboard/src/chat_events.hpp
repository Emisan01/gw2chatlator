// chat_events.hpp — our own declaration of what the Nexus add-on "Events: Chat" (Vonsh, Nexus library id 0x67770263,
// https://github.com/jsantorek/GW2-Chat) raises: the Nexus event "EV_CHAT:Message" with a `const ChatEventsMessage*`
// for every chat line of every channel. Only the parts we read are declared; the layout follows its public
// interface (its repository has no licence file, so its header is not copied). Checked by static_asserts (x64).
#pragma once

#include <cstddef>
#include <cstdint>

namespace bridge {

constexpr uint32_t kChatEventsSignature = 0x67770263;
constexpr const char* kChatEventsEvent = "EV_CHAT:Message";

enum class ChatEventsType : int32_t {
    Error = 0,
    Guild,
    GuildMotD,
    Local,  // "say"
    Map,
    Party,
    Squad,
    SquadMessage,    // a commander's squad message (text only)
    SquadBroadcast,
    TeamPvP,
    TeamWvW,
    Whisper,
    Emote,
    EmoteCustom
};

constexpr uint32_t kChatEventsWhisperFromMe = 1u << 4;  // flags of a Whisper: you sent it
constexpr uint32_t kChatEventsBroadcast = 1u << 3;      // flags of a SquadMessage: a broadcast

struct ChatEventsGeneric {   // Guild (as its base), Local, Map, Party, Squad, TeamPvP, TeamWvW (as its base), Whisper
    uint8_t account[16];     // GUID
    const char* characterName;  // UTF-8, null-terminated
    const char* accountName;    // may be null
    const char* content;
};

struct ChatEventsMessage {
    uint32_t timeLow, timeHigh;  // FILETIME
    ChatEventsType type;
    uint32_t flags;
    union {
        ChatEventsGeneric generic;
        const char* squadMessage;  // SquadMessage / SquadBroadcast: the text only
    };
};

static_assert(sizeof(void*) != 8 || sizeof(ChatEventsGeneric) == 40, "Events: Chat layout changed");
static_assert(sizeof(void*) != 8 || offsetof(ChatEventsMessage, generic) == 16, "Events: Chat layout changed");

}  // namespace bridge
