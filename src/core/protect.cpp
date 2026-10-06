// protect.cpp
#include "protect.hpp"

#include <algorithm>

namespace gct {

namespace {
struct Protected {
    Span span;
    std::wstring replacement;
};
}  // namespace

ProtectedText ProtectForTranslation(const std::wstring& body, const Glossary* glossary, const WordSet* keepWords) {
    ProtectedText r;
    std::vector<Protected> prot;
    std::vector<Span> taken;

    for (const Span& s : FindChatCodes(body)) {
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

    if (keepWords && !keepWords->empty()) {
        for (const Span& w : WordSpans(body)) {
            if (std::any_of(taken.begin(), taken.end(), [&](const Span& t) { return t.Overlaps(w); })) continue;
            if (keepWords->count(CaseFold(body.substr(w.start, w.length))))
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
