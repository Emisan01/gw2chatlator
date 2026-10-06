// gw2_text.cpp
#include "gw2_text.hpp"

#include <cstdint>

#include "text.hpp"

namespace gct {

namespace {

struct ScriptRange {
    uint32_t from, to;
    const wchar_t* name;
};

const ScriptRange kScripts[] = {
    {0x0370, 0x03FF, L"Griechisch"},   {0x0400, 0x052F, L"Kyrillisch"},   {0x0590, 0x05FF, L"Hebr\u00e4isch"},
    {0x0600, 0x06FF, L"Arabisch"},     {0x0750, 0x077F, L"Arabisch"},     {0x08A0, 0x08FF, L"Arabisch"},
    {0x0900, 0x097F, L"Devanagari"},   {0x0E00, 0x0E7F, L"Thai"},         {0x1100, 0x11FF, L"Koreanisch"},
    {0x3040, 0x30FF, L"Japanisch"},    {0x3400, 0x4DBF, L"Chinesisch"},   {0x4E00, 0x9FFF, L"Chinesisch"},
    {0xAC00, 0xD7AF, L"Koreanisch"},   {0xFB1D, 0xFB4F, L"Hebr\u00e4isch"}, {0xFB50, 0xFDFF, L"Arabisch"},
    {0xFE70, 0xFEFF, L"Arabisch"},
};

bool IsRtlChar(uint32_t c) {
    return (c >= 0x0590 && c <= 0x08FF) || (c >= 0xFB1D && c <= 0xFDFF) || (c >= 0xFE70 && c <= 0xFEFF);
}

bool IsStrongLtr(uint32_t c) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) return true;
    if (c >= 0xC0 && c <= 0x24F && c != 0xD7 && c != 0xF7) return true;
    if (c >= 0x0370 && c <= 0x052F) return true;  // Greek, Cyrillic
    if (c >= 0x0900 && c < 0xFB1D && !(c >= 0x2000 && c <= 0x2BFF) && !(c >= 0x3000 && c <= 0x303F)) return true;
    return false;
}

}  // namespace

std::wstring UnsupportedScript(const std::wstring& text) {
    for (wchar_t ch : text) {
        const uint32_t c = static_cast<uint32_t>(ch);
        if (c < 0x0370) continue;  // Latin, Latin-1, Extended-A/B, IPA
        for (const ScriptRange& r : kScripts)
            if (c >= r.from && c <= r.to) return r.name;
    }
    return {};
}

bool IsRtlText(const std::wstring& text) {
    for (wchar_t ch : text) {
        const uint32_t c = static_cast<uint32_t>(ch);
        if (IsRtlChar(c)) return true;
        if (IsStrongLtr(c)) return false;
    }
    return false;
}

std::vector<std::wstring> SplitForChat(const std::wstring& text, const std::wstring& prefix, size_t maxCodePoints) {
    std::vector<std::wstring> parts;
    const size_t budget = maxCodePoints > CodePointCount(prefix) + 10 ? maxCodePoints - CodePointCount(prefix) : 10;
    std::wstring rest = Trim(text);
    while (!rest.empty()) {
        if (CodePointCount(rest) <= budget) {
            parts.push_back(prefix + rest);
            break;
        }
        // Largest prefix of `rest` within budget (in UTF-16 units, surrogate-safe).
        size_t cut = 0, cps = 0;
        while (cut < rest.size() && cps < budget) {
            const uint32_t c = static_cast<uint32_t>(rest[cut]);
            cut += (sizeof(wchar_t) == 2 && c >= 0xD800 && c <= 0xDBFF && cut + 1 < rest.size()) ? 2 : 1;
            ++cps;
        }
        size_t best = std::wstring::npos;
        for (size_t i = cut; i > cut / 3; --i) {  // sentence end, not too early
            const wchar_t c = rest[i - 1];
            if ((c == L'.' || c == L'!' || c == L'?' || c == 0x3002 || c == 0xFF01 || c == 0xFF1F) &&
                (i == rest.size() || rest[i] == L' ' || c >= 0x3000)) {
                best = i;
                break;
            }
        }
        if (best == std::wstring::npos) {
            for (size_t i = cut; i > cut / 4; --i)
                if (rest[i - 1] == L' ') { best = i; break; }
        }
        if (best == std::wstring::npos) best = cut;  // one long word / CJK without spaces
        parts.push_back(prefix + Trim(rest.substr(0, best)));
        rest = Trim(rest.substr(best));
    }
    return parts;
}

}  // namespace gct
