// spell_service.cpp
#include "spell_service.hpp"

#include <windows.h>

#include <algorithm>

#include "core/slang.hpp"
#include "win/files.hpp"
#include "win/keyboard_layout.hpp"

namespace gct {
namespace {

bool HasDigit(const std::wstring& w) {
    return std::any_of(w.begin(), w.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; });
}

}  // namespace

bool SpellService::Init(const std::vector<std::wstring>& tags, const std::wstring& userWordsPath) {
    userPath_ = userWordsPath;
    std::string data;
    if (ReadFileBytes(userPath_, data)) {
        size_t pos = 0;
        while (pos < data.size()) {
            size_t eol = data.find('\n', pos);
            if (eol == std::string::npos) eol = data.size();
            std::wstring w = Trim(FromUtf8(data.substr(pos, eol - pos)));
            pos = eol + 1;
            if (!w.empty() && w[0] == 0xFEFF) w.erase(0, 1);  // BOM from Notepad
            if (!w.empty() && w[0] != L'#') user_.insert(CaseFold(w));
        }
    }
    ClearCache();
    return checker_.Init(tags);
}

void SpellService::UseLearnedLanguage(const std::wstring& primaryLang, const std::wstring& dir) {
    const std::wstring lang = ToLowerAscii(primaryLang.empty() ? std::wstring(L"xx") : primaryLang);
    const std::wstring path = dir + L"\\learned_" + lang + L".txt";
    layout_ = LayoutFor(GetKeyboardLayout(0));  // runs at start and on every layout switch
    neighbors_ = layout_.AsNeighbors();
    if (path == learnedPath_) return;
    SaveLearned();
    learnedPath_ = path;
    model_ = WordModel();
    std::string data;
    if (ReadFileBytes(path, data)) model_.Parse(data);
}

void SpellService::Learn(const std::wstring& sentText) {
    model_.Learn(sentText);
    SaveLearned();
}

void SpellService::SaveLearned() {
    if (learnedPath_.empty() || !model_.Dirty()) return;
    const size_t slash = learnedPath_.find_last_of(L'\\');
    if (slash != std::wstring::npos) EnsureDir(learnedPath_.substr(0, slash));
    if (WriteFileAtomic(learnedPath_, model_.Serialize())) model_.ClearDirty();
}

bool SpellService::Forget(const std::wstring& word) {
    const bool found = model_.Forget(word);
    session_.erase(CaseFold(word));
    SaveLearned();
    return found;
}

void SpellService::ForgetAll() {
    model_.Clear();
    SaveLearned();
    // Every language, not just the active one.
    const size_t slash = learnedPath_.find_last_of(L'\\');
    if (slash == std::wstring::npos) return;
    const std::wstring dir = learnedPath_.substr(0, slash);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\learned_*.txt").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) DeleteFileW((dir + L"\\" + fd.cFileName).c_str());
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

void SpellService::ClearCache() const {
    rejectCache_.clear();
    suggestCache_.clear();
}

bool SpellService::CheckerRejects(const std::wstring& word) const {
    if (auto it = rejectCache_.find(word); it != rejectCache_.end()) return it->second;
    bool rejected = false;
    for (const SpellIssue& issue : checker_.Check(word))
        if (issue.span.start == 0 && issue.span.length == word.size() && issue.kind != SpellIssue::Kind::Delete)
            rejected = true;
    if (rejectCache_.size() > 4000) rejectCache_.clear();
    return rejectCache_[word] = rejected;
}

const std::vector<std::wstring>& SpellService::CheckerSuggest(const std::wstring& word) const {
    if (auto it = suggestCache_.find(word); it != suggestCache_.end()) return it->second;
    if (suggestCache_.size() > 2000) suggestCache_.clear();
    return suggestCache_[word] = checker_.Suggest(word, 6);
}

void SpellService::RejectCorrection(const std::wstring& original) {
    model_.Confirm(original);
    session_.insert(CaseFold(original));
    SaveLearned();
}

bool SpellService::IsKnown(const std::wstring& word) const {
    const std::wstring f = CaseFold(word);
    return BuiltinSpellIgnore().count(f) || game_.count(f) || user_.count(f) || session_.count(f) || model_.Knows(word);
}

bool SpellService::IsMisspelled(const std::wstring& word) const {
    if (!checker_.Ready() || IsKnown(word)) return false;
    return CheckerRejects(word);
}

std::vector<Span> SpellService::ProtectedSpans(const std::wstring& text) {
    std::vector<Span> spans = FindChatCodes(text);
    size_t first = 0;
    while (first < text.size() && (text[first] == L' ' || text[first] == L'\t')) ++first;
    if (first < text.size() && text[first] == L'/') {
        const ChatSplit split = SplitChatCommand(text.substr(first));
        spans.push_back({first, split.prefix.size()});
    }
    return spans;
}

std::vector<SpellIssue> SpellService::Check(const std::wstring& text, size_t caret) const {
    std::vector<SpellIssue> out;
    if (!checker_.Ready()) return out;
    const std::vector<Span> blocked = ProtectedSpans(text);

    for (SpellIssue& issue : checker_.Check(text)) {
        const Span& s = issue.span;
        if (std::any_of(blocked.begin(), blocked.end(), [&](const Span& b) { return b.Overlaps(s); })) continue;
        if (caret != std::wstring::npos && caret >= s.start && caret <= s.end()) continue;  // still typing
        const std::wstring word = text.substr(s.start, s.length);
        if (HasDigit(word)) continue;
        if (IsKnown(word)) continue;  // also silences doubled words the player ignored ("ha ha")
        out.push_back(std::move(issue));
    }
    return out;
}

std::vector<std::wstring> SpellService::Suggest(const std::wstring& word) const { return CheckerSuggest(word); }

std::optional<std::wstring> SpellService::AutoCorrection(const std::wstring& word, AutoCorrectMode mode) const {
    if (mode == AutoCorrectMode::Off || word.size() < 2 || IsKnown(word)) return std::nullopt;
    if (checker_.Ready()) {
        for (const SpellIssue& issue : checker_.Check(word)) {
            if (issue.kind == SpellIssue::Kind::Replace && issue.span.start == 0 && issue.span.length == word.size() &&
                !issue.replacement.empty() && issue.replacement != word)
                return issue.replacement;  // Windows' own sure fix ("teh" -> "the")
        }
    }
    if (mode != AutoCorrectMode::Phone) return std::nullopt;
    // Only words the dictionary rejects; without a dictionary only what you taught it.
    if (checker_.Ready() && !IsMisspelled(word)) return std::nullopt;
    const std::wstring fix =
        ChooseCorrection(word, checker_.Ready() ? CheckerSuggest(word) : std::vector<std::wstring>(), model_);
    if (fix.empty() || fix == word) return std::nullopt;
    return fix;
}

WordSuggestions SpellService::Suggestions(const std::wstring& text, size_t caret, AutoCorrectMode mode) const {
    WordSuggestions s;
    if (caret > text.size()) caret = text.size();
    for (const Span& b : ProtectedSpans(text))
        if (caret > b.start && caret <= b.end()) return s;  // inside "/w Name, " or a chat code

    size_t start = caret;
    while (start > 0 && IsWordChar(text[start - 1])) --start;
    size_t end = caret;
    while (end < text.size() && IsWordChar(text[end])) ++end;

    // The word before the current one (for completions and next-word).
    auto wordBefore = [&](size_t pos) {
        size_t e = pos;
        while (e > 0 && !IsWordChar(text[e - 1])) {
            if (text[e - 1] == L'.' || text[e - 1] == L'!' || text[e - 1] == L'?') return std::wstring();
            --e;
        }
        size_t b = e;
        while (b > 0 && IsWordChar(text[b - 1])) --b;
        return text.substr(b, e - b);
    };

    if (start == caret) {  // between words: what usually comes next
        if (caret == 0 || (caret > 0 && text[caret - 1] != L' ')) return s;
        const std::wstring prev = wordBefore(caret);
        if (prev.empty()) return s;
        s.words = model_.Next(prev, 3);
        if (!s.words.empty()) {
            s.kind = WordSuggestions::Kind::Next;
            s.replace = {caret, 0};
        }
        return s;
    }

    const std::wstring partial = text.substr(start, caret - start);
    if (HasDigit(partial)) return s;
    const std::wstring prev = wordBefore(start);
    s.replace = {start, end - start};

    std::vector<std::wstring> words = model_.Complete(partial, prev, 3);
    // Before much is learned: GW2 words fill the bar ("Teq" -> "Tequatl").
    if (words.size() < 3 && partial.size() >= 2) {
        const std::wstring p = WordKey(partial);
        for (const std::wstring& w : Gw2StarterWords()) {
            if (words.size() >= 3) break;
            const std::wstring k = WordKey(w);
            if (k.size() <= p.size() || k.compare(0, p.size(), p) != 0) continue;
            if (std::any_of(words.begin(), words.end(), [&](const std::wstring& x) { return WordKey(x) == k; })) continue;
            words.push_back(CaseFoldChar(partial[0]) != partial[0] ? MatchCase(partial, w) : w);
        }
    }
    // Mid-word typo ("helo", "komt"): your own words that start like it with
    // one slip get the first, highlighted place, as on a phone keyboard.
    if (words.empty() && mode != AutoCorrectMode::Off) {
        std::vector<std::wstring> fuzzy = model_.CompleteFuzzy(partial, prev, 3, neighbors_);
        if (!fuzzy.empty()) {
            s.kind = WordSuggestions::Kind::Correction;
            s.words = std::move(fuzzy);
            s.autoIndex = 0;
            return s;
        }
    }
    // A typo the phone logic would fix gets the first, highlighted place.
    if (mode == AutoCorrectMode::Phone && partial.size() >= 4 && words.empty()) {
        if (auto fix = AutoCorrection(partial, mode)) {
            s.kind = WordSuggestions::Kind::Correction;
            s.words.push_back(*fix);
            s.autoIndex = 0;
            for (const std::wstring& w : checker_.Ready() ? CheckerSuggest(partial) : std::vector<std::wstring>())
                if (s.words.size() < 3 && w != *fix && w.find(L' ') == std::wstring::npos) s.words.push_back(w);
            return s;
        }
    }
    if (words.empty() && checker_.Ready() && partial.size() >= 3 && IsMisspelled(partial)) {
        for (const std::wstring& w : CheckerSuggest(partial))
            if (words.size() < 3 && w.find(L' ') == std::wstring::npos) words.push_back(w);
        if (!words.empty()) {
            s.kind = WordSuggestions::Kind::Correction;
            s.words = words;
            return s;
        }
    }
    if (!words.empty()) {
        s.kind = WordSuggestions::Kind::Completion;
        s.words = words;
        s.autoIndex = 0;
    }
    return s;
}

WordChoices SpellService::Choices(const std::wstring& text, size_t caret, AutoCorrectMode mode) const {
    WordChoices c;
    if (mode == AutoCorrectMode::Off || caret > text.size()) return c;
    if (caret < text.size() && IsWordChar(text[caret])) return c;  // only at the end of a word
    for (const Span& b : ProtectedSpans(text))
        if (caret > b.start && caret <= b.end()) return c;
    size_t start = caret;
    while (start > 0 && IsWordChar(text[start - 1])) --start;
    const std::wstring typed = text.substr(start, caret - start);
    if (typed.size() < 2 || HasDigit(typed)) return c;
    c.replace = {start, caret - start};

    // The word before (for completions that usually follow it).
    size_t e = start;
    while (e > 0 && text[e - 1] == L' ') --e;
    size_t b = e;
    while (b > 0 && IsWordChar(text[b - 1])) --b;
    const std::wstring prev = text.substr(b, e - b);

    const bool valid = IsKnown(typed) || (checker_.Ready() ? !CheckerRejects(typed) : model_.Knows(typed));
    std::vector<std::wstring> completions = model_.Complete(typed, prev, 3);
    const std::wstring p = WordKey(typed);
    for (const std::wstring& w : Gw2StarterWords()) {
        if (completions.size() >= 3) break;
        const std::wstring k = WordKey(w);
        if (k.size() > p.size() && k.compare(0, p.size(), p) == 0)
            completions.push_back(CaseFoldChar(typed[0]) != typed[0] ? MatchCase(typed, w) : w);
    }
    std::vector<std::wstring> fixes;
    if (!valid && typed.size() >= 3) {
        // The whole hand one key off: the strongest hint there is.
        for (const std::wstring& v : HandShiftVariants(typed, layout_))
            if (model_.Knows(v) || (checker_.Ready() && !CheckerRejects(v))) fixes.push_back(MatchCase(typed, v));
        for (const std::wstring& w : model_.NearSlip(typed, layout_, 3)) fixes.push_back(w);
        if (checker_.Ready())
            for (const std::wstring& w : CheckerSuggest(typed))
                if (w.find(L' ') == std::wstring::npos) fixes.push_back(w);
        for (const std::wstring& w : model_.CompleteFuzzy(typed, prev, 2, neighbors_)) fixes.push_back(w);
    }

    auto add = [&](const std::wstring& w) {
        if (c.words.size() >= 5 || w.empty()) return;
        const std::wstring k = WordKey(w);
        for (const std::wstring& x : c.words)
            if (WordKey(x) == k) return;
        c.words.push_back(w);
    };
    if (valid) {
        add(typed);
        c.firstIsTyped = true;
    }
    // An unfinished word ("Tequ") wants its completion; a finished word with
    // a slip ("helo", "jsööp") its correction.
    if (!completions.empty()) {
        for (const std::wstring& w : completions) add(w);
        for (const std::wstring& w : fixes) add(w);
    } else {
        for (const std::wstring& w : fixes) add(w);
    }
    if (c.words.size() <= 1 && c.firstIsTyped) return {};  // nothing to choose
    if (c.words.empty()) return {};
    if (c.firstIsTyped) c.highlight = 0;
    else c.highlight = mode == AutoCorrectMode::Phone ? 0 : -1;
    return c;
}

void SpellService::AddUserWord(const std::wstring& word) {
    const std::wstring w = Trim(word);
    if (w.empty() || !user_.insert(CaseFold(w)).second) return;
    std::string line;
    if (GetFileAttributesW(userPath_.c_str()) == INVALID_FILE_ATTRIBUTES)
        line = "# My GW2 words: never marked as spelling errors. One word per line.\r\n";
    line += ToUtf8(w) + "\r\n";
    AppendFileBytes(userPath_, line);
}

void SpellService::IgnoreForSession(const std::wstring& word) { session_.insert(CaseFold(word)); }

}  // namespace gct
