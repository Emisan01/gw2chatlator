// hotkey.cpp
#include "hotkey.hpp"

#include <cwchar>
#include <vector>

#include "text.hpp"

namespace gct {

std::optional<Hotkey> ParseHotkey(const std::wstring& s) {
    Hotkey hk;
    std::wstring t = ToLowerAscii(Trim(s));
    if (t.empty()) return std::nullopt;

    std::vector<std::wstring> parts;
    size_t start = 0;
    while (true) {
        size_t plus = t.find(L'+', start);
        parts.push_back(Trim(t.substr(start, plus == std::wstring::npos ? std::wstring::npos : plus - start)));
        if (plus == std::wstring::npos) break;
        start = plus + 1;
    }
    if (parts.empty()) return std::nullopt;

    for (size_t i = 0; i + 1 < parts.size(); ++i) {
        const std::wstring& m = parts[i];
        if (m == L"ctrl" || m == L"control" || m == L"strg") hk.mods |= kModCtrl;
        else if (m == L"alt") hk.mods |= kModAlt;
        else if (m == L"shift" || m == L"umschalt") hk.mods |= kModShift;
        else if (m == L"win") hk.mods |= kModWin;
        else return std::nullopt;
    }

    const std::wstring& k = parts.back();
    bool keyNeedsNoModifier = false;
    if (k.size() == 1 && k[0] >= L'a' && k[0] <= L'z') {
        hk.vk = 0x41 + (k[0] - L'a');
    } else if (k.size() == 1 && k[0] >= L'0' && k[0] <= L'9') {
        hk.vk = 0x30 + (k[0] - L'0');
    } else if (k.size() >= 2 && k[0] == L'f') {
        wchar_t* end = nullptr;
        long n = std::wcstol(k.c_str() + 1, &end, 10);
        if (!end || *end != 0 || n < 1 || n > 24) return std::nullopt;
        hk.vk = 0x70 + static_cast<unsigned>(n - 1);
        keyNeedsNoModifier = true;
    } else if (k.rfind(L"numpad", 0) == 0 && k.size() == 7 && k[6] >= L'0' && k[6] <= L'9') {
        hk.vk = 0x60 + (k[6] - L'0');
    } else if (k == L"space" || k == L"leertaste") {
        hk.vk = 0x20;
    } else if (k == L"insert" || k == L"einfg") {
        hk.vk = 0x2D; keyNeedsNoModifier = true;
    } else if (k == L"home" || k == L"pos1") {
        hk.vk = 0x24;
    } else if (k == L"end" || k == L"ende") {
        hk.vk = 0x23;
    } else if (k == L"pageup" || k == L"pgup" || k == L"bildauf") {
        hk.vk = 0x21;
    } else if (k == L"pagedown" || k == L"pgdn" || k == L"bildab") {
        hk.vk = 0x22;
    } else if (k == L"pause") {
        hk.vk = 0x13; keyNeedsNoModifier = true;
    } else if (k == L"scrolllock" || k == L"rollen") {
        hk.vk = 0x91; keyNeedsNoModifier = true;
    } else {
        return std::nullopt;
    }
    // A bare letter/digit/space would be stolen from every app (and from GW2).
    if (hk.mods == 0 && !keyNeedsNoModifier) return std::nullopt;
    return hk;
}

}  // namespace gct
