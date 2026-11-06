// kb_waypoint.cpp — a waypoint from the chat. A click on it opens a preview: GW2's own map tiles around the point, the points
// near it, a list of your last ones; nothing goes to the game for that. Switched on in the options (off by default), a click
// on a point there gets the game's own reaction: its code is whispered to yourself, so its place in GW2's chat is known, and
// clicked once with the real mouse in GW2's own chat: onto the link, about 400 ms for GW2's mouse-over to land, then the
// press (GW2 reads the mouse once a frame and takes no click on a link it has not seen the mouse on).
// One click of yours, one message and one click of ours; every step only while GW2 is in front. No hook, no game memory.

#include "kb_shared.hpp"

#include "chat/ChatLinks.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <deque>
#include <random>
#include <set>
#include <string>
#include <thread>

namespace kb {
namespace {

std::mutex s_mu;  // after g_mu
bool s_open = false;
uint32_t s_selected = 0;
int s_zoomOut = 1;  // steps below the map's finest tiles
std::deque<uint32_t> s_history;  // the ones you flew to, the newest first
std::deque<uint32_t> s_collection;  // the ones you keep, the newest first
bool s_historyRead = false;
std::string s_awaited;  // the code on its way, without its brackets
std::set<std::string> s_askedTiles;

std::thread s_thread;
std::atomic<bool> s_busy{false}, s_stop{false}, s_through{false}, s_teaching{false}, s_lineMoved{false};
std::atomic<bool> s_sendDone{true}, s_sendFailed{false};
std::atomic<int> s_linesCame{0};  // a whisper to yourself comes back twice: "from" and "to"
std::atomic<unsigned long long> s_teachUntil{0};
std::atomic<unsigned> s_frames{0};
// Where the link stands in the last line of GW2's chat, from one click of yours; in the game window's client pixels.
std::atomic<int> s_linkX{-1}, s_linkY{-1}, s_linkW{0}, s_linkH{0};
std::atomic<bool> s_linkRead{false}, s_linkToSave{false};

constexpr size_t kHistoryMax = 30, kCollectionMax = 200;

void Say(const std::wstring& text, ULONGLONG ms) {
    for (int waited = 0; waited < 2000 && !s_sendDone && !s_stop; waited += 20) Sleep(20);  // a send's last word clears the status
    std::lock_guard<std::mutex> lock(g_mu);
    SetStatus(text, ms);
}

bool WaitFrames(unsigned n) {  // until the render thread drew n more frames (at most half a second)
    const unsigned from = s_frames;
    for (int waited = 0; waited < 500 && !s_stop; waited += 2) {
        if (s_frames - from >= n) return true;
        Sleep(2);
    }
    return false;
}

int Dice(int from, int to) {  // every pause thrown anew
    thread_local std::mt19937 dice(static_cast<unsigned>(GetTickCount64() ^ GetCurrentThreadId()));
    return std::uniform_int_distribution<int>(from, to)(dice);
}

void MoveCursor(POINT at) {  // as a real mouse would: absolute, over the whole virtual desktop
    const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN), vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int vw = std::max(1, GetSystemMetrics(SM_CXVIRTUALSCREEN) - 1), vh = std::max(1, GetSystemMetrics(SM_CYVIRTUALSCREEN) - 1);
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dx = static_cast<LONG>((static_cast<long long>(at.x - vx) * 65535) / vw);
    in.mi.dy = static_cast<LONG>((static_cast<long long>(at.y - vy) * 65535) / vh);
    in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    SendInput(1, &in, sizeof(in));
}

// One left click at `at`, on GW2's own window: our windows let the mouse through, the cursor comes onto the link in two steps
// as a hand does, stays about 400 ms for GW2's mouse-over, the button down, held, up; the mouse is yours again where it
// stands. Your hand moving the cursor ends it: yours.
bool RealClick(HWND gw2, POINT at, POINT was) {
    const auto front = [gw2] { return GetForegroundWindow() == gw2; };
    if (!front()) return false;
    struct Through {
        Through() { s_through = true; }
        ~Through() { s_through = false; }
    } through;
    if (!WaitFrames(2) || !front()) return false;
    MoveCursor(POINT{at.x - 3, at.y});
    if (!WaitFrames(2)) {
        MoveCursor(was);
        return false;
    }
    MoveCursor(at);
    if (!WaitFrames(2)) {
        MoveCursor(was);
        return false;
    }
    Sleep(Dice(380, 450));
    POINT now{};
    GetCursorPos(&now);
    if (std::abs(now.x - at.x) > 2 || std::abs(now.y - at.y) > 2) return false;
    const HWND under = WindowFromPoint(at);
    const bool buttonHeld = (GetAsyncKeyState(VK_LBUTTON) | GetAsyncKeyState(VK_RBUTTON) | GetAsyncKeyState(VK_MBUTTON)) & 0x8000;
    MumbleLink mumble;
    const MumbleState ms = mumble.Read();
    if (!front() || !under || GetAncestor(under, GA_ROOT) != gw2 || buttonHeld || (ms.live && ms.TextboxHasFocus())) {  // typing stops the flight
        MoveCursor(was);
        return false;
    }
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    SendInput(1, &in, sizeof(in));
    Sleep(Dice(40, 70));
    in.mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(1, &in, sizeof(in));
    return true;
}

// Your click in the preview is still under way, and with "keep typing" the input holds the keyboard since the last send:
// the button up, our windows off the keyboard, and that held for a few frames (at most 1.5 s).
void LetGoOfKeyboard() {
    unsigned freeSince = s_frames;
    for (int waited = 0; waited < 1500 && !s_stop; waited += 5) {
        if (!g_keyboardFree || (GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
            g_letGoKeyboard = true;
            freeSince = s_frames;
        } else if (s_frames - freeSince >= 3) {
            return;
        }
        Sleep(5);
    }
}

void Flight(uint32_t id) {
    std::wstring own, before;
    bool copyOnly;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        own = g_ownName;
        copyOnly = g_cfg.copyOnly;
        // Where GW2's line goes back to: GW2 does not say. The footer's channel, else the one we last saw it in.
        before = ComposePrefix();
        if (before.empty()) before = g_gw2LineChannel;
        if (before == L"/r ") before.clear();  // after this whisper a reply would answer yourself
    }
    if (own.empty() || copyOnly) {
        Say(copyOnly ? Tr(L"In \"only copy\" mode the waypoint cannot be posted.") : Tr(L"Your character's name is not known yet."), 8000);
        return;
    }
    const std::string code = PieUI::ChatLinks::EncodeMap(id);  // "[&BCkEAAA=]"
    {
        std::lock_guard<std::mutex> lock(s_mu);
        s_awaited = code.size() > 3 ? code.substr(2, code.size() - 3) : code;
    }
    s_linesCame = 0;
    s_lineMoved = false;
    s_sendDone = false;
    s_sendFailed = false;
    const std::wstring line = L"/w " + own + L", " + FromUtf8(code);
    g_worker.PostFront([line, before] {
        bool hadInput;
        {  // as every send: your input lets go of the keyboard and what you type meanwhile is kept for it
            std::lock_guard<std::mutex> lock(g_mu);
            g_sending = true;
            hadInput = g_inputActive || g_focusInput;
            g_bufferHadInputFocus = hadInput;
            g_bufferingStartTime = GetTickCount64();
            g_bufferingKeys = true;
        }
        LetGoOfKeyboard();
        SendNow({line}, line, std::wstring());  // through GW2's own chat line: so the link stands in GW2's own chat
        bool setBack;
        {
            std::lock_guard<std::mutex> lock(g_mu);
            if (!g_history.empty() && g_history.front() == line) g_history.pop_front();  // not for your Up key
            if (ToUtf8(line) == g_input) g_input[0] = 0;  // a send that failed gives its text back: this one was never yours
            g_syncedChannel.clear();  // GW2's line now stands in this whisper
            s_sendFailed = !g_status.empty();  // the reason stands in the status
            setBack = !s_sendFailed && !before.empty();
            if (setBack) {  // the same run goes on: the input keeps waiting
                g_sending = true;
                g_focusInput = false;
                g_bufferHadInputFocus = hadInput;
                g_bufferingStartTime = GetTickCount64();
                g_bufferingKeys = true;
            }
        }
        if (setBack) {  // before the click, not after it: typing stops the flight, and later you are playing again
            LetGoOfKeyboard();
            const std::wstring error = SetLineChannel(before);
            std::lock_guard<std::mutex> lock(g_mu);
            g_sending = false;
            g_bufferingStartTime = GetTickCount64();
            if (hadInput || g_cfg.returnFocus) g_focusInput = true;
            if (!error.empty()) SetStatus(error, 8000);
        }
        s_sendDone = true;
    });
    for (int waited = 0; waited < 15000 && !s_sendDone && !s_stop; waited += 10) Sleep(10);  // the whole run, only then the mouse
    if (s_sendFailed) return;
    for (int waited = 0; waited < 3000 && s_linesCame == 0 && !s_stop; waited += 20) Sleep(20);
    if (s_linesCame == 0) {
        Say(Tr(L"The code did not come back in the chat: GW2 may not let you whisper to yourself here."), 10000);
        return;
    }
    for (int waited = 0; waited < 400 && s_linesCame < 2 && !s_stop; waited += 10) Sleep(10);  // both, before the place counts
    if (before.empty()) Say(Tr(L"GW2's line stays in the whisper to you: which channel it was in before, I have not seen yet."), 8000);
    if (s_linkX < 0) {  // once: your own click shows where the link stands
        s_teachUntil = GetTickCount64() + 30000;
        s_teaching = true;
        Say(Tr(L"Click the new link in GW2's chat once yourself, at its start. I remember the place, from then on it goes by itself."), 30000);
        return;
    }
    const HWND gw2 = FindGw2Window(GetCurrentProcessId());
    RECT client{};
    if (!gw2 || !GetClientRect(gw2, &client) || client.right <= 0 || client.bottom <= 0) return;
    POINT at{s_linkX * client.right / std::max(1, s_linkW.load()), s_linkY * client.bottom / std::max(1, s_linkH.load())};
    ClientToScreen(gw2, &at);
    if (!WaitFrames(3)) return;  // the line is drawn
    if (s_lineMoved) {  // someone's line pushed ours up: the place is no longer known, nothing is clicked blind
        Say(Tr(L"Another line came in between: nothing was clicked. Try again."), 8000);
        return;
    }
    POINT was{};
    GetCursorPos(&was);
    if (!RealClick(gw2, at, was)) {
        Say(Tr(L"The click was left out: GW2 was not in front, the mouse moved, or GW2's chat line was open."), 8000);
        return;
    }
    MumbleLink mumble;
    for (int waited = 0; waited < 1500 && !s_stop; waited += 50) {
        const MumbleState ms = mumble.Read();
        if (ms.live && ms.MapOpen()) {  // the game flies: the mouse just goes free, and the preview stands in nobody's way
            std::lock_guard<std::mutex> lock(s_mu);
            s_open = false;
            return;
        }
        Sleep(50);
    }
    POINT now{};
    GetCursorPos(&now);
    if (std::abs(now.x - at.x) <= 2 && std::abs(now.y - at.y) <= 2) MoveCursor(was);  // not hit and not moved by you: back
    Say(Tr(L"The map did not open: the link was not hit. Teach the place again in the preview; GW2's chat must show whispers and stand at its end."), 12000);
}

void SaveLists(const std::wstring& dataDir) {  // on the worker; only the points' public numbers
    std::string history, collection;
    {
        std::lock_guard<std::mutex> lock(s_mu);
        for (const uint32_t id : s_history) history += std::to_string(id) + "\n";
        for (const uint32_t id : s_collection) collection += std::to_string(id) + "\n";
    }
    if (g_wiped) return;
    WriteFileAtomic(dataDir + L"\\poi-history.txt", history);
    WriteFileAtomic(dataDir + L"\\poi-collection.txt", collection);
}

void ToggleCollection(uint32_t id) {
    std::wstring dataDir;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        dataDir = g_cfg.dataDir;
    }
    {
        std::lock_guard<std::mutex> lock(s_mu);
        const auto at = std::find(s_collection.begin(), s_collection.end(), id);
        if (at != s_collection.end()) {
            s_collection.erase(at);
        } else {
            s_collection.push_front(id);
            if (s_collection.size() > kCollectionMax) s_collection.pop_back();
        }
    }
    g_worker.Post([dataDir] { SaveLists(dataDir); });
}

void Fly(uint32_t id) {  // the game's thread
    std::wstring dataDir;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        if (s_busy.exchange(true)) return;
        dataDir = g_cfg.dataDir;
    }
    {
        std::lock_guard<std::mutex> lock(s_mu);
        s_history.erase(std::remove(s_history.begin(), s_history.end(), id), s_history.end());
        s_history.push_front(id);
        if (s_history.size() > kHistoryMax) s_history.pop_back();
    }
    g_worker.Post([dataDir] { SaveLists(dataDir); });
    if (s_thread.joinable()) s_thread.join();  // the last one has ended: s_busy
    s_stop = false;
    s_thread = std::thread([id] {
        Shielded([id] { Flight(id); });
        s_busy = false;
    });
}

Texture_t* Tile(int continent, int floor, int zoom, int x, int y) {
    if (!g_api || !g_api->Textures_Get || !g_api->Textures_GetOrCreateFromURL) return nullptr;
    char id[64], path[64];
    snprintf(id, sizeof(id), "GW2KB_TILE_%d_%d_%d_%d_%d", continent, floor, zoom, x, y);
    snprintf(path, sizeof(path), "/%d/%d/%d/%d/%d.jpg", continent, floor, zoom, x, y);
    bool first;
    {
        std::lock_guard<std::mutex> lock(s_mu);
        first = s_askedTiles.insert(id).second;
    }
    if (first) return g_api->Textures_GetOrCreateFromURL(id, "https://tiles.guildwars2.com", path);  // once per tile: only its numbers go out
    return g_api->Textures_Get(id);
}

void DrawMap(const gct::ChatLinks::PoiView& center, bool mayFly) {
    const ImVec2 p = ImGui::GetCursorScreenPos(), size = ImGui::GetContentRegionAvail();
    if (size.x < 8.0f || size.y < 8.0f) return;
    ImGui::InvisibleButton("##wparea", size);
    const bool hovered = ImGui::IsItemHovered();
    if (hovered && ImGui::GetIO().MouseWheel != 0.0f) s_zoomOut = std::clamp(s_zoomOut + (ImGui::GetIO().MouseWheel > 0.0f ? -1 : 1), 0, 4);
    const int finest = center.continent == 2 ? 6 : 7;  // the API's max_zoom of Tyria and the Mists
    const int zoom = std::max(0, finest - s_zoomOut);
    const float perUnit = 1.0f / static_cast<float>(1 << (finest - zoom));
    const float left = center.x - size.x * 0.5f / perUnit, top = center.y - size.y * 0.5f / perUnit;
    const float tileUnits = 256.0f / perUnit;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(p, ImVec2(p.x + size.x, p.y + size.y), true);
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), ImGui::GetColorU32(ImGuiCol_FrameBg));
    int shown = 0;
    for (int ty = std::max(0, static_cast<int>(std::floor(top / tileUnits))); ty * tileUnits < top + size.y / perUnit && shown < 30; ++ty) {
        for (int tx = std::max(0, static_cast<int>(std::floor(left / tileUnits))); tx * tileUnits < left + size.x / perUnit && shown < 30; ++tx, ++shown) {
            const Texture_t* tile = Tile(center.continent, center.floor, zoom, tx, ty);
            if (!tile || !tile->Resource) continue;
            const ImVec2 a(p.x + (tx * tileUnits - left) * perUnit, p.y + (ty * tileUnits - top) * perUnit);
            dl->AddImage(tile->Resource, a, ImVec2(a.x + 256.0f, a.y + 256.0f));
        }
    }
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const ImU32 accent = ImGui::GetColorU32(UiAccent()), dark = IM_COL32(0, 0, 0, 200);
    const gct::ChatLinks::PoiView* hit = nullptr;
    const std::vector<gct::ChatLinks::PoiView> around = g_links.PoisIn(center.continent, left, top, left + size.x / perUnit, top + size.y / perUnit);
    const float r = ImGui::GetFontSize() * 0.32f;
    float hitDistance = r * r * 6.0f;
    for (const gct::ChatLinks::PoiView& poi : around) {
        const ImVec2 at(p.x + (poi.x - left) * perUnit, p.y + (poi.y - top) * perUnit);
        const bool waypoint = poi.type == "waypoint";
        const float size2 = waypoint ? r : r * 0.6f;
        dl->AddCircleFilled(at, size2 + 1.0f, dark, 12);
        dl->AddCircleFilled(at, size2, waypoint ? accent : ImGui::GetColorU32(ImGuiCol_Text), 12);
        const float dx = mouse.x - at.x, dy = mouse.y - at.y, d = dx * dx + dy * dy;
        if (hovered && d < hitDistance) {
            hitDistance = d;
            hit = &poi;
        }
    }
    const ImVec2 middle(p.x + size.x * 0.5f, p.y + size.y * 0.5f);
    dl->AddCircle(middle, r * 2.2f, dark, 24, 3.5f);
    dl->AddCircle(middle, r * 2.2f, accent, 24, 2.0f);
    dl->PopClipRect();
    const float mx = mouse.x - middle.x, my = mouse.y - middle.y;
    if (!hit && hovered && mx * mx + my * my < r * r * 6.0f) hit = &center;  // the point itself always answers, map or no map
    if (hit) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGui::SetTooltip("%s\n%s", hit->name.c_str(), hit->map.c_str());
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            std::lock_guard<std::mutex> lock(s_mu);
            s_selected = hit->id;
        }
        if (mayFly && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) Fly(hit->id);
    }
}

// The table is not there yet: the point itself is, and its code is all a flight needs.
void DrawBareMarker(uint32_t id, bool mayFly) {
    const ImVec2 p = ImGui::GetCursorScreenPos(), size = ImGui::GetContentRegionAvail();
    if (size.x < 8.0f || size.y < 8.0f) return;
    ImGui::InvisibleButton("##wparea", size);
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), ImGui::GetColorU32(ImGuiCol_FrameBg));
    const ImVec2 middle(p.x + size.x * 0.5f, p.y + size.y * 0.5f), mouse = ImGui::GetIO().MousePos;
    const float r = ImGui::GetFontSize() * 0.32f;
    const ImU32 accent = ImGui::GetColorU32(UiAccent()), dark = IM_COL32(0, 0, 0, 200);
    dl->AddCircleFilled(middle, r + 1.0f, dark, 12);
    dl->AddCircleFilled(middle, r, accent, 12);
    dl->AddCircle(middle, r * 2.2f, dark, 24, 3.5f);
    dl->AddCircle(middle, r * 2.2f, accent, 24, 2.0f);
    const float mx = mouse.x - middle.x, my = mouse.y - middle.y;
    if (!hovered || mx * mx + my * my >= r * r * 6.0f) return;
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImGui::SetTooltip("%s", UI(Tr(L"Waypoint")).c_str());
    if (mayFly && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) Fly(id);
}

std::wstring PoiKind(const std::string& type) {
    if (type == "waypoint") return Tr(L"Waypoint");
    if (type == "vista") return Tr(L"Vista");
    if (type == "unlock") return Tr(L"Unlock point");
    return Tr(L"Point of interest");
}

void ReadIds(const std::wstring& path, std::deque<uint32_t>& into, size_t most) {
    std::string text;
    if (!ReadFileBytes(path, text)) return;
    size_t at = 0;
    while (at < text.size() && into.size() < most) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        if (const unsigned long id = std::strtoul(text.c_str() + at, nullptr, 10)) into.push_back(static_cast<uint32_t>(id));
        at = end + 1;
    }
}

// A click on a name only shows it in the preview.
void PoiList(const std::wstring& title, const std::vector<uint32_t>& ids, uint32_t selected) {
    if (ids.empty()) return;
    ImGui::TextDisabled("%s", UI(title).c_str());
    ImGui::PushID(&ids);
    for (const uint32_t id : ids) {
        gct::ChatLinks::PoiView poi;
        if (!g_links.Poi(id, &poi)) continue;
        ImGui::PushID(static_cast<int>(id));
        if (ImGui::Selectable(poi.name.c_str(), id == selected)) {
            std::lock_guard<std::mutex> lock(s_mu);
            s_selected = id;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", poi.map.c_str());
        ImGui::PopID();
    }
    ImGui::PopID();
    ImGui::Spacing();
}

void ReadStored(const std::wstring& ini, const std::wstring& dataDir) {  // once
    if (!s_linkRead.exchange(true)) {
        s_linkX = static_cast<int>(GetPrivateProfileIntW(L"Waypoint", L"LinkX", static_cast<UINT>(-1), ini.c_str()));
        s_linkY = static_cast<int>(GetPrivateProfileIntW(L"Waypoint", L"LinkY", static_cast<UINT>(-1), ini.c_str()));
        s_linkW = static_cast<int>(GetPrivateProfileIntW(L"Waypoint", L"LinkW", 0, ini.c_str()));
        s_linkH = static_cast<int>(GetPrivateProfileIntW(L"Waypoint", L"LinkH", 0, ini.c_str()));
        if (s_linkW <= 0 || s_linkH <= 0) s_linkX = -1;
    }
    std::lock_guard<std::mutex> lock(s_mu);
    if (s_historyRead) return;
    s_historyRead = true;
    ReadIds(dataDir + L"\\poi-history.txt", s_history, kHistoryMax);
    ReadIds(dataDir + L"\\poi-collection.txt", s_collection, kCollectionMax);
}

}  // namespace

void OpenWaypointPreview(uint32_t id) {  // g_mu held or not: only s_mu
    std::lock_guard<std::mutex> lock(s_mu);
    s_selected = id;
    s_open = true;
}

void OpenWaypointWindow() {  // from the menu: your lists, without a code from the chat
    std::lock_guard<std::mutex> lock(s_mu);
    s_open = true;
}

bool WaypointPassThrough() { return s_through.load(); }

bool WaypointSawLine(bool whisper, const char* content) {  // from Events: Chat, every line; true: ours, not for our window
    if (!s_busy.load() || !content) return false;
    std::lock_guard<std::mutex> lock(s_mu);
    if (s_awaited.empty()) return false;
    if (whisper && std::strstr(content, s_awaited.c_str())) {
        ++s_linesCame;
        return true;
    }
    if (s_linesCame > 0) s_lineMoved = true;
    return false;
}

void WaypointSawClick(HWND hwnd, LPARAM lp) {  // the game's thread: a left press
    if (!s_teaching.load()) return;
    if (GetTickCount64() > s_teachUntil.load()) {
        s_teaching = false;
        return;
    }
    if (ImGui::GetIO().WantCaptureMouse) return;  // on a window of ours or of another add-on: not the link
    s_teaching = false;
    RECT client{};
    if (!GetClientRect(hwnd, &client)) return;
    s_linkW = client.right;
    s_linkH = client.bottom;
    s_linkX = static_cast<short>(LOWORD(lp));
    s_linkY = static_cast<short>(HIWORD(lp));
    s_linkToSave = true;
}

void WaypointStop() {
    s_stop = true;
    s_teaching = false;
    if (s_thread.joinable()) s_thread.join();
    s_busy = false;
}

void RenderWaypointPreview() {
    ++s_frames;
    if (s_linkToSave.exchange(false)) {
        std::lock_guard<std::mutex> lock(g_mu);
        g_cfg.SaveValue(L"Waypoint", L"LinkX", std::to_wstring(s_linkX.load()));
        g_cfg.SaveValue(L"Waypoint", L"LinkY", std::to_wstring(s_linkY.load()));
        g_cfg.SaveValue(L"Waypoint", L"LinkW", std::to_wstring(s_linkW.load()));
        g_cfg.SaveValue(L"Waypoint", L"LinkH", std::to_wstring(s_linkH.load()));
        SetStatus(Tr(L"Place remembered. A click in the preview now does this by itself."), 8000);
    }
    uint32_t selected;
    std::vector<uint32_t> history, collection;
    {
        std::lock_guard<std::mutex> lock(s_mu);
        if (!s_open || s_through.load()) return;
    }
    bool mayFly;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        ReadStored(g_cfg.iniPath, g_cfg.dataDir);
        mayFly = g_cfg.waypointClick && !s_busy.load();
    }
    {
        std::lock_guard<std::mutex> lock(s_mu);
        if (!s_selected) s_selected = !s_collection.empty() ? s_collection.front() : !s_history.empty() ? s_history.front() : 0;
        selected = s_selected;
        history.assign(s_history.begin(), s_history.end());
        collection.assign(s_collection.begin(), s_collection.end());
    }
    const float nexusPx = FollowUiPx();
    RequestUiFont(nexusPx);
    ImFont* uf = g_uiFont.load();
    ImGui::SetNextWindowSize(ImVec2(nexusPx * 42.0f, nexusPx * 26.0f), ImGuiCond_FirstUseEver);
    bool open = true;
    const std::string title = UI(Tr(L"Waypoints")) + "###kbwaypoint";
    const bool visible = ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_NoCollapse);
    bool pushedUf = false;
    if (uf && uf->FontSize > 0.0f) {
        ImGui::SetWindowFontScale(nexusPx / uf->FontSize);
        ImGui::PushFont(uf);
        pushedUf = true;
    }
    if (visible) {
        const float fs = ImGui::GetFontSize();
        const float footer = ImGui::GetFrameHeightWithSpacing() * 2.0f;
        const float listW = fs * 13.0f;
        const float mapH = std::max(fs * 6.0f, ImGui::GetContentRegionAvail().y - footer);
        gct::ChatLinks::PoiView poi;
        const bool known = g_links.Poi(selected, &poi);
        ImGui::BeginChild("##wpmap", ImVec2(std::max(fs * 8.0f, ImGui::GetContentRegionAvail().x - listW - ImGui::GetStyle().ItemSpacing.x), mapH), false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        if (known) DrawMap(poi, mayFly);
        else if (selected) DrawBareMarker(selected, mayFly);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##wplist", ImVec2(0.0f, mapH), true);
        PoiList(Tr(L"Collection"), collection, selected);
        PoiList(Tr(L"Your last ones"), history, selected);
        ImGui::EndChild();
        if (known) {
            ImGui::TextUnformatted(poi.name.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("%s · %s", UI(PoiKind(poi.type)).c_str(), poi.map.c_str());
        } else if (selected) {
            ImGui::TextDisabled("%s", UI(Tr(L"The table of waypoints is on its way …")).c_str());
        } else {
            ImGui::TextDisabled("%s", UI(Tr(L"Click a waypoint in the chat, or choose one from your lists.")).c_str());
        }
        if (mayFly && selected) {
            if (ImGui::Button(UI(Tr(L"Show it in GW2")).c_str())) Fly(selected);
            ImGui::SameLine();
        }
        if (selected) {
            const bool kept = std::find(collection.begin(), collection.end(), selected) != collection.end();
            if (ImGui::Button(UI(kept ? Tr(L"Remove from collection") : Tr(L"Add to collection")).c_str())) ToggleCollection(selected);
            ImGui::SameLine();
        }
        if (mayFly && selected) {
            if (ImGui::Button(UI(Tr(L"Teach the place again")).c_str())) {
                s_linkX = -1;
                Fly(selected);
            }
        } else if (!g_cfg.waypointClick) {
            ImGui::TextDisabled("%s", UI(Tr(L"Options > Chat: a click here can get GW2's own map flight.")).c_str());
        }
    }
    if (pushedUf) ImGui::PopFont();
    ImGui::End();
    if (!open) {
        std::lock_guard<std::mutex> lock(s_mu);
        s_open = false;
    }
}

}  // namespace kb
