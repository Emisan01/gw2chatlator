// mumble.cpp
#include "mumble.hpp"

#include "json.hpp"
#include "text.hpp"

namespace gct {

MumbleIdentity ParseMumbleIdentity(const std::wstring& json) {
    MumbleIdentity id;
    JsonValue v;
    if (!ParseJson(ToUtf8(json), v) || !v.IsObject()) return id;
    id.name = FromUtf8(v.GetString("name"));
    if (const JsonValue* u = v.Get("uisz"); u && u->type == JsonValue::Type::Number) id.uiSize = static_cast<int>(u->n);
    if (const JsonValue* m = v.Get("map_id"); m && m->type == JsonValue::Type::Number)
        id.mapId = static_cast<uint32_t>(m->n);
    id.commander = v.GetBool("commander");
    return id;
}

}  // namespace gct
