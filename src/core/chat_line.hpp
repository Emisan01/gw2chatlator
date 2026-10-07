// chat_line.hpp — turns OCR'd GW2 chat lines into messages:
// channel (by colour or channel tag), speaker, text; wrapped lines merged.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gct {

enum class Channel : uint8_t { Unknown, Say, Map, Party, Squad, Team, Whisper, Guild, System };

std::wstring ChannelLabel(Channel c);      // UI label in the UI language: "Map", "Party" ...
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

struct OcrWord {
    std::wstring text;
    Rgb color;            // colour of this word's text pixels
    bool hasColor = false;
};

struct OcrLine {
    std::wstring text;
    Rgb color;      // average colour of the text pixels
    int top = 0;    // position inside the captured area (pixels)
    int height = 0;
    int left = 0;   // horizontal extent of the recognized words (0/0 = unknown)
    int width = 0;
    std::vector<OcrWord> words{};  // optional: per-word colours (the channel tag / name colour wins)
};

// Colour that decides the channel: the first coloured words after the
// timestamp (tag and speaker name), else the line average.
Rgb LeadColor(const OcrLine& line);
// Colour of the message text: the last coloured words, else the line average.
Rgb TailColor(const OcrLine& line);

struct ChatMessage {
    Channel channel = Channel::Unknown;
    bool outgoingWhisper = false;  // "To Name: ..." — your own whisper
    std::wstring speaker;          // "Emi" (guild tag removed)
    std::wstring text;             // the message itself
    std::wstring raw;              // the full line(s) as read
    Rgb color;                     // text colour of its first line (for calibration)
    bool stamped = false;          // started with a timestamp (OCR-tolerant)
    bool tagOnly = false;          // only a timestamp / channel tag, no text (the input line)
    bool freeText = false;         // from a free screen area: a paragraph, no chat rules
};

// False for OCR noise: mostly symbols, no real word (e.g. a window being
// dragged over the chat, half-covered letters).
bool LooksLikeChatText(const std::wstring& text);

// Length of an OCR-mangled timestamp at the start of `s` ("[19:17]" read as
// "C9;17J", "1927 J", "19:17)", "t9\u202220J" ...), 0 if none.
size_t OcrTimestampLength(const std::wstring& s);

// Length of a (possibly OCR-mangled) channel tag at the start of `s`
// ("[M]", "CSJ", "[Sagen)", "CKontakteJ"), 0 if none. `channel` receives the
// channel (System for contact/friend notices).
size_t FuzzyTagLength(const std::wstring& s, Channel* channel);

// Length of a timestamp with misread brackets plus a one-letter channel tag
// at the start of `s` ("117:46J[M]", "(17-46)(M)", "[17:48J1IWJ]"), 0 if none.
size_t MangledStampAndTagLength(const std::wstring& s, Channel* channel);

// One visual line: optional timestamp, optional channel tag ([Map], [M] ...),
// optional guild tag, "Speaker: text". Lines without a speaker (system
// messages, emotes, wrapped continuations) have an empty speaker.
// `tagChannel` receives the channel from a tag, Unknown if none.
ChatMessage ParseChatLine(const std::wstring& line, Channel* tagChannel = nullptr);

// Where the GW2 chat is in a picture of the game's corner: several lines that
// start with a timestamp, aligned on the left (the chat's lines begin at the
// same place), followed by their wrapped lines. `area` gets the block of
// chat lines in the picture's pixels. Needs the lines' left/width.
struct ChatBlock {
    int left = 0, top = 0, right = 0, bottom = 0;
    int stamped = 0;  // how many timestamp lines were found
};
bool LocateChatLines(const std::vector<OcrLine>& lines, ChatBlock* area);

// All lines of one capture, top to bottom -> messages. Continuation lines
// (no timestamp, no tag, no speaker, same colour, directly below) are
// appended to the message above. Whisper prefixes ("From"/"To", "Von"/"An",
// "De"/"\u00c0" ...) are removed from the speaker. When the capture shows
// timestamps, everything above the first stamped line (the tab bar, a
// message cut off at the top) is skipped; lines that are only a tag (the
// input line) and messages without text are dropped.
std::vector<ChatMessage> BuildMessages(const std::vector<OcrLine>& lines, const std::vector<ChannelColor>& palette);

// Free screen area (any text, not a chat): lines that follow closely are one
// paragraph; a larger gap or a clearly different indent starts a new one.
// Long paragraphs are cut after a sentence (translators take ~450 characters
// well). No timestamps, names or channels are looked for.
std::vector<ChatMessage> BuildFreeTextMessages(const std::vector<OcrLine>& lines, size_t maxChars = 450);

}  // namespace gct
