// mumble.cpp
#include "mumble.hpp"

#include <algorithm>
#include <cmath>

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

float Gw2BottomBarOffset(float displayH, int uiSize) {
    float uiScale = 1.0f;
    if (uiSize == 0) uiScale = 0.89f;
    else if (uiSize == 1) uiScale = 1.0f;
    else if (uiSize == 2) uiScale = 1.11f;
    else if (uiSize == 3) uiScale = 1.25f;
    else uiScale = 0.89f;  // default to Small if unknown

    const float resScale = std::max(1.0f, displayH / 1080.0f);
    // 28px at 1080p aligns the bottom edge flush with the bottom edge of the native
    // mini-chat speech bubble icon across all 4 UI scales and resolutions, leaving the XP bar free.
    return std::round(28.0f * uiScale * resScale);
}

}  // namespace gct
