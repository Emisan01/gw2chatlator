// spell_service.hpp — everything that helps while typing:
//  * Windows spell checker (offline, follows the keyboard layout)
//  * GW2 knowledge: built-in slang, official game names, your own word list
//  * a phone-keyboard word model that learns what you send (per language):
//    completions, next-word suggestions and phone-style autocorrection
// Lives on the UI thread.
#pragma once

#include <optional>
#include <string>
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
};

class SpellService {
public:
    // `tags` from SpellTagCandidates; `userWordsPath` is created on first add.
    bool Init(const std::vector<std::wstring>& tags, const std::wstring& userWordsPath);
    bool Ready() const { return checker_.Ready(); }
    // Another language (the keyboard layout changed). False if Windows has
    // no checker for it — spelling is then off until the next switch, rather
    // than marking every word of a foreign language as wrong.
    bool SwitchLanguage(const std::vector<std::wstring>& tags) { return checker_.Init(tags); }
    const std::wstring& Tag() const { return checker_.Tag(); }

    void SetGameWords(WordSet words) { game_ = std::move(words); }

    // The learned words of this language ("de"), from `dir`\learned_de.txt.
    void UseLearnedLanguage(const std::wstring& primaryLang, const std::wstring& dir);
    void Learn(const std::wstring& sentText);
    void SaveLearned();
    // Backspace right after an autocorrection: the word was meant as typed.
    void RejectCorrection(const std::wstring& original);
    const WordModel& Model() const { return model_; }

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

    void AddUserWord(const std::wstring& word);    // persists
    void IgnoreForSession(const std::wstring& word);

    // Ranges no spell logic may touch: chat codes and a "/w Name, " prefix.
    static std::vector<Span> ProtectedSpans(const std::wstring& text);

private:
    bool IsKnown(const std::wstring& word) const;
    bool IsMisspelled(const std::wstring& word) const;

    SpellChecker checker_;
    WordSet game_, user_, session_;
    std::wstring userPath_;
    WordModel model_;
    std::wstring learnedPath_;
};

}  // namespace gct
