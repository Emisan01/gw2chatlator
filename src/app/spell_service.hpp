// spell_service.hpp — Windows spell checker plus GW2 knowledge:
// built-in slang, official game names and the player's own word list
// (gw2-woerter.txt). Lives on the UI thread.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "core/text.hpp"
#include "win/spellcheck.hpp"

namespace gct {

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

    // Issues worth showing. Skips GW2 words, anything with digits, chat
    // codes, the chat-command prefix and the word the caret is touching
    // (still being typed). `caret` = npos to check everything.
    std::vector<SpellIssue> Check(const std::wstring& text, size_t caret) const;

    std::vector<std::wstring> Suggest(const std::wstring& word) const;

    // Windows' own safe correction for a just-finished word, if any.
    std::optional<std::wstring> AutoCorrection(const std::wstring& word) const;

    void AddUserWord(const std::wstring& word);    // persists
    void IgnoreForSession(const std::wstring& word);

    // Ranges no spell logic may touch: chat codes and a "/w Name, " prefix.
    static std::vector<Span> ProtectedSpans(const std::wstring& text);

private:
    bool IsKnown(const std::wstring& word) const;

    SpellChecker checker_;
    WordSet game_, user_, session_;
    std::wstring userPath_;
};

}  // namespace gct
