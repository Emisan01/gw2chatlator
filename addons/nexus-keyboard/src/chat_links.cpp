#include "chat_links.hpp"

#include <algorithm>
#include <string>

#include "core/i18n.hpp"
#include "core/text.hpp"

namespace gct {
namespace {

ImVec4 RarityColor(uint8_t rarity) {
    switch (rarity) {
        case DR_Junk:       return ImVec4(0.66f, 0.66f, 0.68f, 1.f);
        case DR_Basic:      return ImVec4(1.0f, 1.0f, 1.0f, 1.f);
        case DR_Fine:       return ImVec4(0.38f, 0.64f, 0.85f, 1.f);
        case DR_Masterwork: return ImVec4(0.10f, 0.70f, 0.10f, 1.f);
        case DR_Rare:       return ImVec4(0.99f, 0.82f, 0.05f, 1.f);
        case DR_Exotic:     return ImVec4(1.0f, 0.64f, 0.0f, 1.f);
        case DR_Ascended:   return ImVec4(0.98f, 0.24f, 0.55f, 1.f);
        case DR_Legendary:  return ImVec4(0.61f, 0.27f, 0.97f, 1.f);
        default:            return ImVec4(0.62f, 0.78f, 1.f, 1.f);
    }
}

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

void OnDecoderReady(void*) {}
void OnDecoderUnloading(void*) {}
void OnDecoderResolved(void*) {}

}  // namespace

void ChatLinks::Start(AddonAPI_t* api) {
    api_ = api;
    if (api_) {
        if (api_->Events_Subscribe) {
            api_->Events_Subscribe(EV_DECODER_RING_READY, OnDecoderReady);
            api_->Events_Subscribe(EV_DECODER_RING_UNLOADING, OnDecoderUnloading);
            api_->Events_Subscribe(EV_DECODER_RING_RESOLVED, OnDecoderResolved);
        }
        if (api_->Events_Raise) {
            api_->Events_Raise(EV_DECODER_RING_PING, nullptr);
        }
    }
}

void ChatLinks::Stop() {
    if (api_) {
        if (api_->Events_Unsubscribe) {
            api_->Events_Unsubscribe(EV_DECODER_RING_READY, OnDecoderReady);
            api_->Events_Unsubscribe(EV_DECODER_RING_UNLOADING, OnDecoderUnloading);
            api_->Events_Unsubscribe(EV_DECODER_RING_RESOLVED, OnDecoderResolved);
        }
        api_ = nullptr;
    }
}

DecoderRingApi* ChatLinks::GetDecoder() const {
    if (!api_ || !api_->DataLink_Get) return nullptr;
    auto* p = static_cast<DecoderRingApi*>(api_->DataLink_Get(DECODER_RING_DATALINK));
    return (p && p->apiVersion >= DECODER_RING_API_VERSION) ? p : nullptr;
}

bool ChatLinks::HasDecoder() const {
    return GetDecoder() != nullptr;
}

std::string ChatLinks::Label(const PieUI::ChatLinks::LinkSegment& seg) {
    DecoderRingApi* dr = GetDecoder();
    if (dr) {
        DecoderRecord rec{};
        if (dr->Resolve(static_cast<uint8_t>(seg.linkType), seg.primaryId, seg.raw.c_str(), &rec) == DR_Resolved) {
            if (rec.name[0] != '\0') {
                return "[" + (seg.count > 1 ? std::to_string(seg.count) + " " : "") + std::string(rec.name) + "]";
            }
        }
    }
    if (!seg.display.empty()) return seg.display;
    const std::string kn = ToUtf8(KindName(seg.linkType));
    return "[" + kn + "]";
}

std::string ChatLinks::Where(const PieUI::ChatLinks::LinkSegment& seg) {
    DecoderRingApi* dr = GetDecoder();
    DecoderRecord rec{};
    if (!dr || dr->Resolve(static_cast<uint8_t>(seg.linkType), seg.primaryId, seg.raw.c_str(), &rec) != DR_Resolved ||
        rec.name[0] == '\0')
        return {};
    return rec.mapName[0] ? std::string(rec.name) + " – " + rec.mapName : std::string(rec.name);
}

bool ChatLinks::Render(const PieUI::ChatLinks::LinkSegment& seg, const std::string& label,
                       const std::function<void(const std::string&, ImVec4)>& draw) {
    DecoderRingApi* dr = GetDecoder();
    DecoderRecord rec{};
    bool resolved = false;
    if (dr) {
        if (dr->Resolve(static_cast<uint8_t>(seg.linkType), seg.primaryId, seg.raw.c_str(), &rec) == DR_Resolved) {
            resolved = true;
        }
    }
    const ImVec4 col = resolved ? RarityColor(rec.rarity) : ImVec4(0.62f, 0.78f, 1.f, 1.f);
    if (draw) {
        draw(label, col);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        ImGui::TextUnformatted(label.c_str());
        ImGui::PopStyleColor();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        Tooltip(seg, rec, resolved);
    }
    return ImGui::IsItemClicked();
}

void ChatLinks::Tooltip(const PieUI::ChatLinks::LinkSegment& seg, const DecoderRecord& rec, bool resolved) {
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);

    Texture_t* iconTex = nullptr;
    if (resolved && rec.iconUrl[0] != '\0' && api_ && api_->Textures_GetOrCreateFromURL) {
        iconTex = api_->Textures_GetOrCreateFromURL(rec.iconUrl, rec.iconUrl, nullptr);
    }
    if (iconTex && iconTex->Resource) {
        const float sz = ImGui::GetFontSize() * 2.2f;
        ImGui::Image(iconTex->Resource, ImVec2(sz, sz));
        ImGui::SameLine();
    }

    ImGui::BeginGroup();
    if (resolved && rec.name[0] != '\0') {
        const std::string name = (seg.count > 1 ? std::to_string(seg.count) + " " : "") + std::string(rec.name);
        ImGui::TextColored(RarityColor(rec.rarity), "%s", name.c_str());
        if (rec.linkType == PieUI::ChatLinks::LINK_MAP) {
            if (rec.mapName[0] != '\0') {
                ImGui::TextDisabled("%s", rec.mapName);
            }
        } else {
            ImGui::TextDisabled("%s", ToUtf8(KindName(seg.linkType)).c_str());
        }
    } else {
        const std::string kn = ToUtf8(KindName(seg.linkType));
        ImGui::TextUnformatted(kn.c_str());
        if (GetDecoder()) {
            ImGui::TextDisabled("...");
        }
    }
    ImGui::EndGroup();

    if (resolved) {
        if (rec.description[0] != '\0') {
            ImGui::TextUnformatted(rec.description);
        }
        const ImVec4 green(0.55f, 0.85f, 0.45f, 1.f);
        for (uint8_t i = 0; i < rec.factCount && i < 16; ++i) {
            if (rec.facts[i].text[0] != '\0') {
                ImGui::TextColored(green, "%s", rec.facts[i].text);
            }
        }
    }

    ImGui::TextDisabled("%s", (seg.raw + "  ·  " + ToUtf8(seg.linkType == PieUI::ChatLinks::LINK_MAP
                                                            ? Tr(L"Click: find it in GW2's chat (it opens the map)")
                                                            : Tr(L"Click: open it in GW2's chat"))).c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

}  // namespace gct
