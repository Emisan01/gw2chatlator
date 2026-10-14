// chat_links.hpp — GW2 chat codes ("[&AgGvWwEA]") in the chat window, as the game links them: "[Item name]" in its
// rarity colour, and on mouse-over what it is (icon, type, level, description). A small module of its own: it decodes
// the code (core/chat_code), asks the public GW2 API on its own thread and keeps the answers.
//
// Chat codes have been misused (floods, odd payloads): only short codes of known kinds are looked up, at most a few
// requests a minute, the cache is capped, and what comes back is only shown – never acted on (invariant 4).
#pragma once

#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>

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
    };

    void Start(AddonAPI_t* api);  // the thread; `api` for icons (Textures_GetOrCreateFromURL), may be null
    void Stop();
    void SetLanguage(const std::wstring& apiLang);  // "en", "de", "fr", "es", "zh"

    // Draws the code as the game links it (on the current line); on hover the tooltip. Game thread only.
    // True when it was clicked (the caller may put the code into the message).
    bool Render(const std::wstring& code);
    // The text Render draws ("[Item name]", "[Waypoint]", or the code while it is looked up) – for line wrapping.
    std::wstring Label(const std::wstring& code);

private:
    Info Get(const std::wstring& code);  // the cached answer (queues the lookup once)
    static std::wstring LabelOf(const std::wstring& code, const Info& info, ImVec4* color);
    void Run();
    Info Fetch(const std::wstring& code, const std::wstring& lang);
    void Tooltip(const std::wstring& code, const Info& info);

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
