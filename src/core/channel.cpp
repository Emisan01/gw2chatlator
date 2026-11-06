// channel.cpp
#include "channel.hpp"

#include "i18n.hpp"

namespace gct {

std::wstring ChannelLabel(Channel c) {
    switch (c) {
        case Channel::Say: return Tr(L"Say");
        case Channel::Map: return Tr(L"Map");
        case Channel::Party: return Tr(L"Party");
        case Channel::Squad: return Tr(L"Squad");
        case Channel::Team: return Tr(L"Team");
        case Channel::Whisper: return Tr(L"Whisper");
        case Channel::Guild: return Tr(L"Guild");
        case Channel::System: return Tr(L"System");
        default: return L"";
    }
}

const wchar_t* ChannelCommand(Channel c) {
    switch (c) {
        case Channel::Say: return L"/s";
        case Channel::Map: return L"/m";
        case Channel::Party: return L"/p";
        case Channel::Squad: return L"/d";
        case Channel::Team: return L"/t";
        case Channel::Guild: return L"/g";
        case Channel::Whisper: return L"/w";
        default: return nullptr;
    }
}

}  // namespace gct
