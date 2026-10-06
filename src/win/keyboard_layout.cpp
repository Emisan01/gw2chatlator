// keyboard_layout.cpp
#include "keyboard_layout.hpp"

#include <string>

namespace gct {

KeyLayout LayoutFor(HKL layout) {
    // Scan codes of the letter rows and how far each row is shifted to the right.
    struct Row {
        UINT first, last;
        float offset;
    };
    const Row rows[] = {{0x10, 0x1B, 0.0f}, {0x1E, 0x28, 0.25f}, {0x2C, 0x35, 0.75f}};
    KeyLayout k;
    int letters = 0;
    BYTE state[256] = {};
    for (int r = 0; r < 3; ++r) {
        for (UINT sc = rows[r].first; sc <= rows[r].last; ++sc) {
            const UINT vk = MapVirtualKeyExW(sc, MAPVK_VSC_TO_VK_EX, layout);
            if (!vk) continue;
            wchar_t buf[4] = {};
            // Flag 4: do not change the keyboard state (no dead-key leftovers).
            if (ToUnicodeEx(vk, sc, state, buf, 4, 4, layout) != 1) continue;
            const std::wstring key = WordKey(std::wstring(1, buf[0]));
            if (key.size() != 1) continue;
            k.Set(key[0], r, static_cast<float>(sc - rows[r].first) + rows[r].offset);
            ++letters;
        }
    }
    return letters >= 10 ? k : KeyLayout{};
}

}  // namespace gct
