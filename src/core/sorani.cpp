// sorani.cpp — see sorani.hpp. The Arabic-based letters are written as code points (core builds with /utf-8).

#include "sorani.hpp"

#include <algorithm>
#include <cwctype>
#include <vector>

namespace gct {

namespace {

// The Sorani letters (Arabic-based alphabet).
constexpr wchar_t kHamza = 0x0626;   // ئ – carries a vowel at a syllable's start, no sound of its own
constexpr wchar_t kAlef = 0x0627;    // ا a
constexpr wchar_t kAe = 0x06D5;      // ە e
constexpr wchar_t kEe = 0x06CE;      // ێ ê
constexpr wchar_t kOo = 0x06C6;      // ۆ o
constexpr wchar_t kWaw = 0x0648;     // و w / u (doubled: û)
constexpr wchar_t kYeh = 0x06CC;     // ی y / î

struct Letter {
    wchar_t ar;
    const wchar_t* latin;
};
// Consonants, one sound each.
constexpr Letter kConsonants[] = {
    {0x0628, L"b"}, {0x067E, L"p"}, {0x062A, L"t"}, {0x062C, L"c"}, {0x0686, L"ç"}, {0x062D, L"h"},
    {0x062E, L"x"}, {0x062F, L"d"}, {0x0631, L"r"}, {0x0695, L"ř"}, {0x0632, L"z"}, {0x0698, L"j"},
    {0x0633, L"s"}, {0x0634, L"ş"}, {0x0639, L""}, {0x063A, L"gh"}, {0x0641, L"f"}, {0x06A4, L"v"},
    {0x0642, L"q"}, {0x06A9, L"k"}, {0x06AF, L"g"}, {0x0644, L"l"}, {0x06B5, L"ł"}, {0x0645, L"m"},
    {0x0646, L"n"}, {0x0647, L"h"},
};

// One spelling for letters written more than one way (Arabic forms in Sorani text) and nothing for marks.
wchar_t Normal(wchar_t c) {
    switch (c) {
        case 0x0643: return 0x06A9;  // ك -> ک
        case 0x064A: case 0x0649: return kYeh;  // ي ى -> ی
        case 0x0629: return kAe;     // ة -> ە
        case 0x06BE: return 0x0647;  // ھ -> ه
        default: return c;
    }
}
bool Dropped(wchar_t c) {
    return (c >= 0x064B && c <= 0x0652) || c == 0x0640 || c == 0x200C || c == 0x200D;  // vowel marks, tatweel, ZWNJ/ZWJ
}
bool IsSoraniLetter(wchar_t c) {
    if (c == kHamza || c == kAlef || c == kAe || c == kEe || c == kOo || c == kWaw || c == kYeh) return true;
    for (const Letter& l : kConsonants)
        if (l.ar == c) return true;
    return false;
}
bool IsVowelLetter(wchar_t c) { return c == kAlef || c == kAe || c == kEe || c == kOo; }
const wchar_t* ConsonantLatin(wchar_t c) {
    for (const Letter& l : kConsonants)
        if (l.ar == c) return l.latin;
    return nullptr;
}
bool IsLatinVowel(wchar_t c) {
    switch (c) {
        case L'a': case L'e': case L'i': case L'o': case L'u':
        case 0x00EA: case 0x00EE: case 0x00FB:  // ê î û
            return true;
        default: return false;
    }
}

struct Sound {
    std::wstring latin;
    bool vowel;    // a e ê o u û î
    bool glide;    // w or y as consonants: "xw", "dy" need no short i before them
};

// One word of Sorani letters to Latin.
std::wstring WordToLatin(const std::wstring& w) {
    std::vector<Sound> s;
    for (size_t i = 0; i < w.size(); ++i) {
        const wchar_t c = w[i];
        const bool atStart = i == 0;
        const bool prevVowel = i > 0 && IsVowelLetter(w[i - 1]);
        const bool nextVowel = i + 1 < w.size() && IsVowelLetter(w[i + 1]);
        if (c == kHamza) continue;
        if (c == kAlef) s.push_back({L"a", true, false});
        else if (c == kAe) s.push_back({L"e", true, false});
        else if (c == kEe) s.push_back({L"ê", true, false});
        else if (c == kOo) s.push_back({L"o", true, false});
        else if (c == kWaw) {
            if (i + 1 < w.size() && w[i + 1] == kWaw && !(i + 2 < w.size() && IsVowelLetter(w[i + 2]))) {
                s.push_back({L"û", true, false});  // وو: û
                ++i;
            } else if (atStart || prevVowel || nextVowel) {
                s.push_back({L"w", false, true});
            } else {
                s.push_back({L"u", true, false});
            }
        } else if (c == kYeh) {
            if (atStart || prevVowel || nextVowel) s.push_back({L"y", false, true});
            else s.push_back({L"î", true, false});
        } else if (const wchar_t* l = ConsonantLatin(c)) {
            if (*l) s.push_back({l, false, false});
        }
    }
    // The unwritten short i.
    const bool anyVowel = std::any_of(s.begin(), s.end(), [](const Sound& x) { return x.vowel; });
    std::wstring out;
    for (size_t i = 0; i < s.size(); ++i) {
        out += s[i].latin;
        const bool firstOfTwo = i == 0 && s.size() >= 2 && !s[0].vowel && !s[1].vowel;
        if (firstOfTwo && (!anyVowel || !s[1].glide)) out += L'i';
        // the same consonant twice at a word's end: "-in" ("چۆنن" -> "çonin"); inside a word it can be two words
        // grown together ("çakkerewe"), so only at the end
        const bool lastPairSame = s.size() >= 3 && i == s.size() - 2 && !s[i].vowel && !s[i].glide &&
                                  s[i].latin == s[i + 1].latin;
        if (lastPairSame) out += L'i';
    }
    return out;
}

wchar_t LatinUpper(wchar_t c) {
    if (c >= L'a' && c <= L'z') return static_cast<wchar_t>(c - 32);
    switch (c) {
        case 0x00E7: return 0x00C7;  // ç
        case 0x015F: return 0x015E;  // ş
        case 0x00EA: return 0x00CA;  // ê
        case 0x00EE: return 0x00CE;  // î
        case 0x00FB: return 0x00DB;  // û
        case 0x0159: return 0x0158;  // ř
        case 0x0142: return 0x0141;  // ł
        default: return c;
    }
}
wchar_t LatinLower(wchar_t c) {
    if (c >= L'A' && c <= L'Z') return static_cast<wchar_t>(c + 32);
    switch (c) {
        case 0x00C7: return 0x00E7;
        case 0x015E: return 0x015F;
        case 0x00CA: return 0x00EA;
        case 0x00CE: return 0x00EE;
        case 0x00DB: return 0x00FB;
        case 0x0158: return 0x0159;
        case 0x0141: return 0x0142;
        default: return c;
    }
}

}  // namespace

std::wstring SoraniToLatin(const std::wstring& arabicScript) {
    std::wstring out, word;
    bool sentenceStart = true;
    auto flush = [&] {
        if (word.empty()) return;
        std::wstring latin = WordToLatin(word);
        if (sentenceStart && !latin.empty()) latin[0] = LatinUpper(latin[0]);
        if (!latin.empty()) sentenceStart = false;
        out += latin;
        word.clear();
    };
    for (wchar_t c : arabicScript) {
        if (Dropped(c)) continue;
        c = Normal(c);
        if (IsSoraniLetter(c)) {
            word += c;
            continue;
        }
        flush();
        if (c >= 0x0660 && c <= 0x0669) c = static_cast<wchar_t>(L'0' + (c - 0x0660));  // Arabic-Indic digits
        else if (c >= 0x06F0 && c <= 0x06F9) c = static_cast<wchar_t>(L'0' + (c - 0x06F0));
        else if (c == 0x060C) c = L',';
        else if (c == 0x061B) c = L';';
        else if (c == 0x061F) c = L'?';
        else if (c == 0x066A) c = L'%';
        else if (c == 0x06D4) c = L'.';
        if (c == L'.' || c == L'!' || c == L'?') sentenceStart = true;
        else if (std::iswalnum(c)) sentenceStart = false;
        out += c;
    }
    flush();
    return out;
}

std::wstring SoraniToArabic(const std::wstring& latin) {
    std::wstring out;
    std::wstring w;
    auto flush = [&] {
        if (w.empty()) return;
        bool prevVowel = false;
        for (size_t i = 0; i < w.size(); ++i) {
            const wchar_t c = w[i];
            const wchar_t next = i + 1 < w.size() ? w[i + 1] : 0;
            if (IsLatinVowel(c)) {
                if (i == 0 || prevVowel) out += kHamza;  // a vowel starting a syllable sits on ئ
                switch (c) {
                    case L'a': out += kAlef; break;
                    case L'e': out += kAe; break;
                    case 0x00EA: out += kEe; break;
                    case L'o': out += kOo; break;
                    case L'u': out += kWaw; break;
                    case 0x00FB: out += kWaw; out += kWaw; break;
                    case 0x00EE: out += kYeh; break;
                    case L'i': if (i == 0) out += kYeh; break;  // the short i is not written (at the start: ئی)
                    default: break;
                }
                prevVowel = true;
                continue;
            }
            prevVowel = false;
            if (c == L'g' && next == L'h') { out += 0x063A; ++i; continue; }    // gh: غ
            if (c == L'r' && next == L'r') { out += 0x0695; ++i; continue; }    // rr: ڕ
            if (c == L'l' && next == L'l') { out += 0x06B5; ++i; continue; }    // ll: ڵ
            if (c == L'w') { out += kWaw; continue; }
            if (c == L'y') { out += kYeh; continue; }
            bool found = false;
            for (const Letter& l : kConsonants)
                if (l.latin[0] == c && l.latin[1] == 0 && l.ar != 0x062D) {  // h is ه, not ح
                    out += l.ar;
                    found = true;
                    break;
                }
            if (!found) out += c;
        }
        w.clear();
    };
    for (wchar_t raw : latin) {
        const wchar_t c = LatinLower(raw);
        const bool letter = (c >= L'a' && c <= L'z') || c == 0x00E7 || c == 0x015F || c == 0x00EA || c == 0x00EE ||
                            c == 0x00FB || c == 0x0159 || c == 0x0142;
        if (letter) {
            w += c;
            continue;
        }
        flush();
        if (raw == L'?') out += static_cast<wchar_t>(0x061F);
        else if (raw == L',') out += static_cast<wchar_t>(0x060C);
        else if (raw == L';') out += static_cast<wchar_t>(0x061B);
        else out += raw;
    }
    flush();
    return out;
}

}  // namespace gct
