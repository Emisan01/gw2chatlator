// kb_waypoint.cpp — a waypoint from the chat. A click on it, or on a point in the waypoints window, gets the game's own
// reaction: its code is whispered to yourself, so its place in GW2's chat is known, and
// clicked once with the real mouse in GW2's own chat: onto the link, about 400 ms for GW2's mouse-over to land, then the
// press (GW2 reads the mouse once a frame and takes no click on a link it has not seen the mouse on).
// One click of yours, one message and one click of ours; every step only while GW2 is in front. No hook, no game memory.
// The window (menu, or when the flight cannot run): GW2's own map tiles around the point, the points near it, your lists.

#include "kb_shared.hpp"

#include "chat/ChatLinks.h"
#include "core/chat_code.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <deque>
#include <map>
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
float s_panX = 0.0f, s_panY = 0.0f;  // the right mouse moved the map this far from the point (map units)
uint32_t s_panFor = 0;               // the point they belong to: a new point starts from itself
bool s_panning = false;
bool s_moreOpen = false;  // the window's "More": folded unless you open it
std::deque<uint32_t> s_history;  // the ones you flew to, the newest first
std::deque<uint32_t> s_collection;  // the ones you keep, the newest first
bool s_historyRead = false;
std::string s_awaited;  // the code on its way, without its brackets
std::set<std::string> s_askedTiles;

std::thread s_thread;
std::atomic<bool> s_busy{false}, s_stop{false}, s_through{false}, s_teaching{false}, s_lineMoved{false};
std::atomic<bool> s_sendDone{true}, s_sendFailed{false}, s_runBroke{false};
std::atomic<int> s_linesCame{0};  // a whisper to yourself comes back twice: "from" and "to"
std::atomic<unsigned long long> s_teachUntil{0}, s_throughUntil{0};
std::atomic<unsigned> s_frames{0};
// Where the link stands in the newest line of GW2's chat, from one click of yours, in the game window's client pixels. It
// moves with your character's name, the window's size and GW2's interface size: learned once for each, nothing guessed.
std::atomic<int> s_linkX{-1}, s_linkY{-1};
std::atomic<bool> s_forgetPlace{false}, s_mapOpenFrame{false}, s_hidePreview{false};
std::atomic<uint32_t> s_flyRequest{0};  // a click in the chat, drawn with g_mu held: the flight starts in the next frame
std::atomic<unsigned> s_runCount{0}, s_runWaiting{0};  // the run posted to the worker and not begun yet; 0: none
std::atomic<int> s_previewL{0}, s_previewT{0}, s_previewR{0}, s_previewB{0};  // the preview's rectangle, empty while it is closed
std::wstring s_placeKey;  // s_mu: the place being learned
std::map<std::wstring, POINT> s_places;  // s_mu: read or learned this session
std::atomic<unsigned long long> s_linkClickAt{0};

constexpr size_t kHistoryMax = 30, kCollectionMax = 200;
constexpr int kMaxTiles = 200;

// From the first key to the click: our windows let the mouse through and your clicks wait (a click on a window of ours would
// take the keyboard, one in the world would shut GW2's line, either tears the run apart). It ends by itself, whatever happens.
void HoldWindows() {
    s_throughUntil = GetTickCount64() + 8000;
    s_through = true;
}

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

// With the buttons swapped in Windows the physical right one is the main one: GetAsyncKeyState and SendInput speak physical.
bool MainButtonSwapped() { return GetSystemMetrics(SM_SWAPBUTTON) != 0; }

// One left click at `at`, on GW2's own window: our windows let the mouse through, the cursor comes onto the link in two steps
// as a hand does, stays about 400 ms for GW2's mouse-over, the button down, held, up; the mouse is yours again where it
// stands. Your hand moving the cursor ends it: yours.
bool RealClick(HWND gw2, POINT at, POINT was) {
    const auto front = [gw2] { return GetForegroundWindow() == gw2; };
    if (!front()) return false;
    HoldWindows();
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
    if (!front() || !under || GetAncestor(under, GA_ROOT) != gw2 || buttonHeld) {
        MoveCursor(was);
        return false;
    }
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwExtraInfo = gct::kGw2SenderExtraInfo;  // ours: not one of the clicks that wait
    in.mi.dwFlags = MainButtonSwapped() ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_LEFTDOWN;
    SendInput(1, &in, sizeof(in));
    Sleep(Dice(40, 70));
    in.mi.dwFlags = MainButtonSwapped() ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_LEFTUP;
    SendInput(1, &in, sizeof(in));
    return true;
}

POINT StoredPlace(const std::wstring& key) {
    std::wstring ini;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        ini = g_cfg.iniPath;
    }
    std::lock_guard<std::mutex> lock(s_mu);
    const auto known = s_places.find(key);
    if (known != s_places.end()) return known->second;
    const POINT at{static_cast<int>(GetPrivateProfileIntW(L"Waypoint", (key + L"X").c_str(), -1, ini.c_str())),
                   static_cast<int>(GetPrivateProfileIntW(L"Waypoint", (key + L"Y").c_str(), -1, ini.c_str()))};
    s_places[key] = at;
    return at;
}

// Your click in the preview is still under way, and with "keep typing" the input holds the keyboard since the last send:
// the button up, our windows off the keyboard, and that held for a few frames (at most 1.5 s).
void LetGoOfKeyboard() {
    unsigned freeSince = s_frames;
    for (int waited = 0; waited < 1500 && !s_stop; waited += 5) {
        if (!g_keyboardFree || (GetAsyncKeyState(MainButtonSwapped() ? VK_RBUTTON : VK_LBUTTON) & 0x8000)) {
            g_letGoKeyboard = true;
            freeSince = s_frames;
        } else if (s_frames - freeSince >= 3) {
            return;
        }
        Sleep(5);
    }
}

void Flight(uint32_t id) {
    struct EndHold {
        ~EndHold() {
            s_through = false;
            s_hidePreview = false;
        }
    } endHold;
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
    {  // A run of ours that broke off may have left GW2's line half done: not one key into that. A line simply open (Enter
       // in the game) is fine: every send goes through it.
        MumbleLink mumble;
        const MumbleState ms = mumble.Read();
        const bool open = ms.live && ms.TextboxHasFocus();
        if (open && s_runBroke) {
            TechLog('S', L"waypoint: GW2's line open after a run that broke off, nothing done");
            Say(Tr(L"GW2's chat line is open. Close it with Enter, then click again."), 8000);
            return;
        }
        if (!open) s_runBroke = false;
    }
    const std::string code = PieUI::ChatLinks::EncodeMap(id);  // "[&BCkEAAA=]"
    const std::wstring line = L"/w " + own + L", " + FromUtf8(code);
    {  // the same rules as every message: no spam by clicking, but a repeated click locks nothing
        std::lock_guard<std::mutex> lock(g_mu);
        const uint64_t now = GetTickCount64();
        const SendGuard::Verdict verdict = g_guard.WouldAllow(line, now);
        if (!verdict.allow) {
            SetStatus(GuardRefusal(verdict.reason), 10000);
            TechLog('S', L"waypoint: refused by the send guard");
            return;
        }
        g_guard.Check(line, false, false, now);
    }
    {
        std::lock_guard<std::mutex> lock(s_mu);
        s_awaited = code.size() > 3 ? code.substr(2, code.size() - 3) : code;
    }
    s_linesCame = 0;
    s_lineMoved = false;
    s_sendDone = false;
    s_sendFailed = false;
    const unsigned run = ++s_runCount;
    s_runWaiting = run;
    g_worker.PostFront([line, run] {
        unsigned waiting = run;
        if (!s_runWaiting.compare_exchange_strong(waiting, 0)) return;  // your click gave up waiting: nothing goes out late
        {  // as every send: your input lets go of the keyboard and what you type meanwhile is kept for it
            std::lock_guard<std::mutex> lock(g_mu);
            g_sending = true;
            g_bufferHadInputFocus = g_inputActive || g_focusInput;
            g_bufferingStartTime = GetTickCount64();
            g_bufferingKeys = true;
        }
        HoldWindows();
        LetGoOfKeyboard();
        const bool sent = SendNow({line}, line, std::wstring());  // through GW2's own chat line: so the link stands in GW2's own chat
        std::lock_guard<std::mutex> lock(g_mu);
        if (!g_history.empty() && g_history.front() == line) g_history.pop_front();  // not for your Up key
        if (ToUtf8(line) == g_input) g_input[0] = 0;  // a send that failed gives its text back: this one was never yours
        g_syncedChannel.clear();  // GW2's line now stands in this whisper: the next message from the footer's channel sets it again
        s_sendFailed = !sent;  // the reason stands in the status
        if (s_sendFailed) s_runBroke = true;
        s_sendDone = true;
    });
    // The worker may be busy with a translation or the model's start: the click waits at most 15 s, then it is off.
    for (int waited = 0; waited < 15000 && s_runWaiting == run && !s_stop; waited += 10) Sleep(10);
    unsigned stillWaiting = run;
    if (s_runWaiting.compare_exchange_strong(stillWaiting, 0)) {
        s_sendDone = true;
        TechLog('S', L"waypoint: the worker stayed busy, nothing sent");
        Say(Tr(L"Still busy with a translation: the waypoint was not posted. Click again."), 8000);
        return;
    }
    for (int waited = 0; waited < 15000 && !s_sendDone && !s_stop; waited += 10) Sleep(10);  // the whole send, only then the mouse
    if (s_sendFailed) return;
    TechLog('S', L"waypoint: whisper sent");
    for (int waited = 0; waited < 3000 && s_linesCame == 0 && !s_stop; waited += 20) Sleep(20);
    if (s_linesCame == 0) {
        Say(Tr(L"The code did not come back in the chat: GW2 may not let you whisper to yourself here."), 10000);
        return;
    }
    for (int waited = 0; waited < 400 && s_linesCame < 2 && !s_stop; waited += 10) Sleep(10);  // both, before the place counts
    const HWND gw2 = FindGw2Window(GetCurrentProcessId());
    RECT client{};
    if (!gw2 || !GetClientRect(gw2, &client) || client.right <= 0 || client.bottom <= 0) return;
    const std::wstring placeKey = ChatCodePlaceKey(own, client.right, client.bottom, static_cast<int>(g_uiSize.load()));
    POINT at{-1, -1};
    if (!s_forgetPlace.exchange(false)) at = StoredPlace(placeKey);
    if (at.x < 0 || at.y < 0) {  // once: your own click shows where the link stands
        {
            std::lock_guard<std::mutex> lock(s_mu);
            s_placeKey = placeKey;
        }
        s_teachUntil = GetTickCount64() + 30000;
        s_teaching = true;
        Say(Tr(L"Click the new link in GW2's chat once yourself, at its start. I remember the place, from then on it goes by itself."), 30000);
        return;
    }
    ClientToScreen(gw2, &at);
    if (!WaitFrames(3)) {  // the line is drawn; no frames, a minimised game: no click, and it says so
        Say(Tr(L"The click was left out: GW2 was not in front, or the mouse moved."), 8000);
        return;
    }
    if (s_lineMoved) {  // someone's line pushed ours up: the place is no longer known, nothing is clicked blind
        Say(Tr(L"Another line came in between: nothing was clicked. Try again."), 8000);
        return;
    }
    POINT was{};
    GetCursorPos(&was);
    {
        MumbleLink line;
        const MumbleState ms = line.Read();
        TechLog('S', L"waypoint: click at " + std::to_wstring(at.x) + L"," + std::to_wstring(at.y) +
                         (ms.live && ms.TextboxHasFocus() ? L", GW2's line open" : L", GW2's line shut"));
    }
    if (!RealClick(gw2, at, was)) {
        Say(Tr(L"The click was left out: GW2 was not in front, or the mouse moved."), 8000);
        return;
    }
    s_hidePreview = true;  // still out of the way while GW2 opens its map
    s_through = false;     // the press is out: the mouse and your clicks are yours again
    MumbleLink mumble;
    for (int waited = 0; waited < 1500 && !s_stop; waited += 50) {
        const MumbleState ms = mumble.Read();
        if (ms.live && ms.MapOpen()) {  // the game flies: the mouse just goes free, and the preview stands in nobody's way
            TechLog('S', L"waypoint: clicked, map open after " + std::to_wstring(waited) + L" ms");
            std::lock_guard<std::mutex> lock(s_mu);
            s_open = false;
            return;
        }
        Sleep(50);
    }
    POINT now{};
    GetCursorPos(&now);
    if (std::abs(now.x - at.x) <= 2 && std::abs(now.y - at.y) <= 2) MoveCursor(was);  // not hit and not moved by you: back
    TechLog('S', L"waypoint: clicked, the map did not open");
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
    // The right mouse held moves this map, at most two window sizes from the point: the tiles stay few.
    if (s_panFor != center.id) {
        s_panFor = center.id;
        s_panX = s_panY = 0.0f;
    }
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) s_panning = true;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) s_panning = false;
    if (s_panning) {
        const float reachX = size.x * 2.0f / perUnit, reachY = size.y * 2.0f / perUnit;
        s_panX = std::clamp(s_panX - ImGui::GetIO().MouseDelta.x / perUnit, -reachX, reachX);
        s_panY = std::clamp(s_panY - ImGui::GetIO().MouseDelta.y / perUnit, -reachY, reachY);
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    }
    const float left = center.x + s_panX - size.x * 0.5f / perUnit, top = center.y + s_panY - size.y * 0.5f / perUnit;
    const float tileUnits = 256.0f / perUnit;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(p, ImVec2(p.x + size.x, p.y + size.y), true);
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), ImGui::GetColorU32(ImGuiCol_FrameBg));
    int shown = 0;
    for (int ty = std::max(0, static_cast<int>(std::floor(top / tileUnits))); ty * tileUnits < top + size.y / perUnit && shown < kMaxTiles; ++ty) {
        for (int tx = std::max(0, static_cast<int>(std::floor(left / tileUnits))); tx * tileUnits < left + size.x / perUnit && shown < kMaxTiles; ++tx, ++shown) {
            const Texture_t* tile = Tile(center.continent, center.floor, zoom, tx, ty);
            if (!tile || !tile->Resource) continue;
            const ImVec2 a(p.x + (tx * tileUnits - left) * perUnit, p.y + (ty * tileUnits - top) * perUnit);
            dl->AddImage(tile->Resource, a, ImVec2(a.x + 256.0f, a.y + 256.0f));
        }
    }
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const ImU32 accent = ImGui::GetColorU32(UiAccent()), dark = IM_COL32(0, 0, 0, 200);
    const gct::ChatLinks::PoiView* hit = nullptr;
    const std::vector<gct::ChatLinks::PoiView> around = g_links.PoisIn(center.continent, center.floor, left, top, left + size.x / perUnit, top + size.y / perUnit);
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
    const ImVec2 middle(p.x + (center.x - left) * perUnit, p.y + (center.y - top) * perUnit);  // the point, wherever the map stands
    dl->AddCircle(middle, r * 2.2f, dark, 24, 3.5f);
    dl->AddCircle(middle, r * 2.2f, accent, 24, 2.0f);
    dl->PopClipRect();
    const float mx = mouse.x - middle.x, my = mouse.y - middle.y;
    if (!hit && hovered && mx * mx + my * my < r * r * 6.0f) hit = &center;  // the point itself always answers, map or no map
    if (s_panning) hit = nullptr;  // moving the map, not pointing at anything
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
// The small control at a row's right edge, apart from the name: a cross (out of the collection) or a plus (into it). Drawn,
// not a glyph: the same in every font.
bool RowMark(bool cross, const std::wstring& tip) {
    const float size = ImGui::GetTextLineHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton("##mark", ImVec2(size, size));
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (hovered) dl->AddRectFilled(p, ImVec2(p.x + size, p.y + size), ImGui::GetColorU32(ImGuiCol_ButtonHovered), size * 0.2f);
    const ImU32 col = ImGui::GetColorU32(hovered ? ImGuiCol_Text : ImGuiCol_TextDisabled);
    const float r = size * (cross ? 0.22f : 0.28f), w = std::max(1.0f, size * 0.09f);
    const ImVec2 m(p.x + size * 0.5f, p.y + size * 0.5f);
    if (cross) {
        dl->AddLine(ImVec2(m.x - r, m.y - r), ImVec2(m.x + r, m.y + r), col, w);
        dl->AddLine(ImVec2(m.x - r, m.y + r), ImVec2(m.x + r, m.y - r), col, w);
    } else {
        dl->AddLine(ImVec2(m.x - r, m.y), ImVec2(m.x + r, m.y), col, w);
        dl->AddLine(ImVec2(m.x, m.y - r), ImVec2(m.x, m.y + r), col, w);
    }
    if (hovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGui::SetTooltip("%s", UI(tip).c_str());
    }
    return pressed;
}

// A list under a header that folds (with how many it holds). A click on a name shows the point; a second click on the one
// shown brings the map back to it. At the right edge: the collection's rows a cross, the others a plus while not kept.
void PoiList(const std::wstring& title, const std::vector<uint32_t>& ids, uint32_t selected, const std::vector<uint32_t>& collection,
             bool isCollection) {
    if (ids.empty()) return;
    ImGui::PushID(&ids);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    const std::string head = UI(title) + "  " + std::to_string(ids.size()) + "###head";
    const bool open = ImGui::TreeNodeEx(head.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth |
                                                          ImGuiTreeNodeFlags_NoTreePushOnOpen);
    ImGui::PopStyleColor();
    if (open) {
        const float markW = ImGui::GetTextLineHeight();
        const float nameW = std::max(1.0f, ImGui::GetContentRegionAvail().x - markW - ImGui::GetStyle().ItemSpacing.x);
        for (const uint32_t id : ids) {
            gct::ChatLinks::PoiView poi;
            if (!g_links.Poi(id, &poi)) continue;
            ImGui::PushID(static_cast<int>(id));
            if (ImGui::Selectable(poi.name.c_str(), id == selected, 0, ImVec2(nameW, 0.0f))) {
                std::lock_guard<std::mutex> lock(s_mu);
                if (s_selected == id) s_panFor = 0;  // shown already: the map comes back to it
                s_selected = id;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", poi.map.c_str());
            const bool kept = isCollection || std::find(collection.begin(), collection.end(), id) != collection.end();
            ImGui::SameLine();
            if (isCollection || !kept) {
                if (RowMark(isCollection, isCollection ? Tr(L"Remove from collection") : Tr(L"Add to collection"))) ToggleCollection(id);
            } else {
                ImGui::Dummy(ImVec2(markW, markW));
            }
            ImGui::PopID();
        }
    }
    ImGui::PopID();
    ImGui::Spacing();
}

void ReadStored(const std::wstring& dataDir) {  // once
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

void FlyToWaypoint(uint32_t id) {  // a click on a waypoint in the chat; g_mu held or not: only s_mu
    {
        std::lock_guard<std::mutex> lock(s_mu);
        s_selected = id;
        s_open = true;
    }
    s_flyRequest = id;
}

void OpenWaypointWindow() {  // from the menu: your lists, without a code from the chat
    std::lock_guard<std::mutex> lock(s_mu);
    s_open = true;
}

bool WaypointPassThrough() { return s_through.load() && GetTickCount64() < s_throughUntil.load(); }

bool WaypointSwallowsMouse(UINT msg) {  // the game's thread: a press that waits, and its release, whenever that comes
    static bool held[3] = {};
    int button;
    bool down;
    switch (msg) {
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK: button = 0; down = true; break;
        case WM_LBUTTONUP: button = 0; down = false; break;
        case WM_RBUTTONDOWN:
        case WM_RBUTTONDBLCLK: button = 1; down = true; break;
        case WM_RBUTTONUP: button = 1; down = false; break;
        case WM_MBUTTONDOWN:
        case WM_MBUTTONDBLCLK: button = 2; down = true; break;
        case WM_MBUTTONUP: button = 2; down = false; break;
        default: return false;
    }
    if (down) {
        if (!WaypointPassThrough()) return false;
        held[button] = true;
        return true;
    }
    if (!held[button]) return false;
    held[button] = false;
    return true;
}

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

void WaypointSawClick(LPARAM lp) {  // the game's thread: a left press
    if (!s_teaching.load()) return;
    if (GetTickCount64() > s_teachUntil.load()) {
        s_teaching = false;
        return;
    }
    const int clickX = static_cast<short>(LOWORD(lp)), clickY = static_cast<short>(HIWORD(lp));
    const std::wstring where = std::to_wstring(clickX) + L"," + std::to_wstring(clickY);
    if (ImGui::GetIO().WantCaptureMouse) {  // on a window of ours or of another add-on: not the link
        TechLog('S', L"waypoint: learning, click at " + where + L" on a window, ignored");
        return;
    }
    // the preview just came back after the run: ImGui does not know it is under the mouse yet
    if (clickX >= s_previewL && clickX < s_previewR && clickY >= s_previewT && clickY < s_previewB) {
        TechLog('S', L"waypoint: learning, click at " + where + L" on the preview, ignored");
        return;
    }
    if (s_mapOpenFrame) {  // a click in the open map opens nothing
        TechLog('S', L"waypoint: learning, click at " + where + L" with the map open, ignored");
        return;
    }
    TechLog('S', L"waypoint: learning, click at " + where);
    s_linkX = clickX;
    s_linkY = clickY;
    s_linkClickAt = GetTickCount64();  // it counts when GW2's map opens: only the link does that
}

void WaypointStop() {
    s_stop = true;
    s_teaching = false;
    if (s_thread.joinable()) s_thread.join();
    s_busy = false;
}

void RenderWaypointPreview() {
    ++s_frames;
    bool mapOpenNow;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        mapOpenNow = g_mapOpen;
    }
    s_mapOpenFrame = mapOpenNow;
    if (const unsigned long long clicked = s_linkClickAt.load(); clicked && !mapOpenNow && GetTickCount64() - clicked > 1500) {
        s_linkClickAt = 0;
        TechLog('S', L"waypoint: that click did not open the map, not remembered");
    }
    if (s_linkClickAt.load() && mapOpenNow) {  // your click opened GW2's map: that was the link
        s_linkClickAt = 0;
        s_teaching = false;
        const POINT place{s_linkX.load(), s_linkY.load()};
        std::wstring key;
        {
            std::lock_guard<std::mutex> lock(s_mu);
            key = s_placeKey;
            s_places[key] = place;
        }
        TechLog('S', L"waypoint: place learned at " + std::to_wstring(place.x) + L"," + std::to_wstring(place.y));
        std::lock_guard<std::mutex> lock(g_mu);
        g_cfg.SaveValue(L"Waypoint", (key + L"X").c_str(), std::to_wstring(place.x));
        g_cfg.SaveValue(L"Waypoint", (key + L"Y").c_str(), std::to_wstring(place.y));
        SetStatus(Tr(L"Place remembered. A click in the preview now does this by itself."), 8000);
    }
    uint32_t selected;
    std::vector<uint32_t> history, collection;
    {
        std::lock_guard<std::mutex> lock(s_mu);
        if (!s_open) s_previewL = s_previewT = s_previewR = s_previewB = 0;
        if (!s_open || WaypointPassThrough() || s_hidePreview) return;
    }
    const bool teaching = s_teaching.load() && GetTickCount64() <= s_teachUntil.load();  // no new run: your click on the link comes first
    bool mayFly, onMap, namesOn;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        ReadStored(g_cfg.dataDir);
        namesOn = g_cfg.chatCodeNames;
        mayFly = !s_busy.load() && !teaching;
        onMap = (g_inMap || g_test.forceMap) && !g_mapOpen;
    }
    if (const uint32_t wanted = s_flyRequest.exchange(0); wanted && mayFly) {
        Fly(wanted);
        return;
    }
    if (!onMap) return;  // as our chat: never on a loading screen or at the character choice, never over GW2's open map
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
    ImGui::SetNextWindowSizeConstraints(ImVec2(nexusPx * 36.0f, nexusPx * 16.0f), ImVec2(FLT_MAX, FLT_MAX));  // the buttons always fit
    bool open = true;
    const std::string title = UI(Tr(L"Waypoints")) + "###kbwaypoint";
    const bool visible = ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_NoCollapse);
    {
        const ImVec2 at = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
        s_previewL = static_cast<int>(at.x);
        s_previewT = static_cast<int>(at.y);
        s_previewR = static_cast<int>(at.x + size.x);
        s_previewB = static_cast<int>(at.y + size.y);
    }
    bool pushedUf = false;
    if (uf && uf->FontSize > 0.0f) {
        ImGui::SetWindowFontScale(nexusPx / uf->FontSize);
        ImGui::PushFont(uf);
        pushedUf = true;
    }
    if (visible) {
        const float fs = ImGui::GetFontSize();
        const std::string teachText = UI(Tr(L"Click the new link in GW2's chat once yourself, at its start. I remember the place, from then on it goes by itself."));
        const float rowH = teaching ? ImGui::CalcTextSize(teachText.c_str(), nullptr, false, ImGui::GetContentRegionAvail().x).y + ImGui::GetStyle().ItemSpacing.y
                                    : ImGui::GetTextLineHeightWithSpacing();
        const float footer = rowH + ImGui::GetFrameHeightWithSpacing() * (s_moreOpen ? 2.0f : 1.0f);
        const float listW = fs * 13.0f;
        const float mapH = std::max(fs * 6.0f, ImGui::GetContentRegionAvail().y - footer);
        gct::ChatLinks::PoiView poi;
        const bool known = g_links.Poi(selected, &poi);
        ImGui::BeginChild("##wpmap", ImVec2(std::max(fs * 8.0f, ImGui::GetContentRegionAvail().x - listW - ImGui::GetStyle().ItemSpacing.x), mapH), false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        if (known && (poi.x != 0.0f || poi.y != 0.0f)) DrawMap(poi, mayFly);
        else if (selected) DrawBareMarker(selected, mayFly);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##wplist", ImVec2(0.0f, mapH), true);
        PoiList(Tr(L"Collection"), collection, selected, collection, true);
        PoiList(Tr(L"Your last ones"), history, selected, collection, false);
        ImGui::EndChild();
        if (teaching) {
            ImGui::PushStyleColor(ImGuiCol_Text, UiWarm());
            ImGui::TextWrapped("%s", teachText.c_str());
            ImGui::PopStyleColor();
        } else if (known) {
            ImGui::TextUnformatted(poi.name.c_str());
            ImGui::SameLine();
            const std::string kind = UI(PoiKind(poi.type));
            if (poi.name.find(kind) != std::string::npos) ImGui::TextDisabled("%s", poi.map.c_str());  // "Wegmarke Fort Marriner" says it already
            else ImGui::TextDisabled("%s · %s", kind.c_str(), poi.map.c_str());
        } else if (selected) {
            ImGui::TextDisabled("%s", UI(!namesOn || g_links.PoisLoaded() ? Tr(L"Waypoint") : Tr(L"The table of waypoints is on its way …")).c_str());
        } else {
            ImGui::TextDisabled("%s", UI(Tr(L"Click a waypoint in the chat, or choose one from your lists.")).c_str());
        }
        if (selected && !teaching) {  // the point shown: in or out of the collection, at the line's right edge as in the lists
            const bool kept = std::find(collection.begin(), collection.end(), selected) != collection.end();
            ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - ImGui::GetTextLineHeight()));
            ImGui::PushID("shown");
            if (RowMark(kept, kept ? Tr(L"Remove from collection") : Tr(L"Add to collection"))) ToggleCollection(selected);
            ImGui::PopID();
        }
        if (mayFly && selected) {
            if (ImGui::Button(UI(Tr(L"Show it in GW2")).c_str())) Fly(selected);
            ImGui::SameLine();
        }
        // What is rarely needed stays folded: teaching the link's place again.
        ImGui::AlignTextToFramePadding();
        s_moreOpen = ImGui::TreeNodeEx(UI(Tr(L"More") + L"###wpmore").c_str(), ImGuiTreeNodeFlags_NoTreePushOnOpen);
        if (s_moreOpen) {
            if (mayFly && selected) {
                if (ImGui::Button(UI(Tr(L"Teach the place again")).c_str())) {
                    s_forgetPlace = true;
                    Fly(selected);
                }
            } else {
                ImGui::TextDisabled("%s", UI(Tr(L"Click a waypoint in the chat, or choose one from your lists.")).c_str());
            }
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
