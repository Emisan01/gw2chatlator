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
    // Chat is casual: no style nitpicks, but keep spelling and grammar.
    body += "&disabledCategories=" + UrlEncode("STYLE,REDUNDANCY,TYPOGRAPHY,CASING");
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

bool RateLimiter::Allow(unsigned long long nowMs) {
    sent_.erase(std::remove_if(sent_.begin(), sent_.end(), [&](unsigned long long t) { return nowMs - t >= 60000; }),
                sent_.end());
    if (static_cast<int>(sent_.size()) >= perMinute_) return false;
    sent_.push_back(nowMs);
    return true;
}

}  // namespace gct
