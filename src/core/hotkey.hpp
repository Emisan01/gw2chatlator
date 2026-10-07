// hotkey.hpp — parses hotkey strings like "Ctrl+Alt+T" from the INI.
#pragma once

#include <optional>
#include <string>

namespace gct {

// Modifier bits match Win32 MOD_ALT / MOD_CONTROL / MOD_SHIFT / MOD_WIN.
constexpr unsigned kModAlt = 0x1, kModCtrl = 0x2, kModShift = 0x4, kModWin = 0x8;

struct Hotkey {
    unsigned mods = 0;
    unsigned vk = 0;  // Win32 virtual-key code
};

// Accepts English and German names: "Ctrl+Alt+T", "Strg+Umschalt+F9", "F10".
// Letters, digits and Space require a modifier — they would otherwise be
// taken away from every program, GW2 included.
std::optional<Hotkey> ParseHotkey(const std::wstring& s);
// The same string back ("Ctrl+Alt+Shift+T"); empty for keys ParseHotkey does not know.
std::wstring FormatHotkey(const Hotkey& hk);

}  // namespace gct
