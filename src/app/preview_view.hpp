// preview_view.hpp — shows what will land in the GW2 chat before you press
// Enter: the translated line, the back-translation into your language (so you
// can check a message in a language you cannot read) and notes such as
// "Teil 1/2" or a script warning.
#pragma once

#include <windows.h>

#include <functional>
#include <string>

#include "app/theme.hpp"

namespace gct {

class PreviewView {
public:
    struct Content {
        std::wstring text;      // the chat line (or the next part of it)
        std::wstring back;      // back-translation, shown as "≈ ..." (≈ is in every Windows UI font)
        std::wstring note;      // "Teil 1/2", warnings ...
        bool current = false;   // false: greyed out (text changed, translation pending)
        bool warn = false;      // note in warning colour
        std::wstring placeholder;  // shown when `text` is empty
    };

    static bool Register(HINSTANCE inst);
    bool Create(HWND parent, HINSTANCE inst, const Theme* theme, std::function<void()> onClick);
    HWND Hwnd() const { return hwnd_; }

    void Set(Content c);
    const Content& Get() const { return c_; }
    int PreferredHeight() const;

private:
    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);
    void Paint();
    void CopyText(const std::wstring& s);

    HWND hwnd_ = nullptr;
    const Theme* theme_ = nullptr;
    std::function<void()> onClick_;
    Content c_;
};

}  // namespace gct
