// snippets.cpp — see snippets.hpp.

#include "core/snippets.hpp"

#include <algorithm>

namespace gct {
namespace {

bool Space(wchar_t c) { return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n'; }

std::wstring Trimmed(const std::wstring& s) {
    size_t a = 0, b = s.size();
    while (a < b && Space(s[a])) ++a;
    while (b > a && Space(s[b - 1])) --b;
    return s.substr(a, b - a);
}

}  // namespace

void Snippets::Parse(const std::wstring& text) {
    items_.clear();
    std::wstring line;
    auto take = [this, &line] {
        std::wstring block;
        for (size_t i = 0; i < line.size(); ++i) {
            if (line[i] == L'\\' && i + 1 < line.size()) {
                ++i;
                block += line[i] == L'n' ? L'\n' : line[i];
            } else if (line[i] != L'\r') {
                block += line[i];
            }
        }
        line.clear();
        if (items_.size() < kMax) Add(block);
    };
    for (wchar_t c : text) {
        if (c == L'\n') take();
        else line += c;
    }
    if (!line.empty()) take();
}

std::wstring Snippets::Serialize() const {
    std::wstring out;
    for (const std::wstring& s : items_) {
        for (wchar_t c : s) {
            if (c == L'\\') out += L"\\\\";
            else if (c == L'\n') out += L"\\n";
            else if (c != L'\r') out += c;
        }
        out += L'\n';
    }
    return out;
}

Snippets::Added Snippets::Add(const std::wstring& text) {
    std::wstring s = text;
    s.erase(std::remove(s.begin(), s.end(), L'\r'), s.end());  // as the file keeps it (E6: saved = loaded)
    s = Trimmed(s);
    if (s.size() > kMaxChars) {
        size_t cut = kMaxChars;
        if (s[cut - 1] >= 0xD800 && s[cut - 1] <= 0xDBFF) --cut;  // never half a surrogate pair
        s = Trimmed(s.substr(0, cut));
    }
    if (s.empty()) return Added::Empty;
    if (std::find(items_.begin(), items_.end(), s) != items_.end()) return Added::Twice;
    if (items_.size() >= kMax) return Added::Full;
    items_.push_back(s);
    return Added::Yes;
}

bool Snippets::Remove(size_t index) {
    if (index >= items_.size()) return false;
    items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

}  // namespace gct
