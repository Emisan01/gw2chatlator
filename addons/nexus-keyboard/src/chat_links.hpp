// chat_links.hpp — GW2 chat codes ("[Item name]") in the chat window, as the game links them:
// integrated via PieOrCake's Decoder Ring (metadata & tooltips) and ChatLinks::SegmentLine.
#pragma once

#include <functional>
#include <string>

#include "chat/ChatLinks.h"
#include "chat/DecoderRingApi.h"
#include "imgui/imgui.h"
#include "nexus/Nexus.h"

namespace gct {

class ChatLinks {
public:
    void Start(AddonAPI_t* api);
    void Stop();
    void SetLanguage(const std::wstring& /*apiLang*/) {}

    // Renders the link segment. Returns true if clicked.
    bool Render(const PieUI::ChatLinks::LinkSegment& seg, const std::string& label,
                const std::function<void(const std::string&, ImVec4)>& draw = {});

    // Label for line wrapping and display (e.g. "[Item name]" or "[Item]")
    std::string Label(const PieUI::ChatLinks::LinkSegment& seg);

    // A waypoint: "Commodore's Quarter Waypoint – Lion's Arch" (Decoder Ring), else "" – where the map has to go.
    std::string Where(const PieUI::ChatLinks::LinkSegment& seg);

    // True if Decoder Ring is currently available
    bool HasDecoder() const;

private:
    DecoderRingApi* GetDecoder() const;
    void Tooltip(const PieUI::ChatLinks::LinkSegment& seg, const DecoderRecord& rec, bool resolved);

    AddonAPI_t* api_ = nullptr;
};

}  // namespace gct
