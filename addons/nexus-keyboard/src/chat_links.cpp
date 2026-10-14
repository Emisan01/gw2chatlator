#include "chat_links.hpp"

#include <windows.h>

#include <algorithm>

#include "core/chat_code.hpp"
#include "core/i18n.hpp"
#include "core/json.hpp"
#include "core/text.hpp"
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

ImVec4 Rgb(uint32_t rgb) {
    return ImVec4(((rgb >> 16) & 0xFF) / 255.f, ((rgb >> 8) & 0xFF) / 255.f, (rgb & 0xFF) / 255.f, 1.f);
}

}  // namespace

void ChatLinks::Start(AddonAPI_t* api) {
    api_ = api;
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
}

ChatLinks::Info ChatLinks::Get(const std::wstring& code) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = cache_.find(code);
    if (it != cache_.end()) return it->second;
    Info asked;
    const ChatCode c = DecodeChatCode(code);
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
    std::wstring type = FromUtf8(v.GetString("type"));
    if (const JsonValue* d = v.Get("details"); d && d->IsObject() && !d->GetString("type").empty())
        type += L" · " + FromUtf8(d->GetString("type"));
    info.type = type;
    if (!info.name.empty()) info.state = 1;
    return info;
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
        std::wstring sub = KindName(info.kind);
        if (!info.type.empty()) sub += L" · " + info.type;
        if (info.level > 0) sub += L" · " + TrF(L"level {1}", {std::to_wstring(info.level)});
        ImGui::TextDisabled("%s", ToUtf8(sub).c_str());
    } else {
        ImGui::TextUnformatted(ToUtf8(KindName(info.kind)).c_str());
        if (info.state == 0) ImGui::TextDisabled("...");
    }
    ImGui::EndGroup();
    if (info.state == 1 && !info.description.empty()) ImGui::TextUnformatted(ToUtf8(info.description).c_str());
    ImGui::TextDisabled("%s", ToUtf8(code + L"  ·  " + Tr(L"Click: into your message")).c_str());
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
