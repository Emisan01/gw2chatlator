// input_box.cpp
#include "input_box.hpp"

#include "app/modal_scope.hpp"

#include <windowsx.h>

#include <algorithm>

#include "core/i18n.hpp"

namespace gct {
namespace {

constexpr wchar_t kProp[] = L"gct.InputBox";

// Characters that take the dropdown's choice (not ':' and ')': names, smileys).
bool IsChoiceAccept(wchar_t c) {
    return c == L' ' || c == L'.' || c == L',' || c == L'!' || c == L'?' || c == L';' || c == 0x060C || c == 0x061B ||
           c == 0x061F || c == 0x06D4;
}

bool IsWordBoundary(wchar_t c) {
    return c == L' ' || c == L'.' || c == L',' || c == L'!' || c == L'?' || c == L';' || c == L':' || c == L')' ||
           c == 0x060C || c == 0x061B || c == 0x061F || c == 0x06D4;  // Arabic comma, semicolon, ?, full stop
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

// ---------------------------------------------------------------------------
// The word being typed: what Space would write is shown grey right after the
// caret ("Teq|uatl"); Tab shows the next choice, Space writes it.
// ---------------------------------------------------------------------------
void InputBox::UpdateChoices() {
    if (!suggestOn_ || !spell_ || GetFocus() != hwnd_) return;
    const std::wstring text = Text();
    const size_t caret = Caret();
    if (text == choicesText_ && caret == choicesCaret_) return;
    choices_ = spell_->Choices(text, caret, mode_);
    if (text == dismissedText_) choices_ = WordChoices{};  // Esc hid it for exactly this text
    choicesText_ = text;
    choicesCaret_ = caret;
    InvalidateRect(hwnd_, nullptr, TRUE);
}

// The choice for exactly the current text (typing can be faster than the
// dropdown's update: then it is worked out right now).
const WordChoices& InputBox::CurrentChoices() {
    const std::wstring text = Text();
    const size_t caret = Caret();
    if (!spell_ || text != choicesText_ || caret != choicesCaret_) {
        choices_ = spell_ ? spell_->Choices(text, caret, mode_) : WordChoices{};
        choicesText_ = text;
        choicesCaret_ = caret;
    }
    if (text == dismissedText_) choices_ = WordChoices{};
    return choices_;
}

// Puts the highlighted entry in place of the typed word, followed by
// `boundary` (the space or punctuation that finished it; 0 = nothing).
// Backspace right after it brings the typed word back.
bool InputBox::ApplyChoice(wchar_t boundary) {
    const WordChoices c = choices_;
    if (c.highlight < 0 || static_cast<size_t>(c.highlight) >= c.words.size()) return false;
    const std::wstring text = Text();
    if (c.replace.end() > text.size()) return false;
    const std::wstring typed = text.substr(c.replace.start, c.replace.length);
    const std::wstring& word = c.words[static_cast<size_t>(c.highlight)];
    const std::wstring tail = boundary ? std::wstring(1, boundary) : std::wstring();
    ReplaceRange(c.replace, word + tail);
    const size_t caret = c.replace.start + word.size() + tail.size();
    SendMessageW(hwnd_, EM_SETSEL, caret, caret);
    lastFix_.valid = false;
    // Learned at the moment of choosing: Tab to another suggestion says more than going on with the first.
    if (word != typed && spell_) spell_->Chose(Text(), c.replace.start, word, c.highlight > 0);
    if (word != typed) {
        if (boundary) lastFix_ = {true, c.replace.start, typed, word, boundary};
        if (cb_.onAutoCorrected) cb_.onAutoCorrected(typed, word);
    }
    UpdateSuggestions();
    return true;
}

void InputBox::CycleChoice(int step) {
    const int n = static_cast<int>(choices_.words.size());
    if (n == 0) return;
    choices_.highlight = choices_.highlight < 0 ? (step > 0 ? 0 : n - 1) : (choices_.highlight + step + n) % n;
    InvalidateRect(hwnd_, nullptr, TRUE);  // the grey word changes
}

std::wstring InputBox::Text() const {
    const int n = GetWindowTextLengthW(hwnd_);
    std::wstring s(static_cast<size_t>(n) + 1, L'\0');
    GetWindowTextW(hwnd_, s.data(), n + 1);
    s.resize(static_cast<size_t>(n));
    return s;
}

void InputBox::Clear() {
    keyPresses_ = 0;
    pasted_ = false;
    issues_.clear();
    grammar_.clear();
    lastFix_.valid = false;
    KillTimer(hwnd_, kSpellTimer);
    choices_ = WordChoices{};
    dismissedText_.clear();
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
                      s.replace.length == suggestions_.replace.length && s.phrase == suggestions_.phrase;
    suggestions_ = std::move(s);
    if (!same) {
        if (cb_.onSuggestions) cb_.onSuggestions(suggestions_);
        InvalidateRect(hwnd_, nullptr, TRUE);  // the grey completion after the caret
    }
    UpdateChoices();
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
    if (spell_) spell_->Chose(Text(), s.replace.start, word, true);  // picked from the word bar
    lastFix_.valid = false;
    UpdateSuggestions();
}

// The grey phrase at the end of the text, all words at once, followed by a space.
void InputBox::AcceptPhrase() {
    const WordSuggestions s = suggestions_;
    const std::wstring text = Text();
    if (s.phrase.empty() || s.replace.start != text.size()) return;
    ReplaceRange({text.size(), 0}, s.phrase + L" ");
    const size_t caret = text.size() + s.phrase.size() + 1;
    SendMessageW(hwnd_, EM_SETSEL, caret, caret);
    if (spell_ && !s.words.empty()) spell_->Chose(Text(), text.size(), s.words[0], true);
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

namespace {
// The grey suggestion is for Latin script only – the languages GW2 itself shows. Arabic, Cyrillic, Chinese … keep the
// word bar and the spell checker as before (no instant suggestion without a proven model for them).
bool LatinOnly(const std::wstring& s) {
    for (wchar_t c : s) {
        const uint32_t u = static_cast<uint32_t>(c);
        if (u >= 0x0250 && !(u >= 0x1E00 && u <= 0x1EFF) && IsWordChar(c)) return false;
    }
    return true;
}
}  // namespace

// The rest of the highlighted completion, greyed out right after the caret
// ("Teq|uatl"); Space writes it. Latin script only.
void InputBox::DrawGhost(HDC dc) const {
    const WordChoices& c = choices_;
    if (suggestOn_ && GetFocus() == hwnd_ && !c.Changes()) DrawNextGhost(dc);
    if (!suggestOn_ || !c.Changes() || static_cast<size_t>(c.highlight) >= c.words.size() ||
        GetFocus() != hwnd_)
        return;
    const std::wstring text = Text();
    const size_t caret = Caret();
    if (text != choicesText_ || caret != choicesCaret_) return;  // worked out for another text
    if (caret == 0 || caret > text.size() || caret != c.replace.end() || c.replace.start >= caret) return;
    if (caret < text.size() && text[caret] != L' ') return;
    const std::wstring typed = text.substr(c.replace.start, caret - c.replace.start);
    const std::wstring& word = c.words[static_cast<size_t>(c.highlight)];
    // A completion shows its rest ("Teq|uatl"); a correction shows the word Space would write ("komt| kommt").
    const bool extends = word.size() > typed.size() && CaseFold(word.substr(0, typed.size())) == CaseFold(typed);
    if (!LatinOnly(typed) || !LatinOnly(word)) return;
    const std::wstring rest = extends ? word.substr(typed.size()) : L"  \u2192 " + word;

    const int saved = SaveDC(dc);
    SelectObject(dc, theme_->fontText);
    const POINT last = PosFromChar(caret - 1);
    if (last.x == -32768) {
        RestoreDC(dc, saved);
        return;
    }
    SIZE sz{};
    GetTextExtentPoint32W(dc, &text[caret - 1], 1, &sz);
    RECT clip;
    SendMessageW(hwnd_, EM_GETRECT, 0, reinterpret_cast<LPARAM>(&clip));
    IntersectClipRect(dc, clip.left, clip.top, clip.right, clip.bottom);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, Theme::kMuted);
    TextOutW(dc, last.x + sz.cx, last.y, rest.c_str(), static_cast<int>(rest.size()));
    RestoreDC(dc, saved);
}

// The next word when it is (almost) always the same ("kommst du |mit") – or the whole rest of a phrase you write
// again and again ("gute nacht |bis morgen mit micro") – grey after the space at the end of the text. Tab takes all
// of it, → one word, typing goes on as usual. Latin script only.
void InputBox::DrawNextGhost(HDC dc) const {
    const WordSuggestions& s = suggestions_;
    if (s.kind != WordSuggestions::Kind::Next || s.autoIndex != 0 || s.words.empty()) return;
    const std::wstring text = Text();
    const size_t caret = Caret();
    if (caret < 2 || caret != text.size() || text[caret - 1] != L' ' || s.replace.start != caret) return;
    const std::wstring& shown = s.phrase.empty() ? s.words[0] : s.phrase;
    if (!LatinOnly(text) || !LatinOnly(shown)) return;
    const POINT last = PosFromChar(caret - 1);
    if (last.x == -32768) return;
    const int saved = SaveDC(dc);
    SelectObject(dc, theme_->fontText);
    SIZE sz{};
    GetTextExtentPoint32W(dc, L" ", 1, &sz);
    RECT clip;
    SendMessageW(hwnd_, EM_GETRECT, 0, reinterpret_cast<LPARAM>(&clip));
    IntersectClipRect(dc, clip.left, clip.top, clip.right, clip.bottom);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, Theme::kMuted);
    TextOutW(dc, last.x + sz.cx, last.y, shown.c_str(), static_cast<int>(shown.size()));
    RestoreDC(dc, saved);
}

void InputBox::ReplaceRange(Span span, const std::wstring& text) {
    SendMessageW(hwnd_, EM_SETSEL, span.start, span.end());
    SendMessageW(hwnd_, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(text.c_str()));
}

bool InputBox::ShowSpellMenu(LPARAM lp) {
    const ModalScope modal;
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

    enum : UINT { kDelete = 1, kAdd = 2, kIgnore = 3, kExplain = 4, kSuggestBase = 10 };
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
        if (cb_.onExplain) AppendMenuW(menu, MF_STRING, kExplain, TrF(L"Explain “{1}” (slang, abbreviation) …", {word}).c_str());
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
    } else if (cmd == kExplain) {
        cb_.onExplain(word);  // stores the word (and its meaning) and marks it correct
        RunSpellCheck(std::wstring::npos);
    } else if (cmd == kIgnore) {
        spell_->IgnoreForSession(word);
        RunSpellCheck(std::wstring::npos);
    } else if (cmd >= kSuggestBase && cmd - kSuggestBase < suggestions.size()) {
        ReplaceRange(issue.span, suggestions[cmd - kSuggestBase]);
    }
    return true;
}

// Right-click on a word you taught the tool (no spell mark there): forget it,
// plus the usual clipboard commands the EDIT's own menu would have offered.
bool InputBox::ShowWordMenu(LPARAM lp) {
    const ModalScope modal;
    if (!spell_) return false;
    const std::wstring text = Text();
    POINT screen{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    size_t idx;
    if (screen.x == -1 && screen.y == -1) {
        idx = Caret();
        POINT p = PosFromChar(idx > 0 ? idx - 1 : 0);
        if (p.x == -32768) p = {0, 0};
        p.y += theme_->textLineHeight;
        screen = p;
        ClientToScreen(hwnd_, &screen);
    } else {
        POINT client = screen;
        ScreenToClient(hwnd_, &client);
        idx = LOWORD(SendMessageW(hwnd_, EM_CHARFROMPOS, 0, MAKELPARAM(client.x, client.y)));
    }
    size_t start = std::min(idx, text.size()), end = start;
    while (start > 0 && IsWordChar(text[start - 1])) --start;
    while (end < text.size() && IsWordChar(text[end])) ++end;
    const std::wstring word = text.substr(start, end - start);
    if (word.empty() || (!spell_->IsLearned(word) && !cb_.onExplain)) return false;
    const bool learned = spell_->IsLearned(word);

    enum : UINT { kForget = 1, kCut, kCopy, kPaste, kAll, kExplain };
    DWORD a = 0, b = 0;
    SendMessageW(hwnd_, EM_GETSEL, reinterpret_cast<WPARAM>(&a), reinterpret_cast<LPARAM>(&b));
    const UINT sel = a != b ? MF_STRING : MF_STRING | MF_GRAYED;
    HMENU menu = CreatePopupMenu();
    if (learned) AppendMenuW(menu, MF_STRING, kForget, TrF(L"Forget “{1}”", {word}).c_str());
    if (cb_.onExplain)
        AppendMenuW(menu, MF_STRING, kExplain, TrF(L"Explain “{1}” (slang, abbreviation) …", {word}).c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, sel, kCut, Tr(L"Cut").c_str());
    AppendMenuW(menu, sel, kCopy, Tr(L"Copy").c_str());
    AppendMenuW(menu, IsClipboardFormatAvailable(CF_UNICODETEXT) ? MF_STRING : MF_STRING | MF_GRAYED, kPaste,
                Tr(L"Paste").c_str());
    AppendMenuW(menu, MF_STRING, kAll, Tr(L"Select all").c_str());
    const UINT cmd = static_cast<UINT>(TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY | (UiRtl() ? TPM_LAYOUTRTL : 0), screen.x, screen.y, 0,
        hwnd_, nullptr));
    DestroyMenu(menu);
    switch (cmd) {
        case kExplain:
            cb_.onExplain(word);
            RunSpellCheck(std::wstring::npos);
            break;
        case kForget:
            spell_->Forget(word);
            UpdateSuggestions();
            if (cb_.onForgotten) cb_.onForgotten(word);
            break;
        case kCut: SendMessageW(hwnd_, WM_CUT, 0, 0); break;
        case kCopy: SendMessageW(hwnd_, WM_COPY, 0, 0); break;
        case kPaste: SendMessageW(hwnd_, WM_PASTE, 0, 0); break;
        case kAll: SendMessageW(hwnd_, EM_SETSEL, 0, -1); break;
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
    const size_t caret = Caret();
    if (caret < 3) return;
    CorrectWordEndingAt(caret - 1, true);  // the boundary character just typed sits at caret - 1
}

// Enter finishes the last word like a space would (no undo: it is sent).
void InputBox::FinishWordAtCaret() {
    lastFix_.valid = false;
    const size_t caret = Caret();
    const std::wstring text = Text();
    if (caret < 2 || caret > text.size() || (caret < text.size() && IsWordChar(text[caret]))) return;
    CorrectWordEndingAt(caret, false);
}

void InputBox::CorrectWordEndingAt(size_t end, bool boundaryTyped) {
    if (!spell_ || mode_ == AutoCorrectMode::Off) return;
    const size_t caret = Caret();
    const std::wstring text = Text();
    if (end > text.size() || caret > text.size()) return;
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
    if (boundaryTyped) lastFix_ = {true, start, word, *replacement, text[end]};
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
            if (wp != VK_RETURN && wp != VK_SHIFT && wp != VK_CONTROL && wp != VK_MENU && wp != VK_LWIN && wp != VK_RWIN &&
                wp != VK_CAPITAL)
                ++keyPresses_;
            if (ctrl && (wp == 'V' || wp == VK_INSERT)) pasted_ = true;
            if (wp == VK_RETURN) {
                if (!shift) {
                    // Enter finishes the word too: the grey word, else autocorrection.
                    if (CurrentChoices().Changes()) ApplyChoice(0);
                    else FinishWordAtCaret();
                    if (cb_.onEnter) cb_.onEnter(ctrl);
                }
                return 0;
            }
            if (wp == VK_ESCAPE) {
                if (CurrentChoices().Changes()) {  // first Esc only hides the grey word
                    dismissedText_ = Text();
                    choices_ = WordChoices{};
                    InvalidateRect(hwnd_, nullptr, TRUE);
                    return 0;
                }
                if (cb_.onEscape) cb_.onEscape();
                return 0;
            }
            // Tab: the next suggestion as the grey word (Shift+Tab back). Space then writes it.
            if (!ctrl && wp == VK_TAB && !CurrentChoices().Empty()) {
                if (choices_.highlight < 0 && !shift) choices_.highlight = choices_.firstIsTyped ? 0 : -1;
                CycleChoice(shift ? -1 : 1);
                return 0;
            }
            // Right arrow at the end of the word: take the grey word without a space.
            if (!ctrl && !shift && wp == VK_RIGHT && Caret() == Text().size() && CurrentChoices().Changes()) {
                ApplyChoice(0);
                return 0;
            }
            // Right arrow at the end after a space: take the grey next word.
            if (!ctrl && !shift && wp == VK_RIGHT && Caret() == Text().size() &&
                suggestions_.kind == WordSuggestions::Kind::Next && suggestions_.autoIndex == 0 &&
                !suggestions_.words.empty()) {
                AcceptSuggestion(0);
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
            if (!ctrl && wp == VK_TAB && !suggestions_.phrase.empty() && suggestions_.autoIndex == 0 &&
                Caret() == Text().size()) {  // the grey phrase: all of it
                AcceptPhrase();
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
        case WM_PASTE:
            pasted_ = true;
            break;
        case WM_CHAR: {
            if (wp == 0x08 && swallowBackspaceChar_) {
                swallowBackspaceChar_ = false;
                return 0;
            }
            // Characters produced by the shortcuts above: no beeps, no newlines.
            if (wp == L'\r' || wp == L'\n' || wp == 0x1B || wp == 0x01 || wp == 0x0C || wp == 0x15 || wp == 0x7F)
                return 0;
            if (wp == L'\t') return 0;  // Ctrl+Tab / Tab: no tab characters in a chat line
            // Space or punctuation finishes the word: the dropdown's highlighted
            // entry goes in (a valid word as typed stays as it is).
            if (IsChoiceAccept(static_cast<wchar_t>(wp)) && mode_ != AutoCorrectMode::Off && CurrentChoices().Changes()) {
                ApplyChoice(static_cast<wchar_t>(wp));
                return 0;
            }
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
            HDC dc = GetDC(hwnd_);
            if (!issues_.empty() || !grammar_.empty()) DrawSquiggles(dc);
            DrawGhost(dc);
            ReleaseDC(hwnd_, dc);
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
            InvalidateRect(hwnd_, nullptr, TRUE);  // no grey completion without focus
            return r;
        }
        case WM_CONTEXTMENU:
            if (ShowSpellMenu(lp) || ShowWordMenu(lp)) return 0;
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
