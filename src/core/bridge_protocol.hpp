// bridge_protocol.hpp — what the senders of chat lines write into GW2 Keyboard's pipe: the optional Nexus add-on
// "GW2 Chat Bridge" (addons/nexus, party/squad from Unofficial Extras) and the OCR reader (everything visible in the
// chat area). One direction only: the senders write, the program reads – nothing ever goes back (the add-on lives in
// the game process; nothing that arrives here can make it do anything – invariants 1 and 4). Shared by all sides,
// portable. See docs/NEXUS_PLAN.md and docs/READER_PLAN.md.
#pragma once

#include <string>

namespace gct {

constexpr int kBridgeProtocol = 1;
constexpr size_t kBridgeMaxLine = 8192;  // longer lines are dropped by the reader
// Named pipe (the program creates it with a DACL for the current user only; the senders connect as clients).
constexpr const wchar_t* kBridgePipeName = L"\\\\.\\pipe\\GW2ChatBridge";

struct BridgeMessage {
    enum class Kind { Hello, Chat } kind = Kind::Chat;
    // Where a line comes from: "extras" (the Nexus add-on, party/squad from Unofficial Extras), "chat" (the Nexus
    // add-on, every channel from Vonsh's "Events: Chat") or "ocr" (the reader). Older add-ons send none (= extras).
    std::string src = "extras";
    // Hello: sent once per connection.
    int protocol = kBridgeProtocol;
    std::string addonVersion;   // "1.5.0"
    bool extras = false;        // Unofficial Extras is loaded in the game (party/squad arrive exactly)
    bool chatEvents = false;    // "Events: Chat" is loaded in the game (every channel arrives exactly)
    // Chat: one line – exact text (extras) or read from the screen (ocr).
    std::string channel;        // "party" "squad" "map" "say" "whisper" "guild" "team" "system" "" (unknown)
    bool outgoing = false;      // a whisper you sent ("To Name: ...")
    int subgroup = -1;          // 0..14 = sent to that subgroup, -1 = whole squad / party
    bool broadcast = false;     // commander broadcast
    std::string time;           // iso8601 server time, e.g. "2022-07-09T11:45:24.888Z"
    std::string account;        // ":Name.1234" as sent by the game
    std::string character;      // character name (the speaker)
    std::string text;           // the message, UTF-8
};

// The channels a line may carry (anything else is read as unknown "").
bool IsBridgeChannel(const std::string& channel);

// One JSON object per line, '\n' at the end. Text is cut to stay below kBridgeMaxLine.
std::string EncodeBridgeMessage(const BridgeMessage& m);
// Reads one line (with or without '\n'). False for anything malformed, unknown or too long.
bool DecodeBridgeMessage(const std::string& line, BridgeMessage& out);

}  // namespace gct
