// input_box.hpp — the message field: a multi-line EDIT with spell marks
// (red squiggles, blue for grammar), a suggestion menu on right-click,
// autocorrection when a word is finished (Backspace right after it undoes
// it) and the word bar (Tab takes the highlighted word).
#pragma once

#include <windows.h>

#include <functional>
#include <string>
#include <vector>

#include "app/spell_service.hpp"
#include "app/theme.hpp"

namespace gct {

class InputBox {
public:
    struct Callbacks {
        std::function<void(bool sendOriginal)> onEnter;
        std::function<void()> onEscape;
        std::function<void()> onCycleLang;        // Ctrl+L
        std::function<void()> onRomanize;         // Ctrl+U
        std::function<void()> onSwitchTab;        // Ctrl+Tab
        std::function<void(const std::wstring& from, const std::wstring& to)> onAutoCorrected;
        std::function<void(const std::wstring& original)> onCorrectionUndone;
        std::function<void(const std::wstring& word)> onForgotten;  // "Forget word" in a menu
        std::function<void(const std::wstring& word)> onExplain;    // "Explain this word" (my words: slang + meaning)
        std::function<void(const WordSuggestions&)> onSuggestions;  // the word bar changed
        // Keyboard layout changed; locale name like "ar-SA".
        std::function<void(const std::wstring& locale)> onKeyboardLanguage;
    };

    bool Create(HWND parent, HINSTANCE inst, const Theme* theme, SpellService* spell, AutoCorrectMode mode,
                bool suggestions, Callbacks cb);
    void SetAutoCorrect(AutoCorrectMode mode) { mode_ = mode; }
    void SetSuggestions(bool on);
    // Puts the word of the bar into the text (slot 0..2).
    void AcceptSuggestion(size_t index);
    void AcceptPhrase();
    // The word bar again (after a word was forgotten).
    void RefreshSuggestions() { UpdateSuggestions(); }
    const WordSuggestions& CurrentSuggestions() const { return suggestions_; }
    // Grammar marks from LanguageTool for exactly this text (dropped when the text changes).
    void SetGrammarIssues(const std::wstring& forText, std::vector<SpellIssue> issues);
    HWND Hwnd() const { return hwnd_; }

    std::wstring Text() const;
    // Note: no EN_CHANGE follows (multi-line EDIT + WM_SETTEXT); the owner
    // has to update itself.
    void Clear();
    // Keys pressed for the current text (Enter and modifier keys not counted) and whether something was pasted:
    // the measure of how much the word help saves. Reset by Clear().
    int KeyPresses() const { return keyPresses_; }
    bool Pasted() const { return pasted_; }

    // Owner forwards EN_CHANGE: marks are hidden until the next check.
    void OnTextChanged();
    // Re-applies the inner padding after a resize.
    void ApplyPadding();
    // Font and padding again after the theme changed (other monitor DPI).
    void ApplyTheme();
    // Right-to-left reading order (Arabic, Hebrew ...). Users can also
    // toggle it themselves with Ctrl + right Shift (Windows standard).
    void SetRtl(bool rtl);
    bool Rtl() const { return rtl_; }
    // Check the whole text again (after a spell-language switch).
    void RecheckSpelling() { RunSpellCheck(std::wstring::npos); }
    // Locale of the active keyboard layout ("de-DE").
    static std::wstring KeyboardLocale();

private:
    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);

    size_t Caret() const;
    POINT PosFromChar(size_t i) const;
    void RunSpellCheck(size_t caret);
    void DrawSquiggles(HDC dc) const;
    void DrawGhost(HDC dc) const;
    bool ShowSpellMenu(LPARAM lp);
    bool ShowWordMenu(LPARAM lp);
    void ReplaceRange(Span span, const std::wstring& text);
    void TryAutoCorrect();
    void FinishWordAtCaret();
    void UpdateChoices();
    const WordChoices& CurrentChoices();
    bool ApplyChoice(wchar_t boundary);
    void DrawNextGhost(HDC dc) const;
    void CycleChoice(int step);
    // Autocorrects the word ending at `end` (exclusive). `boundaryTyped`: the
    // character at `end` was just typed (space ...) and Backspace may undo.
    void CorrectWordEndingAt(size_t end, bool boundaryTyped);
    bool UndoAutoCorrect();
    void DeletePreviousWord();
    void UpdateSuggestions();

    static constexpr UINT_PTR kSpellTimer = 0x4743;
    static constexpr UINT_PTR kSuggestTimer = 0x4744;
    static constexpr UINT kSpellDelayMs = 350;
    static constexpr UINT kSuggestDelayMs = 10;  // word bar: next message loop turn (the timer minimum)

    HWND hwnd_ = nullptr;
    WNDPROC orig_ = nullptr;
    const Theme* theme_ = nullptr;
    SpellService* spell_ = nullptr;
    AutoCorrectMode mode_ = AutoCorrectMode::Phone;
    bool suggestOn_ = true;
    bool rtl_ = false;
    Callbacks cb_;
    std::vector<SpellIssue> issues_;
    std::vector<SpellIssue> grammar_;
    WordSuggestions suggestions_;
    WordChoices choices_;
    std::wstring choicesText_;   // text and caret `choices_` was worked out for
    size_t choicesCaret_ = static_cast<size_t>(-1);
    std::wstring dismissedText_; // Esc closed the dropdown for this text
    // The last autocorrection, for Backspace-undo: text after the fix.
    struct Fix {
        bool valid = false;
        size_t start = 0;       // where the corrected word starts
        std::wstring original;  // as typed
        std::wstring corrected;
        wchar_t boundary = 0;   // the character that finished the word
    } lastFix_;
    bool swallowBackspaceChar_ = false;
    int keyPresses_ = 0;
    bool pasted_ = false;
};

}  // namespace gct
