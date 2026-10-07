// chat_line.cpp
#include "chat_line.hpp"

#include <algorithm>
#include <iterator>
#include <cmath>
#include <cstdlib>
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

// Characters text recognition puts in place of a bracket.
bool BracketLike(wchar_t c) { return IsOpener(c) || IsCloser(c) || c == L'1'; }

// The letters GW2 uses in short channel tags: [M]ap/[K]arte, [P]arty, [W]hisper/[F]lüstern,
// [S]ay, [D] squad, [T]eam, [G]uild. "[M]" also stands for French/Spanish clients.
Channel TagLetter(wchar_t c) {
    switch (c) {
        case L'M': case L'K': return Channel::Map;
        case L'P': return Channel::Party;
        case L'W': case L'F': return Channel::Whisper;
        case L'S': return Channel::Say;
        case L'D': return Channel::Squad;
        case L'T': return Channel::Team;
        case L'G': return Channel::Guild;
        default: return Channel::Unknown;
    }
}

}  // namespace

size_t MangledStampAndTagLength(const std::wstring& s, Channel* channel) {
    // "117:46J[M]", "(17-46)(M)", "(17: 46)(M)", "(17-471(M)", "[17:48J1IWJ]", "10M]", "[(W]":
    // a timestamp whose brackets were misread, then a one-letter channel tag.
    size_t i = 0;
    int digits = 0, real = 0, seps = 0, spaces = 0, brackets = 0;
    while (i < s.size() && i < 14) {
        const wchar_t c = s[i];
        const bool nextDigitish = i + 1 < s.size() && (IsDigit(s[i + 1]) || TimestampSep(s[i + 1]));
        if (IsDigit(c)) {
            ++digits;
            ++real;
        } else if (DigitLike(c) && TagLetter(c) == Channel::Unknown && nextDigitish) {
            ++digits;  // "tO:13": O for 1
        } else if (TimestampSep(c) || c == L'-') {
            if (++seps > 1) return 0;
        } else if (c == L' ') {
            if (++spaces > 1 || digits == 0) return 0;
        } else if (BracketLike(c) && TagLetter(c) == Channel::Unknown) ++brackets;
        else break;
        ++i;
    }
    if (i >= s.size()) return 0;
    const Channel ch = TagLetter(s[i]);
    // Without a timestamp only an obvious tag counts ("[(W]"); with one, two digits are enough.
    if (ch == Channel::Unknown || (real < 2 && !(digits == 0 && brackets >= 2 && seps == 0))) return 0;
    if (digits > 6) return 0;  // "17:47 10M]": four of the time, two misread brackets
    // The tag needs an opening bracket of some sort, or sits right after the
    // time when its bracket was lost ("10M]").
    if (i == 0 || !(BracketLike(s[i - 1]) || (IsDigit(s[i - 1]) && digits >= 2))) return 0;
    size_t j = i + 1;
    if (j >= s.size() || !IsCloser(s[j])) return 0;
    while (j < s.size() && IsCloser(s[j])) ++j;   // "WJ]"
    if (j < s.size() && s[j] != L' ') return 0;
    if (channel) *channel = ch;
    return j;
}

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
        } else if (DigitLike(c) && (digits > 0 || opener) && i + 1 < s.size() &&
                   (IsDigit(s[i + 1]) || TimestampSep(s[i + 1]))) {
            ++digits;  // "C9;17J", also first: "tO:13J" (O for 1)
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

std::vector<ChatMessage> BuildFreeTextMessages(const std::vector<OcrLine>& lines, size_t maxChars) {
    // A screen area often has several columns side by side (a sidebar, the
    // text, a picture): lines sorted top to bottom alternate between them.
    // Each line therefore continues the open block right above it in its own
    // column (same left edge, small gap), not simply the previous line.
    struct Block {
        ChatMessage msg;
        int top = 0, left = 0, right = 0, lastBottom = 0, lastHeight = 0;
        bool open = true;
    };
    std::vector<Block> blocks;
    auto endsSentence = [](const std::wstring& t) {
        const std::wstring x = Trim(t);
        return !x.empty() && std::wcschr(L".!?:;。！？؟", x.back()) != nullptr;
    };
    for (const OcrLine& l : lines) {
        const std::wstring t = Trim(l.text);
        if (t.empty()) continue;
        const bool hasBox = l.width > 0;
        int best = -1, bestGap = 0;
        for (size_t i = 0; i < blocks.size(); ++i) {
            Block& b = blocks[i];
            if (!b.open) continue;
            const int h = std::max(1, std::max(b.lastHeight, l.height));
            const int gap = l.top - b.lastBottom;
            if (gap > h * 2) {
                b.open = false;  // far above: this block is finished
                continue;
            }
            if (gap < -h / 2 || gap > h * 7 / 10) continue;
            if (hasBox && b.right > b.left) {
                const bool aligned = std::abs(l.left - b.left) <= h * 2;
                const bool overlaps = l.left < b.right && l.left + l.width > b.left;
                if (!aligned || !overlaps) continue;
            }
            if (best < 0 || gap < bestGap) {
                best = static_cast<int>(i);
                bestGap = gap;
            }
        }
        if (best >= 0) {
            Block& b = blocks[static_cast<size_t>(best)];
            const bool tooLong = b.msg.text.size() + t.size() > maxChars && endsSentence(b.msg.text);
            if (tooLong || b.msg.text.size() > maxChars * 2) {
                b.open = false;  // long enough: the next line starts a new block in this column
                best = -1;
            }
        }
        if (best < 0) {
            Block nb;
            nb.top = l.top;
            nb.left = l.left;
            nb.right = l.left + l.width;
            nb.msg.color = l.color;
            blocks.push_back(std::move(nb));
            best = static_cast<int>(blocks.size()) - 1;
        }
        Block& b = blocks[static_cast<size_t>(best)];
        std::wstring& text = b.msg.text;
        // A word broken with a hyphen at the line end is joined again.
        if (!text.empty() && text.back() == L'-' && text.size() > 1 && IsWordChar(text[text.size() - 2])) text.pop_back();
        else if (!text.empty()) text += L' ';
        text += t;
        b.lastBottom = l.top + l.height;
        b.lastHeight = l.height;
        if (hasBox) {
            if (b.right <= b.left) b.left = l.left;
            b.left = std::min(b.left, l.left);
            b.right = std::max(b.right, l.left + l.width);
        }
    }
    // Reading order: by the top of each block, left column first on the same height.
    std::stable_sort(blocks.begin(), blocks.end(), [](const Block& a, const Block& b) {
        const int h = std::max(1, std::max(a.lastHeight, b.lastHeight));
        if (std::abs(a.top - b.top) > h / 2) return a.top < b.top;
        return a.left < b.left;
    });
    std::vector<ChatMessage> out;
    for (Block& b : blocks) {
        b.msg.text = Trim(b.msg.text);
        if (b.msg.text.empty()) continue;
        b.msg.freeText = true;
        b.msg.raw = b.msg.text;
        out.push_back(std::move(b.msg));
    }
    return out;
}

bool LooksLikeFreeText(const std::wstring& text) {
    size_t tokens = 0, clean = 0, words = 0;
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && text[i] == L' ') ++i;
        if (i >= text.size()) break;
        size_t j = i;
        while (j < text.size() && text[j] != L' ') ++j;
        std::wstring t = text.substr(i, j - i);
        i = j;
        // Punctuation at the edges is normal: "(Hallo," "Welt!)".
        size_t a = 0, b = t.size();
        while (a < b && std::wcschr(L"([{\"'„“»«", t[a])) ++a;
        while (b > a && std::wcschr(L".,!?:;)]}\"'“”«»…", t[b - 1])) --b;
        const std::wstring core = t.substr(a, b - a);
        ++tokens;
        if (core.empty()) {
            if (t == L"-" || t == L"–" || t == L"&" || t == L"+") ++clean;  // a dash between words
            continue;
        }
        size_t letters = 0, digits = 0, odd = 0;
        for (wchar_t c : core) {
            if (c >= L'0' && c <= L'9') ++digits;
            else if (IsWordChar(c)) ++letters;
            else if (c != L'\'' && c != L'-' && c != L'/' && c != L'.' && c != L'@' && c != L':' && c != 0x2019) ++odd;
        }
        if (odd == 0 && (letters > 0 || digits > 0)) {
            ++clean;
            if (letters >= 2) ++words;
        }
    }
    if (tokens == 0 || words == 0) return false;
    return clean * 10 >= tokens * 7;  // at least 70 % clean tokens
}

bool SameFreeParagraph(const std::wstring& a, const std::wstring& b) {
    auto norm = [](const std::wstring& s) {
        std::wstring o;
        for (wchar_t c : CaseFold(s))
            if (IsWordChar(c)) o += c;
            else if (!o.empty() && o.back() != L' ') o += L' ';
        return Trim(o);
    };
    const std::wstring x = norm(a), y = norm(b);
    if (x.empty() || y.empty()) return false;
    if (x == y) return true;
    // Grown (someone is typing, a text streams in): the shorter is the start of the longer.
    size_t p = 0;
    while (p < x.size() && p < y.size() && x[p] == y[p]) ++p;
    const size_t shorter = std::min(x.size(), y.size());
    if (p >= 12 && p * 10 >= shorter * 6) return true;
    // Read a little differently: most words are the same.
    auto words = [](const std::wstring& s) {
        std::vector<std::wstring> w;
        std::wstring cur;
        for (wchar_t c : s) {
            if (c == L' ') {
                if (!cur.empty()) w.push_back(cur);
                cur.clear();
            } else {
                cur += c;
            }
        }
        if (!cur.empty()) w.push_back(cur);
        std::sort(w.begin(), w.end());
        return w;
    };
    const std::vector<std::wstring> wa = words(x), wb = words(y);
    if (wa.size() < 4 || wb.size() < 4) return false;
    std::vector<std::wstring> common;
    std::set_intersection(wa.begin(), wa.end(), wb.begin(), wb.end(), std::back_inserter(common));
    return common.size() * 10 >= std::min(wa.size(), wb.size()) * 8;
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

Rgb TailColor(const OcrLine& line) {
    int r = 0, g = 0, b = 0, n = 0;
    for (size_t i = line.words.size(); i-- > 0 && n < 3;) {
        const OcrWord& w = line.words[i];
        if (!w.hasColor || std::max({w.color.r, w.color.g, w.color.b}) < 60) continue;
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
        // A lone misread bracket before the name: ") Name: ...", "J Von Name: ...".
        if (round == 0 && t.size() > 2 && t[1] == L' ' &&
            (t[0] == L']' || t[0] == L')' || t[0] == L'}' || t[0] == L'|' || t[0] == L'J')) {
            t = Trim(t.substr(2));
            continue;
        }
        if (!m.stamped && !sawTag) {
            Channel c = Channel::Unknown;
            if (const size_t n = MangledStampAndTagLength(t, &c); n > 0) {
                m.stamped = true;
                tag = c;
                sawTag = true;
                t = Trim(t.substr(n));
                continue;
            }
        }
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

    size_t colon = t.find(L':');
    // "Name; text": text recognition read the colon as a semicolon (only right
    // after a timestamp or tag, where a name is expected; "ok; bis gleich" in
    // a wrapped line stays text).
    if (colon == std::wstring::npos && (m.stamped || sawTag)) {
        const size_t semi = t.find(L';');
        if (semi != std::wstring::npos && semi + 1 < t.size() && t[semi + 1] == L' ') colon = semi;
    }
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

bool LocateChatLines(const std::vector<OcrLine>& lines, ChatBlock* area) {
    struct Stamp {
        size_t index;
        int left;
    };
    std::vector<Stamp> stamps;
    for (size_t i = 0; i < lines.size(); ++i) {
        const OcrLine& l = lines[i];
        if (l.width <= 0) continue;
        if (ParseChatLine(l.text).stamped) stamps.push_back({i, l.left});
    }
    if (stamps.size() < 2) return false;
    // The biggest group of timestamp lines that start at the same place.
    size_t bestCount = 0;
    int bestLeft = 0;
    for (const Stamp& s : stamps) {
        const int tolerance = std::max(6, lines[s.index].height);
        size_t count = 0;
        for (const Stamp& o : stamps)
            if (std::abs(o.left - s.left) <= tolerance) ++count;
        if (count > bestCount) {
            bestCount = count;
            bestLeft = s.left;
        }
    }
    if (bestCount < 2) return false;
    size_t first = lines.size(), last = 0;
    for (const Stamp& s : stamps) {
        if (std::abs(s.left - bestLeft) > std::max(6, lines[s.index].height)) continue;
        first = std::min(first, s.index);
        last = std::max(last, s.index);
    }
    // Wrapped lines below the last timestamp line belong to the chat too, as
    // long as they follow on the line spacing and start near the same place.
    const int pitch = std::max(1, (lines[last].top - lines[first].top) / std::max<int>(1, static_cast<int>(last - first)));
    while (last + 1 < lines.size()) {
        const OcrLine& n = lines[last + 1];
        if (n.width <= 0 || n.top - lines[last].top > pitch * 3 / 2 || n.left < bestLeft - pitch ||
            n.left > bestLeft + pitch * 4 || ParseChatLine(n.text).tagOnly)
            break;
        ++last;
    }
    ChatBlock b;
    b.left = bestLeft;
    b.top = lines[first].top;
    b.right = bestLeft;
    b.bottom = 0;
    for (size_t i = first; i <= last; ++i) {
        const OcrLine& l = lines[i];
        if (l.width <= 0) continue;
        b.left = std::min(b.left, l.left);
        b.right = std::max(b.right, l.left + l.width);
        b.bottom = std::max(b.bottom, l.top + l.height);
    }
    b.stamped = static_cast<int>(bestCount);
    if (area) *area = b;
    return true;
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
    // Without timestamps, a line without tag and "Name:" (a system notice) is
    // only believed when the frame looks like chat: most lines have a speaker
    // or a tag. The character selection, a tooltip or a window over the chat
    // area have none.
    size_t nonEmpty = 0, structuredLines = 0;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (Trim(lines[i].text).empty()) continue;
        ++nonEmpty;
        const ChatMessage& m = parsed[i].m;
        if (m.stamped || parsed[i].tag != Channel::Unknown || !m.speaker.empty()) ++structuredLines;
    }
    const bool plainNotices = !anyStamped && structuredLines * 2 >= nonEmpty && structuredLines > 0;
    size_t lastStamped = 0;
    for (size_t i = 0; i < parsed.size(); ++i)
        if (parsed[i].m.stamped) lastStamped = i;

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
        // The input line itself ("[Gruppe] what you are typing"): below the
        // last stamped line, a channel tag but no timestamp.
        if (anyStamped && !m.stamped && tag != Channel::Unknown && i > lastStamped) continue;

        const int gap = line.top - prevBottom;
        const bool below = gap >= -std::max(2, line.height / 2) && gap <= std::max(6, line.height);
        const Rgb lead = LeadColor(line);
        const bool structured = m.stamped || tag != Channel::Unknown || !m.speaker.empty();
        // A wrapped line continues the message text, so it has the colour of
        // the text at the end of the line above, not of the speaker's name.
        if (!structured && !out.empty() && below && ColorsClose(lead, prevColor)) {
            out.back().text += L" " + m.text;
            out.back().raw += L" " + Trim(line.text);
            prevBottom = line.top + line.height;
            prevColor = TailColor(line);
            continue;
        }
        // Only chat starts a message: a timestamp, a channel tag or "Name: text"
        // (or a plain notice in a frame that clearly is chat, see above).
        // Anything else (character data on the selection screen, a tooltip, a
        // window lying over the chat) is not read.
        if (!structured && !plainNotices) continue;

        m.color = lead;
        m.channel = tag != Channel::Unknown ? tag : ClassifyColor(lead, palette);
        if (m.channel == Channel::Unknown && m.speaker.empty()) m.channel = Channel::System;
        if (m.channel == Channel::Whisper) StripWhisperPrefix(m);
        if (m.channel == Channel::System && !m.speaker.empty()) {  // "Event: ..." in system yellow
            m.text = m.speaker + L": " + m.text;
            m.speaker.clear();
        }
        prevColor = TailColor(line);
        prevBottom = line.top + line.height;
        out.push_back(std::move(m));
    }
    // A speaker with nothing said ("Name:" cut at the bottom) is not a message,
    // and neither is a fragment without a speaker that is hardly any letters
    // ("80 t", "@ <>"): misread symbols, icons, numbers.
    out.erase(std::remove_if(out.begin(), out.end(),
                             [](const ChatMessage& x) {
                                 const std::wstring t = Trim(x.text);
                                 if (t.empty()) return true;
                                 if (!x.speaker.empty()) return false;
                                 size_t letters = 0, visible = 0;
                                 for (wchar_t c : t) {
                                     if (c == L' ') continue;
                                     ++visible;
                                     if (IsWordChar(c) && !IsDigit(c)) ++letters;
                                 }
                                 return letters < 3 || letters * 10 < visible * 5;
                             }),
              out.end());
    return out;
}

}  // namespace gct
