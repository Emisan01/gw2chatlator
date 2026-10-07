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
        for (const Span& w : WordSpans(body)) {
            if (std::any_of(taken.begin(), taken.end(), [&](const Span& t) { return t.Overlaps(w); })) continue;
            if (IsKeepWord(*keepWords, body.substr(w.start, w.length)))
                prot.push_back({w, body.substr(w.start, w.length)});
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

std::wstring JoinSegments(const std::vector<Segment>& segments) {
    std::wstring out;
    for (const Segment& s : segments) out += s.text;
    return out;
}

bool HasProtected(const std::vector<Segment>& segments) {
    return std::any_of(segments.begin(), segments.end(), [](const Segment& s) { return s.keep; });
}

}  // namespace gct
