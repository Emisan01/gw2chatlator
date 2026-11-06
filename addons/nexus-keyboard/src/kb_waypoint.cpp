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
std::deque<uint32_t> s_history;  // the newest first
bool s_historyRead = false;
std::string s_awaited;  // the code on its way, without its brackets
std::set<std::string> s_askedTiles;

std::thread s_thread;
std::atomic<bool> s_busy{false}, s_stop{false}, s_through{false}, s_teaching{false}, s_lineCame{false}, s_lineMoved{false};
std::atomic<unsigned long long> s_teachUntil{0};
std::atomic<unsigned> s_frames{0};
// Where the link stands in the last line of GW2's chat, from one click of yours; in the game window's client pixels.
std::atomic<int> s_linkX{-1}, s_linkY{-1}, s_linkW{0}, s_linkH{0};
std::atomic<bool> s_linkRead{false}, s_linkToSave{false};

constexpr size_t kHistoryMax = 30;

void Say(const std::wstring& text, ULONGLONG ms) {
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

void Flight(uint32_t id) {
    std::wstring own;
    bool copyOnly;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        own = g_ownName;
        copyOnly = g_cfg.copyOnly;
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
    s_lineCame = false;
    s_lineMoved = false;
    const std::wstring line = L"/w " + own + L", " + FromUtf8(code);
    g_worker.PostFront([line] {
        SendNow({line}, line, std::wstring());  // through GW2's own chat line: so the link stands in GW2's own chat
        std::lock_guard<std::mutex> lock(g_mu);
        if (!g_history.empty() && g_history.front() == line) g_history.pop_front();  // not for your Up key
        g_syncedChannel.clear();  // GW2's line now stands in this whisper: the next message sets its channel again
    });
    for (int waited = 0; waited < 5000 && !s_lineCame && !s_stop; waited += 20) Sleep(20);
    if (!s_lineCame) {
        Say(Tr(L"The code did not come back in the chat: GW2 may not let you whisper to yourself here."), 10000);
        return;
    }
    Sleep(Dice(250, 350));  // a whisper to yourself may come back twice: the second line too before the place counts
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

void SaveHistory(const std::wstring& dataDir) {  // on the worker
    std::string out;
    {
        std::lock_guard<std::mutex> lock(s_mu);
        for (const uint32_t id : s_history) out += std::to_string(id) + "\n";
    }
    if (!g_wiped) WriteFileAtomic(dataDir + L"\\poi-history.txt", out);
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
    g_worker.Post([dataDir] { SaveHistory(dataDir); });
    if (s_thread.joinable()) s_thread.join();  // the last one has ended: s_busy
    s_stop = false;
    s_thread = std::thread([id] {
        Shielded([id] { Flight(id); });
        s_busy = false;
    });
}

void NoTexture(const char*, Texture_t*) {}

Texture_t* Tile(int continent, int floor, int zoom, int x, int y) {
    if (!g_api || !g_api->Textures_Get || !g_api->Textures_LoadFromURL) return nullptr;
    char id[64], path[64];
    snprintf(id, sizeof(id), "GW2KB_TILE_%d_%d_%d_%d_%d", continent, floor, zoom, x, y);
    snprintf(path, sizeof(path), "/%d/%d/%d/%d/%d.jpg", continent, floor, zoom, x, y);
    bool first;
    {
        std::lock_guard<std::mutex> lock(s_mu);
        first = s_askedTiles.insert(id).second;
    }
    if (first) {  // once per tile: GW2's public map tiles, only their numbers go out
        g_api->Textures_LoadFromURL(id, "https://tiles.guildwars2.com", path, NoTexture);
        return nullptr;
    }
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

std::wstring PoiKind(const std::string& type) {
    if (type == "waypoint") return Tr(L"Waypoint");
    if (type == "vista") return Tr(L"Vista");
    if (type == "unlock") return Tr(L"Unlock point");
    return Tr(L"Point of interest");
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
    std::string text;
    if (!ReadFileBytes(dataDir + L"\\poi-history.txt", text)) return;
    size_t at = 0;
    while (at < text.size() && s_history.size() < kHistoryMax) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        if (const unsigned long id = std::strtoul(text.c_str() + at, nullptr, 10)) s_history.push_back(static_cast<uint32_t>(id));
        at = end + 1;
    }
}

}  // namespace

void OpenWaypointPreview(uint32_t id) {  // g_mu held or not: only s_mu
    std::lock_guard<std::mutex> lock(s_mu);
    s_selected = id;
    s_open = true;
}

bool WaypointPassThrough() { return s_through.load(); }

bool WaypointSawLine(bool whisper, const char* content) {  // from Events: Chat, every line; true: ours, not for our window
    if (!s_busy.load() || !content) return false;
    std::lock_guard<std::mutex> lock(s_mu);
    if (s_awaited.empty()) return false;
    if (whisper && std::strstr(content, s_awaited.c_str())) {
        s_lineCame = true;
        return true;
    }
    if (s_lineCame) s_lineMoved = true;
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
    std::vector<uint32_t> history;
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
        selected = s_selected;
        history.assign(s_history.begin(), s_history.end());
    }
    const float nexusPx = FollowUiPx();
    RequestUiFont(nexusPx);
    ImFont* uf = g_uiFont.load();
    ImGui::SetNextWindowSize(ImVec2(nexusPx * 42.0f, nexusPx * 26.0f), ImGuiCond_FirstUseEver);
    bool open = true;
    const std::string title = UI(Tr(L"Waypoint")) + "###kbwaypoint";
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
        else ImGui::TextDisabled("%s", UI(Tr(L"The table of waypoints is on its way …")).c_str());
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##wplist", ImVec2(0.0f, mapH), true);
        ImGui::TextDisabled("%s", UI(Tr(L"Your last ones")).c_str());
        for (const uint32_t id : history) {
            gct::ChatLinks::PoiView past;
            if (!g_links.Poi(id, &past)) continue;
            ImGui::PushID(static_cast<int>(id));
            if (ImGui::Selectable(past.name.c_str(), id == selected)) {
                std::lock_guard<std::mutex> lock(s_mu);
                s_selected = id;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", past.map.c_str());
            ImGui::PopID();
        }
        ImGui::EndChild();
        if (known) {
            ImGui::TextUnformatted(poi.name.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("%s · %s", UI(PoiKind(poi.type)).c_str(), poi.map.c_str());
        }
        if (mayFly && known) {
            if (ImGui::Button(UI(Tr(L"Show it in GW2")).c_str())) Fly(selected);
            ImGui::SameLine();
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
