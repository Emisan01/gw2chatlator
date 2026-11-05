#include "chat_links.hpp"

#include <windows.h>

#include <algorithm>
#include <string>

#include "core/chat_code.hpp"
#include "core/i18n.hpp"
#include "core/text.hpp"
#include "win/http.hpp"

namespace gct {
namespace {

std::wstring KindName(PieUI::ChatLinks::ChatLinkType kind) {
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

// The API's list for a kind of code. None: recipes have no name of their own, waypoints are not asked one by one.
const wchar_t* ApiWhat(PieUI::ChatLinks::ChatLinkType kind) {
    using namespace PieUI::ChatLinks;
    switch (kind) {
        case LINK_ITEM: return L"items";
        case LINK_SKILL: return L"skills";
        case LINK_TRAIT: return L"traits";
        case LINK_SKIN: return L"skins";
        case LINK_OUTFIT: return L"outfits";
        case LINK_ACHIEVEMENT: return L"achievements";
        default: return nullptr;
    }
}

uint64_t KeyOf(const PieUI::ChatLinks::LinkSegment& seg) { return (static_cast<uint64_t>(seg.linkType) << 32) | seg.primaryId; }

constexpr size_t kMaxPerRequest = 50, kMaxPending = 200, kMaxNames = 5000;
constexpr unsigned long long kRetryMs = 60000, kStuckMs = 30000;  // no answer: one try a minute; a job lost in a full queue

}  // namespace

void ChatLinks::Start(AddonAPI_t* api, Poster post) {
    api_ = api;
    std::lock_guard<std::mutex> lock(mu_);
    post_ = std::move(post);
}

void ChatLinks::Stop() {
    api_ = nullptr;
    std::lock_guard<std::mutex> lock(mu_);
    post_ = nullptr;
    pending_.clear();
    names_.clear();
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
    retryAt_ = 0;
}

void ChatLinks::Kick() {
    const unsigned long long now = GetTickCount64();
    if (running_ && now - startedAt_ > kStuckMs) running_ = false;
    if (running_ || pending_.empty() || !post_ || now < retryAt_) return;
    running_ = true;
    startedAt_ = now;
    post_([this] { Fetch(); });
}

void ChatLinks::Fetch() {
    std::vector<uint64_t> keys;
    std::vector<uint32_t> ids;
    std::wstring lang;
    const wchar_t* what = nullptr;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (pending_.empty()) {
            running_ = false;
            return;
        }
        const uint64_t kind = pending_.front() >> 32;  // one list per request
        what = ApiWhat(static_cast<PieUI::ChatLinks::ChatLinkType>(kind));
        for (auto it = pending_.begin(); it != pending_.end() && keys.size() < kMaxPerRequest;) {
            if ((*it >> 32) == kind) {
                keys.push_back(*it);
                ids.push_back(static_cast<uint32_t>(*it));
                it = pending_.erase(it);
            } else {
                ++it;
            }
        }
        lang = apiLang_;
    }
    HttpResponse r;
    if (what) r = HttpsRequest(L"GET", L"api.guildwars2.com", ChatCodeNamesPath(what, ids, lang), L"", "", nullptr, 2u << 20, 5000);
    // 206: some of them exist; 404: none of them does – an answer as well, those are not asked again.
    const bool answered = !what || (r.transportOk && (r.status == 200 || r.status == 206 || r.status == 404));
    const std::vector<ChatCodeName> got = answered ? ParseChatCodeNames(r.body) : std::vector<ChatCodeName>{};
    std::lock_guard<std::mutex> lock(mu_);
    running_ = false;
    if (lang == apiLang_) {
        if (answered) {
            if (names_.size() > kMaxNames) names_.clear();
            for (const uint64_t key : keys) names_[key];  // known now, a name or none
            const uint64_t high = keys.empty() ? 0 : keys.front() & 0xFFFFFFFF00000000ull;
            for (const ChatCodeName& n : got) {
                Name& name = names_[high | n.id];
                name.text = n.name;
                name.color = RarityColor(n.rarity);
            }
        } else {
            retryAt_ = GetTickCount64() + kRetryMs;
            pending_.insert(pending_.end(), keys.begin(), keys.end());
        }
    }
    Kick();
}

std::string ChatLinks::Label(const PieUI::ChatLinks::LinkSegment& seg) {
    if (seg.primaryId && ApiWhat(seg.linkType)) {
        std::lock_guard<std::mutex> lock(mu_);
        if (on_) {
            const uint64_t key = KeyOf(seg);
            const auto it = names_.find(key);
            if (it != names_.end()) {
                if (!it->second.text.empty())
                    return "[" + (seg.count > 1 ? std::to_string(seg.count) + " " : std::string()) + it->second.text + "]";
            } else {
                if (pending_.size() < kMaxPending && std::find(pending_.begin(), pending_.end(), key) == pending_.end()) pending_.push_back(key);
                Kick();
            }
        }
    }
    if (!seg.display.empty()) return seg.display;
    return "[" + ToUtf8(KindName(seg.linkType)) + "]";
}

bool ChatLinks::Render(const PieUI::ChatLinks::LinkSegment& seg, const std::string& label,
                       const std::function<void(const std::string&, ImVec4)>& draw) {
    ImVec4 col(0.62f, 0.78f, 1.f, 1.f);
    {  // an item in its rarity's colour, as in the game
        std::lock_guard<std::mutex> lock(mu_);
        const auto it = on_ ? names_.find(KeyOf(seg)) : names_.end();
        if (it != names_.end() && it->second.color) {
            const uint32_t c = it->second.color;
            col = ImVec4(((c >> 16) & 0xFF) / 255.f, ((c >> 8) & 0xFF) / 255.f, (c & 0xFF) / 255.f, 1.f);
        }
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

void ChatLinks::Tooltip(const PieUI::ChatLinks::LinkSegment& seg) {  // its kind and the code itself
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
    ImGui::TextUnformatted(ToUtf8(KindName(seg.linkType)).c_str());
    ImGui::TextDisabled("%s", seg.raw.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

}  // namespace gct
