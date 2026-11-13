#include "core/languagetool_protocol.hpp"

#include <algorithm>

#include "core/json.hpp"
#include "core/mymemory_protocol.hpp"  // UrlEncode

namespace gct {
namespace {

// Index into `text` (wchar_t units) for a UTF-16 offset.
size_t FromUtf16Offset(const std::wstring& text, size_t utf16) {
    if constexpr (sizeof(wchar_t) == 2) return std::min(utf16, text.size());
    size_t units = 0, i = 0;
    while (i < text.size() && units < utf16) {
        units += static_cast<unsigned long>(text[i]) >= 0x10000 ? 2 : 1;
        ++i;
    }
    return i;
}

}  // namespace

std::wstring LanguageToolLang(const std::wstring& in) {
    std::wstring c = Trim(in);
    if (c.empty()) return L"auto";
    for (auto& ch : c)
        if (ch == L'_') ch = L'-';
    const size_t dash = c.find(L'-');
    std::wstring primary = ToLowerAscii(c.substr(0, dash));
    std::wstring region = dash == std::wstring::npos ? L"" : ToUpperAscii(c.substr(dash + 1));
    if (primary == L"zh") return L"zh-CN";
    if (region == L"HANS" || region == L"HANT") region.clear();
    if (region.empty()) {
        // LanguageTool wants a variant for these to check spelling.
        if (primary == L"de") return L"de-DE";
        if (primary == L"en") return L"en-US";
        if (primary == L"pt") return L"pt-PT";
        return primary;
    }
    return primary + L"-" + region;
}

std::string BuildLanguageToolForm(const std::wstring& text, const std::wstring& lang, const std::wstring& motherTongue) {
    std::string body = "text=" + UrlEncode(ToUtf8(text)) + "&language=" + UrlEncode(ToUtf8(LanguageToolLang(lang)));
    if (!Trim(motherTongue).empty() && LanguageToolLang(lang) == L"auto")
        body += "&preferredVariants=" + UrlEncode(ToUtf8(LanguageToolLang(motherTongue)));
    // Chat is casual: no style nitpicks, keep spelling and grammar.
    body += "&disabledCategories=" + UrlEncode("STYLE,REDUNDANCY,TYPOGRAPHY");
    return body;
}

LtResult ParseLanguageToolResponse(const std::string& body, const std::wstring& text) {
    LtResult r;
    JsonValue root;
    if (!ParseJson(body, root) || !root.IsObject()) {
        const std::string trimmed = body.substr(0, std::min<size_t>(body.size(), 160));
        r.error = trimmed.empty() ? L"empty answer" : FromUtf8(trimmed);
        return r;
    }
    if (const JsonValue* lang = root.Get("language")) r.language = FromUtf8(lang->GetString("code"));
    const JsonValue* matches = root.Get("matches");
    if (!matches || !matches->IsArray()) {
        r.error = L"unexpected answer";
        return r;
    }
    for (const JsonValue& m : matches->arr) {
        LtMatch x;
        const JsonValue* off = m.Get("offset");
        const JsonValue* len = m.Get("length");
        if (!off || !len || off->type != JsonValue::Type::Number || len->type != JsonValue::Type::Number) continue;
        const size_t start = FromUtf16Offset(text, static_cast<size_t>(off->n));
        const size_t end = FromUtf16Offset(text, static_cast<size_t>(off->n + len->n));
        if (end <= start || end > text.size()) continue;
        x.span = {start, end - start};
        x.message = FromUtf8(m.GetString("message"));
        x.shortMessage = FromUtf8(m.GetString("shortMessage"));
        if (const JsonValue* reps = m.Get("replacements"); reps && reps->IsArray())
            for (const JsonValue& rep : reps->arr) {
                const std::wstring v = FromUtf8(rep.GetString("value"));
                if (!v.empty() && x.replacements.size() < 8) x.replacements.push_back(v);
            }
        if (const JsonValue* rule = m.Get("rule")) {
            x.ruleId = FromUtf8(rule->GetString("id"));
            x.issueType = FromUtf8(rule->GetString("issueType"));
            if (const JsonValue* cat = rule->Get("category")) {
                x.categoryId = FromUtf8(cat->GetString("id"));
            }
        }
        r.matches.push_back(std::move(x));
    }
    r.ok = true;
    return r;
}

std::wstring NormalizeLanguageToolUrl(const std::wstring& in) {
    std::wstring u = Trim(in);
    if (u.empty()) u = L"https://api.languagetool.org";
    while (!u.empty() && u.back() == L'/') u.pop_back();
    if (u.size() >= 9 && u.compare(u.size() - 9, 9, L"/v2/check") == 0) return u;
    if (u.size() >= 3 && u.compare(u.size() - 3, 3, L"/v2") == 0) return u + L"/check";
    return u + L"/v2/check";
}

bool CanKeepGrammarMarks(const std::wstring& checkedText, const std::wstring& newText) {
    if (checkedText.empty() || newText.size() < checkedText.size()) return false;
    return newText.compare(0, checkedText.size(), checkedText) == 0;
}

std::vector<WordMark> MergeGrammarMarks(const std::vector<WordMark>& spellMarks,
                                       const std::wstring& text,
                                       const std::wstring& cachedText,
                                       const std::vector<WordMark>& cachedMarks) {
    std::vector<WordMark> merged = spellMarks;
    if (!CanKeepGrammarMarks(cachedText, text)) return merged;

    for (const auto& gm : cachedMarks) {
        if (gm.span.end() > cachedText.size() || gm.span.end() > text.size()) continue;
        if (text.compare(gm.span.start, gm.span.length, cachedText, gm.span.start, gm.span.length) != 0) continue;

        auto it = std::find_if(merged.begin(), merged.end(), [&](const WordMark& sm) {
            return sm.span.Overlaps(gm.span);
        });
        if (it != merged.end()) {
            if (gm.isGrammar) {
                *it = gm;
            }
        } else {
            merged.push_back(gm);
        }
    }
    std::sort(merged.begin(), merged.end(), [](const WordMark& a, const WordMark& b) {
        return a.span.start < b.span.start;
    });
    return merged;
}

LtApplyResult ApplyLanguageToolMatches(const std::wstring& currentText,
                                      const std::wstring& checkedText,
                                      const std::vector<LtMatch>& matches,
                                      AutoCorrectMode mode,
                                      const std::set<std::wstring>& revertedWords,
                                      const std::set<std::wstring>& revertedRules) {
    LtApplyResult result;
    result.text = currentText;
    if (mode == AutoCorrectMode::Off || mode == AutoCorrectMode::Safe || mode == AutoCorrectMode::Phone) {
        return result;
    }
    if (!CanKeepGrammarMarks(checkedText, currentText)) return result;

    struct ValidMatch {
        LtMatch m;
        bool shouldReplace = false;
    };
    std::vector<ValidMatch> valid;
    valid.reserve(matches.size());

    for (const auto& m : matches) {
        if (m.span.end() > checkedText.size() || m.span.end() > currentText.size()) continue;
        const std::wstring orig = checkedText.substr(m.span.start, m.span.length);
        if (currentText.compare(m.span.start, m.span.length, orig) != 0) continue;

        bool replace = false;
        if (!m.replacements.empty()) {
            const std::wstring folded = CaseFold(orig);
            const bool wordReverted = revertedWords.find(folded) != revertedWords.end();
            const bool ruleReverted = !m.ruleId.empty() && revertedRules.find(m.ruleId) != revertedRules.end();
            if (!wordReverted && !ruleReverted) {
                if (mode == AutoCorrectMode::FallenAngel) {
                    replace = true;
                } else if (mode == AutoCorrectMode::Strong) {
                    replace = m.Spelling() && m.replacements.size() == 1;
                }
            }
        }
        valid.push_back({m, replace});
    }

    struct AppliedChange {
        size_t origStart;
        size_t origLen;
        std::wstring orig;
        std::wstring rep;
        bool isGrammar;
        std::wstring ruleId;
        std::wstring message;
    };
    std::vector<AppliedChange> applied;

    std::vector<ValidMatch> toReplace;
    for (const auto& vm : valid) {
        if (vm.shouldReplace) toReplace.push_back(vm);
    }
    // Sort descending by start position to replace in-place from back to front
    std::sort(toReplace.begin(), toReplace.end(), [](const ValidMatch& a, const ValidMatch& b) {
        return a.m.span.start > b.m.span.start;
    });

    std::wstring newText = currentText;
    for (const auto& vm : toReplace) {
        const size_t start = vm.m.span.start;
        const size_t len = vm.m.span.length;
        const std::wstring orig = checkedText.substr(start, len);
        const std::wstring rep = vm.m.replacements.front();
        if (orig == rep) continue;

        newText.replace(start, len, rep);
        applied.push_back({start, len, orig, rep, vm.m.IsGrammar(), vm.m.ruleId, vm.m.message});
    }

    result.text = newText;
    result.replacedCount = applied.size();

    // Sort ascending by origStart to map positions into newText
    std::sort(applied.begin(), applied.end(), [](const AppliedChange& a, const AppliedChange& b) {
        return a.origStart < b.origStart;
    });

    auto mapPos = [&](size_t origPos) {
        size_t pos = origPos;
        for (const auto& app : applied) {
            if (app.origStart + app.origLen <= origPos) {
                pos += (app.rep.length() - app.origLen);
            }
        }
        return pos;
    };

    // Marks for auto-applied changes (green underline)
    for (const auto& app : applied) {
        WordMark wm;
        wm.span = {mapPos(app.origStart), app.rep.length()};
        wm.fix = app.rep;
        wm.original = app.orig;
        wm.wasReplaced = true;
        wm.isGrammar = app.isGrammar;
        wm.ruleId = app.ruleId;
        wm.message = app.message;
        result.marks.push_back(std::move(wm));
    }

    // Marks for unreplaced items (red/amber underline)
    for (const auto& vm : valid) {
        if (vm.shouldReplace) continue;
        WordMark wm;
        wm.span = {mapPos(vm.m.span.start), vm.m.span.length};
        wm.fix = vm.m.replacements.empty() ? L"" : vm.m.replacements.front();
        wm.wasReplaced = false;
        wm.isGrammar = vm.m.IsGrammar();
        wm.ruleId = vm.m.ruleId;
        wm.message = vm.m.message;
        wm.shortMessage = vm.m.shortMessage;
        result.marks.push_back(std::move(wm));
    }

    std::sort(result.marks.begin(), result.marks.end(), [](const WordMark& a, const WordMark& b) {
        return a.span.start < b.span.start;
    });

    return result;
}

bool RateLimiter::Allow(unsigned long long nowMs) {
    sent_.erase(std::remove_if(sent_.begin(), sent_.end(), [&](unsigned long long t) { return nowMs - t >= 60000; }),
                sent_.end());
    if (static_cast<int>(sent_.size()) >= perMinute_) return false;
    sent_.push_back(nowMs);
    return true;
}

unsigned long long RateLimiter::WaitMs(unsigned long long nowMs) {
    sent_.erase(std::remove_if(sent_.begin(), sent_.end(), [&](unsigned long long t) { return nowMs - t >= 60000; }),
                sent_.end());
    if (static_cast<int>(sent_.size()) < perMinute_) return 0;
    const unsigned long long oldest = sent_.front();
    return (oldest + 60000 > nowMs) ? (oldest + 60000 - nowMs) : 0;
}

}  // namespace gct

