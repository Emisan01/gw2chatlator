// local_mt_protocol.cpp — portable local translator helper protocol.
#include "local_mt_protocol.hpp"

#include <algorithm>
#include <cctype>
#include <cwctype>

#ifdef _WIN32
#include <windows.h>
#endif

#include "json.hpp"
#include "langs.hpp"
#include "text.hpp"

namespace gct {
namespace {

std::string Lower(std::string value) {
    for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

std::string Code(const std::wstring& value) {
    return Lower(ToUtf8(PrimaryLang(value)));
}

bool IsWordCodepoint(uint32_t cp) {
    if (cp < 0x80) return std::isalnum(static_cast<unsigned char>(cp)) != 0;
#ifdef _WIN32
    if (cp <= 0xFFFF && !(cp >= 0xD800 && cp <= 0xDFFF)) {
        const wchar_t ch = static_cast<wchar_t>(cp);
        WORD type = 0;
        return GetStringTypeW(CT_CTYPE1, &ch, 1, &type) && (type & (C1_ALPHA | C1_DIGIT)) != 0;
    }
    // Supplementary script letters are represented by surrogate pairs on Windows. The decorative-symbol
    // filter below excludes emoji, musical notation, and dingbats; remaining supplementary code points are
    // meaningful script characters for this non-empty-output check.
    return cp > 0xFFFF;
#else
    // Locale-independent fallback for portable core builds. Production runs on Windows and uses Unicode
    // character types above; these ranges cover the scripts commonly used in chat.
    return (cp >= 0x00C0 && cp <= 0x02FF) || (cp >= 0x0370 && cp <= 0x052F) ||
           (cp >= 0x0531 && cp <= 0x0588) || (cp >= 0x05D0 && cp <= 0x05EA) ||
           (cp >= 0x0620 && cp <= 0x06D3) || (cp >= 0x0900 && cp <= 0x1FFF) ||
           (cp >= 0x2C00 && cp <= 0x2DFF) || (cp >= 0x3041 && cp <= 0x30FF) ||
           (cp >= 0x3100 && cp <= 0x312F) || (cp >= 0x3130 && cp <= 0x318F) ||
           (cp >= 0x3400 && cp <= 0x9FFF) || (cp >= 0xA000 && cp <= 0xA7FF) ||
           (cp >= 0xAC00 && cp <= 0xD7AF) || (cp >= 0xF900 && cp <= 0xFAFF) || cp > 0xFFFF;
#endif
}

}  // namespace

const std::vector<LocalMtModelInfo>& LocalMtModelCatalog() {
    static const std::vector<LocalMtModelInfo> models = {
        {"de-en", "Helsinki-NLP/opus-mt-de-en", "", "Apache-2.0"},
        {"en-de", "Helsinki-NLP/opus-mt-en-de", "", "CC-BY-4.0"},
        {"fr-en", "Helsinki-NLP/opus-mt-fr-en", "", "Apache-2.0"},
        {"en-fr", "Helsinki-NLP/opus-mt-en-fr", "", "Apache-2.0"},
        {"es-en", "Helsinki-NLP/opus-mt-es-en", "", "Apache-2.0"},
        {"en-es", "Helsinki-NLP/opus-mt-en-es", "", "Apache-2.0"},
        {"it-en", "Helsinki-NLP/opus-mt-it-en", "", "Apache-2.0"},
        {"en-it", "Helsinki-NLP/opus-mt-en-it", "", "Apache-2.0"},
        {"pl-en", "Helsinki-NLP/opus-mt-pl-en", "", "Apache-2.0"},
        {"en-pl", "Helsinki-NLP/opus-mt-en-zlw", ">>pol<<", "Apache-2.0"},
        {"pt-en", "Helsinki-NLP/opus-mt-ROMANCE-en", "", "Apache-2.0"},
        {"en-pt", "Helsinki-NLP/opus-mt-en-ROMANCE", ">>pt<<", "Apache-2.0"},
        {"tr-en", "Helsinki-NLP/opus-mt-tr-en", "", "Apache-2.0"},
        {"en-tr", "Helsinki-NLP/opus-mt-en-trk", ">>tur<<", "Apache-2.0"},
        {"ru-en", "Helsinki-NLP/opus-mt-ru-en", "", "CC-BY-4.0"},
        {"en-ru", "Helsinki-NLP/opus-mt-en-ru", "", "Apache-2.0"},
    };
    return models;
}

std::string BuildLocalMtRequest(int id, const std::string& source, const std::string& target,
                                const std::string& utf8Text, int beam) {
    std::string req = "{\"cmd\":\"translate\",\"id\":" + std::to_string(id) + ",\"src\":\"" + JsonEscape(Lower(source)) +
           "\",\"tgt\":\"" + JsonEscape(Lower(target)) + "\",\"text\":\"" + JsonEscape(utf8Text) + "\"";
    if (beam > 0) req += ",\"beam\":" + std::to_string(beam);
    req += "}";
    return req;
}

bool ParseLocalMtRequest(const std::string& line, LocalMtRequest& out) {
    out = {};
    JsonValue json;
    if (!ParseJson(line, json) || !json.IsObject() || json.GetString("cmd") != "translate") return false;
    const JsonValue* id = json.Get("id");
    const JsonValue* source = json.Get("src");
    const JsonValue* target = json.Get("tgt");
    const JsonValue* text = json.Get("text");
    if (!id || id->type != JsonValue::Type::Number || id->n < 0 || id->n > 2147483647.0 ||
        !source || source->type != JsonValue::Type::String || !target || target->type != JsonValue::Type::String ||
        !text || text->type != JsonValue::Type::String)
        return false;
    out.id = static_cast<int>(id->n);
    out.source = source->s;
    out.target = target->s;
    out.text = text->s;
    const JsonValue* beam = json.Get("beam");
    if (beam && beam->type == JsonValue::Type::Number && beam->n >= 1 && beam->n <= 8) {
        out.beam = static_cast<int>(beam->n);
    }
    return true;
}

bool ParseLocalMtMessage(const std::string& line, LocalMtMessage& out) {
    out = {};
    JsonValue json;
    if (!ParseJson(line, json) || !json.IsObject()) return false;
    const std::string event = json.GetString("evt");
    if (event == "ready") {
        out.kind = LocalMtMessage::Kind::Ready;
        return true;
    }
    if (event == "loaded") {
        const JsonValue* ms = json.Get("ms");
        const JsonValue* pair = json.Get("pair");
        if (!pair || pair->type != JsonValue::Type::String || !ms || ms->type != JsonValue::Type::Number || ms->n < 0)
            return false;
        out.kind = LocalMtMessage::Kind::Loaded;
        out.pair = pair->s;
        out.milliseconds = static_cast<int64_t>(ms->n);
        return true;
    }
    if (event != "result" && event != "error") return false;
    const JsonValue* id = json.Get("id");
    if (!id || id->type != JsonValue::Type::Number || id->n < 0 || id->n > 2147483647.0) return false;
    out.id = static_cast<int>(id->n);
    if (event == "result") {
        const JsonValue* text = json.Get("text");
        if (!text || text->type != JsonValue::Type::String) return false;
        out.kind = LocalMtMessage::Kind::Result;
        out.text = text->s;
        return true;
    }
    const JsonValue* message = json.Get("msg");
    if (!message || message->type != JsonValue::Type::String) return false;
    out.kind = LocalMtMessage::Kind::Error;
    out.error = message->s;
    return true;
}

std::string NllbLanguageTag(const std::string& langCode) {
    const std::string code = Lower(langCode);
    if (code == "de" || code == "de-de") return "deu_Latn";
    if (code == "en" || code == "en-us" || code == "en-gb") return "eng_Latn";
    if (code == "fr" || code == "fr-fr") return "fra_Latn";
    if (code == "es" || code == "es-es") return "spa_Latn";
    if (code == "it" || code == "it-it") return "ita_Latn";
    if (code == "pl") return "pol_Latn";
    if (code == "pt" || code == "pt-pt" || code == "pt-br") return "por_Latn";
    if (code == "ru") return "rus_Cyrl";
    if (code == "tr") return "tur_Latn";
    if (code == "uk" || code == "ukr") return "ukr_Cyrl";
    if (code == "nl") return "nld_Latn";
    if (code == "hu") return "hun_Latn";
    if (code == "nb" || code == "no") return "nob_Latn";
    if (code == "da") return "dan_Latn";
    if (code == "sv") return "swe_Latn";
    if (code == "cs") return "ces_Latn";
    if (code == "ro") return "ron_Latn";
    if (code == "el") return "ell_Grek";
    if (code == "ar") return "ara_Arab";
    if (code == "he") return "heb_Hebr";
    if (code == "ko") return "kor_Hang";
    if (code == "ja") return "jpn_Jpan";
    if (code == "zh" || code == "zh-hans" || code == "zh-cn") return "zho_Hans";
    if (code == "zh-hant" || code == "zh-tw") return "zho_Hant";
    if (code == "fi") return "fin_Latn";
    if (code == "bg") return "bul_Cyrl";
    if (code == "hr") return "hrv_Latn";
    if (code == "sk") return "slk_Latn";
    if (code == "sl") return "slv_Latn";
    if (code == "et") return "est_Latn";
    if (code == "lv") return "lvs_Latn";
    if (code == "lt") return "lit_Latn";
    if (code == "id") return "ind_Latn";
    if (code == "ms") return "zsm_Latn";
    if (code == "vi") return "vie_Latn";
    if (code == "th") return "tha_Thai";
    if (code == "hi") return "hin_Deva";
    if (code.size() >= 8 && code[3] == '_') return code;
    return {};
}

std::vector<std::string> LocalMtRoute(const std::wstring& source, const std::wstring& target,
                                      const std::vector<std::string>& installedPairs) {
    const std::string src = Code(source), tgt = Code(target);
    if (src.empty() || tgt.empty() || src == tgt) return {};
    auto has = [&](const std::string& pair) {
        return std::find(installedPairs.begin(), installedPairs.end(), pair) != installedPairs.end();
    };
    const std::string direct = src + "-" + tgt;
    if (has(direct)) return {direct};
    if (has("nllb-200") && !NllbLanguageTag(src).empty() && !NllbLanguageTag(tgt).empty()) {
        return {direct};
    }
    if (src == "en" || tgt == "en") return {};
    const std::string first = src + "-en", second = "en-" + tgt;
    return has(first) && has(second) ? std::vector<std::string>{first, second} : std::vector<std::string>{};
}

std::wstring CleanLocalMtOutput(const std::wstring& source, const std::wstring& output) {
    std::wstring cleaned;
    cleaned.reserve(output.size());
    for (size_t i = 0; i < output.size();) {
        const size_t start = i;
        uint32_t cp = static_cast<uint32_t>(output[i++]);
        if (cp >= 0xD800 && cp <= 0xDBFF && i < output.size()) {
            const uint32_t low = static_cast<uint32_t>(output[i]);
            if (low >= 0xDC00 && low <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                ++i;
            }
        }
        const bool musicalArtifact = (cp >= 0x266A && cp <= 0x266F) || (cp >= 0x1D100 && cp <= 0x1D1FF);
        if (musicalArtifact && source.find(output.substr(start, i - start)) == std::wstring::npos) continue;
        cleaned.append(output, start, i - start);
    }
    const auto whitespace = [](wchar_t c) {
        return std::iswspace(static_cast<wint_t>(c)) != 0 || c == L'\u00A0';
    };
    while (!cleaned.empty() && whitespace(cleaned.front())) cleaned.erase(cleaned.begin());
    while (!cleaned.empty() && whitespace(cleaned.back())) cleaned.pop_back();
    return cleaned;
}

bool IsPlausibleLocalMtOutput(const std::wstring& source, const std::wstring& output) {
    if (output.empty()) return false;
    bool hasWord = false;
    for (size_t i = 0; i < output.size(); ++i) {
        uint32_t cp = static_cast<uint32_t>(output[i]);
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < output.size()) {
            const uint32_t low = static_cast<uint32_t>(output[i + 1]);
            if (low >= 0xDC00 && low <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                ++i;
            }
        }
        if (IsWordCodepoint(cp)) hasWord = true;
        const bool decorative = (cp >= 0x2600 && cp <= 0x27BF) || (cp >= 0x1F300 && cp <= 0x1FAFF) ||
                                (cp >= 0x1D100 && cp <= 0x1D1FF);
        if (!decorative) continue;
        std::wstring symbol;
        if (cp <= 0xFFFF) symbol.push_back(static_cast<wchar_t>(cp));
        else {
            const uint32_t u = cp - 0x10000;
            symbol.push_back(static_cast<wchar_t>(0xD800 + (u >> 10)));
            symbol.push_back(static_cast<wchar_t>(0xDC00 + (u & 0x3FF)));
        }
        if (source.find(symbol) == std::wstring::npos) return false;
    }
    return hasWord;
}

namespace {
bool IsDecorativeCodepoint(uint32_t cp) {
    return (cp >= 0x2190 && cp <= 0x2BFF) ||    // arrows, math operators (⊱⊰), technical, boxes, shapes, symbols, dingbats
           (cp >= 0x1F000 && cp <= 0x1FAFF) ||  // emoji, cards, enclosed supplements
           (cp >= 0x1D100 && cp <= 0x1D1FF) ||  // musical notation
           (cp >= 0xE000 && cp <= 0xF8FF) ||    // private use
           (cp >= 0xE0000 && cp <= 0xE007F);    // tags (flags)
}
bool IsJoiner(uint32_t cp) { return cp == 0x200D || (cp >= 0xFE00 && cp <= 0xFE0F); }
bool IsSpace(wchar_t c) { return std::iswspace(static_cast<wint_t>(c)) != 0 || c == L'\u00A0'; }
}  // namespace

std::vector<Segment> SplitForLocalMt(const std::vector<Segment>& segments) {
    std::vector<Segment> out;
    auto add = [&out](std::wstring text, bool keep) {
        if (text.empty()) return;
        if (!out.empty() && out.back().keep == keep && keep) out.back().text += text;
        else out.push_back({std::move(text), keep});
    };
    auto addText = [&](const std::wstring& run) {  // spaces around it and a run without a word stay as they are
        size_t b = 0, e = run.size();
        while (b < e && IsSpace(run[b])) ++b;
        while (e > b && IsSpace(run[e - 1])) --e;
        std::wstring core = run.substr(b, e - b);
        bool word = false;
        for (size_t i = 0; i < core.size() && !word; ++i) {
            uint32_t cp = static_cast<uint32_t>(core[i]);
            if (cp >= 0xD800 && cp <= 0xDBFF) continue;
            word = IsWordCodepoint(cp);
        }
        add(run.substr(0, b), true);
        add(core, !word);
        add(run.substr(e), true);
    };
    for (const Segment& segment : segments) {
        if (segment.keep) {
            add(segment.text, true);
            continue;
        }
        const std::wstring& s = segment.text;
        std::wstring text;
        bool prevDecorative = false;
        for (size_t i = 0; i < s.size();) {
            const size_t start = i;
            uint32_t cp = static_cast<uint32_t>(s[i++]);
            if (cp >= 0xD800 && cp <= 0xDBFF && i < s.size()) {
                const uint32_t low = static_cast<uint32_t>(s[i]);
                if (low >= 0xDC00 && low <= 0xDFFF) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    ++i;
                }
            }
            const bool decorative = IsDecorativeCodepoint(cp) || (prevDecorative && IsJoiner(cp));
            if (decorative) {
                if (!text.empty()) {
                    addText(text);
                    text.clear();
                }
                add(s.substr(start, i - start), true);
            } else {
                text.append(s, start, i - start);
            }
            prevDecorative = decorative;
        }
        if (!text.empty()) addText(text);
    }
    return out;
}

}  // namespace gct
