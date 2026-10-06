// input_box.cpp
#include "input_box.hpp"

#include <windowsx.h>

#include <algorithm>

#include "core/i18n.hpp"

namespace gct {
namespace {

constexpr wchar_t kProp[] = L"gct.InputBox";

bool IsWordBoundary(wchar_t c) {
    return c == L' ' || c == L'.' || c == L',' || c == L'!' || c == L'?' || c == L';' || c == L':' || c == L')';
}

}  // namespace

bool InputBox::Create(HWND parent, HINSTANCE inst, const Theme* theme, SpellService* spell, AutoCorrectMode mode,
                      bool suggestions, Callbacks cb) {
    theme_ = theme;
    spell_ = spell;
    mode_ = mode;
    suggestOn_ = suggestions;
    cb_ = std::move(cb);
    hwnd_ = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL, 0, 0,
                            10, 10, parent, nullptr, inst, nullptr);
    if (!hwnd_) return false;
    SendMessageW(hwnd_, WM_SETFONT, reinterpret_cast<WPARAM>(theme_->fontText), FALSE);
    SendMessageW(hwnd_, EM_SETLIMITTEXT, 2000, 0);
    SetPropW(hwnd_, kProp, this);
    orig_ = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(hwnd_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(Proc)));
    return true;
}

std::wstring InputBox::Text() const {
    const int n = GetWindowTextLengthW(hwnd_);
    std::wstring s(static_cast<size_t>(n) + 1, L'\0');
    GetWindowTextW(hwnd_, s.data(), n + 1);
    s.resize(static_cast<size_t>(n));
    return s;
}

void InputBox::Clear() {
    issues_.clear();
    grammar_.clear();
    lastFix_.valid = false;
    KillTimer(hwnd_, kSpellTimer);
    SetWindowTextW(hwnd_, L"");
    InvalidateRect(hwnd_, nullptr, TRUE);  // squiggles live outside the text the EDIT repaints
}

void InputBox::OnTextChanged() {
    if (!issues_.empty() || !grammar_.empty()) {
        issues_.clear();  // positions are stale now
        grammar_.clear();
        InvalidateRect(hwnd_, nullptr, TRUE);
    }
    if (spell_ && spell_->Ready()) SetTimer(hwnd_, kSpellTimer, kSpellDelayMs, nullptr);
    if (suggestOn_) SetTimer(hwnd_, kSuggestTimer, kSuggestDelayMs, nullptr);
}

void InputBox::SetSuggestions(bool on) {
    suggestOn_ = on;
    suggestions_ = WordSuggestions();
    if (cb_.onSuggestions) cb_.onSuggestions(suggestions_);
    if (on) UpdateSuggestions();
}

void InputBox::UpdateSuggestions() {
    KillTimer(hwnd_, kSuggestTimer);
    WordSuggestions s;
    if (suggestOn_ && spell_) s = spell_->Suggestions(Text(), Caret(), mode_);
    const bool same = s.kind == suggestions_.kind && s.words == suggestions_.words &&
                      s.autoIndex == suggestions_.autoIndex && s.replace.start == suggestions_.replace.start &&
                      s.replace.length == suggestions_.replace.length;
    suggestions_ = std::move(s);
    if (!same && cb_.onSuggestions) cb_.onSuggestions(suggestions_);
}

void InputBox::AcceptSuggestion(size_t index) {
    if (index >= suggestions_.words.size()) return;
    const WordSuggestions s = suggestions_;
    const std::wstring text = Text();
    if (s.replace.end() > text.size()) return;
    std::wstring word = s.words[index];
    // Completions and next words are followed by a space, like on a phone.
    const bool spaceAfter = s.replace.end() >= text.size() || text[s.replace.end()] != L' ';
    ReplaceRange(s.replace, word + (spaceAfter ? L" " : L""));
    const size_t caret = s.replace.start + word.size() + 1;
    SendMessageW(hwnd_, EM_SETSEL, caret, caret);
    lastFix_.valid = false;
    UpdateSuggestions();
}

void InputBox::SetGrammarIssues(const std::wstring& forText, std::vector<SpellIssue> issues) {
    if (forText != Text()) return;  // typed on meanwhile
    grammar_ = std::move(issues);
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void InputBox::ApplyPadding() {
    RECT r;
    GetClientRect(hwnd_, &r);
    InflateRect(&r, -theme_->S(7), -theme_->S(5));
    SendMessageW(hwnd_, EM_SETRECT, 0, reinterpret_cast<LPARAM>(&r));
}

void InputBox::ApplyTheme() {
    SendMessageW(hwnd_, WM_SETFONT, reinterpret_cast<WPARAM>(theme_->fontText), TRUE);
    ApplyPadding();
    issues_.clear();  // squiggle positions moved with the font
    if (spell_ && spell_->Ready()) SetTimer(hwnd_, kSpellTimer, kSpellDelayMs, nullptr);
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void InputBox::SetRtl(bool rtl) {
    if (rtl == rtl_ || !hwnd_) return;
    rtl_ = rtl;
    LONG_PTR ex = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
    ex = rtl ? (ex | WS_EX_RTLREADING | WS_EX_RIGHT) : (ex & ~static_cast<LONG_PTR>(WS_EX_RTLREADING | WS_EX_RIGHT));
    SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, ex);
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    ApplyPadding();
    InvalidateRect(hwnd_, nullptr, TRUE);
}

std::wstring InputBox::KeyboardLocale() {
    const HKL hkl = GetKeyboardLayout(0);
    const LANGID lang = LOWORD(reinterpret_cast<UINT_PTR>(hkl));
    wchar_t name[LOCALE_NAME_MAX_LENGTH] = {};
    if (LCIDToLocaleName(MAKELCID(lang, SORT_DEFAULT), name, LOCALE_NAME_MAX_LENGTH, 0) <= 0) return {};
    return name;
}

size_t InputBox::Caret() const {
    DWORD a = 0, b = 0;
    SendMessageW(hwnd_, EM_GETSEL, reinterpret_cast<WPARAM>(&a), reinterpret_cast<LPARAM>(&b));
    return b;
}

POINT InputBox::PosFromChar(size_t i) const {
    const LRESULT r = SendMessageW(hwnd_, EM_POSFROMCHAR, i, 0);
    if (r == -1) return {-32768, -32768};
    return {static_cast<short>(LOWORD(r)), static_cast<short>(HIWORD(r))};
}

// ---------------------------------------------------------------------------
// Spell marks
// ---------------------------------------------------------------------------
void InputBox::RunSpellCheck(size_t caret) {
    KillTimer(hwnd_, kSpellTimer);
    if (!spell_ || !spell_->Ready()) return;
    std::vector<SpellIssue> fresh = spell_->Check(Text(), caret);
    const bool same = fresh.size() == issues_.size() &&
                      std::equal(fresh.begin(), fresh.end(), issues_.begin(), [](const SpellIssue& a, const SpellIssue& b) {
                          return a.span.start == b.span.start && a.span.length == b.span.length;
                      });
    issues_ = std::move(fresh);
    if (!same) InvalidateRect(hwnd_, nullptr, TRUE);
}

void InputBox::DrawSquiggles(HDC dc) const {
    if (issues_.empty() && grammar_.empty()) return;
    std::vector<SpellIssue> all = issues_;
    all.insert(all.end(), grammar_.begin(), grammar_.end());
    const std::wstring text = Text();
    RECT clip;
    GetClientRect(hwnd_, &clip);
    const int saved = SaveDC(dc);
    IntersectClipRect(dc, clip.left, clip.top, clip.right, clip.bottom);
    SelectObject(dc, theme_->fontText);
    TEXTMETRICW tm{};
    GetTextMetricsW(dc, &tm);
    const int amp = std::max(1, theme_->S(2));
    const int step = std::max(2, theme_->S(2));

    auto charRight = [&](size_t j, POINT pj) {
        if (j + 1 < text.size()) {
            const POINT pn = PosFromChar(j + 1);
            if (pn.y == pj.y && pn.x > pj.x) return static_cast<int>(pn.x);
        }
        SIZE sz{};
        GetTextExtentPoint32W(dc, &text[j], 1, &sz);
        return static_cast<int>(pj.x + sz.cx);
    };

    for (const SpellIssue& issue : all) {
        if (issue.span.end() > text.size()) continue;
        const COLORREF color =
            (issue.kind == SpellIssue::Kind::Delete || issue.grammar) ? Theme::kSquiggleSoft : Theme::kSquiggle;
        HPEN pen = CreatePen(PS_SOLID, std::max(1, theme_->S(1)), color);
        HGDIOBJ oldPen = SelectObject(dc, pen);

        // One zigzag per visual line the word covers (words can wrap).
        size_t i = issue.span.start;
        while (i < issue.span.end()) {
            const POINT p0 = PosFromChar(i);
            if (p0.x == -32768) break;
            int x2 = p0.x;
            size_t j = i;
            while (j < issue.span.end()) {
                const POINT pj = PosFromChar(j);
                if (pj.y != p0.y) break;
                x2 = charRight(j, pj);
                ++j;
            }
            if (j == i) ++j;
            const int y = p0.y + tm.tmAscent + theme_->S(2);
            std::vector<POINT> pts;
            bool up = false;
            for (int x = p0.x; x <= x2; x += step, up = !up) pts.push_back({x, up ? y : y + amp});
            if (pts.size() >= 2) Polyline(dc, pts.data(), static_cast<int>(pts.size()));
            i = j;
        }
        SelectObject(dc, oldPen);
        DeleteObject(pen);
    }
    RestoreDC(dc, saved);
}

void InputBox::ReplaceRange(Span span, const std::wstring& text) {
    SendMessageW(hwnd_, EM_SETSEL, span.start, span.end());
    SendMessageW(hwnd_, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(text.c_str()));
}

bool InputBox::ShowSpellMenu(LPARAM lp) {
    if ((issues_.empty() && grammar_.empty()) || !spell_) return false;
    const std::wstring text = Text();
    POINT screen{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    size_t idx;
    if (screen.x == -1 && screen.y == -1) {  // keyboard (Shift+F10 / menu key)
        idx = Caret();
        POINT p = PosFromChar(idx > 0 ? idx - 1 : 0);
        if (p.x == -32768) p = {0, 0};
        p.y += theme_->textLineHeight;
        screen = p;
        ClientToScreen(hwnd_, &screen);
    } else {
        POINT client = screen;
        ScreenToClient(hwnd_, &client);
        const LRESULT r = SendMessageW(hwnd_, EM_CHARFROMPOS, 0, MAKELPARAM(client.x, client.y));
        idx = LOWORD(r);
    }

    const SpellIssue* hit = nullptr;
    for (const SpellIssue& is : issues_)
        if (idx >= is.span.start && idx <= is.span.end()) { hit = &is; break; }
    if (!hit)
        for (const SpellIssue& is : grammar_)
            if (idx >= is.span.start && idx <= is.span.end()) { hit = &is; break; }
    if (!hit || hit->span.end() > text.size()) return false;
    const SpellIssue issue = *hit;  // issues_ may change while the menu is open
    const std::wstring word = text.substr(issue.span.start, issue.span.length);

    enum : UINT { kDelete = 1, kAdd = 2, kIgnore = 3, kSuggestBase = 10 };
    HMENU menu = CreatePopupMenu();
    std::vector<std::wstring> suggestions;
    if (issue.grammar) {
        std::wstring msg = issue.message.size() > 90 ? issue.message.substr(0, 88) + L"\u2026" : issue.message;
        if (!msg.empty()) {
            AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, msg.c_str());
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        }
        suggestions = issue.suggestions;
        for (size_t k = 0; k < suggestions.size(); ++k)
            AppendMenuW(menu, MF_STRING, kSuggestBase + static_cast<UINT>(k), suggestions[k].c_str());
        if (suggestions.empty()) AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, Tr(L"(no suggestions)").c_str());
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kIgnore, Tr(L"Ignore here").c_str());
    } else if (issue.kind == SpellIssue::Kind::Delete) {
        AppendMenuW(menu, MF_STRING, kDelete, Tr(L"Remove the doubled word").c_str());
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kIgnore, Tr(L"Ignore here").c_str());
    } else {
        suggestions = spell_->Suggest(word);
        if (issue.kind == SpellIssue::Kind::Replace &&
            std::find(suggestions.begin(), suggestions.end(), issue.replacement) == suggestions.end())
            suggestions.insert(suggestions.begin(), issue.replacement);
        for (size_t k = 0; k < suggestions.size(); ++k)
            AppendMenuW(menu, MF_STRING, kSuggestBase + static_cast<UINT>(k), suggestions[k].c_str());
        if (suggestions.empty()) AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, Tr(L"(no suggestions)").c_str());
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kAdd, Tr(L"Add to my GW2 words").c_str());
        AppendMenuW(menu, MF_STRING, kIgnore, Tr(L"Ignore here").c_str());
    }
    const UINT cmd = static_cast<UINT>(TrackPopupMenu(menu,
                                                      TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY |
                                                          (UiRtl() ? TPM_LAYOUTRTL : 0),
                                                      screen.x, screen.y, 0, hwnd_, nullptr));
    DestroyMenu(menu);

    if (cmd == kIgnore && issue.grammar) {
        grammar_.erase(std::remove_if(grammar_.begin(), grammar_.end(),
                                      [&](const SpellIssue& g) { return g.span.start == issue.span.start; }),
                       grammar_.end());
        InvalidateRect(hwnd_, nullptr, TRUE);
        return true;
    }
    if (cmd == kDelete) {
        Span s = issue.span;
        while (s.start > 0 && text[s.start - 1] == L' ') { --s.start; ++s.length; }
        ReplaceRange(s, L"");
    } else if (cmd == kAdd) {
        spell_->AddUserWord(word);
        RunSpellCheck(std::wstring::npos);
    } else if (cmd == kIgnore) {
        spell_->IgnoreForSession(word);
        RunSpellCheck(std::wstring::npos);
    } else if (cmd >= kSuggestBase && cmd - kSuggestBase < suggestions.size()) {
        ReplaceRange(issue.span, suggestions[cmd - kSuggestBase]);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Autocorrection when a word is finished. Safe: only what Windows itself
// marks as a sure replacement. Phone: also the most likely meant word for a
// typo (see ChooseCorrection). Backspace right after it undoes it.
// ---------------------------------------------------------------------------
void InputBox::TryAutoCorrect() {
    lastFix_.valid = false;
    if (!spell_ || mode_ == AutoCorrectMode::Off) return;
    const size_t caret = Caret();
    const std::wstring text = Text();
    if (caret < 3 || caret > text.size()) return;

    const size_t end = caret - 1;  // the boundary character just typed
    size_t start = end;
    while (start > 0 && IsWordChar(text[start - 1])) --start;
    if (end - start < 2) return;
    const Span span{start, end - start};
    for (const Span& b : SpellService::ProtectedSpans(text))
        if (b.Overlaps(span)) return;

    const std::wstring word = text.substr(start, end - start);
    if (std::any_of(word.begin(), word.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; })) return;
    const auto replacement = spell_->AutoCorrection(word, mode_);
    if (!replacement) return;

    ReplaceRange(span, *replacement);
    const size_t newCaret = caret + replacement->size() - word.size();
    SendMessageW(hwnd_, EM_SETSEL, newCaret, newCaret);
    lastFix_ = {true, start, word, *replacement, text[end]};
    if (cb_.onAutoCorrected) cb_.onAutoCorrected(word, *replacement);
}

bool InputBox::UndoAutoCorrect() {
    if (!lastFix_.valid) return false;
    lastFix_.valid = false;
    const std::wstring text = Text();
    const size_t fixEnd = lastFix_.start + lastFix_.corrected.size();
    DWORD a = 0, b = 0;
    SendMessageW(hwnd_, EM_GETSEL, reinterpret_cast<WPARAM>(&a), reinterpret_cast<LPARAM>(&b));
    // Only directly after the correction and its boundary character.
    if (a != b || b != fixEnd + 1 || fixEnd >= text.size() || text[fixEnd] != lastFix_.boundary ||
        text.compare(lastFix_.start, lastFix_.corrected.size(), lastFix_.corrected) != 0)
        return false;
    ReplaceRange({lastFix_.start, lastFix_.corrected.size() + 1}, lastFix_.original + lastFix_.boundary);
    const size_t caret = lastFix_.start + lastFix_.original.size() + 1;
    SendMessageW(hwnd_, EM_SETSEL, caret, caret);
    if (spell_) spell_->RejectCorrection(lastFix_.original);
    if (cb_.onCorrectionUndone) cb_.onCorrectionUndone(lastFix_.original);
    return true;
}

void InputBox::DeletePreviousWord() {
    DWORD a = 0, b = 0;
    SendMessageW(hwnd_, EM_GETSEL, reinterpret_cast<WPARAM>(&a), reinterpret_cast<LPARAM>(&b));
    if (a != b) {
        SendMessageW(hwnd_, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L""));
        return;
    }
    const std::wstring t = Text();
    size_t i = std::min<size_t>(a, t.size());
    while (i > 0 && t[i - 1] == L' ') --i;
    while (i > 0 && t[i - 1] != L' ') --i;
    ReplaceRange({i, a - i}, L"");
}

// ---------------------------------------------------------------------------
LRESULT CALLBACK InputBox::Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = static_cast<InputBox*>(GetPropW(h, kProp));
    if (!self) return DefWindowProcW(h, msg, wp, lp);
    if (msg == WM_NCDESTROY) {
        RemovePropW(h, kProp);
        SetWindowLongPtrW(h, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(self->orig_));
        return CallWindowProcW(self->orig_, h, msg, wp, lp);
    }
    return self->Handle(msg, wp, lp);
}

LRESULT InputBox::Handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_KEYDOWN: {
            const bool ctrl = GetKeyState(VK_CONTROL) < 0;
            const bool shift = GetKeyState(VK_SHIFT) < 0;
            if (wp == VK_RETURN) {
                if (!shift && cb_.onEnter) cb_.onEnter(ctrl);
                return 0;
            }
            if (wp == VK_ESCAPE) {
                if (cb_.onEscape) cb_.onEscape();
                return 0;
            }
            if (ctrl && wp == 'A') {
                SendMessageW(hwnd_, EM_SETSEL, 0, -1);
                return 0;
            }
            if (ctrl && wp == 'L') {
                if (cb_.onCycleLang) cb_.onCycleLang();
                return 0;
            }
            if (ctrl && wp == 'U') {
                if (cb_.onRomanize) cb_.onRomanize();
                return 0;
            }
            if (ctrl && wp == VK_TAB) {
                if (cb_.onSwitchTab) cb_.onSwitchTab();
                return 0;
            }
            if (!ctrl && wp == VK_TAB) {  // take the highlighted (else the first) word of the bar
                if (!suggestions_.words.empty())
                    AcceptSuggestion(suggestions_.autoIndex >= 0 ? static_cast<size_t>(suggestions_.autoIndex) : 0);
                return 0;
            }
            if (ctrl && wp == VK_BACK) {
                DeletePreviousWord();
                return 0;
            }
            if (wp == VK_BACK && !shift && UndoAutoCorrect()) {
                swallowBackspaceChar_ = true;  // the WM_CHAR of this Backspace must not delete anything
                return 0;
            }
            if (wp != VK_SHIFT && wp != VK_CONTROL && wp != VK_MENU) lastFix_.valid = lastFix_.valid && wp == VK_BACK;
            break;
        }
        case WM_CHAR: {
            if (wp == 0x08 && swallowBackspaceChar_) {
                swallowBackspaceChar_ = false;
                return 0;
            }
            // Characters produced by the shortcuts above: no beeps, no newlines.
            if (wp == L'\r' || wp == L'\n' || wp == 0x1B || wp == 0x01 || wp == 0x0C || wp == 0x15 || wp == 0x7F)
                return 0;
            if (wp == L'\t') return 0;  // Ctrl+Tab / Tab: no tab characters in a chat line
            const LRESULT r = CallWindowProcW(orig_, hwnd_, msg, wp, lp);
            if (IsWordBoundary(static_cast<wchar_t>(wp))) TryAutoCorrect();
            else if (wp != 0x08) lastFix_.valid = false;
            return r;
        }
        case WM_KEYUP:
        case WM_LBUTTONUP: {  // the caret moved: the word bar follows it
            const LRESULT r = CallWindowProcW(orig_, hwnd_, msg, wp, lp);
            if (suggestOn_ && (msg == WM_LBUTTONUP || wp == VK_LEFT || wp == VK_RIGHT || wp == VK_HOME || wp == VK_END))
                SetTimer(hwnd_, kSuggestTimer, kSuggestDelayMs, nullptr);
            return r;
        }
        case WM_PAINT: {
            const LRESULT r = CallWindowProcW(orig_, hwnd_, msg, wp, lp);
            if (!issues_.empty() || !grammar_.empty()) {
                HDC dc = GetDC(hwnd_);
                DrawSquiggles(dc);
                ReleaseDC(hwnd_, dc);
            }
            return r;
        }
        case WM_TIMER:
            if (wp == kSpellTimer) {
                RunSpellCheck(Caret());
                return 0;
            }
            if (wp == kSuggestTimer) {
                UpdateSuggestions();
                return 0;
            }
            break;
        case WM_KILLFOCUS: {
            const LRESULT r = CallWindowProcW(orig_, hwnd_, msg, wp, lp);
            RunSpellCheck(std::wstring::npos);  // the last word counts as finished
            return r;
        }
        case WM_CONTEXTMENU:
            if (ShowSpellMenu(lp)) return 0;
            break;
        case WM_INPUTLANGCHANGE: {
            const LRESULT r = CallWindowProcW(orig_, hwnd_, msg, wp, lp);
            if (cb_.onKeyboardLanguage) cb_.onKeyboardLanguage(KeyboardLocale());
            return r;
        }
    }
    return CallWindowProcW(orig_, hwnd_, msg, wp, lp);
}

}  // namespace gct
