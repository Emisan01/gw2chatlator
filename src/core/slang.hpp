// slang.hpp — built-in GW2 vocabulary (all entries case-folded).
#pragma once

#include "text.hpp"

namespace gct {

// The built-in GW2 terms: data/gw2-terms.tsv, compiled in (tools/gen_terms.py writes gw2_terms.inc from it).
//   Keep – never translated, in any spelling ("LFG", "lfg"); several words are one term ("Wizard's Vault")
//   Caps – never translated when written like an abbreviation ("AB" Auric Basin; German "ab" stays a word)
//   Word – translated like any word, only never marked as a spelling error ("stacken", "Kommi")
enum class TermKind { Keep, Caps, Word };
struct BuiltinTerm {
    const wchar_t* term;
    TermKind kind;
    const wchar_t* meaning;  // a short note for whoever keeps the list
};
const std::vector<BuiltinTerm>& BuiltinTerms();
// The lines of a term table (the TSV's format: term <TAB> kind <TAB> meaning; "#" comments) – for the test that the
// compiled table matches data/gw2-terms.tsv. An unknown kind becomes Word.
struct TermLine {
    std::wstring term, meaning;
    TermKind kind = TermKind::Word;
};
std::vector<TermLine> ParseTermTable(const std::string& utf8);

// A term of several words ("Wizard's Vault", "Death-Branded Shatterer") is matched word by word, in any case and with
// or without apostrophes ("wizards vault"): its key is the folded words without apostrophes, joined by one space. A
// term of one word is the folded word, as before. kMaxTermWords: the protection looks this far.
std::wstring TermKey(const std::wstring& term);
std::wstring PhraseWordKey(const std::wstring& word);  // one word of a phrase key
constexpr size_t kMaxTermWords = 5;

// Never sent to the translator: abbreviations every GW2 player reads the
// same way in any language (LFG, WvW, DPS, Ele, Chrono ...) and official names of several words.
const WordSet& BuiltinKeepWords();

// gw2-terms.txt in the data folder: your terms on top (they stay from version to version: a guild tag, a guild's
// name, your own short forms; a note may follow after a tab) and the built-in list at the end, written again by every
// version – so a new version brings its new terms and you can read them. Your terms are never translated and never
// marked as spelling errors (any spelling).
std::vector<std::wstring> ParseMyTerms(const std::string& utf8);    // yours: the lines before the built-in block
std::string WriteTermsFile(const std::vector<std::wstring>& mine);  // the whole file: header, yours, built-in block
void AddMyTerms(WordSet& keep, const std::vector<std::wstring>& mine);

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
