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

std::string SourceOf(const std::string& s) { return s == "ocr" || s == "chat" ? s : "extras"; }

}  // namespace

bool IsBridgeChannel(const std::string& c) {
    return c == "party" || c == "squad" || c == "map" || c == "say" || c == "whisper" || c == "guild" || c == "team" ||
           c == "system";
}

std::string EncodeBridgeMessage(const BridgeMessage& m) {
    std::string out = "{\"v\":" + std::to_string(kBridgeProtocol);
    if (m.kind == BridgeMessage::Kind::Hello) {
        out += ",\"t\":\"hello\"";
        Field(out, "src", SourceOf(m.src));
        Field(out, "addon", CutUtf8(m.addonVersion, 32));
        out += m.extras ? ",\"ex\":true" : ",\"ex\":false";
        if (m.chatEvents) out += ",\"ce\":true";
    } else {
        out += ",\"t\":\"chat\"";
        Field(out, "src", SourceOf(m.src));
        Field(out, "ch", IsBridgeChannel(m.channel) ? m.channel : std::string());
        if (m.outgoing) out += ",\"out\":true";
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
    m.src = SourceOf(v.GetString("src"));
    const std::string t = v.GetString("t");
    if (t == "hello") {
        m.kind = BridgeMessage::Kind::Hello;
        m.addonVersion = v.GetString("addon");
        m.extras = v.GetBool("ex");
        m.chatEvents = v.GetBool("ce");
    } else if (t == "chat") {
        m.kind = BridgeMessage::Kind::Chat;
        m.channel = v.GetString("ch");
        if (!IsBridgeChannel(m.channel)) m.channel.clear();
        // The add-on only ever reports party/squad: anything else from it is not believed.
        if (m.src == "extras" && m.channel != "party" && m.channel != "squad") return false;
        m.outgoing = v.GetBool("out");
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
