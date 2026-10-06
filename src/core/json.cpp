// json.cpp — minimal JSON reader/escaper.
#include "json.hpp"

#include <cstdint>
#include <cstdlib>

namespace gct {

namespace {
constexpr uint32_t kReplacement = 0xFFFD;

void AppendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}
}  // namespace

const JsonValue* JsonValue::Get(const std::string& key) const {
    if (type != Type::Object) return nullptr;
    for (const auto& kv : obj)
        if (kv.first == key) return &kv.second;
    return nullptr;
}

namespace {

struct JsonParser {
    const std::string& in;
    size_t pos = 0;
    int depth = 0;

    void Ws() {
        while (pos < in.size() && (in[pos] == ' ' || in[pos] == '\t' || in[pos] == '\n' || in[pos] == '\r')) ++pos;
    }
    bool Lit(const char* lit) {
        size_t n = 0;
        while (lit[n]) ++n;
        if (in.compare(pos, n, lit) != 0) return false;
        pos += n;
        return true;
    }
    static int Hex(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }
    bool Hex4(uint32_t& v) {
        if (pos + 4 > in.size()) return false;
        v = 0;
        for (int i = 0; i < 4; ++i) {
            int h = Hex(in[pos + i]);
            if (h < 0) return false;
            v = (v << 4) | static_cast<uint32_t>(h);
        }
        pos += 4;
        return true;
    }
    bool String(std::string& out) {
        if (pos >= in.size() || in[pos] != '"') return false;
        ++pos;
        while (pos < in.size()) {
            char c = in[pos++];
            if (c == '"') return true;
            if (c != '\\') { out += c; continue; }
            if (pos >= in.size()) return false;
            char e = in[pos++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    uint32_t cp;
                    if (!Hex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        uint32_t lo;
                        if (pos + 6 <= in.size() && in[pos] == '\\' && in[pos + 1] == 'u') {
                            pos += 2;
                            if (!Hex4(lo)) return false;
                            if (lo >= 0xDC00 && lo <= 0xDFFF) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                            else cp = kReplacement;
                        } else {
                            cp = kReplacement;
                        }
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        cp = kReplacement;
                    }
                    AppendUtf8(out, cp);
                    break;
                }
                default: return false;
            }
        }
        return false;
    }
    bool Number(double& out) {
        size_t start = pos;
        if (pos < in.size() && (in[pos] == '-' || in[pos] == '+')) ++pos;
        while (pos < in.size() && ((in[pos] >= '0' && in[pos] <= '9') || in[pos] == '.' || in[pos] == 'e' ||
                                   in[pos] == 'E' || in[pos] == '-' || in[pos] == '+'))
            ++pos;
        if (pos == start) return false;
        out = std::strtod(in.substr(start, pos - start).c_str(), nullptr);
        return true;
    }
    bool Value(JsonValue& v) {
        if (++depth > 64) return false;
        Ws();
        if (pos >= in.size()) return false;
        char c = in[pos];
        bool ok = false;
        if (c == '{') {
            v.type = JsonValue::Type::Object;
            ++pos;
            Ws();
            if (pos < in.size() && in[pos] == '}') { ++pos; ok = true; }
            else {
                while (true) {
                    Ws();
                    std::string key;
                    if (!String(key)) break;
                    Ws();
                    if (pos >= in.size() || in[pos] != ':') break;
                    ++pos;
                    JsonValue child;
                    if (!Value(child)) break;
                    v.obj.emplace_back(std::move(key), std::move(child));
                    Ws();
                    if (pos < in.size() && in[pos] == ',') { ++pos; continue; }
                    if (pos < in.size() && in[pos] == '}') { ++pos; ok = true; }
                    break;
                }
            }
        } else if (c == '[') {
            v.type = JsonValue::Type::Array;
            ++pos;
            Ws();
            if (pos < in.size() && in[pos] == ']') { ++pos; ok = true; }
            else {
                while (true) {
                    JsonValue child;
                    if (!Value(child)) break;
                    v.arr.push_back(std::move(child));
                    Ws();
                    if (pos < in.size() && in[pos] == ',') { ++pos; continue; }
                    if (pos < in.size() && in[pos] == ']') { ++pos; ok = true; }
                    break;
                }
            }
        } else if (c == '"') {
            v.type = JsonValue::Type::String;
            ok = String(v.s);
        } else if (c == 't') {
            v.type = JsonValue::Type::Bool; v.b = true; ok = Lit("true");
        } else if (c == 'f') {
            v.type = JsonValue::Type::Bool; v.b = false; ok = Lit("false");
        } else if (c == 'n') {
            v.type = JsonValue::Type::Null; ok = Lit("null");
        } else {
            v.type = JsonValue::Type::Number; ok = Number(v.n);
        }
        --depth;
        return ok;
    }
};

}  // namespace

bool ParseJson(const std::string& utf8, JsonValue& out) {
    JsonParser p{utf8};
    out = JsonValue{};
    if (!p.Value(out)) return false;
    p.Ws();
    return p.pos == utf8.size();
}

std::string JsonEscape(const std::string& utf8) {
    std::string out;
    out.reserve(utf8.size() + 8);
    static const char* hex = "0123456789abcdef";
    for (unsigned char c : utf8) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (c < 0x20) {
                    out += "\\u00";
                    out += hex[c >> 4];
                    out += hex[c & 0xF];
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

std::string JsonValue::GetString(const std::string& key) const {
    const JsonValue* v = Get(key);
    return (v && v->type == Type::String) ? v->s : std::string();
}

bool JsonValue::GetBool(const std::string& key, bool def) const {
    const JsonValue* v = Get(key);
    return (v && v->type == Type::Bool) ? v->b : def;
}

}  // namespace gct
