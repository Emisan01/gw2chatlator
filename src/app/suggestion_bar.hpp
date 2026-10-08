// suggestion_bar.hpp — the word bar above the input, like on a phone
// keyboard: three slots with completions, corrections or the next word.
// The highlighted slot is what Tab (or the space after a typo) takes.
// Clicking a slot never takes the focus away from the input.
#pragma once

#include <windows.h>

#include <functional>

#include "app/spell_service.hpp"
#include "app/theme.hpp"

namespace gct {

class SuggestionBar {
public:
    static bool Register(HINSTANCE inst);
    bool Create(HWND parent, HINSTANCE inst, const Theme* theme, std::function<void(size_t)> onPick);
    HWND Hwnd() const { return hwnd_; }
    void Set(const WordSuggestions& s);
    int PreferredHeight() const;
    // Right-click on a learned word offers "Forget": `isLearned` decides,
    // `onForget` does it.
    void SetForget(std::function<bool(const std::wstring&)> isLearned,
                   std::function<void(const std::wstring&)> onForget) {
        isLearned_ = std::move(isLearned);
        onForget_ = std::move(onForget);
    }

private:
    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);
    void Paint();
    int SlotAt(int x) const;

    HWND hwnd_ = nullptr;
    const Theme* theme_ = nullptr;
    std::function<void(size_t)> onPick_;
    std::function<bool(const std::wstring&)> isLearned_;
    std::function<void(const std::wstring&)> onForget_;
    std::function<std::wstring(const std::wstring&)> nameFor_;
    std::function<void(const std::wstring&, bool)> setName_;

public:
    // Right-click also offers "is a name" / "is not a name": `nameFor` gives the taught name or "", `setName` sets it.
    void SetNames(std::function<std::wstring(const std::wstring&)> nameFor,
                  std::function<void(const std::wstring&, bool)> setName) {
        nameFor_ = std::move(nameFor);
        setName_ = std::move(setName);
    }

private:
    WordSuggestions s_;
    int hot_ = -1;
};

}  // namespace gct
