// sorani.hpp — Sorani (Central Kurdish, CKB) between its Arabic-based alphabet and the Kurdish Latin alphabet
// (Emi, 2026-11-25): GW2's chat and our window show no Arabic script, NLLB reads and writes it (ckb_Arab). What goes
// out or is shown is Latin; what you write in Latin goes to the model in its own alphabet. Portable, no windows.h.
//
// The Sorani alphabet writes every vowel but one: a e ê o u û î are letters (ا ە ێ ۆ و وو ی); و and ی are a consonant
// (w, y) or a vowel (u, î) by their place – a consonant at a word's start, next to a vowel or doubled as û; the short
// "i" is not written at all and comes back by two rules: a word without any vowel gets it after its first letter
// ("دڵ" -> "dił"), two consonants at a word's start get it between them ("برا" -> "bira"), except before w or y
// ("خوێن" -> "xwên"). ڕ is ř, ڵ is ł (Czech and Polish letters GW2 shows); ح is h, غ is "gh", ع is left out
// (ḧ ẍ are not proven in GW2's font). UNCHECKED by a Sorani speaker.
#pragma once

#include <string>

namespace gct {

std::wstring SoraniToLatin(const std::wstring& arabicScript);
std::wstring SoraniToArabic(const std::wstring& latin);

}  // namespace gct
