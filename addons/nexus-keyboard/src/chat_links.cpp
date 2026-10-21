#include "chat_links.hpp"

#include <windows.h>

#include <algorithm>
#include <atomic>
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

// Decoder Ring is another add-on (Nexus updates it unasked, like Events: Chat): its Resolve runs behind a guard, into a
// record with room to spare – a newer Decoder Ring with a longer record cannot write past ours –, every string is ended
// inside it, and after one fault it is not asked again this session (the chat codes stay plain text).
std::atomic<bool> g_decoderFault{false};

struct RecordRoom {
    DecoderRecord rec;
    unsigned char spare[sizeof(DecoderRecord)];
};

int ResolveRaw(DecoderRingApi* dr, uint8_t type, uint32_t id, const char* code, DecoderRecord* out) {  // SEH: no objects
    __try {
        auto fn = dr->Resolve;
        if (!fn) return -1;  // zeroed while it unloads
        return static_cast<int>(fn(type, id, code, out));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -2;
    }
}

bool ResolveGuarded(DecoderRingApi* dr, const PieUI::ChatLinks::LinkSegment& seg, DecoderRecord& rec) {
    rec = DecoderRecord{};
    if (!dr || g_decoderFault) return false;
    RecordRoom room{};
    const int r = ResolveRaw(dr, static_cast<uint8_t>(seg.linkType), seg.primaryId, seg.raw.c_str(), &room.rec);
    if (r == -2) g_decoderFault = true;
    if (r != DR_Resolved || room.rec.schemaVersion == 0) return false;
    rec = room.rec;
    rec.name[sizeof(rec.name) - 1] = 0;
    rec.iconUrl[sizeof(rec.iconUrl) - 1] = 0;
    rec.description[sizeof(rec.description) - 1] = 0;
    rec.mapName[sizeof(rec.mapName) - 1] = 0;
    for (DecoderFact& f : rec.facts) {
        f.icon[sizeof(f.icon) - 1] = 0;
        f.text[sizeof(f.text) - 1] = 0;
    }
    if (rec.factCount > 16) rec.factCount = 16;
    return true;
}

void OnDecoderReady(void*) {}
void OnDecoderUnloading(void*) {}
void OnDecoderResolved(void*) {}

}  // namespace

bool DecoderRingFaulted() { return g_decoderFault; }

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
    if (g_decoderFault) return nullptr;
    auto* p = static_cast<DecoderRingApi*>(api_->DataLink_Get(DECODER_RING_DATALINK));
    return (p && p->apiVersion >= DECODER_RING_API_VERSION) ? p : nullptr;
}

bool ChatLinks::HasDecoder() const {
    return GetDecoder() != nullptr;
}

std::string ChatLinks::Label(const PieUI::ChatLinks::LinkSegment& seg) {
    DecoderRecord rec;
    if (ResolveGuarded(GetDecoder(), seg, rec) && rec.name[0] != '\0')
        return "[" + (seg.count > 1 ? std::to_string(seg.count) + " " : "") + std::string(rec.name) + "]";
    if (!seg.display.empty()) return seg.display;
    const std::string kn = ToUtf8(KindName(seg.linkType));
    return "[" + kn + "]";
}

bool ChatLinks::Render(const PieUI::ChatLinks::LinkSegment& seg, const std::string& label,
                       const std::function<void(const std::string&, ImVec4)>& draw) {
    DecoderRecord rec;
    const bool resolved = ResolveGuarded(GetDecoder(), seg, rec);
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

Texture_t* GetTextureGuarded(AddonAPI_t* api, const char* url) {  // SEH: no C++ unwinding
    __try {
        if (!api || !api->Textures_GetOrCreateFromURL || !url || url[0] == '\0') return nullptr;
        return api->Textures_GetOrCreateFromURL(url, url, "");
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

void ChatLinks::Tooltip(const PieUI::ChatLinks::LinkSegment& seg, const DecoderRecord& rec, bool resolved) {
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);

    Texture_t* iconTex = nullptr;
    if (resolved && rec.iconUrl[0] != '\0') {
        iconTex = GetTextureGuarded(api_, rec.iconUrl);
    }
    if (iconTex && iconTex->Resource && iconTex->Width > 0 && iconTex->Height > 0) {
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

    ImGui::TextDisabled("%s", seg.raw.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

}  // namespace gct
