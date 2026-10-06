// input_box.hpp — the message field: a multi-line EDIT with spell marks
// (red squiggles), a suggestion menu on right-click and Windows'
// autocorrection when a word is finished.
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
        // Keyboard layout changed; locale name like "ar-SA".
        std::function<void(const std::wstring& locale)> onKeyboardLanguage;
    };

    bool Create(HWND parent, HINSTANCE inst, const Theme* theme, SpellService* spell, bool autoCorrect, Callbacks cb);
    HWND Hwnd() const { return hwnd_; }

    std::wstring Text() const;
    // Note: no EN_CHANGE follows (multi-line EDIT + WM_SETTEXT); the owner
    // has to update itself.
    void Clear();

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
    bool ShowSpellMenu(LPARAM lp);
    void ReplaceRange(Span span, const std::wstring& text);
    void TryAutoCorrect();
    void DeletePreviousWord();

    static constexpr UINT_PTR kSpellTimer = 0x4743;
    static constexpr UINT kSpellDelayMs = 350;

    HWND hwnd_ = nullptr;
    WNDPROC orig_ = nullptr;
    const Theme* theme_ = nullptr;
    SpellService* spell_ = nullptr;
    bool autoCorrect_ = true;
    bool rtl_ = false;
    Callbacks cb_;
    std::vector<SpellIssue> issues_;
};

}  // namespace gct
