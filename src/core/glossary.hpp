// glossary.hpp — official GW2 names as translation glossary.
//
// The GW2 API delivers map, profession, specialization, WvW-objective and
// mount names in en/de/fr/es/zh. Pairing two languages by id gives an exact
// glossary: "Löwenstein" becomes "Lion's Arch" instead of a literal guess.
#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "text.hpp"

namespace gct {

// Localised names of one language, keyed by "category:id" (e.g. "map:15").
using NameTable = std::unordered_map<std::string, std::wstring>;

// UTF-8, one "key<TAB>name" per line, sorted by key. Lines starting with '#'
// are comments.
std::string SerializeNameTable(const NameTable& names);
NameTable ParseNameTable(const std::string& utf8);

// Every word (folded, 2+ characters) of every name — the spell checker
// must not flag official names like "Drachenjägerin" or "Steinnebel".
void CollectNameWords(const NameTable& names, WordSet& out);

struct GlossaryMatch {
    Span span;            // position in the searched text
    std::wstring source;  // official name in the source language
    std::wstring target;  // official name in the target language
};

class Glossary {
public:
    Glossary() = default;

    // Pairs names by key. Skips names shorter than 4 code points and source
    // names that would map to different targets (ambiguous).
    static Glossary Build(const NameTable& source, const NameTable& target);

    // Whole-word, case-insensitive, longest-first, non-overlapping matches.
    // Matches overlapping a blocked span are skipped.
    std::vector<GlossaryMatch> FindAll(const std::wstring& text, const std::vector<Span>& blocked = {}) const;

    // Every word of every source name (folded) — so the spell checker does
    // not flag official names.
    void CollectSourceWords(WordSet& out) const;

    size_t Size() const { return terms_.size(); }
    bool Empty() const { return terms_.empty(); }

private:
    struct Term {
        std::wstring folded, source, target;
    };
    std::vector<Term> terms_;  // longest first
    std::unordered_map<wchar_t, std::vector<size_t>> byFirst_;
};

// The official names compiled in (data/gw2-names.tsv, tools/gen_names.py) of one language ("de", "FR"); empty for a
// language the GW2 API has no names in.
const NameTable& BuiltinNames(const std::wstring& lang);

// The glossary for a sentence written in `source`, translated into `target`: the official names and the usual words
// (data/gw2-words.tsv: raid, skin, waypoint ... in each language). Both ways, by one rule: a name written in
// the sentence's own language becomes the target's official name ("Löwenstein" → "Lion's Arch", "meet at Lion's Arch"
// → "Löwenstein"; English where the target has no names); a name in another language is the writer's choice
// ("Lion's Arch" in a German sentence) and is not touched here. Null when the source has no names or is the target.
const Glossary* BuiltinGlossary(const std::wstring& source, const std::wstring& target);

}  // namespace gct
