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
    std::wstring person;  // a whisper tab for one player (opened by clicking the name); empty otherwise
};

// At most this many whisper tabs for single players; the oldest is reused.
constexpr size_t kMaxPersonTabs = 5;

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

// "Party|party,squad", a player's whisper tab "Rook|whisper|@Rook". Names may
// not contain '|'. Unknown channel keys are ignored; a tab without any known
// channel is rejected.
std::wstring SerializeTab(const ChatTab& t);

// "whisper,party,squad" <-> mask (the keys of SerializeTab; unknown keys ignored).
std::wstring SerializeChannels(ChannelMask m);
ChannelMask ParseChannels(const std::wstring& list);

// What is translated without a click: every chat channel (system lines are
// game notices). Exceptions are chosen by the player: languages not to
// translate, channels unticked; a click on a line still translates it.
ChannelMask DefaultAutoTranslate();
bool ParseTab(const std::wstring& s, ChatTab& out);

}  // namespace gct
