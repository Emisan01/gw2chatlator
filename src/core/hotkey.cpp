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

std::wstring FormatHotkey(const Hotkey& hk) {
    std::wstring key;
    if (hk.vk >= 0x41 && hk.vk <= 0x5A) key = std::wstring(1, static_cast<wchar_t>(L'A' + (hk.vk - 0x41)));
    else if (hk.vk >= 0x30 && hk.vk <= 0x39) key = std::wstring(1, static_cast<wchar_t>(L'0' + (hk.vk - 0x30)));
    else if (hk.vk >= 0x70 && hk.vk <= 0x87) key = L"F" + std::to_wstring(hk.vk - 0x70 + 1);
    else if (hk.vk >= 0x60 && hk.vk <= 0x69) key = L"Numpad" + std::to_wstring(hk.vk - 0x60);
    else if (hk.vk == 0x20) key = L"Space";
    else if (hk.vk == 0x2D) key = L"Insert";
    else if (hk.vk == 0x24) key = L"Home";
    else if (hk.vk == 0x23) key = L"End";
    else if (hk.vk == 0x21) key = L"PageUp";
    else if (hk.vk == 0x22) key = L"PageDown";
    else if (hk.vk == 0x13) key = L"Pause";
    else if (hk.vk == 0x91) key = L"ScrollLock";
    else return L"";
    std::wstring s;
    if (hk.mods & kModCtrl) s += L"Ctrl+";
    if (hk.mods & kModAlt) s += L"Alt+";
    if (hk.mods & kModShift) s += L"Shift+";
    if (hk.mods & kModWin) s += L"Win+";
    return s + key;
}

}  // namespace gct
