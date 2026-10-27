// chat_links.hpp — GW2 chat codes ("[Item name]") in the chat window, as the game links them (ChatLinks::SegmentLine):
// the name the game sent, else the code's kind; on hover its kind and the code itself.
#pragma once

#include <functional>
#include <string>

#include "chat/ChatLinks.h"
#include "imgui/imgui.h"
#include "nexus/Nexus.h"

namespace gct {

class ChatLinks {
public:
    void Start(AddonAPI_t* api);
    void Stop();
    void SetLanguage(const std::wstring& /*apiLang*/) {}

    // Renders the link segment (details on hover). Returns true if clicked – the caller does nothing with it.
    bool Render(const PieUI::ChatLinks::LinkSegment& seg, const std::string& label,
                const std::function<void(const std::string&, ImVec4)>& draw = {});

    // Label for line wrapping and display (e.g. "[Item name]" or "[Item]")
    std::string Label(const PieUI::ChatLinks::LinkSegment& seg);

private:
    void Tooltip(const PieUI::ChatLinks::LinkSegment& seg);

    AddonAPI_t* api_ = nullptr;
};

}  // namespace gct
