#include "chat_links.hpp"

#include <windows.h>

#include <algorithm>
#include <string>

#include "core/chat_code.hpp"
#include "core/i18n.hpp"
#include "core/text.hpp"
#include "win/extras_install.hpp"
#include "win/files.hpp"
#include "win/http.hpp"

namespace gct {
namespace {

using PieUI::ChatLinks::ChatLinkType;

std::wstring KindName(ChatLinkType kind) {
    using namespace PieUI::ChatLinks;
    switch (kind) {
        case LINK_ITEM: return Tr(L"Item");
        case LINK_SKILL: return Tr(L"Skill");
        case LINK_TRAIT: return Tr(L"Trait");
        case LINK_RECIPE: return Tr(L"Recipe");
        case LINK_SKIN: return Tr(L"Skin");
        case LINK_OUTFIT: return Tr(L"Outfit");
        case LINK_ACHIEVEMENT: return Tr(L"Achievement");
        case LINK_MAP: return Tr(L"Waypoint / point of interest");
        case LINK_BUILD: return Tr(L"Build template");
        case LINK_COIN: return Tr(L"Coins");
        case LINK_WVW_OBJECTIVE: return Tr(L"WvW objective");
        default: return Tr(L"Chat code");
    }
}

// The API's list for a kind of code. None: a waypoint comes from the table, the others have no name to ask for.
const wchar_t* ApiWhat(ChatLinkType kind) {
    using namespace PieUI::ChatLinks;
    switch (kind) {
        case LINK_ITEM: return L"items";
        case LINK_SKILL: return L"skills";
        case LINK_TRAIT: return L"traits";
        case LINK_SKIN: return L"skins";
        case LINK_OUTFIT: return L"outfits";
        case LINK_ACHIEVEMENT: return L"achievements";
        case LINK_RECIPE: return L"recipes";
        case LINK_WVW_OBJECTIVE: return L"wvw/objectives";
        default: return nullptr;
    }
}

uint64_t MakeKey(uint64_t kind, uint64_t map, uint64_t id) { return (kind << 56) | ((map & 0xFFFFFF) << 32) | (id & 0xFFFFFFFF); }
uint64_t KeyOf(const PieUI::ChatLinks::LinkSegment& seg) { return MakeKey(seg.linkType, seg.secondaryId, seg.primaryId); }

std::wstring RarityName(const std::string& rarity) {
    if (rarity == "Junk") return Tr(L"Junk");
    if (rarity == "Basic") return Tr(L"Basic");
    if (rarity == "Fine") return Tr(L"Fine");
    if (rarity == "Masterwork") return Tr(L"Masterwork");
    if (rarity == "Rare") return Tr(L"Rare");
    if (rarity == "Exotic") return Tr(L"Exotic");
    if (rarity == "Ascended") return Tr(L"Ascended");
    if (rarity == "Legendary") return Tr(L"Legendary");
    return FromUtf8(rarity);
}

// The API's type words; only these kinds of code say something with them.
std::wstring TypeName(ChatLinkType kind, const std::string& type) {
    using namespace PieUI::ChatLinks;
    if (kind == LINK_WVW_OBJECTIVE) {
        if (type == "Camp") return Tr(L"Camp");
        if (type == "Tower") return Tr(L"Tower");
        if (type == "Keep") return Tr(L"Keep");
        if (type == "Castle") return Tr(L"Castle");
        return {};
    }
    if (kind != LINK_ITEM && kind != LINK_SKIN && kind != LINK_RECIPE) return {};
    if (type == "Armor") return Tr(L"Armor");
    if (type == "Back") return Tr(L"Back item");
    if (type == "Bag") return Tr(L"Bag");
    if (type == "Consumable") return Tr(L"Consumable");
    if (type == "Container") return Tr(L"Container");
    if (type == "CraftingMaterial") return Tr(L"Crafting material");
    if (type == "Gathering") return Tr(L"Gathering tool");
    if (type == "Gizmo") return Tr(L"Gizmo");
    if (type == "JadeTechModule") return Tr(L"Jade tech module");
    if (type == "Key") return Tr(L"Key");
    if (type == "MiniPet") return Tr(L"Miniature");
    if (type == "PowerCore") return Tr(L"Power core");
    if (type == "Relic") return Tr(L"Relic");
    if (type == "Tool") return Tr(L"Tool");
    if (type == "Trait") return Tr(L"Trait guide");
    if (type == "Trinket") return Tr(L"Trinket");
    if (type == "Trophy") return Tr(L"Trophy");
    if (type == "UpgradeComponent") return Tr(L"Upgrade component");
    if (type == "Weapon") return Tr(L"Weapon");
    return {};
}

std::wstring PoiName(const std::string& type) {
    if (type == "waypoint") return Tr(L"Waypoint");
    if (type == "vista") return Tr(L"Vista");
    if (type == "unlock") return Tr(L"Unlock point");
    return Tr(L"Point of interest");
}

bool FileIsOlderThanDays(const std::wstring& path, int days) {
    WIN32_FILE_ATTRIBUTE_DATA file{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &file)) return true;
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    const uint64_t t = (static_cast<uint64_t>(now.dwHighDateTime) << 32) | now.dwLowDateTime;
    const uint64_t w = (static_cast<uint64_t>(file.ftLastWriteTime.dwHighDateTime) << 32) | file.ftLastWriteTime.dwLowDateTime;
    return t > w && t - w > static_cast<uint64_t>(days) * 24 * 3600 * 10000000ull;
}

constexpr size_t kMaxPerRequest = 50, kMaxPending = 200, kMaxNames = 5000;
constexpr unsigned long long kRetryMs = 60000, kStuckMs = 30000;  // no answer: one try a minute; a job lost in a full queue
constexpr int kPoiTableDays = 30, kPoiTries = 3;

ImVec4 ColorOf(uint32_t c) { return ImVec4(((c >> 16) & 0xFF) / 255.f, ((c >> 8) & 0xFF) / 255.f, (c & 0xFF) / 255.f, 1.f); }

}  // namespace

void ChatLinks::Start(AddonAPI_t* api, Poster post, std::wstring dataDir) {
    api_ = api;
    std::lock_guard<std::mutex> lock(mu_);
    post_ = std::move(post);
    dataDir_ = std::move(dataDir);
}

void ChatLinks::Stop() {
    api_ = nullptr;
    std::lock_guard<std::mutex> lock(mu_);
    post_ = nullptr;
    pending_.clear();
    names_.clear();
    pois_.clear();
    poiWanted_ = poiLoaded_ = false;
}

void ChatLinks::SetOptions(const std::wstring& readLang, bool names) {
    std::lock_guard<std::mutex> lock(mu_);
    on_ = names;
    if (readLang == readLang_) return;
    readLang_ = readLang;
    const std::wstring apiLang = ChatCodeApiLang(readLang);
    if (apiLang == apiLang_) return;
    apiLang_ = apiLang;
    names_.clear();
    pending_.clear();
    pois_.clear();
    poiWanted_ = poiLoaded_ = false;
    poiFails_ = 0;
    retryAt_ = 0;
}

void ChatLinks::Kick() {
    const unsigned long long now = GetTickCount64();
    if (running_ && now - startedAt_ > kStuckMs) running_ = false;
    const bool work = !pending_.empty() || (poiWanted_ && !poiLoaded_);
    if (running_ || !work || !post_ || now < retryAt_) return;
    running_ = true;
    startedAt_ = now;
    post_([this] { Fetch(); });
}

void ChatLinks::LoadPois() {
    std::wstring lang, dir;
    {
        std::lock_guard<std::mutex> lock(mu_);
        lang = apiLang_;
        dir = dataDir_;
    }
    const std::wstring file = dir + L"\\poi-" + lang + L".tsv";
    if (FileIsOlderThanDays(file, kPoiTableDays)) {  // missing or old; a failed download leaves what is there
        DownloadReleaseAsset(L"/repos/Emisan01/gw2chatlator/releases/tags/extras", "https://github.com/Emisan01/gw2chatlator/releases/download/",
                             L"GW2Keyboard-POI-" + lang + L".tsv", file, {});
    }
    std::unordered_map<uint32_t, Name> table;
    std::string body;
    if (ReadFileBytes(file, body)) {
        size_t at = 0;
        while (at < body.size()) {
            size_t end = body.find('\n', at);
            if (end == std::string::npos) end = body.size();
            ChatCodePoi poi;
            if (ParseChatCodePoi(body.substr(at, end - at), &poi)) {
                Name& name = table[poi.id];
                name.text = std::move(poi.name);
                name.type = std::move(poi.type);
                name.where = std::move(poi.map);
                name.continent = poi.continent;
                name.floor = poi.floor;
                name.x = poi.x;
                name.y = poi.y;
            }
            at = end + 1;
        }
    }
    std::lock_guard<std::mutex> lock(mu_);
    running_ = false;
    if (lang == apiLang_) {
        if (!table.empty()) {
            pois_ = std::move(table);
            poiLoaded_ = true;
        } else if (++poiFails_ >= kPoiTries) {
            poiLoaded_ = true;  // not this session: the code shows its kind
        } else {
            retryAt_ = GetTickCount64() + kRetryMs;
        }
    }
    Kick();
}

void ChatLinks::Fetch() {
    std::vector<uint64_t> keys;
    std::vector<uint32_t> ids;
    std::vector<std::wstring> objectives;
    std::wstring lang;
    uint64_t kind = 0;
    bool pois = false;
    {
        std::lock_guard<std::mutex> lock(mu_);
        pois = poiWanted_ && !poiLoaded_;
        if (!pois) {
            if (pending_.empty()) {
                running_ = false;
                return;
            }
            kind = pending_.front() >> 56;  // one list per request
            for (auto it = pending_.begin(); it != pending_.end() && keys.size() < kMaxPerRequest;) {
                if ((*it >> 56) == kind) {
                    keys.push_back(*it);
                    ids.push_back(static_cast<uint32_t>(*it));
                    objectives.push_back(std::to_wstring((*it >> 32) & 0xFFFFFF) + L"-" + std::to_wstring(static_cast<uint32_t>(*it)));
                    it = pending_.erase(it);
                } else {
                    ++it;
                }
            }
            lang = apiLang_;
        }
    }
    if (pois) {
        LoadPois();
        return;
    }
    const auto type = static_cast<ChatLinkType>(kind);
    const wchar_t* what = ApiWhat(type);
    const auto ask = [&lang](const std::wstring& path, bool* answered) {
        const HttpResponse r = HttpsRequest(L"GET", L"api.guildwars2.com", path, L"", "", nullptr, 2u << 20, 5000);
        // 206: some of them exist; 404: none of them does – an answer as well, those are not asked again.
        *answered = r.transportOk && (r.status == 200 || r.status == 206 || r.status == 404);
        return *answered ? ParseChatCodeNames(r.body) : std::vector<ChatCodeName>{};
    };
    bool answered = what == nullptr;
    std::vector<ChatCodeName> got;
    if (what) got = ask(type == PieUI::ChatLinks::LINK_WVW_OBJECTIVE ? ChatCodeObjectivesPath(objectives, lang) : ChatCodeNamesPath(what, ids, lang), &answered);
    if (answered && type == PieUI::ChatLinks::LINK_RECIPE) {  // a recipe has no name of its own: what it makes has
        std::vector<uint32_t> items;
        for (const ChatCodeName& recipe : got)
            if (recipe.outputItem) items.push_back(recipe.outputItem);
        if (!items.empty()) {
            const std::vector<ChatCodeName> made = ask(ChatCodeNamesPath(L"items", items, lang), &answered);
            for (ChatCodeName& recipe : got)
                for (const ChatCodeName& item : made)
                    if (item.id == recipe.outputItem) {
                        recipe.name = item.name;
                        recipe.rarity = item.rarity;
                        recipe.type = item.type;
                        recipe.description = item.description;
                        recipe.level = item.level;
                    }
        }
    }
    std::lock_guard<std::mutex> lock(mu_);
    running_ = false;
    if (lang == apiLang_) {
        if (answered) {
            if (names_.size() > kMaxNames) names_.clear();
            for (const uint64_t key : keys) names_[key];  // known now, a name or none
            for (const ChatCodeName& n : got) {
                Name& name = names_[MakeKey(kind, n.mapId, n.id)];
                name.text = n.name;
                name.rarity = n.rarity;
                name.type = n.type;
                name.description = CleanChatCodeText(n.description);
                name.level = n.level;
                name.rating = n.rating;
                name.color = RarityColor(n.rarity);
            }
        } else {
            retryAt_ = GetTickCount64() + kRetryMs;
            pending_.insert(pending_.end(), keys.begin(), keys.end());
        }
    }
    Kick();
}

bool ChatLinks::PoisLoaded() {
    std::lock_guard<std::mutex> lock(mu_);
    return poiLoaded_;
}

bool ChatLinks::Poi(uint32_t id, PoiView* out) {
    std::lock_guard<std::mutex> lock(mu_);
    const auto it = pois_.find(id);
    if (it == pois_.end()) {
        if (on_ && !poiLoaded_) {
            poiWanted_ = true;
            Kick();
        }
        return false;
    }
    out->id = id;
    out->name = it->second.text;
    out->type = it->second.type;
    out->map = it->second.where;
    out->continent = it->second.continent;
    out->floor = it->second.floor;
    out->x = it->second.x;
    out->y = it->second.y;
    return true;
}

std::vector<ChatLinks::PoiView> ChatLinks::PoisIn(int continent, int floor, float left, float top, float right, float bottom) {
    std::vector<PoiView> found;
    std::lock_guard<std::mutex> lock(mu_);
    for (const auto& entry : pois_) {
        const Name& n = entry.second;
        if (n.continent != continent || n.floor != floor || n.x < left || n.x > right || n.y < top || n.y > bottom) continue;
        PoiView view;
        view.id = entry.first;
        view.name = n.text;
        view.type = n.type;
        view.map = n.where;
        view.continent = n.continent;
        view.floor = n.floor;
        view.x = n.x;
        view.y = n.y;
        found.push_back(std::move(view));
        if (found.size() >= 500) break;
    }
    return found;
}

bool ChatLinks::Find(const PieUI::ChatLinks::LinkSegment& seg, Name* out) {
    if (!on_ || !seg.primaryId) return false;
    if (seg.linkType == PieUI::ChatLinks::LINK_MAP) {
        const auto it = pois_.find(seg.primaryId);
        if (it == pois_.end()) return false;
        *out = it->second;
        return true;
    }
    const auto it = names_.find(KeyOf(seg));
    if (it == names_.end()) return false;
    *out = it->second;
    return true;
}

std::string ChatLinks::Label(const PieUI::ChatLinks::LinkSegment& seg) {
    using namespace PieUI::ChatLinks;
    if (seg.primaryId) {
        std::lock_guard<std::mutex> lock(mu_);
        if (on_ && seg.linkType == LINK_MAP) {
            const auto it = pois_.find(seg.primaryId);
            if (it != pois_.end()) return "[" + it->second.text + "]";
            if (!poiLoaded_) {
                poiWanted_ = true;
                Kick();
            }
        } else if (on_ && ApiWhat(seg.linkType)) {
            const uint64_t key = KeyOf(seg);
            const auto it = names_.find(key);
            if (it != names_.end()) {
                if (!it->second.text.empty()) {
                    if (seg.linkType == LINK_RECIPE) return "[" + ToUtf8(Tr(L"Recipe")) + ": " + it->second.text + "]";
                    return "[" + (seg.count > 1 ? std::to_string(seg.count) + " " : std::string()) + it->second.text + "]";
                }
            } else {
                if (pending_.size() < kMaxPending && std::find(pending_.begin(), pending_.end(), key) == pending_.end()) pending_.push_back(key);
                Kick();
            }
        }
    }
    if (seg.linkType == LINK_MAP) return "[" + ToUtf8(Tr(L"Waypoint")) + "]";
    if (seg.linkType == LINK_BUILD || !ApiWhat(seg.linkType)) {  // a build names its class, the rest what the code is
        if (!seg.display.empty()) return seg.display;
    }
    return "[" + ToUtf8(KindName(seg.linkType)) + "]";
}

bool ChatLinks::Render(const PieUI::ChatLinks::LinkSegment& seg, const std::string& label,
                       const std::function<void(const std::string&, ImVec4)>& draw) {
    ImVec4 col(0.62f, 0.78f, 1.f, 1.f);
    {  // an item in its rarity's colour, as in the game
        std::lock_guard<std::mutex> lock(mu_);
        Name name;
        if (Find(seg, &name) && name.color) col = ColorOf(name.color);
    }
    if (draw) {
        draw(label, col);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        ImGui::TextUnformatted(label.c_str());
        ImGui::PopStyleColor();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        Tooltip(seg);
    }
    return ImGui::IsItemClicked();
}

void ChatLinks::Tooltip(const PieUI::ChatLinks::LinkSegment& seg) {  // what it is, as far as known, and the code itself
    using namespace PieUI::ChatLinks;
    Name name;
    bool known = false;
    {
        std::lock_guard<std::mutex> lock(mu_);
        known = Find(seg, &name) && !name.text.empty();
    }
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
    if (known) {
        const std::string title = (seg.count > 1 && seg.linkType == LINK_ITEM ? std::to_string(seg.count) + " " : std::string()) + name.text;
        ImGui::TextColored(name.color ? ColorOf(name.color) : ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", title.c_str());
    }
    std::wstring line = seg.linkType == LINK_MAP && known ? PoiName(name.type) : KindName(seg.linkType);
    const auto add = [&line](const std::wstring& part) {
        if (!part.empty()) line += L" · " + part;
    };
    if (known) {
        if (!name.rarity.empty()) add(RarityName(name.rarity));
        add(TypeName(seg.linkType, name.type));
        if (name.level > 0) add(TrF(L"Level {1}", {std::to_wstring(name.level)}));
        if (name.rating > 0) add(TrF(L"Crafting {1}", {std::to_wstring(name.rating)}));
        if (!name.where.empty()) add(FromUtf8(name.where));
    }
    ImGui::TextUnformatted(ToUtf8(line).c_str());
    if (known && !name.description.empty()) ImGui::TextUnformatted(name.description.c_str());
    ImGui::TextDisabled("%s", seg.raw.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

}  // namespace gct
