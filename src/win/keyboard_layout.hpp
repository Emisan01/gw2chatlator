// keyboard_layout.hpp — where the letters sit on the active keyboard layout
// (QWERTZ, AZERTY, Arabic ...). Feeds the typing help: a slip to the key next
// door, or the whole hand one key off, is the most likely typo.
#pragma once

#include <windows.h>

#include "core/word_model.hpp"

namespace gct {

// The three letter rows of `layout` (letters WordKey'd). Empty if Windows
// reports too few letters.
KeyLayout LayoutFor(HKL layout);

}  // namespace gct
