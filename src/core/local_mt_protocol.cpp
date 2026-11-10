// local_mt_protocol.cpp — portable local translator helper protocol.
#include "local_mt_protocol.hpp"

#include <algorithm>
#include <cctype>
#include <cwchar>
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
                                const std::string& utf8Text, int beam,
                                const std::string& model, const LocalMtDecode& decode) {
    std::string req = "{\"cmd\":\"translate\",\"id\":" + std::to_string(id) + ",\"src\":\"" + JsonEscape(Lower(source)) +
           "\",\"tgt\":\"" + JsonEscape(Lower(target)) + "\",\"text\":\"" + JsonEscape(utf8Text) + "\"";
    if (beam > 0) req += ",\"beam\":" + std::to_string(beam);
    if (!model.empty()) req += ",\"model\":\"" + JsonEscape(model) + "\"";
    const LocalMtDecode neutral;
    if (decode.patience != neutral.patience) req += ",\"patience\":" + std::to_string(decode.patience);
    if (decode.lengthPenalty != neutral.lengthPenalty) req += ",\"lenp\":" + std::to_string(decode.lengthPenalty);
    if (decode.coveragePenalty != neutral.coveragePenalty) req += ",\"covp\":" + std::to_string(decode.coveragePenalty);
    if (decode.repetitionPenalty != neutral.repetitionPenalty) req += ",\"repp\":" + std::to_string(decode.repetitionPenalty);
    if (decode.noRepeatNgram != neutral.noRepeatNgram) req += ",\"ngram\":" + std::to_string(decode.noRepeatNgram);
    if (decode.disableUnk) req += ",\"nounk\":1";
    if (decode.hypotheses != neutral.hypotheses) req += ",\"hyp\":" + std::to_string(decode.hypotheses);
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
    const JsonValue* model = json.Get("model");
    if (model && model->type == JsonValue::Type::String) {
        out.model = model->s;
    }
    const auto number = [&json](const char* key, int lo, int hi, int& into) {  // outside its range: stays neutral
        const JsonValue* v = json.Get(key);
        if (v && v->type == JsonValue::Type::Number && v->n >= lo && v->n <= hi) into = static_cast<int>(v->n);
    };
    number("patience", 100, 400, out.decode.patience);
    number("lenp", 0, 300, out.decode.lengthPenalty);
    number("covp", 0, 100, out.decode.coveragePenalty);
    number("repp", 100, 200, out.decode.repetitionPenalty);
    number("ngram", 0, 6, out.decode.noRepeatNgram);
    int noUnk = 0;
    number("nounk", 0, 1, noUnk);
    out.decode.disableUnk = noUnk == 1;
    number("hyp", 1, 5, out.decode.hypotheses);
    return true;
}

bool HasCap(const LocalMtMessage& msg, const std::string& cap) {
    for (const auto& c : msg.caps) {
        if (c == cap) return true;
    }
    return false;
}

bool ParseLocalMtMessage(const std::string& line, LocalMtMessage& out) {
    out = {};
    JsonValue json;
    if (!ParseJson(line, json) || !json.IsObject()) return false;
    std::string event = json.GetString("evt");
    if (event.empty()) event = json.GetString("t");
    if (event == "ready") {
        out.kind = LocalMtMessage::Kind::Ready;
        out.helper = json.GetString("helper");
        const JsonValue* proto = json.Get("proto");
        if (proto && proto->type == JsonValue::Type::Number) {
            out.proto = static_cast<int>(proto->n);
        } else {
            out.proto = 0;
        }
        const JsonValue* caps = json.Get("caps");
        if (caps && caps->type == JsonValue::Type::Array) {
            for (const auto& c : caps->arr) {
                if (c.type == JsonValue::Type::String && !c.s.empty()) {
                    out.caps.push_back(c.s);
                }
            }
        }
        out.build = json.GetString("build");
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
    if (event == "device") {
        const auto mb = [&json](const char* key) -> int64_t {
            const JsonValue* v = json.Get(key);
            return v && v->type == JsonValue::Type::Number && v->n >= 0 ? static_cast<int64_t>(v->n) : 0;
        };
        out.kind = LocalMtMessage::Kind::Device;
        out.onCard = json.GetString("on") == "card";
        out.cardName = json.GetString("name");
        out.cardDriver = json.GetString("driver");
        out.cardWhy = json.GetString("why");
        out.cardDetail = json.GetString("detail");
        out.cardFreeMb = mb("free");
        out.cardTotalMb = mb("total");
        out.cardNeedMb = mb("need");
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
        const JsonValue* score = json.Get("score");  // helpers with the "score" capability (2.6.5)
        if (score && score->type == JsonValue::Type::Number) {
            out.score = score->n;
            out.scored = true;
        }
        const auto number = [&json](const char* key, int& into) {
            const JsonValue* v = json.Get(key);
            if (v && v->type == JsonValue::Type::Number && v->n >= 0 && v->n < 1e9) into = static_cast<int>(v->n);
        };
        number("ms", out.modelMs);
        number("beam", out.beam);
        number("thr", out.threads);
        number("cores", out.cores);
        number("tin", out.tokensIn);
        number("tout", out.tokensOut);
        if (const JsonValue* alts = json.Get("alts"); alts && alts->type == JsonValue::Type::Array)
            for (const auto& a : alts->arr)
                if (a.type == JsonValue::Type::String && !a.s.empty()) out.alts.push_back(a.s);
        return true;
    }
    const JsonValue* message = json.Get("msg");
    if (!message || message->type != JsonValue::Type::String) return false;
    out.kind = LocalMtMessage::Kind::Error;
    out.error = message->s;
    return true;
}

std::string M2mLanguageTag(const std::string& langCode) {
    static const char* const kLanguages[] = {  // the model's own 100
        "af", "am", "ar", "ast", "az", "ba", "be", "bg", "bn", "br", "bs", "ca", "ceb", "cs", "cy", "da", "de",
        "el", "en", "es", "et", "fa", "ff", "fi", "fr", "fy", "ga", "gd", "gl", "gu", "ha", "he", "hi", "hr",
        "ht", "hu", "hy", "id", "ig", "ilo", "is", "it", "ja", "jv", "ka", "kk", "km", "kn", "ko", "lb", "lg",
        "ln", "lo", "lt", "lv", "mg", "mk", "ml", "mn", "mr", "ms", "my", "ne", "nl", "no", "ns", "oc", "or",
        "pa", "pl", "ps", "pt", "ro", "ru", "sd", "si", "sk", "sl", "so", "sq", "sr", "ss", "su", "sv", "sw",
        "ta", "th", "tl", "tn", "tr", "uk", "ur", "uz", "vi", "wo", "xh", "yi", "yo", "zh", "zu"};
    std::string code = Lower(langCode);
    code = code.substr(0, code.find('-'));  // en-gb, zh-tw: the model has one of each
    if (code == "ukr") code = "uk";
    if (code == "nb") code = "no";
    for (const char* language : kLanguages)
        if (code == language) return "__" + code + "__";
    return {};
}

std::string NllbLanguageTag(const std::string& langCode) {
    std::string code = Lower(langCode);
    if (code == "zh-hant" || code == "zh-tw") return "zho_Hant";
    if (code.size() >= 8 && code[3] == '_') return code;
    code = code.substr(0, code.find('-'));
    if (code == "ukr") code = "uk";
    if (code == "no") code = "nb";
    static const std::pair<const char*, const char*> kTags[] = {
        {"af", "afr_Latn"}, {"ak", "aka_Latn"}, {"am", "amh_Ethi"}, {"ar", "arb_Arab"}, {"as", "asm_Beng"},
        {"ay", "ayr_Latn"}, {"ba", "bak_Cyrl"}, {"be", "bel_Cyrl"}, {"bg", "bul_Cyrl"}, {"bm", "bam_Latn"},
        {"bn", "ben_Beng"}, {"bo", "bod_Tibt"}, {"bs", "bos_Latn"}, {"ca", "cat_Latn"}, {"cs", "ces_Latn"},
        {"cy", "cym_Latn"}, {"da", "dan_Latn"}, {"de", "deu_Latn"}, {"dz", "dzo_Tibt"}, {"ee", "ewe_Latn"},
        {"el", "ell_Grek"}, {"en", "eng_Latn"}, {"eo", "epo_Latn"}, {"es", "spa_Latn"}, {"et", "est_Latn"},
        {"eu", "eus_Latn"}, {"fa", "pes_Arab"}, {"ff", "fuv_Latn"}, {"fi", "fin_Latn"}, {"fj", "fij_Latn"},
        {"fo", "fao_Latn"}, {"fr", "fra_Latn"}, {"ga", "gle_Latn"}, {"gd", "gla_Latn"}, {"gl", "glg_Latn"},
        {"gn", "grn_Latn"}, {"gu", "guj_Gujr"}, {"ha", "hau_Latn"}, {"he", "heb_Hebr"}, {"hi", "hin_Deva"},
        {"hr", "hrv_Latn"}, {"ht", "hat_Latn"}, {"hu", "hun_Latn"}, {"hy", "hye_Armn"}, {"id", "ind_Latn"},
        {"ig", "ibo_Latn"}, {"is", "isl_Latn"}, {"it", "ita_Latn"}, {"ja", "jpn_Jpan"}, {"jv", "jav_Latn"},
        {"ka", "kat_Geor"}, {"kg", "kon_Latn"}, {"ki", "kik_Latn"}, {"kk", "kaz_Cyrl"}, {"km", "khm_Khmr"},
        {"kn", "kan_Knda"}, {"ko", "kor_Hang"}, {"ky", "kir_Cyrl"}, {"lb", "ltz_Latn"}, {"lg", "lug_Latn"},
        {"li", "lim_Latn"}, {"ln", "lin_Latn"}, {"lo", "lao_Laoo"}, {"lt", "lit_Latn"}, {"lv", "lvs_Latn"},
        {"mg", "plt_Latn"}, {"mi", "mri_Latn"}, {"mk", "mkd_Cyrl"}, {"ml", "mal_Mlym"}, {"mn", "khk_Cyrl"},
        {"mr", "mar_Deva"}, {"ms", "zsm_Latn"}, {"mt", "mlt_Latn"}, {"my", "mya_Mymr"}, {"nb", "nob_Latn"},
        {"ne", "npi_Deva"}, {"nl", "nld_Latn"}, {"nn", "nno_Latn"}, {"ny", "nya_Latn"}, {"oc", "oci_Latn"},
        {"om", "gaz_Latn"}, {"or", "ory_Orya"}, {"pa", "pan_Guru"}, {"pl", "pol_Latn"}, {"ps", "pbt_Arab"},
        {"pt", "por_Latn"}, {"qu", "quy_Latn"}, {"rn", "run_Latn"}, {"ro", "ron_Latn"}, {"ru", "rus_Cyrl"},
        {"rw", "kin_Latn"}, {"sa", "san_Deva"}, {"sc", "srd_Latn"}, {"sd", "snd_Arab"}, {"sg", "sag_Latn"},
        {"si", "sin_Sinh"}, {"sk", "slk_Latn"}, {"sl", "slv_Latn"}, {"sm", "smo_Latn"}, {"sn", "sna_Latn"},
        {"so", "som_Latn"}, {"sq", "als_Latn"}, {"sr", "srp_Cyrl"}, {"ss", "ssw_Latn"}, {"st", "sot_Latn"},
        {"su", "sun_Latn"}, {"sv", "swe_Latn"}, {"sw", "swh_Latn"}, {"ta", "tam_Taml"}, {"te", "tel_Telu"},
        {"tg", "tgk_Cyrl"}, {"th", "tha_Thai"}, {"ti", "tir_Ethi"}, {"tk", "tuk_Latn"}, {"tl", "tgl_Latn"},
        {"tn", "tsn_Latn"}, {"tr", "tur_Latn"}, {"ts", "tso_Latn"}, {"tt", "tat_Cyrl"}, {"tw", "twi_Latn"},
        {"ug", "uig_Arab"}, {"uk", "ukr_Cyrl"}, {"ur", "urd_Arab"}, {"uz", "uzn_Latn"}, {"vi", "vie_Latn"},
        {"wo", "wol_Latn"}, {"xh", "xho_Latn"}, {"yi", "ydd_Hebr"}, {"yo", "yor_Latn"}, {"zh", "zho_Hans"},
        {"zu", "zul_Latn"},
    };
    for (const auto& [iso, tag] : kTags)
        if (code == iso) return tag;
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
    if (has("m2m100") && !M2mLanguageTag(src).empty() && !M2mLanguageTag(tgt).empty()) return {direct};
    if (src == "en" || tgt == "en") return {};
    const std::string first = src + "-en", second = "en-" + tgt;
    return has(first) && has(second) ? std::vector<std::string>{first, second} : std::vector<std::string>{};
}

std::wstring TidyPunctuation(const std::wstring& text, const std::string& targetLang) {
    const bool french = Lower(targetLang).rfind("fr", 0) == 0;
    const auto mark = [](wchar_t c) {
        return c == L',' || c == L'.' || c == L'!' || c == L'?' || c == L';' || c == L':';
    };
    std::wstring out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L' ' && !out.empty() && IsWordChar(out.back())) {
            size_t j = i;
            while (j < text.size() && text[j] == L' ') ++j;
            if (j < text.size() && mark(text[j]) && !(french && text[j] != L',' && text[j] != L'.')) {
                size_t k = j;
                while (k < text.size() && mark(text[k])) ++k;
                if (k == text.size() || text[k] == L' ') {
                    i = j - 1;  // the spaces go, the mark follows
                    continue;
                }
            }
        }
        out += text[i];
    }
    return out;
}

std::wstring CleanLocalMtOutput(const std::wstring& source, const std::wstring& output, const std::string& targetLang) {
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
    const auto sentenceEnd = [](wchar_t c) {
        return c == L'.' || c == L'!' || c == L'?' || c == L'…' || c == L'。' || c == L'！' || c == L'？';
    };
    const std::wstring src = Trim(source);
    // A dash the source did not start with: NLLB opens some lines as dialogue ("hot area it is" -> "- Das ist heiß",
    // "Selam!" -> "- Ich weiß nicht.").
    const auto dash = [](wchar_t c) { return c == L'-' || c == L'–' || c == L'—'; };
    if (cleaned.size() >= 2 && dash(cleaned[0]) && whitespace(cleaned[1]) && !(!src.empty() && dash(src[0]))) {
        cleaned.erase(0, 1);
        while (!cleaned.empty() && whitespace(cleaned.front())) cleaned.erase(cleaned.begin());
    }
    if (!src.empty() && !sentenceEnd(src.back()) && cleaned.size() >= 2 &&
        (cleaned.back() == L'.' || cleaned.back() == L'。') && cleaned[cleaned.size() - 2] != L'.') {
        cleaned.pop_back();
        while (!cleaned.empty() && whitespace(cleaned.back())) cleaned.pop_back();  // "am Weg ." -> "am Weg"
    }
    return TidyPunctuation(cleaned, targetLang);
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
    // Spaces around a run, a comma or dash it starts with (", we need" after a keep-word – the model drops it and the
    // words stick together: "WvWWir") and a run without a word stay as they are.
    const auto leading = [](wchar_t c) {
        return IsSpace(c) || c == L',' || c == L';' || c == L':' || c == L'-' || c == L'–' || c == L'—' ||
               c == L')' || c == L']';
    };
    auto addText = [&](const std::wstring& run) {
        size_t b = 0, e = run.size();
        while (b < e && leading(run[b])) ++b;
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

PieceRange WholeSentenceRange(const std::vector<Segment>& pieces) {
    size_t first = 0, last = pieces.size();
    while (first < last && pieces[first].keep) ++first;
    while (last > first && pieces[last - 1].keep) --last;
    if (first == last) return {pieces.size(), pieces.size()};
    // A protected word belongs to the sentence; a run of symbols (decorative or none with a letter) does not.
    const auto word = [](const std::wstring& text) {
        bool letter = false;
        for (wchar_t c : text) {
            if (c >= 0xD800 && c <= 0xDFFF) return false;  // emoji and other symbols outside the BMP
            if (IsDecorativeCodepoint(c)) return false;
            letter = letter || IsWordChar(c);
        }
        return letter;
    };
    if (first > 0 && word(pieces[first - 1].text)) --first;
    if (last < pieces.size() && word(pieces[last].text)) ++last;
    return {first, last};
}

namespace {
// " rez!", "heal alac.": words of plain lowercase letters (an ASCII one first), spaces and sentence marks only.
bool PlainLowercaseWords(const std::wstring& text) {
    bool any = false;
    for (const Span& w : WordSpans(text)) {
        if (text[w.start] < L'a' || text[w.start] > L'z') return false;
        for (size_t i = w.start; i < w.end(); ++i)
            if (CaseFoldChar(text[i]) != text[i]) return false;
        any = true;
    }
    for (wchar_t c : text)
        if (!IsWordChar(c) && c != L' ' && c != L'.' && c != L',' && c != L'!' && c != L'?' && c != L';' && c != L':')
            return false;
    return any;
}
}  // namespace

bool KeepWordsAsNames(std::vector<Segment>& segments) {
    bool changed = false;
    for (Segment& s : segments) {
        if (!s.keep || !PlainLowercaseWords(s.text)) continue;
        for (const Span& w : WordSpans(s.text)) s.text[w.start] = static_cast<wchar_t>(s.text[w.start] - L'a' + L'A');
        changed = true;
    }
    return changed;
}

namespace {
// "X1" … "X9" (any case) as a word of its own.
bool PlaceholderAt(const std::wstring& s, size_t at) {
    return at + 1 < s.size() && (s[at] == L'X' || s[at] == L'x') && s[at + 1] >= L'1' && s[at + 1] <= L'9' &&
           (at == 0 || !IsWordChar(s[at - 1])) && (at + 2 == s.size() || !IsWordChar(s[at + 2]));
}
}  // namespace

Placeholders WithPlaceholders(const std::vector<Segment>& pieces) {
    Placeholders r;
    // " Lion's Arch, " -> "Lion's Arch"; a code or tag stays whole ("[&AgGvWwEA]"), a smiley without a letter is text
    const auto around = [](wchar_t c) { return IsSpace(c) || std::wcschr(L",.!?;:", c) != nullptr; };
    for (const Segment& s : pieces) {
        size_t b = 0, e = s.text.size();
        while (b < e && around(s.text[b])) ++b;
        while (e > b && around(s.text[e - 1])) --e;
        const bool word = std::any_of(s.text.begin() + static_cast<std::ptrdiff_t>(b),
                                      s.text.begin() + static_cast<std::ptrdiff_t>(e), IsWordChar);
        if (!s.keep || !word) {
            r.text += s.text;
            continue;
        }
        r.parts.push_back(s.text.substr(b, e - b));
        r.text += s.text.substr(0, b) + L"X" + std::to_wstring(r.parts.size()) + s.text.substr(e);
    }
    bool own = false;  // an "X1" of the writer's would be taken for a placeholder
    for (const Segment& s : pieces)
        for (size_t i = 0; i < s.text.size() && !own; ++i) own = PlaceholderAt(s.text, i);
    if (own || r.parts.size() > 9) r.parts.clear();
    return r;
}

bool FillPlaceholders(const std::wstring& output, const std::vector<std::wstring>& parts, std::wstring& filled) {
    std::vector<int> seen(parts.size(), 0);
    filled.clear();
    for (size_t i = 0; i < output.size(); ++i) {
        if (PlaceholderAt(output, i)) {
            const size_t n = static_cast<size_t>(output[i + 1] - L'1');
            if (n >= parts.size() || ++seen[n] > 1) return false;
            filled += parts[n];
            ++i;
            continue;
        }
        filled += output[i];
    }
    return std::all_of(seen.begin(), seen.end(), [](int c) { return c == 1; });
}

}  // namespace gct
