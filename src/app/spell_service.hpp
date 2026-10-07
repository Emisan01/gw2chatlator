// spell_service.hpp — everything that helps while typing:
//  * Windows spell checker (offline, follows the keyboard layout)
//  * GW2 knowledge: built-in slang, official game names, your own word list
//  * a phone-keyboard word model that learns what you send (per language):
//    completions, next-word suggestions and phone-style autocorrection
// Lives on the UI thread.
#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "app/config.hpp"
#include "core/text.hpp"
#include "core/word_model.hpp"
#include "win/spellcheck.hpp"

namespace gct {

struct WordSuggestions {
    enum class Kind { None, Completion, Correction, Next };
    Kind kind = Kind::None;
    std::vector<std::wstring> words;  // up to 3
    int autoIndex = -1;               // what Tab takes (highlighted); -1 = none
    Span replace;                     // the part of the text a pick replaces (empty = insert at caret)
    std::wstring phrase;              // Next: the sure rest of a phrase ("bis morgen mit micro"), Tab takes it all
};

// The dropdown under the word being typed, like on a phone keyboard: Space
// takes the highlighted entry, Tab moves through the list.
struct WordChoices {
    std::vector<std::wstring> words;  // up to 5
    int highlight = -1;               // what Space takes; -1: Space is just a space
    bool firstIsTyped = false;        // words[0] is the word as typed (a valid word: Space keeps it)
    Span replace;                     // the word being typed
    bool Empty() const { return words.empty(); }
    // Space would change the text.
    bool Changes() const { return highlight >= 0 && !(firstIsTyped && highlight == 0); }
};

class SpellService {
public:
    // `tags` from SpellTagCandidates; `userWordsPath` is created on first add.
    bool Init(const std::vector<std::wstring>& tags, const std::wstring& userWordsPath);
    bool Ready() const { return checker_.Ready(); }
    // Another language (the keyboard layout changed). False if Windows has
    // no checker for it — spelling is then off until the next switch, rather
    // than marking every word of a foreign language as wrong.
    bool SwitchLanguage(const std::vector<std::wstring>& tags) {
        ClearCache();
        return checker_.Init(tags);
    }
    const std::wstring& Tag() const { return checker_.Tag(); }

    void SetGameWords(WordSet words) { game_ = std::move(words); }

    // The learned words of this language ("de"), from `dir`\learned_de.txt.
    void UseLearnedLanguage(const std::wstring& primaryLang, const std::wstring& dir);
    void Learn(const std::wstring& sentText);
    void SaveLearned();
    // Backspace right after an autocorrection: the word was meant as typed.
    void RejectCorrection(const std::wstring& original);
    const WordModel& Model() const { return model_; }
    bool IsLearned(const std::wstring& word) const { return model_.Count(word) > 0; }
    // A word taught by mistake: gone from the word bar and autocorrection.
    bool Forget(const std::wstring& word);
    // Deletes everything learned, in every language (learned_*.txt).
    void ForgetAll();

    // Issues worth showing. Skips GW2 words, anything with digits, chat
    // codes, the chat-command prefix and the word the caret is touching
    // (still being typed). `caret` = npos to check everything.
    std::vector<SpellIssue> Check(const std::wstring& text, size_t caret) const;

    std::vector<std::wstring> Suggest(const std::wstring& word) const;

    // Correction for a just-finished word: Safe = only Windows' sure fixes,
    // Phone = also the most likely word for a typo (see ChooseCorrection).
    std::optional<std::wstring> AutoCorrection(const std::wstring& word, AutoCorrectMode mode) const;

    // The word bar for the text and caret position.
    WordSuggestions Suggestions(const std::wstring& text, size_t caret, AutoCorrectMode mode) const;
    // The dropdown for the word ending at the caret: the word itself if it is
    // valid, completions, then corrections for typing slips (key next door,
    // the whole hand one key off, the dictionary's ideas). Phone mode
    // highlights the best change for a word that is not valid; Safe mode
    // highlights nothing (Tab chooses).
    WordChoices Choices(const std::wstring& text, size_t caret, AutoCorrectMode mode) const;

    // Words of the chat right now (names, places, what was just asked), newest first: completions offer them right
    // after your own words. Never learned – the word model learns only from what you send.
    void SetContext(std::vector<std::wstring> words) { context_ = std::move(words); }
    // A suggestion was taken: `word` now stands at `start` in `text`. `deliberate` = picked on purpose (clicked, or
    // Tab to another one) – that counts more than simply going on with the first. Off with "learn my words".
    void Chose(const std::wstring& text, size_t start, const std::wstring& word, bool deliberate);
    void SetLearnChoices(bool on) { learnChoices_ = on; }
    void AddUserWord(const std::wstring& word);    // persists
    void IgnoreForSession(const std::wstring& word);

    // Ranges no spell logic may touch: chat codes and a "/w Name, " prefix.
    static std::vector<Span> ProtectedSpans(const std::wstring& text);

private:
    bool IsKnown(const std::wstring& word) const;
    bool IsMisspelled(const std::wstring& word) const;
    // The Windows spell checker is a COM call; the word bar asks on every key
    // press, so answers per word are kept until the language changes.
    bool CheckerRejects(const std::wstring& word) const;
    const std::vector<std::wstring>& CheckerSuggest(const std::wstring& word) const;
    void ClearCache() const;

    SpellChecker checker_;
    KeyLayout layout_;
    KeyNeighbors neighbors_;
    mutable std::unordered_map<std::wstring, bool> rejectCache_;
    mutable std::unordered_map<std::wstring, std::vector<std::wstring>> suggestCache_;
    WordSet game_, user_, session_;
    std::wstring userPath_;
    WordModel model_;
    std::wstring learnedPath_;
    std::wstring learnedLang_;  // "de": the language of the learned words and the starter list
    std::vector<std::wstring> context_;  // words of the recent chat, newest first (SetContext)
    bool learnChoices_ = true;
    void AddContextCompletions(const std::wstring& typed, std::vector<std::wstring>& out, size_t max) const;
};

}  // namespace gct
