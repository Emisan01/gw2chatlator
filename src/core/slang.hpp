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

// The English everyone writes in a GW2 chat, whatever language they type in ("with", "sorry", "thanks", "good"):
// never a typo to correct while writing another language.
const WordSet& CommonChatEnglish();

// German spoken contractions ("habs", "geht's", "gibts"): a way of writing, never a typo – and for a translator
// written out ("hab es", "geht es", "gibt es"; translators leave "habs" as it is and break the sentence). Only the text
// that goes to the translator; what you see and send stays as written.
bool IsGermanContraction(const std::wstring& word);
std::wstring ExpandGermanContractions(const std::wstring& text);

// Two capitals or more ("LA", "HoT", "CoF", "WvW").
bool LooksLikeAbbreviation(const std::wstring& word);

// `word` is in a keep/ignore set from above: in any spelling, or – for letters that are an ordinary word somewhere
// ("la", "hot", "de") – only when written like an abbreviation; entries "=Word" (names the user taught) only exactly so.
bool IsKeepWord(const WordSet& keep, const std::wstring& word);

// What the word bar offers before it has learned anything: GW2 words as
// they are written (English and German forms), for completions only.
// GW2 words for the word bar, for the language you write in ("DE", "EN-GB"; other languages: only the terms
// everyone uses; empty = all, for tests).
const std::vector<std::wstring>& Gw2StarterWords(const std::wstring& lang = L"");

}  // namespace gct
