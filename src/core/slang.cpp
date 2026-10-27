// slang.cpp — the built-in GW2 terms come from data/gw2-terms.tsv (every keep-word is a word the translator will
// never touch, so only terms that read the same in every language). Players add their own terms in gw2-terms.txt
// and words for the spell check via the right-click menu.
#include "slang.hpp"

#include <algorithm>
#include <initializer_list>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace gct {

const std::vector<BuiltinTerm>& BuiltinTerms() {
    static const std::vector<BuiltinTerm> terms = {
#include "gw2_terms.inc"
    };
    return terms;
}

std::vector<TermLine> ParseTermTable(const std::string& utf8) {
    std::vector<TermLine> out;
    size_t pos = utf8.rfind("\xEF\xBB\xBF", 0) == 0 ? 3 : 0;  // BOM
    while (pos < utf8.size()) {
        size_t eol = utf8.find('\n', pos);
        if (eol == std::string::npos) eol = utf8.size();
        std::string raw = utf8.substr(pos, eol - pos);
        pos = eol + 1;
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        std::vector<std::wstring> cols;
        size_t start = 0;
        for (size_t tab; (tab = raw.find('\t', start)) != std::string::npos; start = tab + 1)
            cols.push_back(Trim(FromUtf8(raw.substr(start, tab - start))));
        cols.push_back(Trim(FromUtf8(raw.substr(start))));
        if (cols[0].empty() || cols[0][0] == L'#') continue;
        TermLine line;
        bool gap = false;
        for (wchar_t c : cols[0]) {  // spaces as one, like gen_terms.py
            if (c == L' ') {
                gap = !line.term.empty();
                continue;
            }
            if (gap) line.term += L' ';
            gap = false;
            line.term += c;
        }
        const std::wstring kind = cols.size() > 1 ? cols[1] : L"";
        line.kind = kind == L"keep" ? TermKind::Keep : kind == L"caps" ? TermKind::Caps : TermKind::Word;
        if (cols.size() > 2) line.meaning = cols[2];
        out.push_back(std::move(line));
    }
    return out;
}

std::wstring PhraseWordKey(const std::wstring& word) {
    std::wstring k = CaseFold(word);
    k.erase(std::remove_if(k.begin(), k.end(), [](wchar_t c) { return c == L'\'' || c == L'’'; }), k.end());
    return k;
}

std::wstring TermKey(const std::wstring& term) {
    const std::vector<Span> words = WordSpans(term);
    if (words.size() <= 1) return CaseFold(Trim(term));
    std::wstring key;
    for (const Span& w : words) key += (key.empty() ? L"" : L" ") + PhraseWordKey(term.substr(w.start, w.length));
    return key;
}

namespace {

// Words for the word bar before much is learned, by the language you write in: GW2 terms everyone uses, plus the
// German or English names. Never English suggestions for German or the other way round.
const wchar_t* const kStarterAny[] = {
    L"LFG", L"LFM", L"Tequatl", L"Dungeon", L"Raid", L"Strike", L"Meta", L"Event", L"Champion", L"Commander",
    L"Squad", L"Party", L"Rata Sum", L"Hoelbrak", L"Drakkar", L"Quickness", L"Alacrity", L"Might", L"Boons",
    L"Healer", L"Heal", L"Tank", L"DPS", L"Condi", L"Revive", L"Stack", L"Breakbar", L"Wipe", L"Karma", L"Gold",
    L"Gems", L"Raptor", L"Sorry", L"Infusion",
};
const wchar_t* const kStarterDe[] = {
    L"Fraktal", L"Fraktale", L"Erfolg", L"Kommandeur", L"Trupp", L"Gruppe", L"Gilde", L"Gildenhalle", L"Weltboss",
    L"Löwenstein", L"Götterfels", L"Schwarzzitadelle", L"Wegmarke", L"Drachensturm", L"Silberwüste", L"Stacken",
    L"Legendär", L"Aufgestiegen", L"Exotisch", L"Mystische Schmiede", L"Ektoplasma", L"Edelsteine",
    L"Himmelsschuppe", L"Greif", L"Springer", L"Schakal", L"Käfer", L"Kriegsklaue", L"Danke", L"Bitte", L"Gerne",
    L"Willkommen",
};
const wchar_t* const kStarterEn[] = {
    L"Fractal", L"Fractals", L"Worldboss", L"Achievement", L"Dailies", L"Weekly", L"Guild", L"Guild Hall",
    L"Lion's Arch", L"Divinity's Reach", L"Black Citadel", L"Waypoint", L"Dragonstorm", L"Silverwastes",
    L"Legendary", L"Ascended", L"Exotic", L"Mystic Forge", L"Ectoplasm", L"Skyscale", L"Griffon",
    L"Jackal", L"Beetle", L"Warclaw", L"Thanks", L"Please", L"Welcome",
};

WordSet Make(std::initializer_list<const wchar_t* const*> lists, std::initializer_list<size_t> sizes) {
    WordSet s;
    auto size = sizes.begin();
    for (const wchar_t* const* list : lists) {
        for (size_t i = 0; i < *size; ++i) s.insert(CaseFold(list[i]));
        ++size;
    }
    return s;
}

}  // namespace

const WordSet& BuiltinKeepWords() {
    static const WordSet s = [] {
        WordSet x;
        for (const BuiltinTerm& t : BuiltinTerms()) {
            if (t.kind == TermKind::Keep) x.insert(TermKey(t.term));
            if (t.kind == TermKind::Caps) x.insert(L"^" + CaseFold(t.term));  // see IsKeepWord
        }
        return x;
    }();
    return s;
}

// ---- gw2-terms.txt ----------------------------------------------------------------------------------------------
namespace {
constexpr char kMineMark[] = "# --- your terms ---";
constexpr char kBuiltinMark[] = "# --- built in ---";
}  // namespace

std::vector<std::wstring> ParseMyTerms(const std::string& utf8) {
    std::vector<std::wstring> mine;
    size_t pos = 0;
    while (pos < utf8.size()) {
        size_t eol = utf8.find('\n', pos);
        if (eol == std::string::npos) eol = utf8.size();
        std::string raw = utf8.substr(pos, eol - pos);
        pos = eol + 1;
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        if (raw.rfind(kBuiltinMark, 0) == 0) break;  // the built-in block is the version's, not yours
        raw = raw.substr(0, raw.find('\t'));          // a note may follow after a tab
        std::wstring w = Trim(FromUtf8(raw));
        if (!w.empty() && w[0] == 0xFEFF) w = Trim(w.substr(1));  // BOM from Notepad
        if (w.empty() || w[0] == L'#') continue;
        const bool seen =
            std::any_of(mine.begin(), mine.end(), [&](const std::wstring& m) { return TermKey(m) == TermKey(w); });
        if (!seen) mine.push_back(w);
    }
    return mine;
}

std::string WriteTermsFile(const std::vector<std::wstring>& mine) {
    std::string out =
        "# GW2Chatlator - GW2 terms: never translated, never marked as a spelling error. One term per line; several\r\n"
        "# words are one term (a guild's name), a note may follow after a tab.\r\n"
        "# Add your own below \"your terms\" (a guild tag, your own short forms) - they stay from version to version.\r\n"
        "# The block \"built in\" at the end is written again by every version: changes there are replaced.\r\n"
        "# Written in capitals there: kept only when written like that (\"AB\" Auric Basin, but German \"ab\").\r\n\r\n";
    out += std::string(kMineMark) + "\r\n";
    for (const std::wstring& w : mine) out += ToUtf8(w) + "\r\n";
    out += "\r\n" + std::string(kBuiltinMark) + "\r\n";
    std::vector<std::pair<std::wstring, const wchar_t*>> builtin;
    for (const BuiltinTerm& t : BuiltinTerms()) {
        if (t.kind == TermKind::Keep) builtin.emplace_back(t.term, t.meaning);
        if (t.kind == TermKind::Caps) builtin.emplace_back(ToUpperAscii(t.term), t.meaning);
    }
    std::sort(builtin.begin(), builtin.end(),
              [](const auto& a, const auto& b) { return CaseFold(a.first) < CaseFold(b.first); });
    for (const auto& [term, meaning] : builtin)
        out += ToUtf8(term) + (*meaning ? "\t" + ToUtf8(meaning) : std::string()) + "\r\n";
    return out;
}

void AddMyTerms(WordSet& keep, const std::vector<std::wstring>& mine) {
    for (const std::wstring& w : mine) keep.insert(TermKey(w));
}

// The most common English words of a chat (written for this list, everyday vocabulary).
const wchar_t* const kChatEnglish[] = {
    L"a", L"about", L"after", L"again", L"all", L"almost", L"also", L"always", L"am", L"an", L"and", L"any",
    L"anyone", L"anything", L"are", L"around", L"as", L"ask", L"at", L"away", L"back", L"bad", L"be", L"because",
    L"been", L"before", L"being", L"best", L"better", L"big", L"boss", L"both", L"bring", L"busy", L"but", L"buy",
    L"by", L"call", L"came", L"can", L"cant", L"come", L"coming", L"could", L"cool", L"damn", L"day", L"did",
    L"didnt", L"do", L"does", L"doesnt", L"doing", L"done", L"dont", L"down", L"each", L"easy", L"else", L"enough",
    L"even", L"ever", L"every", L"everyone", L"fast", L"few", L"fine", L"first", L"for", L"found", L"free",
    L"friend", L"friends", L"from", L"fun", L"funny", L"get", L"gets", L"getting", L"give", L"go", L"goes", L"going",
    L"gone", L"good", L"got", L"great", L"guys", L"had", L"happy", L"has", L"have", L"having", L"he", L"hello",
    L"help", L"her", L"here", L"hey", L"hi", L"him", L"his", L"hold", L"home", L"how", L"i", L"if", L"im", L"in",
    L"into", L"is", L"isnt", L"it", L"its", L"just", L"keep", L"kill", L"kind", L"know", L"last", L"late", L"later",
    L"learn", L"leave", L"left", L"let", L"lets", L"like", L"little", L"long", L"look", L"looking", L"lost", L"lot",
    L"lots", L"love", L"made", L"make", L"many", L"maybe", L"me", L"mean", L"mine", L"more", L"most", L"much",
    L"must", L"my", L"need", L"never", L"new", L"next", L"nice", L"night", L"no", L"not", L"nothing", L"now", L"of",
    L"off", L"oh", L"ok", L"okay", L"old", L"on", L"once", L"one", L"only", L"open", L"or", L"other", L"our", L"out",
    L"over", L"party", L"people", L"play", L"player", L"players", L"please", L"point", L"pretty", L"quick", L"quite",
    L"ready", L"real", L"really", L"right", L"run", L"same", L"say", L"see", L"seem", L"send", L"sent", L"she",
    L"should", L"show", L"since", L"so", L"some", L"someone", L"something", L"soon", L"sorry", L"start", L"still",
    L"stop", L"stuff", L"sure", L"take", L"talk", L"team", L"tell", L"than", L"thank", L"thanks", L"that", L"thats",
    L"the", L"their", L"them", L"then", L"there", L"these", L"they", L"thing", L"things", L"think", L"this",
    L"those", L"though", L"thought", L"through", L"time", L"to", L"today", L"together", L"tomorrow", L"too", L"try",
    L"trying", L"turn", L"up", L"us", L"use", L"used", L"very", L"wait", L"want", L"wanted", L"was", L"way", L"we",
    L"well", L"went", L"were", L"what", L"whats", L"when", L"where", L"which", L"while", L"who", L"why", L"will",
    L"with", L"without", L"wont", L"work", L"would", L"yeah", L"yes", L"yet", L"you", L"your", L"yours", L"youre",
};

// Verb forms that take "s" for "es" in spoken German. "hat"/"war" are left out ("hats", "wars" are English words).
const wchar_t* const kContractionStems[] = {
    L"hab", L"geht", L"gibt", L"gab", L"wird", L"ist", L"mach", L"macht", L"kann", L"ging", L"sieht", L"klappt",
    L"passt", L"stimmt", L"wär", L"hätt", L"nimm", L"lohnt", L"braucht", L"dauert", L"kommt",
    L"gefällt", L"tut", L"hilft", L"reicht", L"schaff", L"glaub",
};

// The stem of a contraction ("Habs" -> "Hab", "geht's" -> "geht"), empty if `word` is none.
static std::wstring ContractionStem(const std::wstring& word) {
    size_t cut = 0;
    if (word.size() > 3 && (word[word.size() - 2] == L'\'' || word[word.size() - 2] == L'\u2019') &&
        (word.back() == L's' || word.back() == L'S'))
        cut = 2;
    else if (word.size() > 2 && (word.back() == L's' || word.back() == L'S'))
        cut = 1;
    if (!cut) return {};
    const std::wstring stem = word.substr(0, word.size() - cut);
    const std::wstring key = CaseFold(stem);
    for (const wchar_t* s : kContractionStems)
        if (key == s) return stem;
    return {};
}

bool IsGermanContraction(const std::wstring& word) { return !ContractionStem(word).empty(); }

std::wstring ExpandGermanContractions(const std::wstring& text) {
    std::wstring out;
    size_t i = 0;
    while (i < text.size()) {
        auto inWord = [&](size_t k) {
            return IsWordChar(text[k]) || ((text[k] == L'\'' || text[k] == L'\u2019') && k > 0 && IsWordChar(text[k - 1]));
        };
        if (!inWord(i)) {
            out += text[i++];
            continue;
        }
        size_t e = i;
        while (e < text.size() && inWord(e)) ++e;
        const std::wstring word = text.substr(i, e - i);
        const std::wstring stem = ContractionStem(word);
        out += stem.empty() ? word : stem + L" es";
        i = e;
    }
    return out;
}

const WordSet& CommonChatEnglish() {
    static const WordSet s = Make({kChatEnglish}, {std::size(kChatEnglish)});
    return s;
}

const WordSet& BuiltinSpellIgnore() {
    static const WordSet s = [] {
        WordSet x = BuiltinKeepWords();
        for (const BuiltinTerm& t : BuiltinTerms())
            if (t.kind == TermKind::Word) x.insert(CaseFold(t.term));
        return x;
    }();
    return s;
}

bool LooksLikeAbbreviation(const std::wstring& word) {
    size_t upper = 0, letters = 0;
    for (wchar_t c : word) {
        if (!IsWordChar(c) || (c >= L'0' && c <= L'9')) continue;
        ++letters;
        if (CaseFold(std::wstring(1, c)) != std::wstring(1, c)) ++upper;
    }
    return letters >= 2 && upper >= 2;
}

bool IsKeepWord(const WordSet& keep, const std::wstring& word) {
    const std::wstring f = CaseFold(word);
    return keep.count(f) > 0 || (keep.count(L"^" + f) > 0 && LooksLikeAbbreviation(word)) ||
           keep.count(L"=" + word) > 0;  // a name taught by the user: kept only exactly as taught
}

const std::vector<std::wstring>& Gw2StarterWords(const std::wstring& lang) {
    std::wstring p = ToUpperAscii(Trim(lang));
    p = p.substr(0, p.find(L'-'));
    static const std::vector<std::wstring> any(std::begin(kStarterAny), std::end(kStarterAny));
    static const std::vector<std::wstring> de = [] {
        std::vector<std::wstring> v(std::begin(kStarterAny), std::end(kStarterAny));
        v.insert(v.end(), std::begin(kStarterDe), std::end(kStarterDe));
        return v;
    }();
    static const std::vector<std::wstring> en = [] {
        std::vector<std::wstring> v(std::begin(kStarterAny), std::end(kStarterAny));
        v.insert(v.end(), std::begin(kStarterEn), std::end(kStarterEn));
        return v;
    }();
    static const std::vector<std::wstring> all = [] {
        std::vector<std::wstring> v = de;
        v.insert(v.end(), std::begin(kStarterEn), std::end(kStarterEn));
        return v;
    }();
    if (p.empty()) return all;
    if (p == L"DE") return de;
    if (p == L"EN") return en;
    return any;
}

}  // namespace gct
