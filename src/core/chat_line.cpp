// chat_line.cpp
#include "chat_line.hpp"

#include <algorithm>
#include <cmath>

#include "text.hpp"

namespace gct {

const wchar_t* ChannelLabel(Channel c) {
    switch (c) {
        case Channel::Say: return L"Sagen";
        case Channel::Map: return L"Karte";
        case Channel::Party: return L"Gruppe";
        case Channel::Squad: return L"Trupp";
        case Channel::Team: return L"Team";
        case Channel::Whisper: return L"Fl\u00fcstern";
        case Channel::Guild: return L"Gilde";
        case Channel::System: return L"System";
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

std::wstring RgbToHex(Rgb c) {
    static const wchar_t* hex = L"0123456789abcdef";
    std::wstring s;
    for (uint8_t v : {c.r, c.g, c.b}) {
        s += hex[v >> 4];
        s += hex[v & 15];
    }
    return s;
}

bool RgbFromHex(const std::wstring& in, Rgb& out) {
    std::wstring h = Trim(in);
    if (!h.empty() && h[0] == L'#') h.erase(0, 1);
    if (h.size() != 6) return false;
    auto nib = [](wchar_t c) -> int {
        if (c >= L'0' && c <= L'9') return c - L'0';
        if (c >= L'a' && c <= L'f') return c - L'a' + 10;
        if (c >= L'A' && c <= L'F') return c - L'A' + 10;
        return -1;
    };
    int v[6];
    for (int i = 0; i < 6; ++i)
        if ((v[i] = nib(h[i])) < 0) return false;
    out = {static_cast<uint8_t>(v[0] * 16 + v[1]), static_cast<uint8_t>(v[2] * 16 + v[3]),
           static_cast<uint8_t>(v[4] * 16 + v[5])};
    return true;
}

std::vector<ChannelColor> DefaultChannelColors() {
    return {
        {Channel::Say, {125, 220, 105}},     // green
        {Channel::Map, {240, 165, 155}},     // pale red
        {Channel::Party, {110, 175, 255}},   // blue
        {Channel::Squad, {200, 245, 185}},   // pale green
        {Channel::Team, {255, 85, 75}},      // bright red
        {Channel::Whisper, {200, 140, 255}}, // purple
        {Channel::Guild, {245, 200, 80}},    // gold
        {Channel::Guild, {235, 215, 160}},   // pale gold (guild you don't represent)
    };
}

namespace {

struct Chroma {
    double r, g, b;
    bool grey;
};

// Brightness-independent colour: anti-aliased text averages darker or
// lighter than the font colour, but keeps its ratios.
Chroma ToChroma(Rgb c) {
    const int mx = std::max({c.r, c.g, c.b});
    const int mn = std::min({c.r, c.g, c.b});
    if (mx < 50) return {0, 0, 0, true};
    const double sat = (mx - mn) / static_cast<double>(mx);
    return {c.r / static_cast<double>(mx), c.g / static_cast<double>(mx), c.b / static_cast<double>(mx), sat < 0.12};
}

double ChromaDistance(const Chroma& a, const Chroma& b) {
    return std::sqrt((a.r - b.r) * (a.r - b.r) + (a.g - b.g) * (a.g - b.g) + (a.b - b.b) * (a.b - b.b));
}

bool ColorsClose(Rgb a, Rgb b) {
    const Chroma ca = ToChroma(a), cb = ToChroma(b);
    if (ca.grey || cb.grey) return ca.grey == cb.grey;
    return ChromaDistance(ca, cb) < 0.18;
}

bool IsDigit(wchar_t c) { return c >= L'0' && c <= L'9'; }

// "12:34", "1:23:45", "12:34 PM", "12.34" (OCR reads ':' as '.' sometimes).
// Returns the length consumed at the start of `s`, 0 if no timestamp.
size_t TimestampLength(const std::wstring& s) {
    size_t i = 0;
    auto digits = [&](size_t minN, size_t maxN) {
        size_t n = 0;
        while (i < s.size() && IsDigit(s[i]) && n < maxN) { ++i; ++n; }
        return n >= minN;
    };
    auto sep = [&]() {
        if (i < s.size() && (s[i] == L':' || s[i] == L'.' || s[i] == L';')) { ++i; return true; }
        return false;
    };
    if (!digits(1, 2) || !sep() || !digits(2, 2)) return 0;
    const size_t afterMinutes = i;
    if (sep() && !digits(2, 2)) i = afterMinutes;
    size_t j = i;
    if (j < s.size() && s[j] == L' ') ++j;
    if (j + 1 < s.size()) {
        const std::wstring ap = ToUpperAscii(s.substr(j, 2));
        if (ap == L"AM" || ap == L"PM") i = j + 2;
    }
    return i;
}

Channel TagChannel(const std::wstring& tagIn) {
    const std::wstring t = CaseFold(Trim(tagIn));
    static const std::pair<const wchar_t*, Channel> tags[] = {
        {L"say", Channel::Say},          {L"s", Channel::Say},           {L"sagen", Channel::Say},
        {L"dire", Channel::Say},         {L"decir", Channel::Say},       {L"local", Channel::Say},
        {L"map", Channel::Map},          {L"m", Channel::Map},           {L"karte", Channel::Map},
        {L"carte", Channel::Map},        {L"mapa", Channel::Map},        {L"k", Channel::Map},
        {L"party", Channel::Party},      {L"p", Channel::Party},         {L"gruppe", Channel::Party},
        {L"gr", Channel::Party},         {L"groupe", Channel::Party},    {L"grupo", Channel::Party},
        {L"squad", Channel::Squad},      {L"sq", Channel::Squad},        {L"d", Channel::Squad},
        {L"trupp", Channel::Squad},      {L"escouade", Channel::Squad},  {L"escuadra", Channel::Squad},
        {L"team", Channel::Team},        {L"t", Channel::Team},          {L"\u00e9quipe", Channel::Team},
        {L"equipo", Channel::Team},      {L"guild", Channel::Guild},     {L"g", Channel::Guild},
        {L"gilde", Channel::Guild},      {L"guilde", Channel::Guild},    {L"clan", Channel::Guild},
        {L"whisper", Channel::Whisper},  {L"w", Channel::Whisper},       {L"fl\u00fcstern", Channel::Whisper},
        {L"chuchoter", Channel::Whisper}, {L"susurro", Channel::Whisper}, {L"susurrar", Channel::Whisper},
    };
    for (const auto& [name, ch] : tags)
        if (t == name) return ch;
    if (t.size() == 2 && t[0] == L'g' && t[1] >= L'1' && t[1] <= L'6') return Channel::Guild;  // [G1]..[G6]
    return Channel::Unknown;
}

bool IsGuildTag(const std::wstring& t) {
    if (t.size() < 2 || t.size() > 4) return false;
    return std::all_of(t.begin(), t.end(), IsWordChar);
}

bool IsSpeakerChar(wchar_t c) { return IsWordChar(c) || c == L' ' || c == L'.' || c == L'\'' || c == L'-'; }

bool ValidSpeaker(const std::wstring& s) {
    const std::wstring t = Trim(s);
    const size_t n = CodePointCount(t);
    if (n < 1 || n > 40) return false;
    if (!std::all_of(t.begin(), t.end(), IsSpeakerChar)) return false;
    if (std::count(t.begin(), t.end(), L' ') > 5) return false;
    return std::any_of(t.begin(), t.end(), [](wchar_t c) { return IsWordChar(c) && !IsDigit(c); });
}

void StripWhisperPrefix(ChatMessage& m) {
    static const std::pair<const wchar_t*, bool> prefixes[] = {
        {L"from", false}, {L"von", false}, {L"de", false}, {L"da", false},
        {L"to", true},    {L"an", true},   {L"\u00e0", true}, {L"a", true}, {L"para", true},
    };
    const size_t sp = m.speaker.find(L' ');
    if (sp == std::wstring::npos) return;
    const std::wstring first = CaseFold(m.speaker.substr(0, sp));
    for (const auto& [word, outgoing] : prefixes) {
        if (first == word) {
            m.speaker = Trim(m.speaker.substr(sp + 1));
            m.outgoingWhisper = outgoing;
            return;
        }
    }
}

}  // namespace

Channel ClassifyColor(Rgb c, const std::vector<ChannelColor>& palette) {
    const Chroma x = ToChroma(c);
    if (x.grey) return Channel::Unknown;
    Channel best = Channel::Unknown;
    double bestD = 0.30;  // max accepted distance
    for (const ChannelColor& p : palette) {
        const Chroma y = ToChroma(p.rgb);
        if (y.grey) continue;
        const double d = ChromaDistance(x, y);
        if (d < bestD) {
            bestD = d;
            best = p.channel;
        }
    }
    return best;
}

ChatMessage ParseChatLine(const std::wstring& line, Channel* tagChannel) {
    ChatMessage m;
    m.raw = line;
    std::wstring t = Trim(line);
    Channel tag = Channel::Unknown;

    for (int round = 0; round < 4 && !t.empty(); ++round) {
        if (t[0] == L'[') {
            const size_t close = t.find(L']');
            if (close == std::wstring::npos || close > 30) break;
            const std::wstring inner = Trim(t.substr(1, close - 1));
            if (TimestampLength(inner) == inner.size() && !inner.empty()) {
                t = Trim(t.substr(close + 1));
            } else if (Channel c = TagChannel(inner); c != Channel::Unknown) {
                tag = c;
                t = Trim(t.substr(close + 1));
            } else if (IsGuildTag(inner)) {
                t = Trim(t.substr(close + 1));
            } else {
                break;
            }
        } else if (const size_t ts = TimestampLength(t); ts > 0 && (ts == t.size() || t[ts] == L' ')) {
            t = Trim(t.substr(ts));
        } else {
            break;
        }
    }

    const size_t colon = t.find(L':');
    const bool url = colon != std::wstring::npos && colon + 1 < t.size() && t[colon + 1] == L'/';
    if (colon != std::wstring::npos && colon > 0 && colon <= 48 && !url && ValidSpeaker(t.substr(0, colon))) {
        m.speaker = Trim(t.substr(0, colon));
        m.text = Trim(t.substr(colon + 1));
    } else {
        m.text = t;
    }
    if (tagChannel) *tagChannel = tag;
    return m;
}

std::vector<ChatMessage> BuildMessages(const std::vector<OcrLine>& lines, const std::vector<ChannelColor>& palette) {
    std::vector<ChatMessage> out;
    Rgb prevColor;
    int prevBottom = -100000;

    for (const OcrLine& line : lines) {
        if (Trim(line.text).empty()) continue;
        Channel tag = Channel::Unknown;
        ChatMessage m = ParseChatLine(line.text, &tag);

        const int gap = line.top - prevBottom;
        const bool below = gap >= -std::max(2, line.height / 2) && gap <= std::max(6, line.height);
        if (m.speaker.empty() && tag == Channel::Unknown && !out.empty() && below && ColorsClose(line.color, prevColor)) {
            out.back().text += L" " + m.text;
            out.back().raw += L" " + Trim(line.text);
            prevBottom = line.top + line.height;
            continue;
        }

        m.color = line.color;
        m.channel = tag != Channel::Unknown ? tag : ClassifyColor(line.color, palette);
        if (m.channel == Channel::Unknown && m.speaker.empty()) m.channel = Channel::System;
        if (m.channel == Channel::Whisper) StripWhisperPrefix(m);
        out.push_back(std::move(m));
        prevColor = line.color;
        prevBottom = line.top + line.height;
    }
    return out;
}

}  // namespace gct
