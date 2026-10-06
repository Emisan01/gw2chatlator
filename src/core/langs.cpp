// langs.cpp
#include "langs.hpp"

#include <algorithm>

#include "text.hpp"

namespace gct {

std::wstring PrimaryLang(const std::wstring& code) {
    const std::wstring t = Trim(code);
    const size_t dash = t.find_first_of(L"-_");
    return ToUpperAscii(dash == std::wstring::npos ? t : t.substr(0, dash));
}

std::string Gw2ApiLang(const std::wstring& deeplCode) {
    const std::wstring p = PrimaryLang(deeplCode);
    if (p == L"EN") return "en";
    if (p == L"DE") return "de";
    if (p == L"FR") return "fr";
    if (p == L"ES") return "es";
    if (p == L"ZH") return "zh";
    return "";
}

std::vector<std::wstring> SpellTagCandidates(const std::wstring& lang, const std::wstring& userLocale) {
    std::vector<std::wstring> out;
    auto add = [&](const std::wstring& tag) {
        if (!tag.empty() && std::find(out.begin(), out.end(), tag) == out.end()) out.push_back(tag);
    };

    const std::wstring primary = ToLowerAscii(PrimaryLang(lang));
    if (primary.empty()) return out;

    // "EN-GB" -> "en-GB" (only for real regions, not script codes like ZH-HANS)
    const std::wstring t = Trim(lang);
    const size_t dash = t.find_first_of(L"-_");
    if (dash != std::wstring::npos && t.size() - dash - 1 == 2)
        add(primary + L"-" + ToUpperAscii(t.substr(dash + 1)));

    // The user's own locale if it is the same language ("de-AT" for an Austrian setup).
    if (ToLowerAscii(PrimaryLang(userLocale)) == primary) add(Trim(userLocale));

    static const std::pair<const wchar_t*, const wchar_t*> defaults[] = {
        {L"de", L"de-DE"}, {L"en", L"en-US"}, {L"fr", L"fr-FR"}, {L"es", L"es-ES"}, {L"it", L"it-IT"},
        {L"pt", L"pt-BR"}, {L"nl", L"nl-NL"}, {L"pl", L"pl-PL"}, {L"ru", L"ru-RU"}, {L"sv", L"sv-SE"},
        {L"da", L"da-DK"}, {L"fi", L"fi-FI"}, {L"nb", L"nb-NO"}, {L"cs", L"cs-CZ"}, {L"tr", L"tr-TR"}};
    for (const auto& [p, tag] : defaults)
        if (primary == p) add(tag);
    if (primary == L"de") {
        add(L"de-AT");
        add(L"de-CH");
    }
    if (primary == L"en") add(L"en-GB");
    add(primary);
    return out;
}

}  // namespace gct
