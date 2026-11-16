// gw2_text.cpp
#include "gw2_text.hpp"

#include <cstdint>
#include <cwchar>

#include "i18n.hpp"
#include "langs.hpp"
#include "text.hpp"

namespace gct {

namespace {

struct ScriptRange {
    uint32_t from, to;
    const wchar_t* name;
};

const ScriptRange kScripts[] = {
    {0x0370, 0x03FF, L"Greek"},   {0x0400, 0x052F, L"Cyrillic"},   {0x0590, 0x05FF, L"Hebrew"},
    {0x0600, 0x06FF, L"Arabic"},     {0x0750, 0x077F, L"Arabic"},     {0x08A0, 0x08FF, L"Arabic"},
    {0x0900, 0x097F, L"Devanagari"},   {0x0E00, 0x0E7F, L"Thai"},         {0x1100, 0x11FF, L"Korean"},
    {0x3040, 0x30FF, L"Japanese"},    {0x3400, 0x4DBF, L"Chinese"},   {0x4E00, 0x9FFF, L"Chinese"},
    {0xAC00, 0xD7AF, L"Korean"},   {0xFB1D, 0xFB4F, L"Hebrew"}, {0xFB50, 0xFDFF, L"Arabic"},
    {0xFE70, 0xFEFF, L"Arabic"},
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

const wchar_t* FirstMatchedScript(const std::wstring& text) {
    for (wchar_t ch : text) {
        const uint32_t c = static_cast<uint32_t>(ch);
        if (c < 0x0370) continue;  // Latin, Latin-1, Extended-A/B, IPA
        for (const ScriptRange& r : kScripts)
            if (c >= r.from && c <= r.to) return r.name;
    }
    return nullptr;
}

bool WarnScriptInGw2(const wchar_t* name) {
    // Tested "no" scripts in GW2 chat: adding another script is one line here.
    static constexpr const wchar_t* kWarnInGw2[] = {
        L"Cyrillic",
    };
    for (const wchar_t* w : kWarnInGw2)
        if (std::wcscmp(name, w) == 0) return true;
    return false;
}

std::wstring UnsupportedScript(const std::wstring& text) {
    const wchar_t* name = FirstMatchedScript(text);
    return name ? Tr(name) : std::wstring();
}

std::wstring WarnUnsupportedInGw2(const std::wstring& text) {
    for (wchar_t ch : text) {
        const uint32_t c = static_cast<uint32_t>(ch);
        if (c < 0x0370) continue;
        for (const ScriptRange& r : kScripts) {
            if (c >= r.from && c <= r.to && WarnScriptInGw2(r.name)) return Tr(r.name);
        }
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

bool OurFontCanDraw(uint32_t c) {
    if (c == '\t' || c == '\n' || c == '\r') return true;
    return (c >= 0x0020 && c <= 0x024F) || (c >= 0x0370 && c <= 0x03FF) || (c >= 0x0400 && c <= 0x04FF) ||
           (c >= 0x1E00 && c <= 0x1EFF) || (c >= 0x2000 && c <= 0x206F) || (c >= 0x20A0 && c <= 0x20CF) ||
           (c >= 0x2190 && c <= 0x21FF) || (c >= 0x2200 && c <= 0x22FF) || (c >= 0x25A0 && c <= 0x25FF);
}

std::wstring DisplayForOurFont(const std::wstring& text, const std::wstring& langHint, bool (*canDraw)(uint32_t)) {
    bool (*const check)(uint32_t) = canDraw ? canDraw : &OurFontCanDraw;
    bool undrawn = false;
    bool hasKana = false;
    const wchar_t* scriptName = nullptr;
    for (wchar_t ch : text) {
        const uint32_t c = static_cast<uint32_t>(ch);
        if (c >= 0x3040 && c <= 0x30FF) hasKana = true;
        if (check(c)) continue;
        undrawn = true;
        if (!scriptName) {
            for (const ScriptRange& r : kScripts) {
                if (c >= r.from && c <= r.to) {
                    scriptName = r.name;
                    break;
                }
            }
        }
    }
    if (!undrawn) return text;
    const std::wstring p = PrimaryLang(langHint);
    std::wstring script;
    if (hasKana || p == L"JA") script = Tr(L"Japanese");
    else if (scriptName) script = Tr(scriptName);
    else if (p == L"KO") script = Tr(L"Korean");
    else if (p == L"ZH") script = Tr(L"Chinese");
    else if (p == L"AR") script = Tr(L"Arabic");
    else if (p == L"HE") script = Tr(L"Hebrew");
    else if (p == L"HI") script = Tr(L"Devanagari");
    else if (p == L"TH") script = Tr(L"Thai");
    else script = Tr(L"Other script");
    return TrF(L"{1} – {2} characters", {script, std::to_wstring(CodePointCount(text))});
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
