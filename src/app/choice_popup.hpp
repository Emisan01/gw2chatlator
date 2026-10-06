// choice_popup.hpp — the dropdown under the word being typed: a small list
// that never takes the focus (typing goes on in the input). Space takes the
// highlighted entry, Tab moves on, a click picks one (handled by InputBox).
#pragma once

#include <windows.h>

#include <functional>

#include "app/spell_service.hpp"
#include "app/theme.hpp"

namespace gct {

class ChoicePopup {
public:
    static bool Register(HINSTANCE inst);
    bool Create(HWND owner, HINSTANCE inst, const Theme* theme, std::function<void(size_t)> onPick);
    // Shows `c` with its top-left corner at `screenPos` (below the word).
    void Show(const WordChoices& c, POINT screenPos);
    void Hide();
    bool Visible() const { return visible_; }
    HWND Hwnd() const { return hwnd_; }

private:
    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);
    void Paint();
    int RowHeight() const;
    int RowAt(int y) const;

    HWND hwnd_ = nullptr;
    const Theme* theme_ = nullptr;
    std::function<void(size_t)> onPick_;
    WordChoices c_;
    bool visible_ = false;
    int hot_ = -1;
};

}  // namespace gct
