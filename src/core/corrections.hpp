// corrections.hpp — the correction memory: a translation you corrected once
// never comes back wrong.
//
// Two kinds of entries, per target language:
//  * whole lines: the same text (case and spacing ignored) gets your
//    translation directly, without asking a translator;
//  * phrases: when you changed only a few words ("Weltboss" -> "World Boss"),
//    that change is applied to every later translation into that language.
// Stays on this PC (a small text file next to the settings).
#pragma once

#include <map>
#include <string>
#include <vector>

namespace gct {

class CorrectionMemory {
public:
    static constexpr size_t kMaxLines = 3000, kMaxPhrases = 1000;

    // Your translation of exactly this text into `lang`, if you corrected it.
    bool Lookup(const std::wstring& source, const std::wstring& lang, std::wstring* out) const;
    // A fresh translation with your phrase corrections applied (whole words only).
    std::wstring Apply(const std::wstring& translation, const std::wstring& lang) const;
    // You replaced `wrong` (the translator's output for `source`) by `right`.
    // Stores the line and, if only a few words changed, the phrase.
    void Add(const std::wstring& source, const std::wstring& lang, const std::wstring& wrong, const std::wstring& right);

    size_t Lines() const { return lines_.size(); }
    size_t Phrases() const { return phrases_.size(); }
    void Clear();

    // File format: one entry per line, "L<TAB>lang<TAB>source<TAB>translation" or
    // "P<TAB>lang<TAB>wrong<TAB>right"; tab, newline and backslash escaped. UTF-8.
    std::string Serialize() const;
    void Parse(const std::string& utf8);
    // Adds the entries of another file (someone else's corrections); yours win on conflicts. Returns how many were new.
    size_t Merge(const std::string& utf8);

    // The words that changed between two versions of a sentence, if few
    // (common words at the start and end removed): {"Weltboss"} -> {"World Boss"}.
    static bool ChangedPhrase(const std::wstring& before, const std::wstring& after, std::wstring* from,
                              std::wstring* to);

private:
    struct Phrase {
        std::wstring lang, from, to;
    };
    static std::wstring Key(const std::wstring& source, const std::wstring& lang);
    void AddPhrase(const std::wstring& lang, const std::wstring& from, const std::wstring& to);

    std::map<std::wstring, std::wstring> lines_;  // Key(source, lang) -> translation
    std::vector<std::wstring> lineOrder_;          // oldest first (for the cap)
    std::vector<Phrase> phrases_;
};

}  // namespace gct
