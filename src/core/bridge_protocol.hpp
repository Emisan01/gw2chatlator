// bridge_protocol.hpp — what the optional Nexus add-on "GW2 Chat Bridge" (addons/nexus) sends to our programs.
// One direction only: the add-on writes, the program reads. Nothing ever goes back into the game process, so
// nothing that arrives here can make the add-on do anything (invariants 1 and 4). Shared by both sides, portable.
#pragma once

#include <string>

namespace gct {

constexpr int kBridgeProtocol = 1;
constexpr size_t kBridgeMaxLine = 8192;  // longer lines are dropped by the reader
// Named pipe (the program creates it with a DACL for the current user only; the add-on connects as a client).
constexpr const wchar_t* kBridgePipeName = L"\\\\.\\pipe\\GW2ChatBridge";

struct BridgeMessage {
    enum class Kind { Hello, Chat } kind = Kind::Chat;
    // Hello: sent once per connection.
    int protocol = kBridgeProtocol;
    std::string addonVersion;   // "1.5.0"
    bool extras = false;        // Unofficial Extras is loaded in the game (else no party/squad line can come)
    // Chat: one party/squad line from Unofficial Extras – exact text, no OCR.
    std::string channel;        // "party" | "squad"
    int subgroup = -1;          // 0..14 = sent to that subgroup, -1 = whole squad / party
    bool broadcast = false;     // commander broadcast
    std::string time;           // iso8601 server time, e.g. "2022-07-09T11:45:24.888Z"
    std::string account;        // ":Name.1234" as sent by the game
    std::string character;      // character name
    std::string text;           // the message, UTF-8
};

// One JSON object per line, '\n' at the end. Text is cut to stay below kBridgeMaxLine.
std::string EncodeBridgeMessage(const BridgeMessage& m);
// Reads one line (with or without '\n'). False for anything malformed, unknown or too long.
bool DecodeBridgeMessage(const std::string& line, BridgeMessage& out);

}  // namespace gct
