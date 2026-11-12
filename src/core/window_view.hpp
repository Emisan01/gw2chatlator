// window_view.hpp — the window's views by one rule. Your parts (header, chat, preview, word bar, footer, minimise
// button) are yours and are never rewritten by a view; a view – compact, mini, subtitles – only says what is shown over
// them. One view at a time; switching from any to any other loses nothing, and off, your parts are simply back.
// (Before, compact and mini rewrote the parts and kept one "layout before": compact -> subtitles -> mini overwrote
// it with a view's parts, and your own layout was gone.) Portable, unit-tested.
#pragma once

namespace gct {

enum class WindowView { Parts = 0, Compact = 1, Mini = 2, Subtitles = 3 };

struct WindowParts {
    bool header = true, chat = true, preview = true, words = true, footer = true, minButton = true;
};

// What the window shows: your parts, or what the view shows over them.
inline WindowParts ShownParts(WindowParts own, WindowView view) {
    switch (view) {
        case WindowView::Compact:  // header, chat, input, footer – no word bar, no preview
            own.header = own.chat = own.footer = true;
            own.preview = own.words = false;
            break;
        case WindowView::Mini:  // nothing but the input
            own = {false, false, false, false, false, false};
            break;
        case WindowView::Subtitles:  // the chat's lines and their input (keeps user's word bar setting when writing)
            own.header = own.preview = own.footer = own.minButton = false;
            own.chat = true;
            break;
        case WindowView::Parts: break;
    }
    return own;
}

// A view's switch: on, it is the view; off, your parts – only when it was the one that was on.
inline WindowView SwitchView(WindowView now, WindowView view, bool on) {
    return on ? view : now == view ? WindowView::Parts : now;
}

// Settings from before the views stood over the parts: parts that spell a view become that view, and the parts it
// had switched off are yours again (as switching it off did then) – the window looks the same.
inline WindowView ViewOfOldParts(WindowParts& own) {
    if (own.header && own.chat && !own.preview && !own.words && own.footer) {
        own.preview = own.words = true;
        return WindowView::Compact;
    }
    if (!own.header && !own.chat && !own.preview && !own.words && !own.footer && !own.minButton) {
        own = WindowParts{};
        return WindowView::Mini;
    }
    return WindowView::Parts;
}

}  // namespace gct
