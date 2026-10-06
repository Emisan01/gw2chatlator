// text.cpp — string helpers. Portable, no Win32.
#include "text.hpp"

#include <algorithm>
#include <cstdint>

namespace gct {

// ===========================================================================
// UTF conversion
// ===========================================================================
namespace {

constexpr uint32_t kReplacement = 0xFFFD;

void AppendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

void AppendWide(std::wstring& out, uint32_t cp) {
    if constexpr (sizeof(wchar_t) == 2) {
        if (cp >= 0x10000) {
            cp -= 0x10000;
            out += static_cast<wchar_t>(0xD800 + (cp >> 10));
            out += static_cast<wchar_t>(0xDC00 + (cp & 0x3FF));
            return;
        }
    }
    out += static_cast<wchar_t>(cp);
}

}  // namespace

std::string ToUtf8(const std::wstring& w) {
    std::string out;
    out.reserve(w.size() * 2);
    for (size_t i = 0; i < w.size(); ++i) {
        uint32_t cp = static_cast<uint32_t>(w[i]);
        if constexpr (sizeof(wchar_t) == 2) {
            if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < w.size()) {
                const uint32_t lo = static_cast<uint32_t>(w[i + 1]);
                if (lo >= 0xDC00 && lo <= 0xDFFF) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    ++i;
                } else {
                    cp = kReplacement;
                }
            } else if (cp >= 0xD800 && cp <= 0xDFFF) {
                cp = kReplacement;
            }
        }
        if (cp > 0x10FFFF) cp = kReplacement;
        AppendUtf8(out, cp);
    }
    return out;
}

std::wstring FromUtf8(const std::string& s) {
    std::wstring out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp;
        int extra;
        if (c < 0x80) { cp = c; extra = 0; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
        else { AppendWide(out, kReplacement); ++i; continue; }

        bool bad = false;
        for (int k = 1; k <= extra; ++k) {
            if (i + k >= s.size()) { bad = true; break; }
            const unsigned char cc = static_cast<unsigned char>(s[i + k]);
            if ((cc & 0xC0) != 0x80) { bad = true; break; }
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (bad) { AppendWide(out, kReplacement); ++i; continue; }
        // Reject overlong forms, surrogates and out-of-range values.
        if ((extra == 1 && cp < 0x80) || (extra == 2 && cp < 0x800) || (extra == 3 && cp < 0x10000) ||
            cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            cp = kReplacement;
        }
        AppendWide(out, cp);
        i += static_cast<size_t>(extra) + 1;
    }
    return out;
}

size_t CodePointCount(const std::wstring& w) {
    size_t n = 0;
    for (wchar_t ch : w) {
        const uint32_t c = static_cast<uint32_t>(ch);
        if (sizeof(wchar_t) == 2 && c >= 0xDC00 && c <= 0xDFFF) continue;  // low surrogate
        ++n;
    }
    return n;
}

// ===========================================================================
// Basic helpers
// ===========================================================================
static bool IsSpace(wchar_t c) {
    return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == 0x00A0 || c == 0x3000;
}

std::wstring Trim(const std::wstring& s) {
    size_t a = 0, b = s.size();
    while (a < b && IsSpace(s[a])) ++a;
    while (b > a && IsSpace(s[b - 1])) --b;
    return s.substr(a, b - a);
}

std::wstring ToUpperAscii(std::wstring s) {
    for (auto& c : s)
        if (c >= L'a' && c <= L'z') c = static_cast<wchar_t>(c - L'a' + L'A');
    return s;
}

std::wstring ToLowerAscii(std::wstring s) {
    for (auto& c : s)
        if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c - L'A' + L'a');
    return s;
}

std::wstring AsciiEscape(const std::wstring& s) {
    static const wchar_t* hex = L"0123456789abcdef";
    std::wstring out;
    auto unit = [&](uint32_t u) {
        out += L"\\u";
        for (int shift = 12; shift >= 0; shift -= 4) out += hex[(u >> shift) & 0xF];
    };
    for (wchar_t c : s) {
        const uint32_t u = static_cast<uint32_t>(c);
        if (c == L'\\') out += L"\\\\";
        else if (u < 0x80) out += c;
        else if (u > 0xFFFF) {  // 32-bit wchar_t: write the UTF-16 surrogate pair
            unit(0xD800 + ((u - 0x10000) >> 10));
            unit(0xDC00 + ((u - 0x10000) & 0x3FF));
        } else {
            unit(u);
        }
    }
    return out;
}

std::wstring AsciiUnescape(const std::wstring& s) {
    auto hexAt = [&](size_t i, uint32_t& v) {
        if (i + 4 > s.size()) return false;
        v = 0;
        for (size_t k = i; k < i + 4; ++k) {
            const wchar_t c = s[k];
            v <<= 4;
            if (c >= L'0' && c <= L'9') v |= static_cast<uint32_t>(c - L'0');
            else if (c >= L'a' && c <= L'f') v |= static_cast<uint32_t>(c - L'a' + 10);
            else if (c >= L'A' && c <= L'F') v |= static_cast<uint32_t>(c - L'A' + 10);
            else return false;
        }
        return true;
    };
    std::wstring out;
    for (size_t i = 0; i < s.size(); ++i) {
        uint32_t v = 0;
        if (s[i] == L'\\' && i + 1 < s.size() && s[i + 1] == L'\\') {
            out += L'\\';
            ++i;
        } else if (s[i] == L'\\' && i + 1 < s.size() && s[i + 1] == L'u' && hexAt(i + 2, v)) {
            uint32_t lo = 0;
            if (sizeof(wchar_t) == 4 && v >= 0xD800 && v < 0xDC00 && i + 7 < s.size() && s[i + 6] == L'\\' &&
                s[i + 7] == L'u' && hexAt(i + 8, lo) && lo >= 0xDC00 && lo < 0xE000) {
                out += static_cast<wchar_t>(0x10000 + ((v - 0xD800) << 10) + (lo - 0xDC00));
                i += 11;
            } else {
                out += static_cast<wchar_t>(v);
                i += 5;
            }
        } else {
            out += s[i];
        }
    }
    return out;
}

wchar_t CaseFoldChar(wchar_t c) {
    const uint32_t u = static_cast<uint32_t>(c);
    if (u >= 'A' && u <= 'Z') return static_cast<wchar_t>(u + 32);
    if (u >= 0xC0 && u <= 0xDE && u != 0xD7) return static_cast<wchar_t>(u + 32);
    if (u == 0x178) return static_cast<wchar_t>(0xFF);  // Ÿ
    // Latin Extended-A: upper/lower pairs, parity flips at 0x139 and 0x179.
    if ((u >= 0x100 && u <= 0x137) || (u >= 0x14A && u <= 0x177))
        return static_cast<wchar_t>(u | 1u);
    if ((u >= 0x139 && u <= 0x148) || (u >= 0x179 && u <= 0x17E))
        return static_cast<wchar_t>((u & 1u) ? u + 1 : u);
    return c;
}

std::wstring CaseFold(const std::wstring& s) {
    std::wstring out(s);
    for (auto& c : out) c = CaseFoldChar(c);
    return out;
}

bool IsWordChar(wchar_t c) {
    const uint32_t u = static_cast<uint32_t>(c);
    if ((u >= '0' && u <= '9') || (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z')) return true;
    if (u < 0xC0) return u == 0xAA || u == 0xB5 || u == 0xBA;
    if (u == 0xD7 || u == 0xF7) return false;
    if (u >= 0x2000 && u <= 0x2BFF) return false;  // punctuation, symbols, arrows, shapes
    if (u >= 0x3000 && u <= 0x303F) return false;  // CJK punctuation
    if (u >= 0xFF00 && u <= 0xFF0F) return false;  // full-width punctuation
    if (u >= 0xFF1A && u <= 0xFF20) return false;
    if (u >= 0xFE30 && u <= 0xFE4F) return false;
    return true;
}

static bool IsApostrophe(wchar_t c) { return c == L'\'' || c == 0x2019; }

std::vector<Span> WordSpans(const std::wstring& s) {
    std::vector<Span> out;
    size_t i = 0;
    while (i < s.size()) {
        if (!IsWordChar(s[i])) { ++i; continue; }
        const size_t start = i;
        while (i < s.size()) {
            if (IsWordChar(s[i])) { ++i; continue; }
            if (IsApostrophe(s[i]) && i + 1 < s.size() && IsWordChar(s[i + 1])) { ++i; continue; }
            break;
        }
        out.push_back({start, i - start});
    }
    return out;
}

std::wstring SanitizeChatText(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size());
    bool lastSpace = false;
    for (wchar_t c : s) {
        if (c == L'\r' || c == L'\n' || c == L'\t') c = L' ';
        if (c < 0x20) continue;  // other control characters
        if (c == L' ') {
            if (lastSpace) continue;
            lastSpace = true;
        } else {
            lastSpace = false;
        }
        out += c;
    }
    return Trim(out);
}

std::vector<std::wstring> ParseLangList(const std::wstring& s) {
    std::vector<std::wstring> out;
    size_t start = 0;
    while (true) {
        const size_t comma = s.find(L',', start);
        const std::wstring part =
            Trim(s.substr(start, comma == std::wstring::npos ? std::wstring::npos : comma - start));
        if (!part.empty()) out.push_back(ToUpperAscii(part));
        if (comma == std::wstring::npos) break;
        start = comma + 1;
    }
    return out;
}

// ===========================================================================
// GW2 chat specifics
// ===========================================================================
bool IsAccountName(const std::wstring& t) {
    if (t.size() < 6 || t[t.size() - 5] != L'.') return false;
    return std::all_of(t.end() - 4, t.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; });
}

ChatSplit SplitChatCommand(const std::wstring& text) {
    ChatSplit r;
    const std::wstring t = Trim(text);
    if (t.empty() || t[0] != L'/') {
        r.body = t;
        return r;
    }
    const size_t sp = t.find(L' ');
    if (sp == std::wstring::npos) {  // "/wave" — nothing to translate
        r.prefix = t;
        return r;
    }
    const std::wstring cmd = ToLowerAscii(t.substr(0, sp));
    size_t bodyStart = sp + 1;

    if (cmd == L"/w" || cmd == L"/whisper" || cmd == L"/tell" || cmd == L"/f" || cmd == L"/b") {
        // Recipient detection, best effort:
        //   1. account name "Name.1234" as the next token
        //   2. otherwise up to the first comma (character names with spaces)
        //   3. otherwise the next token
        const size_t tokEnd = t.find(L' ', bodyStart);
        const std::wstring tok =
            t.substr(bodyStart, tokEnd == std::wstring::npos ? std::wstring::npos : tokEnd - bodyStart);
        const size_t comma = t.find(L',', bodyStart);
        if (!IsAccountName(tok) && comma != std::wstring::npos) bodyStart = comma + 1;
        else bodyStart = (tokEnd == std::wstring::npos) ? t.size() : tokEnd + 1;
    }
    while (bodyStart < t.size() && t[bodyStart] == L' ') ++bodyStart;

    r.prefix = t.substr(0, bodyStart);
    if (!r.prefix.empty() && r.prefix.back() != L' ' && bodyStart < t.size()) r.prefix += L' ';
    r.body = t.substr(bodyStart);
    if (r.body.empty()) r.prefix = Trim(r.prefix);
    return r;
}

static bool IsChatCodeChar(wchar_t c) {
    return (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9') || c == L'+' ||
           c == L'/' || c == L'=';
}

std::vector<Span> FindChatCodes(const std::wstring& s) {
    std::vector<Span> out;
    size_t p = 0;
    while ((p = s.find(L"[&", p)) != std::wstring::npos) {
        size_t q = p + 2;
        while (q < s.size() && IsChatCodeChar(s[q])) ++q;
        if (q < s.size() && s[q] == L']' && q - (p + 2) >= 4) {
            out.push_back({p, q + 1 - p});
            p = q + 1;
        } else {
            p += 2;
        }
    }
    return out;
}

}  // namespace gct
