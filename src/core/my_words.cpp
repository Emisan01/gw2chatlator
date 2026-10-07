// my_words.cpp
#include "my_words.hpp"

#include <algorithm>
#include <cwctype>

#include "text.hpp"

namespace gct {

namespace {

std::wstring Key(const std::wstring& w) { return CaseFold(Trim(w)); }

}  // namespace

void MyWords::Parse(const std::wstring& text) {
    entries_.clear();
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(L'\n', start);
        if (end == std::wstring::npos) end = text.size();
        std::wstring line = Trim(text.substr(start, end - start));
        start = end + 1;
        if (line.empty() || line[0] == L'#') continue;
        const size_t eq = line.find(L'=');
        const std::wstring word = Trim(line.substr(0, eq));
        const std::wstring meaning = eq == std::wstring::npos ? L"" : Trim(line.substr(eq + 1));
        if (!word.empty() && word.find(L' ') == std::wstring::npos) Set(word, meaning);
        if (end == text.size()) break;
    }
}

std::wstring MyWords::Serialize() const {
    std::wstring s;
    for (const auto& [w, m] : entries_) s += m.empty() ? w + L"\r\n" : w + L" = " + m + L"\r\n";
    return s;
}

void MyWords::Set(const std::wstring& word, const std::wstring& meaning) {
    const std::wstring k = Key(word);
    if (k.empty()) return;
    for (auto& e : entries_)
        if (Key(e.first) == k) {
            e.second = Trim(meaning);
            return;
        }
    entries_.push_back({Trim(word), Trim(meaning)});
}

bool MyWords::Remove(const std::wstring& word) {
    const std::wstring k = Key(word);
    const auto before = entries_.size();
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [&](const auto& e) { return Key(e.first) == k; }),
                   entries_.end());
    return entries_.size() != before;
}

std::wstring MyWords::MeaningOf(const std::wstring& word) const {
    const std::wstring k = Key(word);
    for (const auto& e : entries_)
        if (Key(e.first) == k) return e.second;
    return L"";
}

bool MyWords::Knows(const std::wstring& word) const {
    const std::wstring k = Key(word);
    for (const auto& e : entries_)
        if (Key(e.first) == k) return true;
    return false;
}

std::wstring MyWords::Expand(const std::wstring& text) const {
    if (entries_.empty()) return text;
    std::wstring out;
    size_t i = 0;
    while (i < text.size()) {
        if (!IsWordChar(text[i])) {
            out += text[i++];
            continue;
        }
        size_t j = i;
        while (j < text.size() && (IsWordChar(text[j]) || (text[j] == L'\'' && j + 1 < text.size() && IsWordChar(text[j + 1]))))
            ++j;
        const std::wstring word = text.substr(i, j - i);
        std::wstring meaning = MeaningOf(word);
        if (meaning.empty()) {
            out += word;
        } else {
            // "Finds" at the start of a sentence -> "Finde es".
            if (IsWordChar(word[0]) && CaseFoldChar(word[0]) != word[0] && !meaning.empty())
                meaning[0] = static_cast<wchar_t>(std::towupper(meaning[0]));
            out += meaning;
        }
        i = j;
    }
    return out;
}

}  // namespace gct
