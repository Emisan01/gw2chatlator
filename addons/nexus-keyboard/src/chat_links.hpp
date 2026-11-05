// chat_links.hpp — GW2 chat codes ("[Item name]") in the chat window, as the game links them (ChatLinks::SegmentLine):
// the name from GW2's public API in the reading language, until it is there the code's kind; on hover its kind and the
// code itself. Only the code's number goes out, in the background; names stay for the session.
#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "chat/ChatLinks.h"
#include "imgui/imgui.h"
#include "nexus/Nexus.h"

namespace gct {

class ChatLinks {
public:
    using Poster = std::function<void(std::function<void()>)>;  // hands a job to a background thread
    void Start(AddonAPI_t* api, Poster post);
    void Stop();
    // Names on or off, and in which language (what is known in another one goes).
    void SetOptions(const std::wstring& readLang, bool names);

    // Renders the link segment (details on hover). Returns true if clicked – the caller does nothing with it.
    bool Render(const PieUI::ChatLinks::LinkSegment& seg, const std::string& label,
                const std::function<void(const std::string&, ImVec4)>& draw = {});

    // Label for line wrapping and display (e.g. "[6 Item name]" or "[Item]")
    std::string Label(const PieUI::ChatLinks::LinkSegment& seg);

private:
    struct Name {
        std::string text;    // empty: the API knows none, not asked again
        uint32_t color = 0;  // 0xRRGGBB of an item's rarity, 0 none
    };
    void Tooltip(const PieUI::ChatLinks::LinkSegment& seg);
    void Kick();   // mu_ held: the next request on its way, if one may go
    void Fetch();  // background

    AddonAPI_t* api_ = nullptr;
    std::mutex mu_;  // never held while another lock of ours is taken, except a job being posted
    Poster post_;
    std::unordered_map<uint64_t, Name> names_;  // kind << 32 | id
    std::vector<uint64_t> pending_;
    std::wstring readLang_ = L"\x01", apiLang_ = L"en";
    bool on_ = true, running_ = false;
    unsigned long long startedAt_ = 0, retryAt_ = 0;
};

}  // namespace gct
