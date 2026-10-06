// chat_line.hpp — turns OCR'd GW2 chat lines into messages:
// channel (by colour or channel tag), speaker, text; wrapped lines merged.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gct {

enum class Channel : uint8_t { Unknown, Say, Map, Party, Squad, Team, Whisper, Guild, System };

const wchar_t* ChannelLabel(Channel c);    // German UI label: "Karte", "Gruppe" ...
const wchar_t* ChannelCommand(Channel c);  // "/m", "/p" ... or nullptr

struct Rgb {
    uint8_t r = 0, g = 0, b = 0;
    bool operator==(const Rgb& o) const { return r == o.r && g == o.g && b == o.b; }
};

std::wstring RgbToHex(Rgb c);                           // "f0a0a0"
bool RgbFromHex(const std::wstring& hex, Rgb& out);     // "#F0A0A0" or "f0a0a0"

struct ChannelColor {
    Channel channel;
    Rgb rgb;
};

// GW2 defaults (Chat panel wiki: Say green, Map pale red, Party blue, Squad
// pale green, Team bright red, Whisper purple, Guild gold). Approximate —
// players can calibrate them in the INI.
std::vector<ChannelColor> DefaultChannelColors();

// Nearest palette colour; Unknown if nothing is close (e.g. white system text).
Channel ClassifyColor(Rgb c, const std::vector<ChannelColor>& palette);

struct OcrLine {
    std::wstring text;
    Rgb color;      // average colour of the text pixels
    int top = 0;    // position inside the captured area (pixels)
    int height = 0;
};

struct ChatMessage {
    Channel channel = Channel::Unknown;
    bool outgoingWhisper = false;  // "To Name: ..." — your own whisper
    std::wstring speaker;          // "Emi" (guild tag removed)
    std::wstring text;             // the message itself
    std::wstring raw;              // the full line(s) as read
    Rgb color;                     // text colour of its first line (for calibration)
};

// One visual line: optional timestamp, optional channel tag ([Map], [M] ...),
// optional guild tag, "Speaker: text". Lines without a speaker (system
// messages, emotes, wrapped continuations) have an empty speaker.
// `tagChannel` receives the channel from a tag, Unknown if none.
ChatMessage ParseChatLine(const std::wstring& line, Channel* tagChannel = nullptr);

// All lines of one capture, top to bottom -> messages. Continuation lines
// (no speaker, same colour, directly below) are appended to the message
// above. Whisper prefixes ("From"/"To", "Von"/"An", "De"/"À" ...) are
// removed from the speaker.
std::vector<ChatMessage> BuildMessages(const std::vector<OcrLine>& lines, const std::vector<ChannelColor>& palette);

}  // namespace gct
