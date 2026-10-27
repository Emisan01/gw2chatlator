// translation_check.cpp
#include "translation_check.hpp"

#include <algorithm>
#include <cwchar>

#include "langs.hpp"

namespace gct {
namespace {

bool IsTerminal(wchar_t c) { return c == L'.' || c == L'!' || c == L'?' || c == L'…' || c == L'。' || c == L'！' || c == L'？'; }
bool IsCloser(wchar_t c) { return c == L'"' || c == L'\'' || c == L')' || c == L']' || c == L'”' || c == L'’' || c == L'»'; }
bool IsBlank(wchar_t c) { return c == L' ' || c == L'\t' || c == L' ' || c == L'　'; }

// The word right before position `dot` (a '.') is an abbreviation: a dot inside it ("z.B", "e.g") or one letter.
bool AbbreviationBefore(const std::wstring& s, size_t dot) {
    size_t b = dot;
    while (b > 0 && !IsBlank(s[b - 1])) --b;
    const std::wstring word = s.substr(b, dot - b);
    if (word.empty()) return false;
    if (word.find(L'.') != std::wstring::npos) return true;
    return word.size() == 1 && IsWordChar(word[0]) && !(word[0] >= L'0' && word[0] <= L'9');
}

size_t Letters(const std::wstring& s) {
    return static_cast<size_t>(std::count_if(s.begin(), s.end(), [](wchar_t c) { return IsWordChar(c); }));
}

bool HasVisible(const std::wstring& s) {
    return std::any_of(s.begin(), s.end(), [](wchar_t c) { return !IsBlank(c); });
}

bool CompactScript(const std::wstring& lang) {
    const std::wstring p = PrimaryLang(lang);
    return p == L"ZH" || p == L"JA" || p == L"KO" || p == L"TH";
}

}  // namespace

std::vector<Sentence> SplitSentences(const std::wstring& text) {
    std::vector<Sentence> out;
    size_t start = 0, i = 0;
    while (i < text.size()) {
        if (!IsTerminal(text[i])) {
            ++i;
            continue;
        }
        const size_t first = i;
        while (i < text.size() && IsTerminal(text[i])) ++i;
        while (i < text.size() && IsCloser(text[i])) ++i;
        if (i < text.size() && !IsBlank(text[i])) continue;  // "3.5", "lol...ok": no end
        if (text[first] == L'.' && i - first == 1 && AbbreviationBefore(text, first)) continue;
        size_t g = i;
        while (g < text.size() && IsBlank(text[g])) ++g;
        out.push_back({text.substr(start, i - start), text.substr(i, g - i)});
        start = i = g;
    }
    if (start < text.size()) out.push_back({text.substr(start), L""});
    return out;
}

std::vector<SentenceSegments> SegmentsBySentence(const std::vector<Segment>& segments) {
    const std::wstring full = JoinSegments(segments);
    // where each segment lies in the message
    std::vector<size_t> begin;
    size_t pos = 0;
    for (const Segment& s : segments) {
        begin.push_back(pos);
        pos += s.text.size();
    }
    const auto insideKeep = [&](size_t at) {  // a sentence end at `at` would cut a protected part
        for (size_t k = 0; k < segments.size(); ++k)
            if (segments[k].keep && begin[k] < at && at < begin[k] + segments[k].text.size()) return true;
        return false;
    };
    // the sentence ends that do not cut a protected part: [start, textEnd) is the sentence, [textEnd, next) its gap
    struct Cut { size_t textEnd, next; };
    std::vector<Cut> cuts;
    size_t at = 0;
    for (const Sentence& s : SplitSentences(full)) {
        const size_t textEnd = at + s.text.size(), next = textEnd + s.gap.size();
        if (!s.gap.empty() && next < full.size() && !insideKeep(textEnd) && !insideKeep(next)) cuts.push_back({textEnd, next});
        at = next;
    }
    cuts.push_back({full.size(), full.size()});

    std::vector<SentenceSegments> out;
    size_t from = 0;
    for (const Cut& c : cuts) {
        SentenceSegments sentence;
        for (size_t k = 0; k < segments.size(); ++k) {
            const size_t a = std::max(begin[k], from), b = std::min(begin[k] + segments[k].text.size(), c.textEnd);
            if (a < b) sentence.segments.push_back({segments[k].text.substr(a - begin[k], b - a), segments[k].keep});
        }
        sentence.gap = full.substr(c.textEnd, c.next - c.textEnd);
        if (!sentence.segments.empty() || !sentence.gap.empty()) out.push_back(std::move(sentence));
        from = c.next;
    }
    return out;
}

size_t CountSentences(const std::wstring& text) {
    size_t n = 0;
    for (const Sentence& s : SplitSentences(text))
        if (Letters(s.text) > 0) ++n;
    return n;
}

std::vector<std::wstring> MissingProtected(const std::vector<Segment>& segments, const std::wstring& output) {
    std::vector<std::wstring> missing;
    const std::wstring folded = CaseFold(output);
    for (const Segment& s : segments) {
        if (!s.keep || !HasVisible(s.text)) continue;
        const std::wstring want = Trim(s.text);
        if (output.find(want) != std::wstring::npos) continue;
        if (Letters(want) > 0 && folded.find(CaseFold(want)) != std::wstring::npos) continue;
        missing.push_back(want);
    }
    return missing;
}

TranslationCheck CheckTranslation(const std::vector<Segment>& segments, const std::wstring& output,
                                  const std::wstring& targetLang) {
    TranslationCheck c;
    const std::vector<std::wstring> missing = MissingProtected(segments, output);
    if (!missing.empty()) {
        c.issue = TranslationIssue::LostProtected;
        c.detail = missing.front();
        return c;
    }
    const std::wstring source = JoinSegments(segments);
    const size_t srcLetters = Letters(source), outLetters = Letters(output);
    if (srcLetters == 0) return c;
    const double ratio = static_cast<double>(outLetters) / static_cast<double>(srcLetters);
    const bool alphabetic = !CompactScript(targetLang);
    const size_t srcSentences = CountSentences(source), outSentences = CountSentences(output);
    // A sentence fewer and clearly shorter: one was dropped (two short ones merged keep their length).
    if (srcSentences >= 2 && outSentences < srcSentences && (!alphabetic || ratio < 0.7)) {
        c.issue = TranslationIssue::LostSentence;
        c.detail = std::to_wstring(outSentences) + L"/" + std::to_wstring(srcSentences);
        return c;
    }
    if (alphabetic && srcLetters >= 20 && ratio < 0.45) {
        c.issue = TranslationIssue::TooShort;
        c.detail = std::to_wstring(static_cast<int>(ratio * 100)) + L"%";
    }
    return c;
}

}  // namespace gct
