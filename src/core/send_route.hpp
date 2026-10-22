// send_route.hpp — where a message goes: the language it is sent in and whether the translator has work. One pure
// function of the state, so its equations can be checked for every state (core_tests): the footer's "Send" choice for
// what you type, "Translate what I say" first for what you said (2.3.0). Portable, no windows.h.
#pragma once

#include <string>
#include <vector>

namespace gct {

struct SendState {
    bool spoken = false;          // the input began with what you said
    bool voiceOn = false;         // voice input is switched on
    bool voiceTranslate = false;  // "Translate what I say"
    std::wstring tabLang;         // a whisper tab's language ("=" = as written, "" = none – the footer decides)
    int sendIdx = 0;              // the footer's "Send": an index into writeLangs; writeLangs.size() = as written
    const std::vector<std::wstring>* writeLangs = nullptr;
    std::wstring dictLang;        // what whisper heard ("EN" after its own translation)
    std::wstring typingLang;      // the language you write in
};

struct SendRoute {
    std::wstring lang;         // the language it goes out in; "" = as written
    bool translating = false;  // the translator has work: the language differs from the one it was written / said in
};

// Spoken and "Translate what I say" on (and voice on): the tab's language, else the footer's, else – the footer says
// "as written" – the first of yours. Otherwise: the tab's language, else the footer's choice. Never "as written" for a
// language GW2's chat cannot show (SendableInGw2): then the first of yours (EN-GB without any).
SendRoute RouteMessage(const SendState& s);

}  // namespace gct
