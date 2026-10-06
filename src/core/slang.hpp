// slang.hpp — built-in GW2 vocabulary (all entries case-folded).
#pragma once

#include "text.hpp"

namespace gct {

// Never sent to the translator: abbreviations every GW2 player reads the
// same way in any language (LFG, WvW, DPS, Ele, Chrono ...).
const WordSet& BuiltinKeepWords();

// Never flagged by the spell checker: the keep-words plus chat slang that is
// fine to translate but not in any dictionary (Kommi, stacken, Fraktale ...).
const WordSet& BuiltinSpellIgnore();

}  // namespace gct
