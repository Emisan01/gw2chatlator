// chat_tabs.hpp — tabs like in the GW2 chat panel: each tab shows a chosen
// set of channels. Stored in the ini as "Name|say,party,whisper".
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "chat_line.hpp"

namespace gct {

using ChannelMask = uint16_t;

constexpr ChannelMask ChannelBit(Channel c) { return static_cast<ChannelMask>(1u << static_cast<unsigned>(c)); }

// Every channel, including system lines and lines whose colour matched no channel.
ChannelMask AllChannels();

struct ChatTab {
    uint32_t id = 0;  // stable while the program runs (lines sent from a tab stay in it)
    std::wstring name;
    ChannelMask channels = 0;
};

// "Chat" (everything) and "Whisper" (names in the UI language).
std::vector<ChatTab> DefaultTabs();

// Templates for "New tab": All, Party & Squad, Guild, Map & Say, Team (WvW), Whisper.
std::vector<ChatTab> TabPresets();

// The channels a user can tick for a tab, in menu order.
const std::vector<Channel>& TabChannels();
// Menu label: like ChannelLabel, but "System lines" / "Other" for System / Unknown.
std::wstring TabChannelLabel(Channel c);

inline bool TabShows(const ChatTab& t, Channel c) { return (t.channels & ChannelBit(c)) != 0; }

// The one channel you can write to in this tab, if it shows exactly one
// (Whisper included); Unknown otherwise — then "the active channel" is used.
Channel SoleSendChannel(ChannelMask m);

// "Party|party,squad". Names may not contain '|'. Unknown channel keys are
// ignored; a tab without any known channel is rejected.
std::wstring SerializeTab(const ChatTab& t);
bool ParseTab(const std::wstring& s, ChatTab& out);

}  // namespace gct
