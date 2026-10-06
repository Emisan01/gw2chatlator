// chat_line.cpp
#include "chat_line.hpp"

#include <algorithm>
#include <cmath>
#include <cwchar>

#include "i18n.hpp"
#include "text.hpp"
#include "word_model.hpp"

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
        {Channel::Squad, {223, 245, 226}},   // squad as sampled from real captures (very pale)
        {Channel::Map, {250, 219, 206}},     // map as sampled from real captures (peach)
        {Channel::System, {247, 249, 46}},   // yellow event / system notices
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
    return {c.r / static_cast<double>(mx), c.g / static_cast<double>(mx), c.b / static_cast<double>(mx), sat < 0.07};
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
    // Notices of the contact list ("[Kontakte] X hat sich angemeldet") and similar.
    static const wchar_t* systemTags[] = {L"kontakte", L"contacts", L"freunde", L"friends", L"contactos",
                                          L"amis", L"system", L"kampf", L"combat"};
    for (const wchar_t* name : systemTags)
        if (t == name) return Channel::System;
    // OCR slips in longer tags: "Sagcn", "Kontakle".
    if (t.size() >= 4) {
        for (const auto& [name, ch] : tags)
            if (std::wcslen(name) >= 4 && EditDistance(t, name, 1) <= 1) return ch;
        for (const wchar_t* name : systemTags)
            if (EditDistance(t, name, 1) <= 1) return Channel::System;
    }
    return Channel::Unknown;
}

bool IsOpener(wchar_t c) {
    return c == L'[' || c == L'(' || c == L'{' || c == L'C' || c == L'c' || c == L't' || c == L'I' || c == L'l' ||
           c == L'|' || c == L'L' || c == L'<';
}

bool IsCloser(wchar_t c) {
    return c == L']' || c == L')' || c == L'}' || c == L'J' || c == L'j' || c == L'|' || c == L'I' || c == L'l' ||
           c == L'>';
}

// Characters OCR puts in place of digits inside a timestamp.
bool DigitLike(wchar_t c) {
    return c == L'O' || c == L'o' || c == L'l' || c == L'I' || c == L'i' || c == L'f' || c == L'S' || c == L'B' ||
           c == L'Z' || c == L'z';
}

bool TimestampSep(wchar_t c) {
    return c == L':' || c == L';' || c == L'.' || c == L',' || c == 0x2022 || c == 0x00B7 || c == L'\'';
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

size_t OcrTimestampLength(const std::wstring& s) {
    const size_t strict = TimestampLength(s);
    if (strict > 0) {
        // "19:35)" / "19:37 J": a strict time followed by a mangled closing bracket.
        size_t j = strict;
        if (j < s.size() && s[j] == L' ' && j + 1 < s.size() && IsCloser(s[j + 1]) &&
            (j + 2 == s.size() || s[j + 2] == L' ' || IsOpener(s[j + 2])))
            return j + 2;
        if (j < s.size() && IsCloser(s[j]) && (j + 1 == s.size() || s[j + 1] == L' ' || IsOpener(s[j + 1])))
            return j + 1;
        if (strict == s.size() || s[strict] == L' ') return strict;
    }
    size_t i = 0;
    bool opener = false;
    if (i < s.size() && IsOpener(s[i])) {
        opener = true;
        ++i;
    }
    const size_t start = i;
    int digits = 0, real = 0, seps = 0, spaces = 0;
    while (i < s.size() && i - start < 8) {
        const wchar_t c = s[i];
        if (IsDigit(c)) {
            ++digits;
            ++real;
        } else if (DigitLike(c) && digits > 0 && i + 1 < s.size() && (IsDigit(s[i + 1]) || TimestampSep(s[i + 1]))) {
            ++digits;
        } else if (TimestampSep(c) && digits > 0 && seps == 0) {
            ++seps;
        } else if (c == L' ' && digits > 0 && spaces == 0 && seps == 0 && i + 1 < s.size() && IsDigit(s[i + 1])) {
            ++spaces;
        } else {
            break;
        }
        ++i;
    }
    if (real < 2 || digits < 3 || digits > 4) return 0;
    size_t end = i;
    size_t j = i;
    if (j < s.size() && s[j] == L' ') ++j;
    const bool closer = j < s.size() && IsCloser(s[j]);
    if (closer) {
        const size_t after = j + 1;
        if (after == s.size() || s[after] == L' ' || IsOpener(s[after])) end = after;
        else if (!opener && seps == 0) return 0;
    }
    if (!opener && end == i && seps == 0) return 0;  // "1234 text" is a number, not a time
    if (!opener && end == i && (end < s.size() && s[end] != L' ')) return 0;
    if (opener && end == i && !(end == s.size() || s[end] == L' ')) return 0;
    return end;
}

size_t FuzzyTagLength(const std::wstring& s, Channel* channel) {
    if (s.size() < 3 || !IsOpener(s[0])) return 0;
    for (size_t k = 2; k < s.size() && k <= 14; ++k) {
        if (!IsCloser(s[k])) {
            if (!IsWordChar(s[k]) || (s[k] >= L'0' && s[k] <= L'9')) {
                if (!(s[k] >= L'1' && s[k] <= L'6')) break;  // "[G3]"
            }
            continue;
        }
        const std::wstring inner = s.substr(1, k - 1);
        const bool followOk = k + 1 == s.size() || s[k + 1] == L' ' || IsOpener(s[k + 1]);
        if (!followOk) continue;
        Channel c = TagChannel(inner);
        if (c == Channel::Unknown) continue;
        // A one-letter tag needs a real bracket on at least one side ("CSJ" is fine,
        // "lMl" is not enough evidence) unless it is a known GW2 letter.
        if (channel) *channel = c;
        return k + 1;
    }
    return 0;
}

bool LooksLikeChatText(const std::wstring& text) {
    size_t letters = 0, other = 0, longest = 0, run = 0;
    for (wchar_t c : text) {
        if (c == L' ') {
            run = 0;
            continue;
        }
        if (IsWordChar(c) && !IsDigit(c)) {
            ++letters;
            longest = std::max(longest, ++run);
        } else {
            run = 0;
            if (!IsDigit(c) && !std::wcschr(L".,!?:;'\"()-/&+%#[]", c)) ++other;
        }
    }
    if (letters == 0) return false;
    if (longest < 2 && letters < 3) return false;          // "l |" ...
    return other * 100 <= (letters + other) * 25;           // at most a quarter odd symbols
}

Rgb LeadColor(const OcrLine& line) {
    if (line.words.empty()) return line.color;
    // Skip the words that make up the timestamp, then take the first coloured words.
    const size_t ts = OcrTimestampLength(Trim(line.text));
    size_t consumed = 0, idx = 0;
    while (idx < line.words.size() && consumed < ts) {
        consumed += line.words[idx].text.size() + 1;
        ++idx;
    }
    int r = 0, g = 0, b = 0, n = 0;
    for (size_t i = idx; i < line.words.size() && n < 3; ++i) {
        const OcrWord& w = line.words[i];
        if (!w.hasColor) continue;
        const int mx = std::max({w.color.r, w.color.g, w.color.b});
        const int mn = std::min({w.color.r, w.color.g, w.color.b});
        if (mx < 60) continue;
        if (n > 0 && mx > 0 && (mx - mn) * 100 / mx < 7) continue;  // a grey word after a coloured one
        r += w.color.r;
        g += w.color.g;
        b += w.color.b;
        ++n;
    }
    if (n == 0) return line.color;
    return {static_cast<uint8_t>(r / n), static_cast<uint8_t>(g / n), static_cast<uint8_t>(b / n)};
}

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
    bool sawTag = false;

    for (int round = 0; round < 4 && !t.empty(); ++round) {
        if (t[0] == L'[') {
            const size_t close = t.find(L']');
            if (close != std::wstring::npos && close <= 30) {
                const std::wstring inner = Trim(t.substr(1, close - 1));
                if (!inner.empty() && TimestampLength(inner) == inner.size()) {
                    t = Trim(t.substr(close + 1));
                    m.stamped = true;
                    continue;
                }
                if (Channel c = TagChannel(inner); c != Channel::Unknown) {
                    tag = c;
                    sawTag = true;
                    t = Trim(t.substr(close + 1));
                    continue;
                }
                if (IsGuildTag(inner) && round > 0) {  // a guild tag follows a timestamp or channel tag
                    t = Trim(t.substr(close + 1));
                    continue;
                }
            }
        }
        if (!m.stamped) {
            if (const size_t ts = OcrTimestampLength(t); ts > 0) {
                t = Trim(t.substr(ts));
                m.stamped = true;
                continue;
            }
        }
        if (!sawTag) {
            Channel c = Channel::Unknown;
            if (const size_t tl = FuzzyTagLength(t, &c); tl > 0) {
                tag = c;
                sawTag = true;
                t = Trim(t.substr(tl));
                continue;
            }
        }
        break;
    }
    m.tagOnly = (m.stamped || sawTag) && t.empty();

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

    struct Parsed {
        ChatMessage m;
        Channel tag;
    };
    std::vector<Parsed> parsed;
    parsed.reserve(lines.size());
    bool anyStamped = false;
    for (const OcrLine& line : lines) {
        Parsed p;
        p.tag = Channel::Unknown;
        p.m = ParseChatLine(line.text, &p.tag);
        anyStamped = anyStamped || p.m.stamped;
        parsed.push_back(std::move(p));
    }
    // With timestamps on, a chat message always starts with one: whatever is
    // above the first stamped line (tab bar, a message cut off at the top)
    // cannot be read reliably.
    bool started = !anyStamped;

    for (size_t i = 0; i < lines.size(); ++i) {
        const OcrLine& line = lines[i];
        ChatMessage& m = parsed[i].m;
        const Channel tag = parsed[i].tag;
        if (Trim(line.text).empty()) continue;
        if (!started) {
            if (!m.stamped) continue;
            started = true;
        }
        if (m.tagOnly) continue;  // the input line's channel marker

        const int gap = line.top - prevBottom;
        const bool below = gap >= -std::max(2, line.height / 2) && gap <= std::max(6, line.height);
        const Rgb lead = LeadColor(line);
        if (m.speaker.empty() && tag == Channel::Unknown && !m.stamped && !out.empty() && below &&
            ColorsClose(lead, prevColor)) {
            out.back().text += L" " + m.text;
            out.back().raw += L" " + Trim(line.text);
            prevBottom = line.top + line.height;
            continue;
        }

        m.color = lead;
        m.channel = tag != Channel::Unknown ? tag : ClassifyColor(lead, palette);
        if (m.channel == Channel::Unknown && m.speaker.empty()) m.channel = Channel::System;
        if (m.channel == Channel::Whisper) StripWhisperPrefix(m);
        if (m.channel == Channel::System && !m.speaker.empty()) {  // "Event: ..." in system yellow
            m.text = m.speaker + L": " + m.text;
            m.speaker.clear();
        }
        prevColor = lead;
        prevBottom = line.top + line.height;
        out.push_back(std::move(m));
    }
    // A speaker with nothing said ("Name:" cut at the bottom) is not a message.
    out.erase(std::remove_if(out.begin(), out.end(), [](const ChatMessage& x) { return Trim(x.text).empty(); }),
              out.end());
    return out;
}

}  // namespace gct
