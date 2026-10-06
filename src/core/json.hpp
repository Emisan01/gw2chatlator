// json.hpp — minimal JSON reader/escaper (enough for DeepL and the GW2 API).
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace gct {

struct JsonValue {
    enum class Type { Null, Bool, Number, String, Array, Object } type = Type::Null;
    bool b = false;
    double n = 0;
    std::string s;  // UTF-8
    std::vector<JsonValue> arr;
    std::vector<std::pair<std::string, JsonValue>> obj;

    const JsonValue* Get(const std::string& key) const;
    // Convenience: string member or empty.
    std::string GetString(const std::string& key) const;
    bool GetBool(const std::string& key, bool def = false) const;
    bool IsArray() const { return type == Type::Array; }
    bool IsObject() const { return type == Type::Object; }
};

// Parses a complete document (trailing garbage = failure). Depth-limited.
bool ParseJson(const std::string& utf8, JsonValue& out);

// Escapes for use inside a JSON string literal (without the quotes).
std::string JsonEscape(const std::string& utf8);

}  // namespace gct
