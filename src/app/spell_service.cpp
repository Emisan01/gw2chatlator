// spell_service.cpp
#include "spell_service.hpp"

#include <windows.h>

#include <algorithm>

#include "core/slang.hpp"
#include "win/files.hpp"

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
    return checker_.Init(tags);
}

void SpellService::UseLearnedLanguage(const std::wstring& primaryLang, const std::wstring& dir) {
    const std::wstring lang = ToLowerAscii(primaryLang.empty() ? std::wstring(L"xx") : primaryLang);
    const std::wstring path = dir + L"\\learned_" + lang + L".txt";
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
    for (const SpellIssue& issue : checker_.Check(word))
        if (issue.span.start == 0 && issue.span.length == word.size() && issue.kind != SpellIssue::Kind::Delete)
            return true;
    return false;
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

std::vector<std::wstring> SpellService::Suggest(const std::wstring& word) const { return checker_.Suggest(word, 6); }

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
        ChooseCorrection(word, checker_.Ready() ? checker_.Suggest(word, 6) : std::vector<std::wstring>(), model_);
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
    // A typo the phone logic would fix gets the first, highlighted place.
    if (mode == AutoCorrectMode::Phone && partial.size() >= 4 && words.empty()) {
        if (auto fix = AutoCorrection(partial, mode)) {
            s.kind = WordSuggestions::Kind::Correction;
            s.words.push_back(*fix);
            s.autoIndex = 0;
            for (const std::wstring& w : checker_.Ready() ? checker_.Suggest(partial, 4) : std::vector<std::wstring>())
                if (s.words.size() < 3 && w != *fix && w.find(L' ') == std::wstring::npos) s.words.push_back(w);
            return s;
        }
    }
    if (words.empty() && checker_.Ready() && partial.size() >= 3 && IsMisspelled(partial)) {
        for (const std::wstring& w : checker_.Suggest(partial, 3))
            if (w.find(L' ') == std::wstring::npos) words.push_back(w);
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
