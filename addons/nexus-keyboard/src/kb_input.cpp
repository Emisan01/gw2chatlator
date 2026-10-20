// kb_input.cpp — the input: soft breaks, the grey word, Tab / Space, words under the mouse, your corrections.

#include "kb_shared.hpp"

namespace kb {

// Flat / RawOffset (the soft breaks): core/soft_wrap.
std::wstring g_teachWord;  // the word a right-click hit (the learning window starts with it)

// The grey text after the caret: what Space writes (a completion shows its rest, a
// correction "> word"); after a space the sure next word or phrase. `replace` gets what Tab takes.
std::wstring GhostRest(const std::wstring& text, bool* correction) {
    if (correction) *correction = false;
    if (g_help.text != text || text.empty()) return {};
    const WordSuggestions& bar = g_help.bar;
    if (g_pick >= 0 && g_pick < static_cast<int>(bar.words.size())) {  // picked with Tab
        const std::wstring& word = bar.words[g_pick];
        const Span r = bar.replace.end() == text.size() ? bar.replace : Span{text.size(), 0};
        const std::wstring typed = text.substr(r.start, r.length);
        if (typed.size() < word.size() && CaseFold(word.substr(0, typed.size())) == CaseFold(typed))
            return word.substr(typed.size());
        if (correction) *correction = true;
        return typed.empty() ? word : L"  > " + word;
    }
    const WordChoices& c = g_help.choices;
    if (text.back() != L' ' && c.highlight >= 0 && c.highlight < static_cast<int>(c.words.size()) &&
        c.replace.end() == text.size() && !(c.firstIsTyped && c.highlight == 0)) {
        const std::wstring& word = c.words[c.highlight];
        const std::wstring typed = text.substr(c.replace.start, c.replace.length);
        if (typed.size() < word.size() && CaseFold(word.substr(0, typed.size())) == CaseFold(typed))
            return word.substr(typed.size());
        if (correction) *correction = true;
        return L"  > " + word;
    }
    const WordSuggestions& b = g_help.bar;
    if (b.kind == WordSuggestions::Kind::Next && !b.words.empty() && text.back() == L' ')
        return b.phrase.empty() ? b.words[0] : b.phrase;
    // Without the word bar nothing shows what Tab would take: its highlighted completion becomes the grey word (Tab takes
    // it, Space writes it then). Space alone still keeps a valid word as typed – no surprises.
    if (!Lay().words && b.kind != WordSuggestions::Kind::Next && b.autoIndex >= 0 &&
        b.autoIndex < static_cast<int>(b.words.size()) && b.replace.end() == text.size() && text.back() != L' ') {
        const std::wstring& word = b.words[b.autoIndex];
        const std::wstring typed = text.substr(b.replace.start, b.replace.length);
        if (!typed.empty() && typed.size() < word.size() && CaseFold(word.substr(0, typed.size())) == CaseFold(typed))
            return word.substr(typed.size());
    }
    return {};
}

// A word of the bar replaces the word at the end (or goes after it), followed by a space.
void TakeWord(const std::wstring& word) {
    const std::wstring& text = g_wFlatInput;
    const WordSuggestions& b = g_help.bar;
    Span r = b.replace;
    if (g_help.text != text || r.end() > text.size()) r = Span{text.size(), 0};
    g_edit.active = true;
    g_edit.startByte = U8Offset(text, r.start);
    g_edit.lenBytes = U8Offset(text, r.end()) - g_edit.startByte;
    g_edit.insert = U8(word + L" ");
    g_focusInput = true;
    if (g_spell) g_worker.Post([text, start = r.start, word] { if (g_spell) g_spell->Chose(text, start, word, true); });
}

// Space at the end of a word: the word picked with Tab, else the grey one (what Space writes – only
// strong choices); the change comes in through g_edit, the space with it. False: just a space.
bool SpaceWrites() {
    const std::wstring& text = g_wFlatInput;
    if (g_help.text != text || text.empty() || text.back() == L' ' || g_cfg.autoCorrect == AutoCorrectMode::Off && g_pick < 0)
        return false;
    std::wstring word;
    Span r;
    bool deliberate = false;
    const WordSuggestions& bar = g_help.bar;
    const WordChoices& c = g_help.choices;
    if (g_pick >= 0 && g_pick < static_cast<int>(bar.words.size())) {
        word = bar.words[g_pick];
        r = bar.replace.end() == text.size() ? bar.replace : Span{text.size(), 0};
        deliberate = true;
    } else if (c.Changes() && c.highlight >= 0 && c.highlight < static_cast<int>(c.words.size()) &&
               c.replace.end() == text.size()) {
        word = c.words[c.highlight];
        r = c.replace;
    } else {
        return false;
    }
    g_pick = -1;
    const std::wstring fixed = text.substr(0, r.start) + word + L" ";
    g_edit.active = true;
    g_edit.startByte = U8Offset(text, r.start);
    g_edit.lenBytes = U8Offset(text, text.size()) - g_edit.startByte;
    g_edit.insert = U8(word + L" ");
    g_undo = {true, U8(fixed), U8(text), text.substr(r.start, r.length)};
    g_worker.Post([text, start = r.start, word, deliberate] { if (g_spell) g_spell->Chose(text, start, word, deliberate); });
    return true;
}

int InputCallback(ImGuiInputTextCallbackData* d) {
    switch (d->EventFlag) {
        case ImGuiInputTextFlags_CallbackCharFilter: {
            const ImWchar ch = d->EventChar;
            if (ch == '\n' || ch == '\r') return 1;  // no line breaks of your own: Enter sends
            if (ch == '\t') {  // Tab / Shift+Tab: the next / previous word of the bar (Space writes it)
                const int n = g_help.text == g_wFlatInput ? static_cast<int>(g_help.bar.words.size()) : 0;
                const int first = g_help.bar.autoIndex >= 0 && g_help.bar.autoIndex < n ? g_help.bar.autoIndex : 0;
                if (n > 0 && g_pick < 0 && !ImGui::GetIO().KeyShift) g_pick = first;  // what the bar / grey word offered
                else if (n > 0) g_pick = ImGui::GetIO().KeyShift ? (g_pick <= 0 ? n - 1 : g_pick - 1) : (g_pick + 1) % n;
                return 1;
            }
            if (ch == ' ') return SpaceWrites() ? 1 : 0;  // the picked / grey word, then the space
            return 0;
        }
        case ImGuiInputTextFlags_CallbackAlways: {
            if (g_edit.active) {  // a word of the bar or an autocorrection
                g_edit.active = false;
                const size_t len = static_cast<size_t>(d->BufTextLen);
                const size_t from = RawOffset(d->Buf, len, g_edit.startByte);  // flat offsets -> the box's own
                const size_t to = std::max(from, RawOffset(d->Buf, len, g_edit.startByte + g_edit.lenBytes));
                if (from <= len) {
                    d->DeleteChars(static_cast<int>(from), static_cast<int>(to - from));
                    d->InsertChars(static_cast<int>(from), g_edit.insert.c_str());
                    d->CursorPos = d->SelectionStart = d->SelectionEnd = d->BufTextLen;
                }
                break;
            }
            // Soft line breaks: the text is broken to the box's width (only when it changed) – at the last space of a line,
            // or inside a word that is longer than the line (a code, a link). The caret keeps its place in the text.
            if (g_wrapWidth > 0) {
                static std::string lastSeen;
                std::string raw(d->Buf, d->BufTextLen);
                int cursor = d->CursorPos;
                for (size_t i = 0; i < raw.size(); ++i)  // Backspace took the line end of a broken word: the letter before
                    if (raw[i] == '\r' && (i + 1 >= raw.size() || raw[i + 1] != '\n')) {
                        size_t start = i;
                        while (start > 0 && (static_cast<unsigned char>(raw[start - 1]) & 0xC0) == 0x80) --start;
                        if (start > 0) --start;  // the first byte of the letter before
                        raw.erase(start, i + 1 - start);
                        cursor = static_cast<int>(start);
                        break;
                    }
                if (raw != lastSeen) {
                    const std::string flat = Flat(raw.c_str());
                    const size_t flatCursor = Flat(raw.substr(0, std::min<size_t>(cursor, raw.size())).c_str()).size();
                    std::string out;
                    size_t lineStart = 0, lastSpace = std::string::npos;
                    for (size_t i = 0; i < flat.size();) {
                        size_t n = 1;  // one character (UTF-8)
                        while (i + n < flat.size() && (static_cast<unsigned char>(flat[i + n]) & 0xC0) == 0x80) ++n;
                        if (flat[i] == ' ') lastSpace = out.size();
                        out.append(flat, i, n);
                        i += n;
                        if (ImGui::CalcTextSize(out.data() + lineStart, out.data() + out.size()).x <= g_wrapWidth) continue;
                        if (lastSpace != std::string::npos && lastSpace >= lineStart) {
                            out[lastSpace] = '\n';
                            lineStart = lastSpace + 1;
                            lastSpace = std::string::npos;
                        } else if (out.size() - n > lineStart) {  // no space in this line: break before this letter
                            out.insert(out.size() - n, "\r\n");
                            lineStart = out.size() - n;
                        }
                    }
                    if (out != std::string(d->Buf, d->BufTextLen)) {
                        size_t at = 0, seen = 0;  // the caret: the same place in the text
                        while (at < out.size() && seen < flatCursor) {
                            if (out[at] == '\r') { at += 2; continue; }
                            ++seen;
                            ++at;
                        }
                        if (at + 1 < out.size() && out[at] == '\r') at += 2;
                        d->DeleteChars(0, d->BufTextLen);
                        d->InsertChars(0, out.c_str());
                        d->CursorPos = d->SelectionStart = d->SelectionEnd = static_cast<int>(std::min(at, out.size()));
                    }
                    lastSeen = out;
                }
            }
            const std::string now = Flat(std::string(d->Buf, d->BufTextLen).c_str());
            // Backspace right after an autocorrection: the word as you typed it comes back (and is not corrected
            // that way again).
            if (g_undo.active && now.size() + 1 == g_undo.after.size() && g_undo.after.compare(0, now.size(), now) == 0) {
                g_undo.active = false;
                d->DeleteChars(0, d->BufTextLen);
                d->InsertChars(0, g_undo.before.c_str());
                g_finishedLast.clear();  // an undone autocorrection is no correction of your own
                g_worker.Post([orig = g_undo.original] { if (g_spell) g_spell->RejectCorrection(orig); });
                break;
            }
            if (g_undo.active && now != g_undo.after) g_undo.active = false;
            break;
        }
        default:
            break;
    }
    return 0;
}
std::vector<SelfFix> g_selfFixes;  // newest first, g_mu
std::vector<std::wstring> g_lastWords;  // the words as they stood before a correction began

void AddSelfFix(const std::wstring& was, const std::wstring& now) {  // g_mu held
    g_selfFixes.erase(std::remove_if(g_selfFixes.begin(), g_selfFixes.end(),
                                     [&](const SelfFix& f) { return CaseFold(f.typed) == CaseFold(was); }),
                      g_selfFixes.end());
    if (was.size() < 3 || now.size() < 3 || CaseFold(was) == CaseFold(now) || EditDistance(was, now, 3) > 2) return;
    g_selfFixes.insert(g_selfFixes.begin(), {was, now});
    if (g_selfFixes.size() > 8) g_selfFixes.resize(8);
    g_test.selfFixes = static_cast<int>(g_selfFixes.size());
    strncpy_s(g_test.lastFix, U8(was + L">" + now).c_str(), _TRUNCATE);
}

void NoteSelfFix(const std::wstring& text) {  // g_mu held; the text changed
    std::vector<std::wstring> words;
    for (const Span& sp : WordSpans(text)) words.push_back(text.substr(sp.start, sp.length));
    const bool spaceAfter = !text.empty() && text.back() == L' ';
    if (words.empty()) g_finishedLast.clear();
    if (words.size() == g_lastWords.size() && !words.empty()) {
        int changed = -1, count = 0;
        for (size_t i = 0; i < words.size(); ++i)
            if (words[i] != g_lastWords[i]) changed = static_cast<int>(i), ++count;
        if (count == 1 && static_cast<size_t>(changed) + 1 < words.size()) {
            // A word before the last one is being corrected: compared with how it stood before (kept as it was).
            AddSelfFix(g_lastWords[changed], words[changed]);
            return;
        }
    }
    // The last word, retyped after a space had finished it ("shcon " → "schon "): when the space comes again.
    if (spaceAfter && !words.empty()) {
        if (g_finishedAt == words.size() - 1 && !g_finishedLast.empty() && words.back() != g_finishedLast)
            AddSelfFix(g_finishedLast, words.back());
        if (words.size() - 1 >= g_finishedAt || g_finishedLast.empty()) {  // a word deleted whole: it waits for its retyping
            g_finishedLast = words.back();
            g_finishedAt = words.size() - 1;
        }
    }
    g_lastWords = words;  // typing at the end, or more than one word changed: the new state to compare with
}

// Your own words ("finds = I think"): what the translator gets instead (<data>\my-words.txt), learned on a right-click.
// Your text blocks (<data>\snippets.txt): only your own text, saved from the input.
void SaveSnippets() {  // g_mu held
    if (!g_wiped) WriteFileAtomic(g_cfg.dataDir + L"\\snippets.txt", ToUtf8(g_snippets.Serialize()));
}

void SaveMyWords() {  // g_mu held
    if (!g_wiped) WriteFileAtomic(g_cfg.dataDir + L"\\my-words.txt", ToUtf8(g_myWords.Serialize()));
}

// The word under the mouse in the input ("" when the mouse is not over a word): the line by its height, the letter by
// width, then the word around it in the text as it is meant (Flat).
std::wstring WordAtMouse(const ImVec2& textAt) {  // g_mu held
    const ImVec2 m = ImGui::GetIO().MousePos;
    const float lh = ImGui::GetTextLineHeight();
    if (m.y < textAt.y || m.x < textAt.x || !g_input[0]) return {};
    const char* p = g_input;
    for (int k = static_cast<int>((m.y - textAt.y) / lh); k > 0 && p; --k) {
        p = std::strchr(p, '\n');
        if (p) ++p;
    }
    if (!p || !*p) return {};
    const char* end = std::strchr(p, '\n');
    if (!end) end = p + std::strlen(p);
    const char* hit = nullptr;
    for (const char* q = p; q < end;) {
        const char* next = q + 1;
        while (next < end && (static_cast<unsigned char>(*next) & 0xC0) == 0x80) ++next;
        if (m.x < textAt.x + ImGui::CalcTextSize(p, next).x) {
            hit = q;
            break;
        }
        q = next;
    }
    if (!hit || *hit == ' ' || *hit == '\r') return {};
    const std::wstring& flat = g_wFlatInput;
    const size_t at = W(Flat(std::string(g_input, static_cast<size_t>(hit - g_input)).c_str())).size();
    auto inWord = [](wchar_t c) { return std::iswalnum(c) || c == L'\'' || c == L'-' || c == L'_'; };
    if (at >= flat.size() || !inWord(flat[at])) return {};
    size_t a = at, b = at;
    while (a > 0 && inWord(flat[a - 1])) --a;
    while (b < flat.size() && inWord(flat[b])) ++b;
    return flat.substr(a, b - a);
}

}  // namespace kb
