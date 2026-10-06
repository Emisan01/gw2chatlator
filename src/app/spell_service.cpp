// spell_service.cpp
#include "spell_service.hpp"

#include <windows.h>

#include <algorithm>

#include "core/slang.hpp"
#include "win/files.hpp"

namespace gct {

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

bool SpellService::IsKnown(const std::wstring& word) const {
    const std::wstring f = CaseFold(word);
    return BuiltinSpellIgnore().count(f) || game_.count(f) || user_.count(f) || session_.count(f);
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
        if (std::any_of(word.begin(), word.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; })) continue;
        if (IsKnown(word)) continue;  // also silences doubled words the player ignored ("ha ha")
        out.push_back(std::move(issue));
    }
    return out;
}

std::vector<std::wstring> SpellService::Suggest(const std::wstring& word) const { return checker_.Suggest(word, 6); }

std::optional<std::wstring> SpellService::AutoCorrection(const std::wstring& word) const {
    if (!checker_.Ready() || word.size() < 2 || IsKnown(word)) return std::nullopt;
    for (const SpellIssue& issue : checker_.Check(word)) {
        if (issue.kind == SpellIssue::Kind::Replace && issue.span.start == 0 && issue.span.length == word.size() &&
            !issue.replacement.empty() && issue.replacement != word)
            return issue.replacement;
    }
    return std::nullopt;
}

void SpellService::AddUserWord(const std::wstring& word) {
    const std::wstring w = Trim(word);
    if (w.empty() || !user_.insert(CaseFold(w)).second) return;
    std::string line;
    if (GetFileAttributesW(userPath_.c_str()) == INVALID_FILE_ATTRIBUTES)
        line = "# Eigene GW2-Woerter: werden nie als Fehler markiert. Ein Wort pro Zeile.\r\n";
    line += ToUtf8(w) + "\r\n";
    AppendFileBytes(userPath_, line);
}

void SpellService::IgnoreForSession(const std::wstring& word) { session_.insert(CaseFold(word)); }

}  // namespace gct
