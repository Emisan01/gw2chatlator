// spell_service.cpp
#include "spell_service.hpp"

#include <windows.h>

#include <algorithm>
#include <cwctype>

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
    // Names you taught: my-names.txt next to your words, one per line, written as they should be.
    const size_t slash = userPath_.find_last_of(L'\\');
    namesPath_ = (slash == std::wstring::npos ? std::wstring() : userPath_.substr(0, slash + 1)) + L"my-names.txt";
    names_.clear();
    data.clear();
    if (ReadFileBytes(namesPath_, data)) {
        size_t pos = 0;
        while (pos < data.size()) {
            size_t eol = data.find('\n', pos);
            if (eol == std::string::npos) eol = data.size();
            std::wstring w = Trim(FromUtf8(data.substr(pos, eol - pos)));
            pos = eol + 1;
            if (!w.empty() && w[0] == 0xFEFF) w.erase(0, 1);
            if (!w.empty() && w[0] != L'#') names_[CaseFold(w)] = w;
        }
    }
    keep_ = BuiltinKeepWords();
    for (const auto& kv : names_) keep_.insert(L"=" + kv.second);  // exactly as taught: "Fallen", not "fallen"
    ClearCache();
    InitPrediction(tags);
    return checker_.Init(tags);
}

bool SpellService::SwitchLanguage(const std::vector<std::wstring>& tags) {
    ClearCache();
    InitPrediction(tags);
    return checker_.Init(tags);
}

void SpellService::InitPrediction(const std::vector<std::wstring>& tags) {
    for (const std::wstring& t : tags)
        if (predict_.Init(t)) return;
    predict_.Init(L"");
}

// Windows' predictions for the word being typed, the word before as context ("wie g" -> "wie geht's"). They come
// as phrases and also with near misses ("Sch" -> "ach", "DFB"): only the last word, and only when it starts like
// `typed`. Case as typed at the start ("Sch" -> "Schon"), else as Windows writes it ("sch" -> "Schatz").
std::vector<std::wstring> SpellService::Predicted(const std::wstring& prev, const std::wstring& typed,
                                                  size_t max, bool* firstSmall) const {
    std::vector<std::wstring> out;
    if (firstSmall) *firstSmall = false;
    if (!predict_.Ready() || typed.empty() || HasDigit(typed)) return out;
    const std::wstring p = WordKey(typed);
    // Windows' list is full of first names ("dan" -> "Daniel", "Ka" -> "Karin"): words written small come first,
    // capitalised ones (names, German nouns) after them.
    std::vector<std::wstring> lower, capital;
    auto take = [&](const std::vector<std::wstring>& cands) {
        for (const std::wstring& c : cands) {
            const size_t sp = c.find_last_of(L' ');
            const std::wstring w = sp == std::wstring::npos ? c : c.substr(sp + 1);
            const std::wstring k = WordKey(w);
            if (k.size() <= p.size() || k.compare(0, p.size(), p) != 0 || HasDigit(w)) continue;
            if (std::any_of(w.begin(), w.end(), [](wchar_t ch) { return !IsWordChar(ch) && ch != L'\''; })) continue;
            auto same = [&](const std::wstring& x) { return WordKey(x) == k; };
            if (std::any_of(lower.begin(), lower.end(), same) || std::any_of(capital.begin(), capital.end(), same))
                continue;
            (CaseFoldChar(w[0]) == w[0] ? lower : capital).push_back(w);
        }
    };
    if (!prev.empty() && !HasDigit(prev)) take(predict_.Candidates(prev + L" " + typed));
    if (lower.size() + capital.size() < max) take(predict_.Candidates(typed));
    if (firstSmall) *firstSmall = !lower.empty();
    for (const auto* list : {&lower, &capital})
        for (const std::wstring& w : *list)
            if (out.size() < max) out.push_back(CaseFoldChar(typed[0]) != typed[0] ? MatchCase(typed, w) : w);
    return out;
}

std::vector<std::wstring> SpellService::BaseCompletions(const std::wstring& prev, const std::wstring& typed,
                                                        size_t max, bool* firstPredicted) const {
    std::vector<std::wstring> starters;
    if (typed.size() >= 2) {
        const std::wstring p = WordKey(typed);
        for (const std::wstring& w : Gw2StarterWords(learnedLang_)) {
            if (starters.size() >= max) break;
            const std::wstring k = WordKey(w);
            if (k.size() > p.size() && k.compare(0, p.size(), p) == 0)
                starters.push_back(CaseFoldChar(typed[0]) != typed[0] ? MatchCase(typed, w) : w);
        }
    }
    bool plain = false;
    const std::vector<std::wstring> predicted = Predicted(prev, typed, max, &plain);
    const bool gw2First = typed.size() >= 4 && !starters.empty();
    // Space may write Windows' guess only when it is an ordinary word: "what" must not become "WhatsApp".
    if (firstPredicted) *firstPredicted = !gw2First && plain;
    std::vector<std::wstring> out;
    for (const auto* list : {gw2First ? &starters : &predicted, gw2First ? &predicted : &starters})
        for (const std::wstring& w : *list)
            if (out.size() < max &&
                std::none_of(out.begin(), out.end(), [&](const std::wstring& x) { return WordKey(x) == WordKey(w); }))
                out.push_back(w);
    return out;
}

// Windows offers the typed word itself: it is a word (used where no dictionary is installed).
bool SpellService::PredictedExactly(const std::wstring& typed) const {
    if (!predict_.Ready() || typed.size() < 2) return false;
    const std::wstring k = WordKey(typed);
    for (const std::wstring& c : predict_.Candidates(typed))
        if (WordKey(c) == k) return true;
    return false;
}

void SpellService::SaveNames() {
    keep_ = BuiltinKeepWords();
    for (const auto& kv : names_) keep_.insert(L"=" + kv.second);  // exactly as taught: "Fallen", not "fallen"
    std::vector<std::wstring> all;
    for (const auto& kv : names_) all.push_back(kv.second);
    std::sort(all.begin(), all.end());
    std::string out = "# Names you taught – written exactly like this. One per line.\n";
    for (const std::wstring& n : all) out += ToUtf8(n) + "\n";
    WriteFileAtomic(namesPath_, out);
}

void SpellService::AddName(const std::wstring& name) {
    const std::wstring n = Trim(name);
    if (n.empty() || n.size() > 40) return;
    names_[CaseFold(n)] = n;
    SaveNames();
}

bool SpellService::RemoveName(const std::wstring& word) {
    if (names_.erase(CaseFold(Trim(word))) == 0) return false;
    SaveNames();
    return true;
}

std::wstring SpellService::NameFor(const std::wstring& word) const {
    const auto it = names_.find(CaseFold(word));
    return it == names_.end() ? std::wstring() : it->second;
}

void SpellService::UseLearnedLanguage(const std::wstring& primaryLang, const std::wstring& dir) {
    const std::wstring lang = ToLowerAscii(primaryLang.empty() ? std::wstring(L"xx") : primaryLang);
    const std::wstring path = dir + L"\\learned_" + lang + L".txt";
    learnedLang_ = lang;
    layout_ = LayoutFor(GetKeyboardLayout(0));  // runs at start and on every layout switch
    neighbors_ = layout_.AsNeighbors();
    if (path == learnedPath_) return;
    SaveLearned();
    learnedPath_ = path;
    model_ = WordModel();
    std::string data;
    if (ReadFileBytes(path, data)) model_.Parse(data);
    typosPath_ = dir + L"\\typos_" + lang + L".txt";
    typos_ = TypoMemory();
    data.clear();
    if (ReadFileBytes(typosPath_, data)) typos_.Parse(data);
}

void SpellService::Learn(const std::wstring& sentText) {
    model_.Learn(sentText);
    SaveLearned();
}

SpellService::ProfileResult SpellService::LearnFromText(const std::wstring& text) {
    ProfileResult r;
    // Sentences: lines, cut after . ! ? followed by a space.
    std::vector<std::wstring> sentences;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t eol = text.find(L'\n', pos);
        if (eol == std::wstring::npos) eol = text.size();
        std::wstring line = Trim(text.substr(pos, eol - pos));
        pos = eol + 1;
        if (!line.empty() && line[0] == 0xFEFF) line.erase(0, 1);
        if (line.empty()) continue;
        // Code, links, paths: not how you write in a chat.
        bool skip = false;
        for (const wchar_t* bad : {L"://", L"www.", L"{", L"}", L";", L"::", L"->", L"\\", L"==", L"</", L"/>"})
            if (line.find(bad) != std::wstring::npos) skip = true;
        size_t letters = 0;
        for (wchar_t c : line)
            if (IsWordChar(c) && !(c >= L'0' && c <= L'9')) ++letters;
        if (skip || letters * 2 < line.size()) continue;
        size_t b = 0;
        for (size_t i = 0; i < line.size(); ++i) {
            const wchar_t c = line[i];
            if ((c == L'.' || c == L'!' || c == L'?') && (i + 1 == line.size() || line[i + 1] == L' ')) {
                sentences.push_back(line.substr(b, i + 1 - b));
                b = i + 1;
            }
        }
        if (b < line.size()) sentences.push_back(line.substr(b));
    }
    auto tokens = [](const std::wstring& s) {
        std::vector<std::wstring> out;
        size_t i = 0;
        while (i < s.size()) {
            while (i < s.size() && !IsWordChar(s[i])) ++i;
            size_t e = i;
            while (e < s.size() && IsWordChar(s[e])) ++e;
            if (e > i) out.push_back(s.substr(i, e - i));
            i = e;
        }
        return out;
    };
    std::unordered_map<std::wstring, int> uses;
    std::unordered_map<std::wstring, std::wstring> forms;  // key -> the first spelling seen
    for (const std::wstring& s : sentences)
        for (const std::wstring& w : tokens(s)) {
            const std::wstring k = WordKey(w);
            ++uses[k];
            forms.emplace(k, w);
        }
    std::unordered_map<std::wstring, std::wstring> fixOf;  // typo key -> fix ("" = no typo), decided once
    const size_t typosBefore = typos_.Size();
    const size_t before = model_.Size();
    for (const std::wstring& s : sentences) {
        std::wstring part;
        bool learned = false;
        auto flush = [&] {
            if (!part.empty()) {
                model_.Learn(part);
                learned = true;
            }
            part.clear();
        };
        for (const std::wstring& w : tokens(s)) {
            if (HasDigit(w)) {
                flush();
                continue;
            }
            std::wstring word = w;
            if (!IsValidWord(w) && NameFor(w).empty()) {
                const std::wstring k = WordKey(w);
                auto f = fixOf.find(k);
                if (f == fixOf.end()) {
                    // A typo when the fix is clear – also a frequent one, as long as the fix is used at least twice as
                    // often (your typical slip); otherwise a word used 3+ times is your slang.
                    std::wstring fix = checker_.Ready() ? GuessFix(w, uses, forms) : std::wstring();
                    if (!fix.empty() && uses[k] >= 3 && uses[WordKey(fix)] < 2 * uses[k]) fix.clear();
                    f = fixOf.emplace(k, fix).first;
                }
                if (!f->second.empty()) {
                    typos_.Add(w, f->second);
                    word = CaseFoldChar(w[0]) != w[0] ? MatchCase(w, f->second) : f->second;
                } else if (uses[k] < 3) {
                    flush();  // an unclear typo: the words around it are not a pair
                    continue;
                }
            }
            part += (part.empty() ? L"" : L" ") + word;
        }
        flush();
        if (learned) ++r.sentences;
    }
    r.newWords = model_.Size() > before ? model_.Size() - before : 0;
    r.typos = typos_.Size() > typosBefore ? typos_.Size() - typosBefore : 0;
    SaveLearned();
    return r;
}

void SpellService::SaveLearned() {
    if (!typosPath_.empty() && typos_.Dirty() && WriteFileAtomic(typosPath_, typos_.Serialize())) typos_.ClearDirty();
    if (learnedPath_.empty() || !model_.Dirty()) return;
    const size_t slash = learnedPath_.find_last_of(L'\\');
    if (slash != std::wstring::npos) EnsureDir(learnedPath_.substr(0, slash));
    if (WriteFileAtomic(learnedPath_, model_.Serialize())) model_.ClearDirty();
}

bool SpellService::Forget(const std::wstring& word) {
    bool found = model_.Forget(word);
    found = typos_.Forget(word) || found;
    typos_.ForgetFix(word);
    session_.erase(CaseFold(word));
    SaveLearned();
    return found;
}

void SpellService::ForgetAll() {
    model_.Clear();
    typos_.Clear();
    SaveLearned();
    // Every language, not just the active one.
    const size_t slash = learnedPath_.find_last_of(L'\\');
    if (slash == std::wstring::npos) return;
    const std::wstring dir = learnedPath_.substr(0, slash);
    WIN32_FIND_DATAW fd;
    for (const wchar_t* pattern : {L"\\learned_*.txt", L"\\typos_*.txt"}) {
        HANDLE h = FindFirstFileW((dir + pattern).c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) DeleteFileW((dir + L"\\" + fd.cFileName).c_str());
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
}

bool SpellService::DictionarySlip(const std::wstring& typed, const std::wstring& fix) const {
    // Only the dictionary's first idea – the one it finds most likely ("shon": "schon", not the swap "Sohn").
    if (!checker_.Ready()) return false;
    for (const std::wstring& s : CheckerSuggest(typed)) {
        if (s.find(L' ') != std::wstring::npos || !LettersOnly(s)) continue;
        return WordKey(s) == WordKey(fix) && LooksLikeSlip(WordKey(typed), WordKey(fix), layout_, false);
    }
    return false;
}

void SpellService::NoteFix(const std::wstring& typed, const std::wstring& fix) {
    if (!learnChoices_ || typed.size() < 3 || fix.empty() || !LettersOnly(typed) || IsValidWord(typed)) return;
    const std::wstring tk = WordKey(typed), fk = WordKey(fix);
    if (fk.size() > tk.size() && fk.compare(0, tk.size(), tk) == 0) return;  // a completion, no typo
    if (fix.find(L' ') != std::wstring::npos) return;
    typos_.Add(typed, fix);
    SaveLearned();
}

std::wstring SpellService::KnownFix(const std::wstring& typed) const {
    if (typed.size() < 3 || IsValidWord(typed)) return {};
    const std::wstring fix = typos_.FixFor(typed);
    if (fix.empty()) return {};
    return CaseFoldChar(typed[0]) != typed[0] ? MatchCase(typed, fix) : fix;
}

// The clear fix of a typo in your own text: a word of the same text one keyboard slip away (LooksLikeSlip) that the
// dictionary knows (the more you use it, the better), else the dictionary's idea when it is the only one a swap or a
// doubled letter away. Two candidates of similar weight: no guess.
std::wstring SpellService::GuessFix(const std::wstring& typo, const std::unordered_map<std::wstring, int>& uses,
                                    const std::unordered_map<std::wstring, std::wstring>& forms) const {
    if (typo.size() < 4 || !LettersOnly(typo)) return {};
    const std::wstring tk = WordKey(typo);
    struct Cand {
        std::wstring word;
        double score;
    };
    std::vector<Cand> cands;
    for (const auto& [k, n] : uses) {
        if (k == tk || (k.size() > tk.size() ? k.size() - tk.size() : tk.size() - k.size()) > 1) continue;
        if (!LooksLikeSlip(tk, k, layout_, false)) continue;
        const std::wstring& form = forms.at(k);
        if (!LettersOnly(form) || !IsValidWord(form)) continue;
        cands.push_back({form, static_cast<double>(n)});
    }
    if (checker_.Ready()) {
        int close = 0;
        std::wstring only;
        for (const std::wstring& s : CheckerSuggest(typo)) {
            if (s.find(L' ') != std::wstring::npos || !LettersOnly(s)) continue;
            if (!LooksLikeSlip(tk, WordKey(s), layout_, true)) continue;
            ++close;
            only = s;
        }
        if (close == 1 && std::none_of(cands.begin(), cands.end(), [&](const Cand& c) { return WordKey(c.word) == WordKey(only); }))
            cands.push_back({only, 0.5});
    }
    if (cands.empty()) return {};
    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.score > b.score; });
    if (cands.size() > 1 && cands[1].score * 2 > cands[0].score) return {};
    return cands[0].word;
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
    typos_.Forget(original);  // meant as typed: no typo of yours
    model_.Confirm(original);
    session_.insert(CaseFold(original));
    SaveLearned();
}

bool SpellService::IsKnown(const std::wstring& word) const {
    const std::wstring f = CaseFold(word);
    return names_.count(f) || IsKeepWord(BuiltinSpellIgnore(), word) || game_.count(f) || user_.count(f) || session_.count(f) ||
           CommonChatEnglish().count(f) || (learnedLang_ == L"de" && IsGermanContraction(word)) || model_.Knows(word);
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

bool SpellService::LettersOnly(const std::wstring& w) {
    for (wchar_t c : w)
        if (!IsWordChar(c) && c != L'\'' && c != L'-' && c != 0x2019) return false;
    return !w.empty();
}

// A real word: known to you (learned, names, GW2 words) or to the dictionary – in any case of its first letter
// ("abend" is "Abend" written small, no error in a chat). Without a dictionary: your words and short words.
bool SpellService::IsValidWord(const std::wstring& w) const {
    if (IsKnown(w)) return true;
    if (!checker_.Ready()) return model_.Knows(w);
    if (!CheckerRejects(w)) return true;
    std::wstring other = w;
    other[0] = std::iswupper(w[0]) ? CaseFoldChar(w[0]) : static_cast<wchar_t>(std::towupper(w[0]));
    return !CheckerRejects(other);
}

std::optional<std::wstring> SpellService::AutoCorrection(const std::wstring& word, AutoCorrectMode mode,
                                                         const std::wstring& prev, const std::wstring& prev2) const {
    if (mode == AutoCorrectMode::Off || word.size() < 2 || IsKnown(word)) return std::nullopt;
    if (IsValidWord(word)) return std::nullopt;  // a real word (also small-written nouns) is never changed
    // One of your typical typos: you meant this word before (profile or a kept correction).
    if (mode == AutoCorrectMode::Phone)
        if (const std::wstring known = KnownFix(word); !known.empty()) return known;
    if (checker_.Ready()) {
        for (const SpellIssue& issue : checker_.Check(word)) {
            if (issue.kind == SpellIssue::Kind::Replace && issue.span.start == 0 && issue.span.length == word.size() &&
                !issue.replacement.empty() && issue.replacement != word)
                return issue.replacement;  // Windows' own sure fix ("teh" -> "the")
        }
    }
    if (mode != AutoCorrectMode::Phone) return std::nullopt;
    // Only words the dictionary rejects – without a dictionary any new word would look like a slip: no correction.
    if (!checker_.Ready() || !IsMisspelled(word)) return std::nullopt;
    const std::wstring fix =
        ChooseCorrection(word, checker_.Ready() ? CheckerSuggest(word) : std::vector<std::wstring>(), model_, prev, prev2);
    if (fix.empty() || fix == word || !LettersOnly(fix) || CaseFold(fix) == CaseFold(word)) return std::nullopt;
    // Only a slip of the fingers (a key next door, two letters swapped, a doubled or left-out letter inside the
    // word) – a changed ending is grammar or another language ("habs", "with"), never corrected.
    if (model_.Knows(fix) ? !LooksLikeSlip(WordKey(word), WordKey(fix), layout_, false) : !DictionarySlip(word, fix))
        return std::nullopt;
    return fix;
}

// The two words before `pos` in the same sentence ("kommst du |" -> "du", "kommst"); empty after . ! ?
void WordsBefore(const std::wstring& text, size_t pos, std::wstring* prev, std::wstring* prev2) {
    auto back = [&](size_t& e) -> std::wstring {
        while (e > 0 && !IsWordChar(text[e - 1])) {
            if (text[e - 1] == L'.' || text[e - 1] == L'!' || text[e - 1] == L'?') {
                e = 0;
                return {};
            }
            --e;
        }
        size_t b = e;
        while (b > 0 && IsWordChar(text[b - 1])) --b;
        std::wstring w = text.substr(b, e - b);
        e = b;
        return w;
    };
    size_t e = std::min(pos, text.size());
    *prev = back(e);
    *prev2 = prev->empty() ? std::wstring() : back(e);
}

void SpellService::Chose(const std::wstring& text, size_t start, const std::wstring& word, bool deliberate) {
    if (!learnChoices_) return;
    std::wstring prev, prev2;
    WordsBefore(text, start, &prev, &prev2);
    model_.Chose(prev2, prev, word, deliberate ? 1.0 : 0.3);
}

// Words of the recent chat that start like `typed` ("Teq" -> "Tequatl", "Ki" -> "Kiro"), after what is already there.
void SpellService::AddContextCompletions(const std::wstring& typed, std::vector<std::wstring>& out, size_t max) const {
    if (typed.size() < 2) return;
    const std::wstring p = WordKey(typed);
    for (const std::wstring& w : context_) {
        if (out.size() >= max) return;
        const std::wstring k = WordKey(w);
        if (k.size() <= p.size() || k.compare(0, p.size(), p) != 0) continue;
        if (std::any_of(out.begin(), out.end(), [&](const std::wstring& x) { return WordKey(x) == k; })) continue;
        out.push_back(w);
    }
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
        std::wstring p1, p2;
        WordsBefore(text, caret, &p1, &p2);
        s.words = model_.Next(prev, 3, p2);
        if (!s.words.empty()) {
            s.kind = WordSuggestions::Kind::Next;
            s.replace = {caret, 0};
            // Almost always this word here: it is offered grey after the caret (Tab or → takes it).
            const std::wstring sure = model_.NextSure(prev, p2);
            if (!sure.empty() && WordKey(sure) == WordKey(s.words[0])) {
                s.autoIndex = 0;
                // A phrase you write again and again: the whole sure rest.
                const std::vector<std::wstring> rest = model_.ContinueSure(prev, p2);
                if (rest.size() >= 2)
                    for (const std::wstring& w : rest) s.phrase += (s.phrase.empty() ? L"" : L" ") + w;
            }
        }
        return s;
    }

    const std::wstring partial = text.substr(start, caret - start);
    if (HasDigit(partial)) return s;
    const std::wstring prev = wordBefore(start);
    s.replace = {start, end - start};
    if (const std::wstring known = KnownFix(partial); !known.empty()) {  // one of your typical typos
        s.kind = WordSuggestions::Kind::Correction;
        s.words = {known};
        s.autoIndex = 0;
        return s;
    }

    std::wstring p1, p2;
    WordsBefore(text, start, &p1, &p2);
    std::vector<std::wstring> words;
    for (const auto& [k, n] : names_)  // names you taught come first
        if (words.size() < 3 && k.size() > WordKey(partial).size() && k.compare(0, WordKey(partial).size(), WordKey(partial)) == 0)
            words.push_back(n);
    for (const std::wstring& w : model_.Complete(partial, prev, 3, p2))
        if (words.size() < 3 && std::none_of(words.begin(), words.end(), [&](const std::wstring& x) { return WordKey(x) == WordKey(w); }))
            words.push_back(w);
    AddContextCompletions(partial, words, 3);  // what the chat is talking about right now
    // Before much is learned: the language's everyday words (Windows) and GW2 words ("Teq" -> "Tequatl").
    if (words.size() < 3)
        for (const std::wstring& w : BaseCompletions(prev, partial, 3, nullptr))
            if (words.size() < 3 && std::none_of(words.begin(), words.end(), [&](const std::wstring& x) { return WordKey(x) == WordKey(w); }))
                words.push_back(w);
    // Mid-word typo ("helo", "komt"): your own words that start like it with
    // one slip get the first, highlighted place, as on a phone keyboard.
    if (words.empty() && mode != AutoCorrectMode::Off) {
        std::vector<std::wstring> fuzzy = model_.CompleteFuzzy(partial, prev, 3, neighbors_, p2);
        if (!fuzzy.empty()) {
            s.kind = WordSuggestions::Kind::Correction;
            s.words = std::move(fuzzy);
            s.autoIndex = 0;
            return s;
        }
    }
    // A typo the phone logic would fix gets the first, highlighted place.
    if (mode == AutoCorrectMode::Phone && partial.size() >= 4 && words.empty()) {
        if (auto fix = AutoCorrection(partial, mode, prev, p2)) {
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
    if (typed.empty() || HasDigit(typed)) return c;
    c.replace = {start, caret - start};
    // A name you taught: written the taught way with Space, the word as typed with Tab ("Fallen", then "fallen").
    if (const std::wstring name = NameFor(typed); !name.empty() && name != typed) {
        c.words = {name, typed};
        c.highlight = 0;
        return c;
    }

    // The word before (for completions that usually follow it).
    size_t e = start;
    while (e > 0 && text[e - 1] == L' ') --e;
    size_t b = e;
    while (b > 0 && IsWordChar(text[b - 1])) --b;
    const std::wstring prev = text.substr(b, e - b);

    const bool valid = IsValidWord(typed) || (!checker_.Ready() && PredictedExactly(typed));
    std::wstring p1, p2;
    WordsBefore(text, start, &p1, &p2);
    // What Space may write without asking: your own words (learned, names) and clear slips on the keyboard. The
    // dictionary's ideas, the starter list and words of the chat are offered with Tab only – measured with
    // tests/tools/typing_bench: they turned correctly typed words into others ("tequatl" -> "gequält").
    std::vector<std::wstring> strong;
    std::vector<std::wstring> completions;
    for (const auto& [k, n] : names_)  // names you taught come first ("Fal" -> "Fallen")
        if (k.size() > WordKey(typed).size() && k.compare(0, WordKey(typed).size(), WordKey(typed)) == 0)
            completions.push_back(n);
    for (const std::wstring& w : model_.Complete(typed, prev, 3, p2)) completions.push_back(w);
    // Without a dictionary a short word may already be finished ("an", "do"): Space completes only from 3 letters
    // on and only when it adds 2 or more.
    for (const std::wstring& w : completions)
        if (checker_.Ready() || (typed.size() >= 3 && w.size() >= typed.size() + 2)) strong.push_back(w);
    AddContextCompletions(typed, completions, 3);
    // The language's everyday words (Windows' prediction) and the GW2 starter list. Windows' best guess is written
    // by Space while the typed part is not a word yet ("Sch" -> "schon"), the others wait for Tab.
    if (completions.size() < 3) {
        bool firstPredicted = false;
        bool first = true;
        for (const std::wstring& w : BaseCompletions(prev, typed, 3, &firstPredicted)) {
            if (completions.size() >= 3) break;
            if (std::any_of(completions.begin(), completions.end(), [&](const std::wstring& x) { return WordKey(x) == WordKey(w); }))
                continue;
            if (first && firstPredicted && !valid && typed.size() >= 2 && w.size() >= typed.size() + 2)
                strong.push_back(w);
            first = false;
            completions.push_back(w);
        }
    }
    // Words of the chat and the starter list: shown grey (Space writes them) only while the typed part is clearly
    // unfinished – not a word at all and the suggestion much longer ("Sch" -> "Schwarzzitadelle"). Without that, a
    // fresh tool showed no grey word at all; "use" -> "User" (+1) stays a Tab offer.
    // Without a dictionary "not a word" is only a guess ("an", "break" may be finished): from 3 letters, +4.
    const size_t minTyped = checker_.Ready() ? 2 : 3, minMore = checker_.Ready() ? 3 : 4;
    if (!valid && typed.size() >= minTyped)
        for (const std::wstring& w : completions)
            if (w.size() >= typed.size() + minMore && w.find(L' ') == std::wstring::npos) strong.push_back(w);
    std::vector<std::wstring> fixes;
    const std::wstring known = valid ? std::wstring() : KnownFix(typed);  // one of your typical typos
    if (!known.empty()) {
        fixes.push_back(known);
        strong.push_back(known);
    }
    if (!valid && typed.size() >= 3) {
        // The whole hand one key off: the strongest hint there is.
        for (const std::wstring& v : HandShiftVariants(typed, layout_))
            if (LettersOnly(v) && (model_.Knows(v) || (checker_.Ready() && !CheckerRejects(v)))) {
                fixes.push_back(MatchCase(typed, v));
                strong.push_back(fixes.back());
            }
        // Without a dictionary every new word looks like a slip of a known one ("many" -> "maybe"): offered only.
        // With one, Space writes it only when it looks like a slip of the fingers ("gehts" stays, "shon" -> "schon").
        for (const std::wstring& w : model_.NearSlip(typed, layout_, 3)) {
            fixes.push_back(w);
            if (checker_.Ready() && LooksLikeSlip(WordKey(typed), WordKey(w), layout_, false)) strong.push_back(w);
        }
        for (const std::wstring& w : model_.CompleteFuzzy(typed, prev, 2, neighbors_, p2)) {
            fixes.push_back(w);
            if (checker_.Ready()) strong.push_back(w);
        }
        if (checker_.Ready())
            for (const std::wstring& w : CheckerSuggest(typed)) {
                if (w.find(L' ') != std::wstring::npos || !LettersOnly(w)) continue;
                fixes.push_back(w);
                // The dictionary's idea is only taken by Space when it is one slip of the fingers away.
                if (DictionarySlip(typed, w)) strong.push_back(w);
            }
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
    if (!known.empty()) add(known);  // what you meant the last times comes first
    if (!completions.empty()) {
        for (const std::wstring& w : completions) add(w);
        for (const std::wstring& w : fixes) add(w);
    } else {
        for (const std::wstring& w : fixes) add(w);
    }
    if (c.words.size() <= 1 && c.firstIsTyped) return {};  // nothing to choose
    if (c.words.empty()) return {};
    if (c.firstIsTyped) {
        c.highlight = 0;
    } else if (mode == AutoCorrectMode::Phone) {
        // The first of your own words or clear slips; the rest waits for Tab.
        c.highlight = -1;
        for (size_t i = 0; i < c.words.size() && c.highlight < 0; ++i)
            if (std::find(strong.begin(), strong.end(), c.words[i]) != strong.end()) c.highlight = static_cast<int>(i);
    } else {
        c.highlight = -1;
    }
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
