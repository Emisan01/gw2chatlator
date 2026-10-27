#include "chat_links.hpp"

#include <string>

#include "core/i18n.hpp"
#include "core/text.hpp"

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

}  // namespace

void ChatLinks::Start(AddonAPI_t* api) { api_ = api; }

void ChatLinks::Stop() { api_ = nullptr; }

std::string ChatLinks::Label(const PieUI::ChatLinks::LinkSegment& seg) {
    if (!seg.display.empty()) return seg.display;
    return "[" + ToUtf8(KindName(seg.linkType)) + "]";
}

bool ChatLinks::Render(const PieUI::ChatLinks::LinkSegment& seg, const std::string& label,
                       const std::function<void(const std::string&, ImVec4)>& draw) {
    const ImVec4 col(0.62f, 0.78f, 1.f, 1.f);
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
