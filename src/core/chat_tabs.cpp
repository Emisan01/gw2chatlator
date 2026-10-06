// chat_tabs.cpp
#include "chat_tabs.hpp"

#include "i18n.hpp"
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

ChatTab Tab(const std::wstring& name, std::initializer_list<Channel> channels) {
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
    // Stored in English; the window shows default names in the UI language.
    return {all};  // one tab with everything; more via right-click on the tab
}

std::vector<ChatTab> TabPresets() {
    ChatTab all;
    all.name = Tr(L"All");
    all.channels = AllChannels();
    return {
        all,
        Tab(Tr(L"Party"), {Channel::Party, Channel::Squad}),
        Tab(Tr(L"Guild"), {Channel::Guild}),
        Tab(Tr(L"Map"), {Channel::Map, Channel::Say}),
        Tab(L"WvW", {Channel::Team, Channel::Squad}),
        Tab(Tr(L"Whisper"), {Channel::Whisper}),
    };
}

const std::vector<Channel>& TabChannels() {
    static const std::vector<Channel> channels = [] {
        std::vector<Channel> v;
        for (const Key& k : kKeys) v.push_back(k.channel);
        return v;
    }();
    return channels;
}

std::wstring TabChannelLabel(Channel c) {
    if (c == Channel::System) return Tr(L"System lines");
    if (c == Channel::Unknown) return Tr(L"Other (unknown colour)");
    return ChannelLabel(c);
}

Channel SoleSendChannel(ChannelMask m) {
    const ChannelMask s = m & kSendable;
    if (s == 0 || (s & (s - 1)) != 0) return Channel::Unknown;  // none or several
    for (const Key& k : kKeys)
        if (ChannelBit(k.channel) == s) return k.channel;
    return Channel::Unknown;
}

std::wstring SerializeChannels(ChannelMask m) {
    std::wstring s;
    for (const Key& k : kKeys) {
        if (!(m & ChannelBit(k.channel))) continue;
        if (!s.empty()) s += L',';
        s += k.key;
    }
    return s;
}

ChannelMask ParseChannels(const std::wstring& list) {
    ChannelMask m = 0;
    size_t start = 0;
    while (start <= list.size()) {
        size_t end = list.find(L',', start);
        if (end == std::wstring::npos) end = list.size();
        const std::wstring key = ToLowerAscii(Trim(list.substr(start, end - start)));
        for (const Key& k : kKeys)
            if (key == k.key) m |= ChannelBit(k.channel);
        start = end + 1;
    }
    return m;
}

ChannelMask DefaultAutoTranslate() { return AllChannels() & ~ChannelBit(Channel::System); }

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
    if (!t.person.empty()) {
        s += L"|@";
        for (wchar_t c : t.person) s += (c == L'|' || c == L'\r' || c == L'\n') ? L' ' : c;
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
    if (const size_t at = list.find(L"|@"); at != std::wstring::npos) {
        t.person = Trim(list.substr(at + 2));
        list.erase(at);
    }
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
