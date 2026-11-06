// channel.hpp — the channels of GW2's chat: their bit, their name in the UI language, their chat command.
#pragma once

#include <cstdint>
#include <string>

namespace gct {

enum class Channel : uint8_t { Unknown, Say, Map, Party, Squad, Team, Whisper, Guild, System };

inline uint32_t ChannelBit(Channel c) { return 1u << static_cast<uint32_t>(c); }

std::wstring ChannelLabel(Channel c);      // UI label in the UI language: "Map", "Party" ...
const wchar_t* ChannelCommand(Channel c);  // "/m", "/p" ... or nullptr

}  // namespace gct
