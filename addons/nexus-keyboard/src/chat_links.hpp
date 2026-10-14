// chat_links.hpp — GW2 chat codes ("[&AgGvWwEA]") in the chat window, as the game links them: "[Item name]" in its
// rarity colour, and on mouse-over what it is (icon, type, level, description). A small module of its own: it decodes
// the code (core/chat_code), asks the public GW2 API on its own thread and keeps the answers.
//
// Chat codes have been misused (floods, odd payloads): only short codes of known kinds are looked up, at most a few
// requests a minute, the cache is capped, and what comes back is only shown – never acted on (invariant 4).
#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include "imgui/imgui.h"
#include "nexus/Nexus.h"

namespace gct {

class ChatLinks {
public:
    struct Info {
        int state = 0;              // 0 asked, 1 known, 2 nothing to show (unknown kind, failed, no name)
        std::wstring name;          // "Polysaturating Reverberating Infusion (Gray)"
        int kind = 0;               // ChatCodeKind
        std::wstring type;          // "UpgradeComponent · Infusion" (items, skins), else empty
        std::wstring description;   // markup removed, at most ~400 characters
        std::string iconUrl;        // https://render.guildwars2.com/file/...png
        int level = 0;
        int count = 1;
        uint32_t rgb = 0;           // rarity colour, 0 = none
        float x = 0, y = 0;         // waypoints / points of interest: continent coordinates
        std::wstring mapName;       // ... and the map they are on
    };

    // The thread; `api` for icons (Textures_GetOrCreateFromURL), may be null; `cacheDir` keeps the waypoint table.
    void Start(AddonAPI_t* api, const std::wstring& cacheDir);
    void Stop();
    void SetLanguage(const std::wstring& apiLang);  // "en", "de", "fr", "es", "zh"

    // Draws the code as the game links it (on the current line); on hover the tooltip. Game thread only.
    // True when it was clicked (the caller may put the code into the message).
    // `draw` draws the label in its colour (the window's look); without it: plain ImGui text.
    bool Render(const std::wstring& code, const std::function<void(const std::string&, ImVec4)>& draw = {});
    // The text Render draws ("[Item name]", "[Waypoint]", or the code while it is looked up) – for line wrapping.
    std::wstring Label(const std::wstring& code);
    // A waypoint / point of interest whose place is known: its continent coordinates (for the world map).
    bool Target(const std::wstring& code, float* x, float* y);

private:
    Info Get(const std::wstring& code);  // the cached answer (queues the lookup once)
    static std::wstring LabelOf(const std::wstring& code, const Info& info, ImVec4* color);
    void Run();
    Info Fetch(const std::wstring& code, const std::wstring& lang);
    void Tooltip(const std::wstring& code, const Info& info);
    // Waypoints and points of interest have no API lookup of their own: the whole table (every map's points with
    // their coordinates) is loaded once – from the cache file, else from the GW2 API – and kept for 14 days.
    struct Poi {
        std::wstring name, mapName;
        std::string type;  // "waypoint", "landmark", "vista", "unlock"
        float x = 0, y = 0;
    };
    std::unordered_map<uint32_t, Poi> LoadPois(const std::wstring& lang);
    Info PoiInfo(uint32_t id) const;  // mu_ held
    std::unordered_map<uint32_t, Poi> pois_;
    int poisState_ = 0;  // 0 not asked, 1 loading, 2 there, 3 failed
    std::wstring cacheDir_;

    AddonAPI_t* api_ = nullptr;
    std::mutex mu_;
    std::condition_variable cv_;
    std::map<std::wstring, Info> cache_;
    std::deque<std::wstring> queue_;
    std::wstring lang_ = L"en";
    bool stop_ = false;
    std::thread thread_;
};

}  // namespace gct
