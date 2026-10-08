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
#include "core/slang.hpp"
#include "core/text.hpp"
#include "core/typo_memory.hpp"
#include "core/word_model.hpp"
#include "win/spellcheck.hpp"
#include "win/text_prediction.hpp"

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
    bool SwitchLanguage(const std::vector<std::wstring>& tags);
    const std::wstring& Tag() const { return checker_.Tag(); }

    void SetGameWords(WordSet words) { game_ = std::move(words); }

    // The learned words of this language ("de"), from `dir`\learned_de.txt.
    void UseLearnedLanguage(const std::wstring& primaryLang, const std::wstring& dir);
    void Learn(const std::wstring& sentText);
    // Your own texts (old chats, mails, notes) as a typing profile: each sentence is learned like a sent message.
    // Lines that look like code or links are skipped. A word the dictionary rejects is one of your typical typos
    // when its fix is clear (a word of your own text one edit away, used at least twice as often, or the only close
    // idea of the dictionary): the typo goes into the typo memory, the sentence is learned with the fix. Otherwise
    // it is your slang when used 3+ times, else the sentence is cut there.
    struct ProfileResult {
        size_t sentences = 0;  // learned
        size_t newWords = 0;   // words the model did not have before
        size_t typos = 0;      // typical typos found (with what was meant)
    };
    ProfileResult LearnFromText(const std::wstring& text);
    void SaveLearned();
    // Backspace right after an autocorrection: the word was meant as typed.
    void RejectCorrection(const std::wstring& original);
    const WordModel& Model() const { return model_; }
    const TypoMemory& Typos() const { return typos_; }
    // A correction was kept (Space/Tab wrote `fix` for `typed`): a typo you make, corrected without guessing next
    // time. Completions are no typos. Backspace right after it (RejectCorrection) forgets it again.
    void NoteFix(const std::wstring& typed, const std::wstring& fix);
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
    // Names you taught ("Fallen" for "A Fallen Warrior"): always written as taught. Typing the plain word offers the
    // name first (Space) and the ordinary word right after (Tab) – "fallen" stays a verb when you want it.
    // Stored next to your words in my-names.txt.
    void AddName(const std::wstring& name);
    bool RemoveName(const std::wstring& word);
    std::wstring NameFor(const std::wstring& word) const;
    // Never translated: the built-in GW2 abbreviations plus the names you taught.
    const WordSet& KeepWords() const { return keep_; }
    void IgnoreForSession(const std::wstring& word);

    // Ranges no spell logic may touch: chat codes and a "/w Name, " prefix.
    static std::vector<Span> ProtectedSpans(const std::wstring& text);

private:
    bool IsKnown(const std::wstring& word) const;
    bool IsValidWord(const std::wstring& word) const;
    static bool LettersOnly(const std::wstring& word);
    // A dictionary idea Space may write: the dictionary's first idea, and only when it is one slip of the fingers
    // away ("shon" -> "schon"; "habs" -> "Harbs" or "shon" -> "Sohn" never).
    bool DictionarySlip(const std::wstring& typed, const std::wstring& fix) const;
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
    TypoMemory typos_;  // your typical typos -> what you meant (typos_<lang>.txt next to the learned words)
    std::wstring typosPath_;
    // The fix you meant with this typo, in the case it was typed; empty if unknown or `typed` is a word.
    std::wstring KnownFix(const std::wstring& typed) const;
    std::wstring GuessFix(const std::wstring& typo, const std::unordered_map<std::wstring, int>& uses,
                          const std::unordered_map<std::wstring, std::wstring>& forms) const;
    std::wstring learnedLang_;  // "de": the language of the learned words and the starter list
    std::vector<std::wstring> context_;  // words of the recent chat, newest first (SetContext)
    std::unordered_map<std::wstring, std::wstring> names_;  // folded -> as taught
    std::wstring namesPath_;
    WordSet keep_ = BuiltinKeepWords();
    void SaveNames();
    bool learnChoices_ = true;
    void AddContextCompletions(const std::wstring& typed, std::vector<std::wstring>& out, size_t max) const;
    // Windows' own word prediction (the touch keyboard's): the basic vocabulary of the language before anything is
    // learned ("Sch" -> "schon", "schön"; after "wie": "g" -> "geht's"). Only words that start like `typed`.
    TextPrediction predict_;
    void InitPrediction(const std::vector<std::wstring>& tags);
    // `firstSmall`: the first one is written small by Windows (an ordinary word, no name or brand like "WhatsApp").
    std::vector<std::wstring> Predicted(const std::wstring& prev, const std::wstring& typed, size_t max,
                                        bool* firstSmall = nullptr) const;
    bool PredictedExactly(const std::wstring& typed) const;
    // Words nobody taught yet: Windows' everyday words and the GW2 starter list. From 4 letters on a GW2 word
    // comes first ("Tequ" -> "Tequatl", not "Tequila"); before, the everyday words ("Sch" -> "schon").
    // `firstPredicted` = the first entry is Windows' best guess.
    std::vector<std::wstring> BaseCompletions(const std::wstring& prev, const std::wstring& typed, size_t max,
                                              bool* firstPredicted) const;
};

}  // namespace gct
