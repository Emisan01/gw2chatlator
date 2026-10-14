#include "core/bridge_protocol.hpp"

#include "core/json.hpp"

namespace gct {
namespace {

// Cuts UTF-8 at a character boundary.
std::string CutUtf8(const std::string& s, size_t maxBytes) {
    if (s.size() <= maxBytes) return s;
    size_t n = maxBytes;
    while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) --n;
    return s.substr(0, n);
}

void Field(std::string& out, const char* key, const std::string& value) {
    out += ",\"";
    out += key;
    out += "\":\"";
    out += JsonEscape(value);
    out += '"';
}

int IntOf(const JsonValue& v, const char* key, int def) {
    const JsonValue* x = v.Get(key);
    return x && x->type == JsonValue::Type::Number ? static_cast<int>(x->n) : def;
}

}  // namespace

std::string EncodeBridgeMessage(const BridgeMessage& m) {
    std::string out = "{\"v\":" + std::to_string(kBridgeProtocol);
    if (m.kind == BridgeMessage::Kind::Hello) {
        out += ",\"t\":\"hello\"";
        Field(out, "addon", CutUtf8(m.addonVersion, 32));
    } else {
        out += ",\"t\":\"chat\"";
        Field(out, "ch", m.channel == "squad" ? "squad" : "party");
        out += ",\"sub\":" + std::to_string(m.subgroup >= 0 && m.subgroup < 15 ? m.subgroup : -1);
        out += m.broadcast ? ",\"bc\":true" : ",\"bc\":false";
        Field(out, "time", CutUtf8(m.time, 40));
        Field(out, "acc", CutUtf8(m.account, 64));
        Field(out, "char", CutUtf8(m.character, 64));
        // GW2 chat lines are ≤ 199 characters; the cut only guards against garbage. Escaping can grow the text up to
        // 6× (escaped control characters), so the limit leaves room for that.
        Field(out, "text", CutUtf8(m.text, 1024));
    }
    out += "}\n";
    return out;
}

bool DecodeBridgeMessage(const std::string& line, BridgeMessage& out) {
    if (line.size() > kBridgeMaxLine) return false;
    std::string s = line;
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    JsonValue v;
    if (!ParseJson(s, v) || !v.IsObject()) return false;
    if (IntOf(v, "v", 0) != kBridgeProtocol) return false;
    BridgeMessage m;
    const std::string t = v.GetString("t");
    if (t == "hello") {
        m.kind = BridgeMessage::Kind::Hello;
        m.addonVersion = v.GetString("addon");
    } else if (t == "chat") {
        m.kind = BridgeMessage::Kind::Chat;
        m.channel = v.GetString("ch");
        if (m.channel != "party" && m.channel != "squad") return false;
        m.subgroup = IntOf(v, "sub", -1);
        if (m.subgroup < -1 || m.subgroup >= 15) m.subgroup = -1;
        m.broadcast = v.GetBool("bc");
        m.time = v.GetString("time");
        m.account = v.GetString("acc");
        m.character = v.GetString("char");
        m.text = v.GetString("text");
        if (m.text.empty()) return false;
    } else {
        return false;
    }
    out = std::move(m);
    return true;
}

}  // namespace gct
