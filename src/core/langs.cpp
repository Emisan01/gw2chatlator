// langs.cpp
#include "langs.hpp"

#include <algorithm>
#include <cstdint>
#include <cwchar>
#include <iterator>

#include "text.hpp"

namespace gct {

std::wstring GuessLanguageByLetters(const std::wstring& text) {
    // Scripts first: one script, one language (most likely in GW2 chat).
    int cyr = 0, ukr = 0, greek = 0, arabic = 0, hebrew = 0, hangul = 0, kana = 0, han = 0, thai = 0;
    struct Marker {
        const wchar_t* letters;
        const wchar_t* lang;
    };
    static const Marker markers[] = {
        {L"ığşİĞŞ", L"TR"},                              // ı ğ ş İ Ğ Ş
        {L"ñÑ¿¡", L"ES"},                                          // ñ ¿ ¡
        {L"ąęłżźńśĄĘŁŻŹ", L"PL"},  // ą ę ł ż ź ń ś
        {L"ßäöüÄÖÜ", L"DE"},                        // ß ä ö ü
        {L"çœêèàâîûÇ", L"FR"},             // ç œ ê è à â î û
        {L"ãõÃÕ", L"PT"},                                          // ã õ
        {L"őűŐŰ", L"HU"},                                          // ő ű
        {L"åøæÅØÆ", L"NB"},                              // å ø æ
        {L"ěřůčžĚŘŮ", L"CS"},                  // ě ř ů č ž
        {L"ățșĂȚȘ", L"RO"},                              // ă ț ș
    };
    int counts[std::size(markers)] = {};
    for (wchar_t c : text) {
        const uint32_t u = static_cast<uint32_t>(c);
        if (u >= 0x0400 && u <= 0x04FF) {
            ++cyr;
            if (u == 0x0456 || u == 0x0457 || u == 0x0454 || u == 0x0491) ++ukr;  // і ї є ґ
        } else if (u >= 0x0370 && u <= 0x03FF) ++greek;
        else if (u >= 0x0600 && u <= 0x06FF) ++arabic;
        else if (u >= 0x0590 && u <= 0x05FF) ++hebrew;
        else if (u >= 0xAC00 && u <= 0xD7AF) ++hangul;
        else if (u >= 0x3040 && u <= 0x30FF) ++kana;
        else if (u >= 0x4E00 && u <= 0x9FFF) ++han;
        else if (u >= 0x0E00 && u <= 0x0E7F) ++thai;
        else
            for (size_t m = 0; m < std::size(markers); ++m)
                if (std::wcschr(markers[m].letters, c)) ++counts[m];
    }
    if (hangul) return L"KO";
    if (kana) return L"JA";  // Japanese mixes Kanji in; Kana decides
    if (han) return L"ZH";
    if (cyr) return ukr ? L"UK" : L"RU";
    if (arabic) return L"AR";
    if (hebrew) return L"HE";
    if (greek) return L"EL";
    if (thai) return L"TH";
    size_t best = std::size(markers);
    for (size_t m = 0; m < std::size(markers); ++m)
        if (counts[m] > 0 && (best == std::size(markers) || counts[m] > counts[best])) best = m;
    return best == std::size(markers) ? std::wstring() : markers[best].lang;
}

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
