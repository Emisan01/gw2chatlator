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
        {"de-en", "Helsinki-NLP/opus-mt-de-en", "", "Apache-2.0", 77066388},
        {"en-de", "Helsinki-NLP/opus-mt-en-de", "", "CC-BY-4.0", 77054675},
        {"fr-en", "Helsinki-NLP/opus-mt-fr-en", "", "Apache-2.0", 77807028},
        {"en-fr", "Helsinki-NLP/opus-mt-en-fr", "", "Apache-2.0", 77807028},
        {"es-en", "Helsinki-NLP/opus-mt-es-en", "", "Apache-2.0", 80732850},
        {"en-es", "Helsinki-NLP/opus-mt-en-es", "", "Apache-2.0", 80732850},
        {"it-en", "Helsinki-NLP/opus-mt-it-en", "", "Apache-2.0", 88911579},
        {"en-it", "Helsinki-NLP/opus-mt-en-it", "", "Apache-2.0", 88730368},
        {"pl-en", "Helsinki-NLP/opus-mt-pl-en", "", "Apache-2.0", 79918234},
        {"en-pl", "Helsinki-NLP/opus-mt-en-zlw", ">>pol<<", "Apache-2.0", 77078002},
        {"pt-en", "Helsinki-NLP/opus-mt-ROMANCE-en", "", "Apache-2.0", 80684165},
        {"en-pt", "Helsinki-NLP/opus-mt-en-ROMANCE", ">>pt<<", "Apache-2.0", 80682455},
        {"tr-en", "Helsinki-NLP/opus-mt-tr-en", "", "Apache-2.0", 79351104},
        {"en-tr", "Helsinki-NLP/opus-mt-en-trk", ">>tur<<", "Apache-2.0", 79078197},
        {"ru-en", "Helsinki-NLP/opus-mt-ru-en", "", "CC-BY-4.0", 79550626},
        {"en-ru", "Helsinki-NLP/opus-mt-en-ru", "", "Apache-2.0", 79562339},
    };
    return models;
}

std::string BuildLocalMtRequest(int id, const std::string& source, const std::string& target,
                                const std::string& utf8Text) {
    return "{\"cmd\":\"translate\",\"id\":" + std::to_string(id) + ",\"src\":\"" + JsonEscape(Lower(source)) +
           "\",\"tgt\":\"" + JsonEscape(Lower(target)) + "\",\"text\":\"" + JsonEscape(utf8Text) + "\"}";
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

std::vector<std::string> LocalMtRoute(const std::wstring& source, const std::wstring& target,
                                      const std::vector<std::string>& installedPairs) {
    const std::string src = Code(source), tgt = Code(target);
    if (src.empty() || tgt.empty() || src == tgt) return {};
    auto has = [&](const std::string& pair) {
        return std::find(installedPairs.begin(), installedPairs.end(), pair) != installedPairs.end();
    };
    const std::string direct = src + "-" + tgt;
    if (has(direct)) return {direct};
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

}  // namespace gct
