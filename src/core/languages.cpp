// languages.cpp
#include "languages.hpp"

#include "langs.hpp"
#include "text.hpp"

namespace gct {

const std::vector<LangInfo>& Languages() {
    static const std::vector<LangInfo> list = {
        {L"DE", L"Deutsch", L"German", true},
        {L"EN-GB", L"English (UK)", L"English (British)", true},
        {L"EN-US", L"English (US)", L"English (American)", true},
        {L"FR", L"Fran\u00e7ais", L"French", true},
        {L"ES", L"Espa\u00f1ol", L"Spanish", true},
        {L"IT", L"Italiano", L"Italian", true},
        {L"PT-BR", L"Portugu\u00eas (BR)", L"Portuguese (Brazilian)", true},
        {L"PT-PT", L"Portugu\u00eas (PT)", L"Portuguese (European)", true},
        {L"NL", L"Nederlands", L"Dutch", true},
        {L"PL", L"Polski", L"Polish", true},
        {L"CS", L"\u010ce\u0161tina", L"Czech", true},
        {L"SK", L"Sloven\u010dina", L"Slovak", true},
        {L"SL", L"Sloven\u0161\u010dina", L"Slovenian", true},
        {L"HU", L"Magyar", L"Hungarian", true},
        {L"RO", L"Rom\u00e2n\u0103", L"Romanian", true},
        {L"SV", L"Svenska", L"Swedish", true},
        {L"DA", L"Dansk", L"Danish", true},
        {L"NB", L"Norsk", L"Norwegian (Bokm\u00e5l)", true},
        {L"FI", L"Suomi", L"Finnish", true},
        {L"ET", L"Eesti", L"Estonian", true},
        {L"LV", L"Latvie\u0161u", L"Latvian", true},
        {L"LT", L"Lietuvi\u0173", L"Lithuanian", true},
        {L"TR", L"T\u00fcrk\u00e7e", L"Turkish", true},
        {L"ID", L"Bahasa Indonesia", L"Indonesian", true},
        {L"VI", L"Ti\u1ebfng Vi\u1ec7t", L"Vietnamese", true},
        {L"LA", L"Latina", L"Latin", true},
        {L"EL", L"\u0395\u03bb\u03bb\u03b7\u03bd\u03b9\u03ba\u03ac", L"Greek", false},
        {L"BG", L"\u0411\u044a\u043b\u0433\u0430\u0440\u0441\u043a\u0438", L"Bulgarian", false},
        {L"RU", L"\u0420\u0443\u0441\u0441\u043a\u0438\u0439", L"Russian", false},
        {L"UK", L"\u0423\u043a\u0440\u0430\u0457\u043d\u0441\u044c\u043a\u0430", L"Ukrainian", false},
        {L"AR", L"\u0627\u0644\u0639\u0631\u0628\u064a\u0629", L"Arabic (Modern Standard)", false},
        {L"HE", L"\u05e2\u05d1\u05e8\u05d9\u05ea", L"Hebrew", false},
        {L"HI", L"\u0939\u093f\u0928\u094d\u0926\u0940", L"Hindi", false},
        {L"TH", L"\u0e44\u0e17\u0e22", L"Thai", false},
        {L"ZH-HANS", L"\u4e2d\u6587\uff08\u7b80\u4f53\uff09", L"Chinese (Simplified)", false},
        {L"ZH-HANT", L"\u4e2d\u6587\uff08\u7e41\u9ad4\uff09", L"Chinese (Traditional)", false},
        {L"JA", L"\u65e5\u672c\u8a9e", L"Japanese", false},
        {L"KO", L"\ud55c\uad6d\uc5b4", L"Korean", false},
    };
    return list;
}

const LangInfo* FindLanguage(const std::wstring& code) {
    const std::wstring c = ToUpperAscii(Trim(code));
    if (c.empty()) return nullptr;
    for (const LangInfo& l : Languages())
        if (c == l.code) return &l;
    const std::wstring p = PrimaryLang(c);
    // Script/region spellings that are not in the table.
    if (p == L"ZH") return FindLanguage(c.find(L"HANT") != std::wstring::npos || c.find(L"TW") != std::wstring::npos ||
                                                c.find(L"HK") != std::wstring::npos
                                            ? L"ZH-HANT"
                                            : L"ZH-HANS");
    if (p == L"EN") return FindLanguage(c.find(L"US") != std::wstring::npos ? L"EN-US" : L"EN-GB");
    if (p == L"PT") return FindLanguage(c.find(L"BR") != std::wstring::npos ? L"PT-BR" : L"PT-PT");
    if (p == L"NO" || p == L"NN") return FindLanguage(L"NB");
    if (p == L"IW") return FindLanguage(L"HE");
    for (const LangInfo& l : Languages())
        if (p == PrimaryLang(l.code)) return &l;
    return nullptr;
}

std::wstring LanguageLabel(const std::wstring& code) {
    const LangInfo* l = FindLanguage(code);
    return l ? l->native : ToUpperAscii(code);
}

std::wstring LanguageEnglishName(const std::wstring& code) {
    const LangInfo* l = FindLanguage(code);
    return l ? l->english : ToUpperAscii(code);
}

std::wstring SourceCode(const std::wstring& code) { return PrimaryLang(code); }

}  // namespace gct
