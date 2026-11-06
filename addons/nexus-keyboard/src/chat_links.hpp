// chat_links.hpp — GW2 chat codes ("[Item name]") in the chat window, as the game links them (ChatLinks::SegmentLine):
// the name in the reading language, until it is there the code's kind; on hover what the code is. Names come from GW2's
// public API, in the background, and stay for the session: only the code's number goes out. Waypoints cannot be asked by
// their number there: their names come from a small table (tools/gen_pois.py) in the extras release, kept beside the settings.
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
    struct PoiView {
        uint32_t id = 0;
        std::string name, type, map;
        int continent = 1, floor = 1;
        float x = 0, y = 0;
    };
    bool Poi(uint32_t id, PoiView* out);  // false while the table is not there (it is asked for)
    bool PoisLoaded();  // the table has come, or will not come this session
    std::vector<PoiView> PoisIn(int continent, int floor, float left, float top, float right, float bottom);
    void Start(AddonAPI_t* api, Poster post, std::wstring dataDir);
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
        std::string rarity, type, description, where;  // for the tooltip; where: a waypoint's map
        int level = -1, rating = -1;
        int continent = 1, floor = 1;  // a waypoint
        float x = 0, y = 0;
        uint32_t color = 0;  // 0xRRGGBB of an item's rarity, 0 none
    };
    bool Find(const PieUI::ChatLinks::LinkSegment& seg, Name* out);  // mu_ held
    void Tooltip(const PieUI::ChatLinks::LinkSegment& seg);
    void Kick();      // mu_ held: the next request on its way, if one may go
    void Fetch();     // background
    void LoadPois();  // background

    AddonAPI_t* api_ = nullptr;
    std::mutex mu_;  // never held while another lock of ours is taken, except a job being posted
    Poster post_;
    std::wstring dataDir_;
    std::unordered_map<uint64_t, Name> names_;  // kind, map and id: KeyOf
    std::unordered_map<uint32_t, Name> pois_;   // waypoints and points of interest of the reading language
    std::vector<uint64_t> pending_;
    bool poiWanted_ = false, poiLoaded_ = false;
    int poiFails_ = 0;
    std::wstring readLang_ = L"\x01", apiLang_ = L"en";
    bool on_ = true, running_ = false;
    unsigned long long startedAt_ = 0, retryAt_ = 0;
};

}  // namespace gct
