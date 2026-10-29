// protect.cpp
#include "protect.hpp"
#include "slang.hpp"

#include <algorithm>
#include <cwchar>

namespace gct {

namespace {
struct Protected {
    Span span;
    std::wstring replacement;
};

// ":D", ":-)", ";P", "xD", "^^", "<3", "o/", "T_T" ... a chat smiley is the
// same in every language; translators like to turn it into words or drop it.
bool IsEmoticon(const std::wstring& t) {
    static const wchar_t* const fixed[] = {L"^^",  L"^.^", L"^_^", L"<3",  L"</3", L"o/",  L"\\o",  L"\\o/",
                                           L"T_T", L"T.T", L"-_-", L"-.-", L"o.O", L"O.o", L"o_O", L"O_o",
                                           L"xD",  L"XD",  L"xd",  L"xP",  L"XP",  L":'(", L"D:",  L">.<"};
    for (const wchar_t* f : fixed)
        if (t == f) return true;
    if (t.size() < 2 || t.size() > 5) return false;
    // eyes, optional nose, then a mouth made of one or more of the same kind
    size_t i = 0;
    if (t[i] != L':' && t[i] != L';' && t[i] != L'=' && t[i] != L'8') return false;
    ++i;
    if (i < t.size() && (t[i] == L'-' || t[i] == L'\'' || t[i] == L'^')) ++i;
    if (i >= t.size()) return false;
    for (; i < t.size(); ++i)
        if (std::wcschr(L")(]D[PpOo3/\\|*$S@", t[i]) == nullptr) return false;
    return true;
}

// "[DS]", "[LFG]", "[Ab1]": a guild tag or a tag in brackets – kept whole, so neither the brackets nor the letters
// inside are translated or torn apart (a keep-word "DS" alone split "[DS]" and its sentence, 2026-10-27). A name in
// brackets the way the game shows a link ("[Shipwreck Peak Waypoint]", typed as text, became "zum Schiffbruch") is
// kept whole too: words, spaces, ' - : , . inside, up to 60 characters.
std::vector<Span> FindTags(const std::wstring& body) {
    std::vector<Span> out;
    const auto inside = [](wchar_t c) { return IsWordChar(c) || std::wcschr(L" '’-:,.", c) != nullptr; };
    for (size_t i = 0; i < body.size(); ++i) {
        if (body[i] != L'[') continue;
        size_t j = i + 1;
        while (j < body.size() && j - i <= 61 && inside(body[j])) ++j;
        const bool letter = std::any_of(body.begin() + static_cast<std::ptrdiff_t>(i + 1),
                                        body.begin() + static_cast<std::ptrdiff_t>(j), IsWordChar);
        if (j < body.size() && body[j] == L']' && letter && body[i + 1] != L' ' && body[j - 1] != L' ') {
            out.push_back({i, j + 1 - i});
            i = j;
        }
    }
    return out;
}

std::vector<Span> FindEmoticons(const std::wstring& body) {
    std::vector<Span> out;
    size_t i = 0;
    while (i < body.size()) {
        while (i < body.size() && body[i] == L' ') ++i;
        size_t j = i;
        while (j < body.size() && body[j] != L' ') ++j;
        if (j > i && IsEmoticon(body.substr(i, j - i))) out.push_back({i, j - i});
        i = j;
    }
    return out;
}
}  // namespace

std::wstring ApplyGlossary(const std::wstring& body, const Glossary* glossary, const WordSet* keepWords,
                           const NameList* names) {
    std::wstring out;
    for (const Segment& s : ProtectForTranslation(body, glossary, keepWords, names).segments) out += s.text;
    return out;
}

ProtectedText ProtectForTranslation(const std::wstring& body, const Glossary* glossary, const WordSet* keepWords,
                                    const NameList* names) {
    ProtectedText r;
    std::vector<Protected> prot;
    std::vector<Span> taken;

    for (const Span& s : FindChatCodes(body)) {
        prot.push_back({s, body.substr(s.start, s.length)});
        taken.push_back(s);
    }
    for (const Span& s : FindLinks(body)) {  // a translated link leads nowhere
        if (std::any_of(taken.begin(), taken.end(), [&](const Span& t) { return t.Overlaps(s); })) continue;
        prot.push_back({s, body.substr(s.start, s.length)});
        taken.push_back(s);
    }
    for (const Span& s : FindTags(body)) {
        if (std::any_of(taken.begin(), taken.end(), [&](const Span& t) { return t.Overlaps(s); })) continue;
        prot.push_back({s, body.substr(s.start, s.length)});
        taken.push_back(s);
    }

    if (glossary) {
        r.glossaryHits = glossary->FindAll(body, taken);
        for (const GlossaryMatch& m : r.glossaryHits) {
            prot.push_back({m.span, m.target});
            taken.push_back(m.span);
        }
    }

    for (const Span& e : FindEmoticons(body)) {
        if (std::any_of(taken.begin(), taken.end(), [&](const Span& t) { return t.Overlaps(e); })) continue;
        prot.push_back({e, body.substr(e.start, e.length)});
        taken.push_back(e);
    }

    if (names) {
        for (const Span& n : names->Find(body)) {
            if (std::any_of(taken.begin(), taken.end(), [&](const Span& t) { return t.Overlaps(n); })) continue;
            prot.push_back({n, body.substr(n.start, n.length)});
            taken.push_back(n);
        }
    }

    if (keepWords && !keepWords->empty()) {
        const std::vector<Span> words = WordSpans(body);
        const auto free = [&](const Span& s) {
            return std::none_of(taken.begin(), taken.end(), [&](const Span& t) { return t.Overlaps(s); });
        };
        for (size_t i = 0; i < words.size(); ++i) {
            // A term of several words first, the longest ("Wizard's Vault" – "Vault" alone made it "Assistent"):
            // the words in a row, only spaces or a hyphen between them.
            size_t last = i;
            std::wstring key = PhraseWordKey(body.substr(words[i].start, words[i].length));
            for (size_t j = i + 1; j < words.size() && j < i + kMaxTermWords; ++j) {
                const std::wstring gap = body.substr(words[j - 1].end(), words[j].start - words[j - 1].end());
                if (gap.empty() || gap.find_first_not_of(L" -") != std::wstring::npos) break;
                key += L' ' + PhraseWordKey(body.substr(words[j].start, words[j].length));
                if (keepWords->count(key)) last = j;
            }
            const Span phrase{words[i].start, words[last].end() - words[i].start};
            if (last > i && free(phrase)) {
                prot.push_back({phrase, body.substr(phrase.start, phrase.length)});
                taken.push_back(phrase);
                i = last;
                continue;
            }
            if (free(words[i]) && IsKeepWord(*keepWords, body.substr(words[i].start, words[i].length)))
                prot.push_back({words[i], body.substr(words[i].start, words[i].length)});
        }
    }

    std::sort(prot.begin(), prot.end(), [](const Protected& a, const Protected& b) { return a.span.start < b.span.start; });

    size_t pos = 0;
    for (const Protected& p : prot) {
        if (p.span.start > pos) r.segments.push_back({body.substr(pos, p.span.start - pos), false});
        r.segments.push_back({p.replacement, true});
        pos = p.span.end();
    }
    if (pos < body.size()) r.segments.push_back({body.substr(pos), false});
    return r;
}

ProtectedText ProtectCodesOnly(const std::wstring& body) {
    std::vector<Span> spans = FindChatCodes(body);
    for (const Span& s : FindLinks(body))
        if (std::none_of(spans.begin(), spans.end(), [&](const Span& t) { return t.Overlaps(s); })) spans.push_back(s);
    std::sort(spans.begin(), spans.end(), [](const Span& a, const Span& b) { return a.start < b.start; });
    ProtectedText r;
    size_t pos = 0;
    for (const Span& s : spans) {
        if (s.start > pos) r.segments.push_back({body.substr(pos, s.start - pos), false});
        r.segments.push_back({body.substr(s.start, s.length), true});
        pos = s.end();
    }
    if (pos < body.size()) r.segments.push_back({body.substr(pos), false});
    return r;
}

bool HasTranslatableText(const std::vector<Segment>& segments) {
    for (const Segment& s : segments)
        if (!s.keep && std::any_of(s.text.begin(), s.text.end(), IsWordChar)) return true;
    return false;
}

std::wstring JoinSegments(const std::vector<Segment>& segments) {
    std::wstring out;
    for (const Segment& s : segments) out += s.text;
    return out;
}

bool HasProtected(const std::vector<Segment>& segments) {
    return std::any_of(segments.begin(), segments.end(), [](const Segment& s) { return s.keep; });
}

}  // namespace gct
