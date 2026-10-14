#include "chat_links.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cwctype>
#include <vector>

#include "core/chat_code.hpp"
#include "core/i18n.hpp"
#include "core/json.hpp"
#include "core/text.hpp"
#include "win/files.hpp"
#include "win/http.hpp"

namespace gct {
namespace {

constexpr size_t kMaxCodeChars = 48;   // a link to one thing is short; longer codes are shown as they are
constexpr size_t kMaxCache = 400;      // then the oldest answers go
constexpr int kMaxPerMinute = 20;      // lookups; a flood of codes waits
constexpr size_t kMaxQueue = 40;

std::wstring KindName(int kind) {  // in the language of the window
    switch (static_cast<ChatCodeKind>(kind)) {
        case ChatCodeKind::Item: return Tr(L"Item");
        case ChatCodeKind::Skill: return Tr(L"Skill");
        case ChatCodeKind::Trait: return Tr(L"Trait");
        case ChatCodeKind::Recipe: return Tr(L"Recipe");
        case ChatCodeKind::Skin: return Tr(L"Skin");
        case ChatCodeKind::Outfit: return Tr(L"Outfit");
        case ChatCodeKind::Achievement: return Tr(L"Achievement");
        case ChatCodeKind::Poi: return Tr(L"Waypoint / point of interest");
        case ChatCodeKind::Build: return Tr(L"Build template");
        case ChatCodeKind::Coin: return Tr(L"Coins");
        case ChatCodeKind::Objective: return Tr(L"WvW objective");
        default: return Tr(L"Chat code");
    }
}

// GW2 descriptions carry markup ("<c=@flavor>...</c>", "<br>"): plain text, capped.
std::wstring PlainDescription(std::wstring s) {
    std::wstring out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'<') {
            const size_t end = s.find(L'>', i);
            if (end == std::wstring::npos) break;
            const std::wstring tag = s.substr(i, end - i + 1);
            if (tag == L"<br>" || tag == L"<br/>") out += L'\n';
            i = end;
            continue;
        }
        if (s[i] != L'\r') out += s[i];
    }
    if (out.size() > 400) out = out.substr(0, 400) + L"…";
    return out;
}

// "UnlimitedConsumable" -> "Unlimited Consumable" (the API's type words, readable).
std::wstring Words(const std::wstring& s) {
    std::wstring out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (i && iswupper(s[i]) && iswlower(s[i - 1])) out += L' ';
        out += s[i];
    }
    return out;
}

std::wstring PoiTypeName(const std::string& type) {
    if (type == "waypoint") return Tr(L"Waypoint");
    if (type == "vista") return Tr(L"Vista");
    if (type == "landmark") return Tr(L"Point of interest");
    return Tr(L"Waypoint / point of interest");
}

ImVec4 Rgb(uint32_t rgb) {
    return ImVec4(((rgb >> 16) & 0xFF) / 255.f, ((rgb >> 8) & 0xFF) / 255.f, (rgb & 0xFF) / 255.f, 1.f);
}

}  // namespace

void ChatLinks::Start(AddonAPI_t* api, const std::wstring& cacheDir) {
    api_ = api;
    cacheDir_ = cacheDir;
    stop_ = false;
    thread_ = std::thread([this] { Run(); });
}

void ChatLinks::Stop() {
    {
        std::lock_guard<std::mutex> lock(mu_);
        stop_ = true;
        queue_.clear();
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
    std::lock_guard<std::mutex> lock(mu_);
    cache_.clear();
    api_ = nullptr;
}

void ChatLinks::SetLanguage(const std::wstring& apiLang) {
    std::lock_guard<std::mutex> lock(mu_);
    if (apiLang == lang_) return;
    lang_ = apiLang;
    cache_.clear();  // names come again in the new language
    pois_.clear();
    poisState_ = 0;
}

ChatLinks::Info ChatLinks::PoiInfo(uint32_t id) const {
    Info info;
    info.kind = static_cast<int>(ChatCodeKind::Poi);
    info.state = 2;
    auto it = pois_.find(id);
    if (it == pois_.end()) return info;
    info.state = 1;
    info.name = it->second.name;
    info.mapName = it->second.mapName;
    info.type = PoiTypeName(it->second.type);
    info.x = it->second.x;
    info.y = it->second.y;
    return info;
}

bool ChatLinks::Target(const std::wstring& code, float* x, float* y) {
    const Info info = Get(code);
    if (info.state != 1 || info.kind != static_cast<int>(ChatCodeKind::Poi)) return false;
    *x = info.x;
    *y = info.y;
    return true;
}

ChatLinks::Info ChatLinks::Get(const std::wstring& code) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = cache_.find(code);
    if (it != cache_.end()) return it->second;
    Info asked;
    const ChatCode c = DecodeChatCode(code);
    if (c.kind == ChatCodeKind::Poi && code.size() <= kMaxCodeChars) {  // from the waypoint table
        if (poisState_ == 2) {
            const Info info = PoiInfo(c.id);
            if (cache_.size() >= kMaxCache) cache_.erase(cache_.begin());
            cache_[code] = info;
            return info;
        }
        if (poisState_ == 0) {
            poisState_ = 1;
            queue_.push_front(L"#pois");
            cv_.notify_one();
        }
        asked.kind = static_cast<int>(ChatCodeKind::Poi);
        asked.state = poisState_ == 3 ? 2 : 0;
        return asked;  // not kept: asked again once the table is there
    }
    if (code.size() > kMaxCodeChars || ChatCodeApiPath(c).empty()) {
        asked.state = 2;  // nothing to ask: shown with its kind only
        asked.kind = static_cast<int>(c.kind);
    } else if (queue_.size() < kMaxQueue) {
        queue_.push_back(code);
        cv_.notify_one();
    } else {
        return asked;  // asked again later, when the queue has room
    }
    if (cache_.size() >= kMaxCache) cache_.erase(cache_.begin());
    cache_[code] = asked;
    return asked;
}

void ChatLinks::Run() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    std::deque<ULONGLONG> recent;  // when the last lookups ran (the per-minute limit)
    for (;;) {
        std::wstring code, lang;
        {
            std::unique_lock<std::mutex> lock(mu_);
            cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
            if (stop_) return;
            code = queue_.front();
            queue_.pop_front();
            lang = lang_;
        }
        if (code == L"#pois") {  // the waypoint table (its own requests, once)
            auto table = LoadPois(lang);
            std::lock_guard<std::mutex> lock(mu_);
            if (stop_) return;
            if (lang == lang_) {
                pois_ = std::move(table);
                poisState_ = pois_.empty() ? 3 : 2;
            }
            continue;
        }
        while (!recent.empty() && GetTickCount64() - recent.front() > 60000) recent.pop_front();
        if (static_cast<int>(recent.size()) >= kMaxPerMinute) {  // wait until a minute is over (or we stop)
            std::unique_lock<std::mutex> lock(mu_);
            cv_.wait_for(lock, std::chrono::milliseconds(60000 - (GetTickCount64() - recent.front())), [this] { return stop_; });
            if (stop_) return;
            queue_.push_front(code);
            continue;
        }
        recent.push_back(GetTickCount64());
        Info info = Fetch(code, lang);
        std::lock_guard<std::mutex> lock(mu_);
        if (stop_) return;
        if (lang == lang_) cache_[code] = info;
    }
}

ChatLinks::Info ChatLinks::Fetch(const std::wstring& code, const std::wstring& lang) {
    const ChatCode c = DecodeChatCode(code);
    Info info;
    info.state = 2;
    info.kind = static_cast<int>(c.kind);
    info.count = c.count;
    auto ask = [&](const std::wstring& path, JsonValue* first) {
        const HttpResponse r = HttpsRequest(L"GET", L"api.guildwars2.com", path + L"&lang=" + lang, L"", "", nullptr, 1u << 20);
        JsonValue v;
        if (!r.transportOk || r.status != 200 || !ParseJson(r.body, v) || !v.IsArray() || v.arr.empty()) return false;
        *first = v.arr.front();
        return true;
    };
    auto num = [](const JsonValue& v, const char* key) {
        const JsonValue* n = v.Get(key);
        return n && n->type == JsonValue::Type::Number ? static_cast<int>(n->n) : 0;
    };
    JsonValue v;
    const std::wstring path = ChatCodeApiPath(c);
    if (path.empty() || !ask(path, &v)) return info;
    if (c.kind == ChatCodeKind::Recipe) {  // a recipe: what it makes
        const int out = num(v, "output_item_id");
        JsonValue item;
        if (out <= 0 || !ask(L"/v2/items?ids=" + std::to_wstring(out), &item)) return info;
        v = item;
    }
    info.name = FromUtf8(v.GetString("name"));
    info.rgb = RarityColor(v.GetString("rarity"));
    info.level = num(v, "level");
    info.iconUrl = v.GetString("icon");
    info.description = PlainDescription(FromUtf8(v.GetString("description")));
    if (info.description.empty()) info.description = PlainDescription(FromUtf8(v.GetString("requirement")));  // achievements
    std::wstring type = Words(FromUtf8(v.GetString("type")));
    if (const JsonValue* d = v.Get("details"); d && d->IsObject() && !d->GetString("type").empty())
        type += L" · " + Words(FromUtf8(d->GetString("type")));
    info.type = type;
    if (!info.name.empty()) info.state = 1;
    return info;
}

// The table of every waypoint / point of interest: Tyria and the Mists from their first level, and the public maps that
// live on another level one by one (about 50 requests). Cached as "id<TAB>x<TAB>y<TAB>type<TAB>name<TAB>map" lines.
std::unordered_map<uint32_t, ChatLinks::Poi> ChatLinks::LoadPois(const std::wstring& lang) {
    std::unordered_map<uint32_t, Poi> table;
    const std::wstring file = cacheDir_ + L"\\pois_" + lang + L".tsv";
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    if (GetFileAttributesExW(file.c_str(), GetFileExInfoStandard, &fa)) {
        FILETIME now;
        GetSystemTimeAsFileTime(&now);
        const ULONGLONG age = ((static_cast<ULONGLONG>(now.dwHighDateTime) << 32) | now.dwLowDateTime) -
                              ((static_cast<ULONGLONG>(fa.ftLastWriteTime.dwHighDateTime) << 32) | fa.ftLastWriteTime.dwLowDateTime);
        std::string data;
        if (age < 14ull * 24 * 3600 * 10000000ull && ReadFileBytes(file, data)) {
            size_t pos = 0;
            while (pos < data.size()) {
                size_t end = data.find('\n', pos);
                if (end == std::string::npos) end = data.size();
                const std::string line = data.substr(pos, end - pos);
                pos = end + 1;
                std::vector<std::string> f;
                for (size_t a = 0, b; a <= line.size(); a = b + 1) {
                    b = line.find('\t', a);
                    if (b == std::string::npos) b = line.size();
                    f.push_back(line.substr(a, b - a));
                }
                if (f.size() < 6) continue;
                Poi poi;
                poi.x = std::strtof(f[1].c_str(), nullptr);
                poi.y = std::strtof(f[2].c_str(), nullptr);
                poi.type = f[3];
                poi.name = FromUtf8(f[4]);
                poi.mapName = FromUtf8(f[5]);
                table[static_cast<uint32_t>(std::strtoul(f[0].c_str(), nullptr, 10))] = poi;
            }
            if (!table.empty()) return table;
        }
    }
    auto get = [&](const std::wstring& path, JsonValue* out) {
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (stop_) return false;
        }
        const HttpResponse r = HttpsRequest(L"GET", L"api.guildwars2.com", path, L"", "", nullptr, 32u << 20);
        return r.transportOk && r.status == 200 && ParseJson(r.body, *out);
    };
    auto takeMap = [&](const JsonValue& map) {  // one map object: its points
        const std::wstring mapName = FromUtf8(map.GetString("name"));
        const JsonValue* points = map.Get("points_of_interest");
        if (!points || !points->IsObject()) return;
        for (const auto& [key, p] : points->obj) {
            const JsonValue* id = p.Get("id");
            const JsonValue* coord = p.Get("coord");
            if (!id || id->type != JsonValue::Type::Number || !coord || !coord->IsArray() || coord->arr.size() < 2) continue;
            Poi poi;
            poi.name = FromUtf8(p.GetString("name"));
            poi.type = p.GetString("type");
            poi.mapName = mapName;
            poi.x = static_cast<float>(coord->arr[0].n);
            poi.y = static_cast<float>(coord->arr[1].n);
            table[static_cast<uint32_t>(id->n)] = poi;
        }
    };
    std::vector<uint32_t> seenMaps;
    for (int continent : {1, 2}) {
        JsonValue floor;
        if (!get(L"/v2/continents/" + std::to_wstring(continent) + L"/floors/1?lang=" + lang, &floor)) continue;
        const JsonValue* regions = floor.Get("regions");
        if (!regions || !regions->IsObject()) continue;
        for (const auto& [rk, region] : regions->obj) {
            const JsonValue* maps = region.Get("maps");
            if (!maps || !maps->IsObject()) continue;
            for (const auto& [mk, map] : maps->obj) {
                seenMaps.push_back(static_cast<uint32_t>(std::strtoul(mk.c_str(), nullptr, 10)));
                takeMap(map);
            }
        }
    }
    // Public maps on another level (Path of Fire, Icebrood Saga, Janthir …): one by one.
    JsonValue all;
    if (!table.empty() && get(L"/v2/maps?ids=all&lang=" + lang, &all) && all.IsArray()) {
        for (const JsonValue& m : all.arr) {
            auto num = [&](const char* k) {
                const JsonValue* n = m.Get(k);
                return n && n->type == JsonValue::Type::Number ? static_cast<long long>(n->n) : -1000000;
            };
            const long long id = num("id"), c = num("continent_id"), f = num("default_floor"), r = num("region_id");
            if (m.GetString("type") != "Public" || id < 0 || c < 0 || r < 0 || f == -1000000) continue;
            if (std::find(seenMaps.begin(), seenMaps.end(), static_cast<uint32_t>(id)) != seenMaps.end()) continue;
            JsonValue map;
            if (get(L"/v2/continents/" + std::to_wstring(c) + L"/floors/" + std::to_wstring(f) + L"/regions/" +
                        std::to_wstring(r) + L"/maps/" + std::to_wstring(id) + L"?lang=" + lang,
                    &map))
                takeMap(map);
            Sleep(60);  // gently
        }
    }
    if (!table.empty()) {
        std::string out;
        for (const auto& [id, poi] : table) {
            char xy[64];
            snprintf(xy, sizeof(xy), "%.1f\t%.1f", poi.x, poi.y);
            auto clean = [](std::wstring t) {
                for (wchar_t& ch : t)
                    if (ch == L'\t' || ch == L'\n' || ch == L'\r') ch = L' ';
                return ToUtf8(t);
            };
            out += std::to_string(id) + "\t" + xy + "\t" + poi.type + "\t" + clean(poi.name) + "\t" + clean(poi.mapName) + "\n";
        }
        EnsureDir(cacheDir_);
        WriteFileAtomic(file, out);
    }
    return table;
}

void ChatLinks::Tooltip(const std::wstring& code, const Info& info) {
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24);
    // The icon through Nexus (loaded in the background; nothing until it is there).
    if (api_ && api_->Textures_GetOrCreateFromURL && info.iconUrl.rfind("https://render.guildwars2.com/", 0) == 0) {
        const std::string endpoint = info.iconUrl.substr(29);  // "/file/..."
        const std::string id = "GW2KB_ICON_" + std::to_string(std::hash<std::string>{}(endpoint));
        if (Texture_t* t = api_->Textures_GetOrCreateFromURL(id.c_str(), "https://render.guildwars2.com", endpoint.c_str());
            t && t->Resource) {
            const float s = ImGui::GetFontSize() * 2.6f;
            ImGui::Image(t->Resource, ImVec2(s, s));
            ImGui::SameLine();
        }
    }
    ImGui::BeginGroup();
    if (info.state == 1) {
        const std::string name = ToUtf8((info.count > 1 ? std::to_wstring(info.count) + L" " : L"") + info.name);
        if (info.rgb) ImGui::TextColored(Rgb(info.rgb), "%s", name.c_str());
        else ImGui::TextUnformatted(name.c_str());
        const bool poi = info.kind == static_cast<int>(ChatCodeKind::Poi);
        std::wstring sub = poi ? info.type : KindName(info.kind);
        if (!poi && !info.type.empty()) sub += L" · " + info.type;
        if (!info.mapName.empty()) sub += L" · " + info.mapName;
        if (info.level > 0) sub += L" · " + TrF(L"level {1}", {std::to_wstring(info.level)});
        ImGui::TextDisabled("%s", ToUtf8(sub).c_str());
    } else {
        ImGui::TextUnformatted(ToUtf8(KindName(info.kind)).c_str());
        if (info.state == 0) ImGui::TextDisabled("...");
    }
    ImGui::EndGroup();
    if (info.state == 1 && !info.description.empty()) ImGui::TextUnformatted(ToUtf8(info.description).c_str());
    ImGui::TextDisabled("%s", ToUtf8(code + L"  ·  " + (info.kind == static_cast<int>(ChatCodeKind::Poi) && info.state == 1
                                                             ? Tr(L"Click: show it on the map · Shift+click: into your message")
                                                             : Tr(L"Click: into your message"))).c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

std::wstring ChatLinks::LabelOf(const std::wstring& code, const Info& info, ImVec4* color) {
    if (info.state == 1) {
        if (info.rgb) *color = Rgb(info.rgb);
        return L"[" + (info.count > 1 ? std::to_wstring(info.count) + L" " : L"") + info.name + L"]";
    }
    if (info.state == 2 && info.kind != static_cast<int>(ChatCodeKind::Unknown)) return L"[" + KindName(info.kind) + L"]";
    return code;
}

std::wstring ChatLinks::Label(const std::wstring& code) {
    ImVec4 color;
    return LabelOf(code, Get(code), &color);
}

bool ChatLinks::Render(const std::wstring& code, const std::function<void(const std::string&, ImVec4)>& draw) {
    const Info info = Get(code);
    ImVec4 color(0.62f, 0.78f, 1.f, 1.f);  // a link without a rarity
    const std::wstring label = LabelOf(code, info, &color);
    if (draw) {
        draw(ToUtf8(label), color);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextUnformatted(ToUtf8(label).c_str());
        ImGui::PopStyleColor();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        Tooltip(code, info);
    }
    return ImGui::IsItemClicked();
}

}  // namespace gct
