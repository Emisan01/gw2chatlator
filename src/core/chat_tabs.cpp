// chat_tabs.cpp
#include "chat_tabs.hpp"

#include "text.hpp"

namespace gct {

namespace {

struct Key {
    Channel channel;
    const wchar_t* key;
};
constexpr Key kKeys[] = {
    {Channel::Say, L"say"},         {Channel::Map, L"map"},       {Channel::Party, L"party"},
    {Channel::Squad, L"squad"},     {Channel::Team, L"team"},     {Channel::Guild, L"guild"},
    {Channel::Whisper, L"whisper"}, {Channel::System, L"system"}, {Channel::Unknown, L"other"},
};

constexpr ChannelMask kSendable = ChannelBit(Channel::Say) | ChannelBit(Channel::Map) | ChannelBit(Channel::Party) |
                                  ChannelBit(Channel::Squad) | ChannelBit(Channel::Team) |
                                  ChannelBit(Channel::Guild) | ChannelBit(Channel::Whisper);

ChatTab Tab(const wchar_t* name, std::initializer_list<Channel> channels) {
    ChatTab t;
    t.name = name;
    for (Channel c : channels) t.channels |= ChannelBit(c);
    return t;
}

}  // namespace

ChannelMask AllChannels() {
    ChannelMask m = 0;
    for (const Key& k : kKeys) m |= ChannelBit(k.channel);
    return m;
}

std::vector<ChatTab> DefaultTabs() {
    ChatTab all;
    all.name = L"Chat";
    all.channels = AllChannels();
    return {all, Tab(L"Fl\u00fcstern", {Channel::Whisper})};
}

const std::vector<ChatTab>& TabPresets() {
    static const std::vector<ChatTab> presets = [] {
        ChatTab all;
        all.name = L"Alles";
        all.channels = AllChannels();
        return std::vector<ChatTab>{
            all,
            Tab(L"Gruppe", {Channel::Party, Channel::Squad}),
            Tab(L"Gilde", {Channel::Guild}),
            Tab(L"Karte", {Channel::Map, Channel::Say}),
            Tab(L"WvW", {Channel::Team, Channel::Squad}),
            Tab(L"Fl\u00fcstern", {Channel::Whisper}),
        };
    }();
    return presets;
}

const std::vector<Channel>& TabChannels() {
    static const std::vector<Channel> channels = [] {
        std::vector<Channel> v;
        for (const Key& k : kKeys) v.push_back(k.channel);
        return v;
    }();
    return channels;
}

const wchar_t* TabChannelLabel(Channel c) {
    if (c == Channel::System) return L"Systemzeilen";
    if (c == Channel::Unknown) return L"Sonstige (Farbe unbekannt)";
    return ChannelLabel(c);
}

Channel SoleSendChannel(ChannelMask m) {
    const ChannelMask s = m & kSendable;
    if (s == 0 || (s & (s - 1)) != 0) return Channel::Unknown;  // none or several
    for (const Key& k : kKeys)
        if (ChannelBit(k.channel) == s) return k.channel;
    return Channel::Unknown;
}

std::wstring SerializeTab(const ChatTab& t) {
    std::wstring s;
    for (wchar_t c : t.name) s += (c == L'|' || c == L'\r' || c == L'\n') ? L' ' : c;
    s += L'|';
    bool first = true;
    for (const Key& k : kKeys) {
        if (!(t.channels & ChannelBit(k.channel))) continue;
        if (!first) s += L',';
        s += k.key;
        first = false;
    }
    return s;
}

bool ParseTab(const std::wstring& s, ChatTab& out) {
    const size_t bar = s.find(L'|');
    if (bar == std::wstring::npos) return false;
    ChatTab t;
    t.name = Trim(s.substr(0, bar));
    if (t.name.empty()) return false;
    std::wstring list = s.substr(bar + 1);
    size_t start = 0;
    while (start <= list.size()) {
        size_t end = list.find(L',', start);
        if (end == std::wstring::npos) end = list.size();
        const std::wstring key = ToLowerAscii(Trim(list.substr(start, end - start)));
        for (const Key& k : kKeys)
            if (key == k.key) t.channels |= ChannelBit(k.channel);
        start = end + 1;
    }
    if (!t.channels) return false;
    out = t;
    return true;
}

}  // namespace gct
