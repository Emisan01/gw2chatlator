// els.hpp — Windows Extended Linguistic Services (offline, built into
// Windows): language detection and transliteration to Latin letters.
// elscore.dll is loaded at runtime; everything degrades to "unavailable".
#pragma once

#include <string>

namespace gct {

// Primary language of `text` as an upper-case code ("DE", "EN", "AR" ...),
// or empty when unsure (very short text) or ELS is unavailable.
std::wstring DetectLanguage(const std::wstring& text);

// Cyrillic / Devanagari -> Latin letters. Returns the input unchanged if
// nothing applies or ELS is unavailable.
std::wstring TransliterateToLatin(const std::wstring& text);


}  // namespace gct
