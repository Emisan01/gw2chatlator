// nexus_kb_selftest.cpp — loads GW2 Keyboard for Nexus (gw2keyboard_nexus.dll) outside the game: a real ImGui
// context, a stand-in Nexus API, a few frames drawn, a chat line raised the way "Events: Chat" does, then unloaded.
// Catches crashes before they could happen inside GW2. Usage: nexus_kb_selftest.exe <path to gw2keyboard_nexus.dll>

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "../../addons/nexus-keyboard/src/chat_events.hpp"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "nexus/Nexus.h"

namespace {

std::vector<GUI_RENDER> g_renders;
EVENT_CONSUME g_chatEvents = nullptr;

void FakeRegister(ERenderType, GUI_RENDER cb) { g_renders.push_back(cb); }
void FakeDeregister(GUI_RENDER cb) {
    for (auto& r : g_renders)
        if (r == cb) r = nullptr;
}
void FakeSubscribe(const char* id, EVENT_CONSUME cb) {
    if (std::strcmp(id, bridge::kChatEventsEvent) == 0) g_chatEvents = cb;
}
void FakeUnsubscribe(const char* id, EVENT_CONSUME) {
    if (std::strcmp(id, bridge::kChatEventsEvent) == 0) g_chatEvents = nullptr;
}
void FakeBind(const char*, INPUTBINDS_PROCESS, const char*) {}
void FakeUnbind(const char*) {}
void FakeLog(ELogLevel, const char* channel, const char* text) { std::printf("[%s] %s\n", channel, text); }
void* Alloc(size_t n, void*) { return std::malloc(n); }

struct TestHooks {  // as in kb_main.cpp
    bool forceMap = false;
    bool noSend = false;
    int sentParts = 0;
    char lastSent[1024] = {};
    char* input = nullptr;
    bool* focus = nullptr;
    int selfFixes = 0;
    char lastFix[128] = {};
    char sendLang[16] = {};
};
void Free(void* p, void*) { std::free(p); }

// Nexus' font functions, stood in: the font is built into the atlas before the next frame and sent to the add-on.
struct FontAsk {
    std::string id, file;
    float size = 0;
    FONTS_RECEIVECALLBACK cb = nullptr;
    ImFontConfig cfg;
};
std::vector<FontAsk> g_fontAsks;
FontAsk g_fontLast;  // for a resize
int g_fontsBuilt = 0;
void FakeFontAdd(const char* id, float size, const char* file, FONTS_RECEIVECALLBACK cb, void* cfg) {
    FontAsk a;
    a.id = id;
    a.file = file;
    a.size = size;
    a.cb = cb;
    if (cfg) a.cfg = *static_cast<ImFontConfig*>(cfg);
    g_fontLast = a;
    g_fontAsks.push_back(a);
}
void FakeFontResize(const char*, float size) {
    g_fontLast.size = size;
    g_fontAsks.push_back(g_fontLast);
}
void FakeFontRelease(const char*, FONTS_RECEIVECALLBACK) { g_fontLast.cb = nullptr; }

void Frame() {
    ImGuiIO& io = ImGui::GetIO();
    for (FontAsk& a : g_fontAsks) {  // between frames, like Nexus
        ImFont* f = io.Fonts->AddFontFromFileTTF(a.file.c_str(), a.size, &a.cfg);
        unsigned char* px = nullptr;
        int tw = 0, th = 0;
        io.Fonts->GetTexDataAsRGBA32(&px, &tw, &th);
        if (f && a.cb) {
            a.cb(a.id.c_str(), f);
            ++g_fontsBuilt;
        }
    }
    g_fontAsks.clear();
    io.DisplaySize = ImVec2(1920, 1080);
    io.DeltaTime = 1.0f / 60.0f;
    ImGui::NewFrame();
    for (size_t i = 0; i < g_renders.size(); ++i) {
        if (!g_renders[i]) continue;
        if (i == 1) {  // the options callback: Nexus draws it inside its own window, far from ours
            ImGui::SetNextWindowPos(ImVec2(1500, 900));
            ImGui::Begin("Nexus options (stand-in)");
            g_renders[i]();
            ImGui::End();
        } else {
            g_renders[i]();
        }
    }
    ImGui::Render();
}

// GW2KB_SHOT=<file.bmp>: the window drawn into a picture (ImGui's triangles rasterized here, no GPU) – to look at it.
void Rasterize(ImDrawData* dd, const unsigned char* tex, int tw, int th, std::vector<float>& img, int W, int H) {
    for (int n = 0; n < dd->CmdListsCount; ++n) {
        const ImDrawList* cl = dd->CmdLists[n];
        for (const ImDrawCmd& cmd : cl->CmdBuffer) {
            if (cmd.UserCallback) continue;
            const int cx0 = std::max(0, (int)cmd.ClipRect.x), cy0 = std::max(0, (int)cmd.ClipRect.y);
            const int cx1 = std::min(W, (int)cmd.ClipRect.z), cy1 = std::min(H, (int)cmd.ClipRect.w);
            for (unsigned i = 0; i + 2 < cmd.ElemCount; i += 3) {
                const ImDrawVert* v[3];
                for (int k = 0; k < 3; ++k) v[k] = &cl->VtxBuffer[cmd.VtxOffset + cl->IdxBuffer[cmd.IdxOffset + i + k]];
                const float x0 = std::min({v[0]->pos.x, v[1]->pos.x, v[2]->pos.x}), x1 = std::max({v[0]->pos.x, v[1]->pos.x, v[2]->pos.x});
                const float y0 = std::min({v[0]->pos.y, v[1]->pos.y, v[2]->pos.y}), y1 = std::max({v[0]->pos.y, v[1]->pos.y, v[2]->pos.y});
                const float area = (v[1]->pos.x - v[0]->pos.x) * (v[2]->pos.y - v[0]->pos.y) - (v[2]->pos.x - v[0]->pos.x) * (v[1]->pos.y - v[0]->pos.y);
                if (std::fabs(area) < 1e-6f) continue;
                for (int y = std::max(cy0, (int)y0); y < std::min(cy1, (int)y1 + 1); ++y)
                    for (int x = std::max(cx0, (int)x0); x < std::min(cx1, (int)x1 + 1); ++x) {
                        const float px = x + 0.5f, py = y + 0.5f;
                        auto edge = [&](const ImDrawVert* a, const ImDrawVert* b) {
                            return ((b->pos.x - a->pos.x) * (py - a->pos.y) - (b->pos.y - a->pos.y) * (px - a->pos.x)) / area;
                        };
                        const float w0 = edge(v[1], v[2]), w1 = edge(v[2], v[0]), w2 = edge(v[0], v[1]);
                        if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                        float col[4] = {0, 0, 0, 0};
                        for (int c = 0; c < 4; ++c)
                            col[c] = (w0 * ((v[0]->col >> (8 * c)) & 0xFF) + w1 * ((v[1]->col >> (8 * c)) & 0xFF) +
                                      w2 * ((v[2]->col >> (8 * c)) & 0xFF)) / 255.0f;
                        const float u = w0 * v[0]->uv.x + w1 * v[1]->uv.x + w2 * v[2]->uv.x;
                        const float vv = w0 * v[0]->uv.y + w1 * v[1]->uv.y + w2 * v[2]->uv.y;
                        const int tx = std::clamp((int)(u * tw), 0, tw - 1), ty = std::clamp((int)(vv * th), 0, th - 1);
                        const unsigned char* t = tex + 4 * (ty * tw + tx);
                        const float a = col[3] * t[3] / 255.0f;
                        float* d = &img[3 * (y * W + x)];
                        for (int c = 0; c < 3; ++c) d[c] = d[c] * (1 - a) + col[c] * (t[c] / 255.0f) * a;
                    }
            }
        }
    }
}

void SaveBmp(const wchar_t* path, const std::vector<float>& img, int W, int H) {
    const int row = (W * 3 + 3) & ~3;
    std::vector<unsigned char> data(54 + row * H, 0);
    auto put32 = [&](int at, uint32_t v) { for (int i = 0; i < 4; ++i) data[at + i] = (v >> (8 * i)) & 0xFF; };
    data[0] = 'B'; data[1] = 'M';
    put32(2, (uint32_t)data.size()); put32(10, 54); put32(14, 40); put32(18, W); put32(22, H);
    data[26] = 1; data[28] = 24; put32(34, row * H);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            for (int c = 0; c < 3; ++c)
                data[54 + (H - 1 - y) * row + x * 3 + (2 - c)] = (unsigned char)std::clamp(img[3 * (y * W + x) + c] * 255.0f, 0.0f, 255.0f);
    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"wb") == 0 && f) {
        fwrite(data.data(), 1, data.size(), f);
        fclose(f);
    }
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        std::printf("usage: nexus_kb_selftest <gw2keyboard_nexus.dll>\n");
        return 2;
    }
    setvbuf(stdout, nullptr, _IONBF, 0);
    ImGui::SetAllocatorFunctions(Alloc, Free);
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;  // no imgui.ini: window sizes of an earlier run must not change this one
    unsigned char* pixels = nullptr;
    int w = 0, h = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);

    {  // no first start: nothing is downloaded during the test
        std::wstring dir = argv[1];
        dir = dir.substr(0, dir.find_last_of(L"\\/") + 1) + L"GW2Keyboard";
        CreateDirectoryW(dir.c_str(), nullptr);
        WritePrivateProfileStringW(L"Nexus", L"SetupDone", L"1", (dir + L"\\gw2-chat-translator.ini").c_str());
        WritePrivateProfileStringW(L"NexusTabs", nullptr, nullptr, (dir + L"\\gw2-chat-translator.ini").c_str());  // no old tabs
        WritePrivateProfileStringW(L"Translate", L"WriteLangs", L"DE,EN-GB,FR,ES", (dir + L"\\gw2-chat-translator.ini").c_str());
        wchar_t alpha[16] = {};  // GW2KB_SHOT_ALPHA: the window's opacity for the picture (60-255)
        WritePrivateProfileStringW(L"Window", L"Opacity", GetEnvironmentVariableW(L"GW2KB_SHOT_ALPHA", alpha, 16) ? alpha : L"255",
                                   (dir + L"\\gw2-chat-translator.ini").c_str());
    }
    HMODULE dll = LoadLibraryW(argv[1]);
    auto getDef = dll ? reinterpret_cast<AddonDefinition_t* (*)()>(GetProcAddress(dll, "GetAddonDef")) : nullptr;
    AddonDefinition_t* def = getDef ? getDef() : nullptr;
    if (!def || def->APIVersion != NEXUS_API_VERSION) {
        std::printf("FAIL: no valid GetAddonDef\n");
        return 1;
    }
    if (def->Provider != UP_GitHub || !def->UpdateLink || std::string(def->UpdateLink).find("Emisan01/gw2chatlator") == std::string::npos) {
        std::printf("FAIL: autoupdate provider not set to UP_GitHub (%d, %s)\n", static_cast<int>(def->Provider), def->UpdateLink ? def->UpdateLink : "null");
        return 1;
    }
    std::printf("add-on: %s %u.%u.%u (Provider: %d, UpdateLink: %s)\n", def->Name, def->Version.Major, def->Version.Minor, def->Version.Build, static_cast<int>(def->Provider), def->UpdateLink);
    AddonAPI_t api{};
    api.ImguiContext = ctx;
    api.ImguiMalloc = reinterpret_cast<void*>(&Alloc);
    api.ImguiFree = reinterpret_cast<void*>(&Free);
    api.GUI_Register = FakeRegister;
    api.GUI_Deregister = FakeDeregister;
    api.Events_Subscribe = FakeSubscribe;
    api.Events_Unsubscribe = FakeUnsubscribe;
    api.InputBinds_RegisterWithString = FakeBind;
    api.InputBinds_Deregister = FakeUnbind;
    api.Log = FakeLog;
    api.Fonts_AddFromFile = FakeFontAdd;
    api.Fonts_Resize = FakeFontResize;
    api.Fonts_Release = FakeFontRelease;
    std::printf("load\n");
    def->Load(&api);
    std::printf("loaded\n");
    bool ok = g_renders.size() == 2 && g_chatEvents != nullptr;
    for (int i = 0; i < 5 && ok; ++i) Frame();
    std::printf("frames ok\n");

    // A chat line as "Events: Chat" raises it (in German, so no translator is asked during the test).
    std::string text = "Hallo zusammen, wer kommt mit zum Weltboss?";
    bridge::ChatEventsMessage m{};
    m.type = bridge::ChatEventsType::Map;
    m.generic.characterName = "Kai Example";
    m.generic.content = text.c_str();
    if (ok) g_chatEvents(&m);
    text.assign(text.size(), 'x');  // the add-on must have copied it
    for (int i = 0; i < 30 && ok; ++i) {
        Frame();
        Sleep(20);
    }

    // GW2KB_SHOT: chat lines of every channel (invented names), then the window as a picture – and nothing else.
    auto hooks0 = reinterpret_cast<TestHooks* (*)()>(GetProcAddress(dll, "Gw2KbTestHooks"));
    wchar_t shotPath[MAX_PATH] = {};
    if (ok && hooks0 && GetEnvironmentVariableW(L"GW2KB_SHOT", shotPath, MAX_PATH)) {
        hooks0()->forceMap = true;
        const struct { bridge::ChatEventsType t; const char* who; const char* say; } lines[] = {
            {bridge::ChatEventsType::Map, "Aria Example", "anyone up for the world boss in five?"},
            {bridge::ChatEventsType::Map, "Bram Sample", "gz! that drop is [&AgGvWwEA] nice"},
            {bridge::ChatEventsType::Party, "Cleo Test", "ready when you are"},
            {bridge::ChatEventsType::Squad, "Dax Demo", "stack on tag please, then push left"},
            {bridge::ChatEventsType::Guild, "Eli Mock", "guild missions at 8, see https://example.org/plan"},
            {bridge::ChatEventsType::Whisper, "Finn Fake", "hey, still need help with the fractal?"},
            {bridge::ChatEventsType::Local, "Gia Placeholder", "hello there"},
            {bridge::ChatEventsType::Map, "Hal Invented", "meet at [&BAwEAAA=] in five"},
            {bridge::ChatEventsType::Party, "Ivy Example", "soup [&AgHFMAAA] and rune [&AgEEYQAA] for you"},
        };
        for (const auto& l : lines) {
            bridge::ChatEventsMessage e{};
            e.type = l.t;
            e.generic.characterName = l.who;
            e.generic.content = l.say;
            g_chatEvents(&e);
        }
        wchar_t frames[16] = {};  // GW2KB_SHOT_FRAMES: wait longer (e.g. the waypoint table on its first load)
        const int n = GetEnvironmentVariableW(L"GW2KB_SHOT_FRAMES", frames, 16) ? _wtoi(frames) : 40;
        wchar_t tabMask[16] = {};  // GW2KB_SHOT_TAB=<channel bits>: a tab of your own in front, its settings open
        if (GetEnvironmentVariableW(L"GW2KB_SHOT_TAB", tabMask, 16))
            if (auto newTab = reinterpret_cast<void (*)(uint32_t, bool)>(GetProcAddress(dll, "Gw2KbTestNewTab")))
                newTab(static_cast<uint32_t>(_wtoi(tabMask)), true);
        wchar_t hover[32] = {};  // GW2KB_SHOT_HOVER=x,y: the mouse there (a tooltip in the picture)
        if (GetEnvironmentVariableW(L"GW2KB_SHOT_HOVER", hover, 32)) {
            float hx = 0, hy = 0;
            swscanf_s(hover, L"%f,%f", &hx, &hy);
            ImGui::GetIO().MousePos = ImVec2(hx, hy);
        }
        wchar_t typed[1024] = {};  // GW2KB_SHOT_TYPE=<text>: typed into the input first (the box, the grey word, …)
        if (GetEnvironmentVariableW(L"GW2KB_SHOT_TYPE", typed, 1024)) {
            for (int i = 0; i < 60; ++i) Frame();  // spelling and prediction start on the worker
            *hooks0()->focus = true;
            for (int i = 0; i < 3; ++i) Frame();
            for (const wchar_t* c = typed; *c; ++c) {
                ImGui::GetIO().AddInputCharacter(*c);
                Frame();
                Sleep(5);
            }
        }
        for (int i = 0; i < n; ++i) {
            Frame();
            if (i == 2)  // bottom left, where it stands in the game (no imgui.ini remembers a place)
                if (ImGuiWindow* w = ImGui::FindWindowByName("GW2Chatlator###kbmain"))
                    ImGui::SetWindowPos("GW2Chatlator###kbmain", ImVec2(0, 1080 - 28 - w->Size.y));
            Sleep(20);
        }
        const int W = 1920, H = 1080;
        std::vector<float> img(3 * W * H);
        for (int y = 0; y < H; ++y)  // a dim, slightly green game-like background
            for (int x = 0; x < W; ++x) {
                float* d = &img[3 * (y * W + x)];
                const bool bright = GetEnvironmentVariableW(L"GW2KB_SHOT_BRIGHT", nullptr, 0) > 0;  // snow / sky
                d[0] = bright ? 0.80f + 0.15f * x / W : 0.22f + 0.10f * x / W;
                d[1] = bright ? 0.84f + 0.10f * y / H : 0.27f + 0.08f * y / H;
                d[2] = bright ? 0.88f : 0.20f;
            }
        ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);  // the atlas as it is now (fonts added since)
        Rasterize(ImGui::GetDrawData(), pixels, w, h, img, W, H);
        SaveBmp(shotPath, img, W, H);
        std::printf("picture: %ls\n", shotPath);
    }

    // Typing: the window without a map, sending only recorded. A long line, Space after a word, Tab, Enter.
    auto hooks = reinterpret_cast<TestHooks* (*)()>(GetProcAddress(dll, "Gw2KbTestHooks"));
    TestHooks* th = hooks ? hooks() : nullptr;
    if (ok && th) {
        th->forceMap = true;
        th->noSend = true;
        ImGuiIO& io = ImGui::GetIO();
        io.KeyMap[ImGuiKey_Enter] = VK_RETURN;
        io.KeyMap[ImGuiKey_Tab] = VK_TAB;
        io.KeyMap[ImGuiKey_Backspace] = VK_BACK;
        io.KeyMap[ImGuiKey_UpArrow] = VK_UP;
        io.KeyMap[ImGuiKey_DownArrow] = VK_DOWN;
        auto frames = [](int n) {
            for (int i = 0; i < n; ++i) {
                Frame();
                Sleep(15);
            }
        };
        auto type = [&](const char* s) {
            for (const char* c = s; *c; ++c) {
                io.AddInputCharacter(static_cast<unsigned char>(*c));
                frames(1);
            }
        };
        auto press = [&](int vk) {
            io.KeysDown[vk] = true;
            frames(1);
            io.KeysDown[vk] = false;
            frames(1);
        };
        frames(60);  // spelling and prediction start on the worker
        *th->focus = true;
        frames(3);
        type("Hallo zusammen, wir gehen jetzt in die Fraktale und dann vielleicht noch ein bisschen in den WvW, wer "
             "mag kommt einfach mit, ich lade euch ein sobald alle da sind und dann geht es los mit dem ersten Boss");
        frames(10);
        const size_t typed = std::strlen(th->input);
        std::printf("typed %zu bytes, line breaks: %d\n", typed, static_cast<int>(std::count(th->input, th->input + typed, '\n')));
        type(" teilw");
        frames(80);  // the typing help for "teilw"
        type(" ");
        frames(10);
        const char* end = th->input + std::strlen(th->input);
        std::printf("after Space: ...%s\n", std::strlen(th->input) > 30 ? end - 30 : th->input);
        type("schn");
        frames(80);
        press(VK_TAB);
        frames(5);
        type(" ");
        frames(10);
        end = th->input + std::strlen(th->input);
        std::printf("after Tab + Space: ...%s\n", std::strlen(th->input) > 30 ? end - 30 : th->input);
        press(VK_RETURN);
        frames(60);
        std::printf("Enter: %d part(s) recorded, input now %zu bytes\n", th->sentParts, std::strlen(th->input));
        ok = ok && th->sentParts >= 2 && std::strlen(th->input) == 0 && !std::strchr(th->lastSent, '\n');
        // Up in the empty box brings the message back, Down empties it again.
        *th->focus = true;
        frames(3);
        press(VK_UP);
        frames(3);
        const size_t recalled = std::strlen(th->input);
        press(VK_DOWN);
        frames(3);
        std::printf("history: Up -> %zu bytes, Down -> %zu bytes\n", recalled, std::strlen(th->input));
        ok = ok && recalled > 100 && std::strlen(th->input) == 0;
        // The window stays where it was put – idle, while chat arrives, while typing (it once kept sliding up).
        {
            auto pos = [] {
                ImGuiWindow* w = ImGui::FindWindowByName("GW2Chatlator###kbmain");
                return w ? w->Pos : ImVec2(-1, -1);
            };
            ImGui::SetWindowPos("GW2Chatlator###kbmain", ImVec2(10, 1080 - 28 - 380));
            frames(5);
            const ImVec2 start = pos();
            frames(150);
            const ImVec2 idle = pos();
            for (int k = 0; k < 30; ++k) {
                bridge::ChatEventsMessage e{};
                e.type = bridge::ChatEventsType::Map;
                e.generic.characterName = "Drift Test";
                e.generic.content = "a line that arrives while the window should stand still";
                g_chatEvents(&e);
                frames(3);
            }
            const ImVec2 chat = pos();
            type("hello there");
            frames(30);
            const ImVec2 typing = pos();
            for (int k = 0; k < 11; ++k) press(VK_BACK);
            frames(10);
            std::printf("window: start %.0f,%.0f  idle %.0f,%.0f  chat %.0f,%.0f  typing %.0f,%.0f\n", start.x, start.y, idle.x,
                        idle.y, chat.x, chat.y, typing.x, typing.y);
            const bool still = std::fabs(idle.y - start.y) < 1 && std::fabs(chat.y - start.y) < 1 && std::fabs(typing.y - start.y) < 1;
            if (!still) std::printf("FAIL: the window moved by itself\n");
            ok = ok && still;
            // A waypoint click: the window gets out of the way (its bottom edge stays) and comes back to the same place.
            if (auto waypoint = reinterpret_cast<void (*)(int)>(GetProcAddress(dll, "Gw2KbTestWaypoint"))) {
                auto size = [] {
                    ImGuiWindow* w = ImGui::FindWindowByName("GW2Chatlator###kbmain");
                    return w ? w->Size : ImVec2(-1, -1);
                };
                ImGui::SetWindowPos("GW2Chatlator###kbmain", ImVec2(0, 1080 - 28 - 360));  // right at the edge
                frames(5);
                const ImVec2 p0 = pos(), s0 = size();
                waypoint(0);
                frames(6);
                const ImVec2 p1 = pos(), s1 = size();
                waypoint(1);
                frames(6);
                const ImVec2 p2 = pos(), s2 = size();
                std::printf("waypoint: before %.0f,%.0f %.0fx%.0f  out of the way %.0f,%.0f %.0fx%.0f  back %.0f,%.0f %.0fx%.0f\n",
                            p0.x, p0.y, s0.x, s0.y, p1.x, p1.y, s1.x, s1.y, p2.x, p2.y, s2.x, s2.y);
                const bool shrunk = s1.x < s0.x && s1.y <= s0.y && std::fabs((p1.y + s1.y) - (p0.y + s0.y)) < 2;
                const bool back = std::fabs(p2.x - p0.x) < 2 && std::fabs(p2.y - p0.y) < 2 && std::fabs(s2.y - s0.y) < 2;
                if (!shrunk || !back) std::printf("FAIL: the window did not come back where it was\n");
                ok = ok && shrunk && back;
                // Three quick clicks on the chat area minimise the window too; it opens again the same way.
                auto click = [&](ImVec2 at) {
                    ImGuiIO& io = ImGui::GetIO();
                    io.MousePos = at;
                    io.MouseDown[0] = true;
                    frames(1);
                    io.MouseDown[0] = false;
                    frames(1);
                };
                // In the mini mode (nothing but the input) the clicks go into the input – there is nothing else.
                const ImVec2 inChat = s2.y > 100 ? ImVec2(p2.x + 200, p2.y + 80) : ImVec2(p2.x + 200, p2.y + s2.y * 0.5f);
                click(inChat);
                click(inChat);
                frames(1);
                const ImVec2 s2b = size();  // two clicks: nothing yet
                click(inChat);
                frames(3);
                const ImVec2 s3 = size();
                ImGui::GetIO().MousePos = ImVec2(-1000, -1000);
                waypoint(1);
                frames(6);
                const ImVec2 s4 = size();
                std::printf("three clicks: two %.0fx%.0f  three %.0fx%.0f  open again %.0fx%.0f\n", s2b.x, s2b.y, s3.x, s3.y, s4.x, s4.y);
                const bool clicks = std::fabs(s2b.x - s2.x) < 2 && s3.x < s2.x && std::fabs(s4.x - s2.x) < 2;
                if (!clicks) std::printf("FAIL: three quick clicks did not minimise the window (or two did)\n");
                ok = ok && clicks;
                // Too small for the chat and all parts: it grows upwards once (whole pixels), its bottom stays, then it
                // stands still (a fraction of a pixel used to move it up every frame without resizing it).
                ImGui::SetWindowSize("GW2Chatlator###kbmain", ImVec2(520, 251));
                frames(3);
                const ImVec2 g0 = pos(), gs0 = size();
                frames(60);
                const ImVec2 g1 = pos(), gs1 = size();
                std::printf("too small: after 3 frames %.0f,%.0f %.0fx%.0f  after 60 more %.0f,%.0f %.0fx%.0f\n", g0.x, g0.y,
                            gs0.x, gs0.y, g1.x, g1.y, gs1.x, gs1.y);
                const bool stands = std::fabs(g1.y - g0.y) < 1 && std::fabs(gs1.y - gs0.y) < 1 &&
                                    std::fabs((g1.y + gs1.y) - (p2.y + 251)) < 1;  // the bottom of the 251 px window
                if (!stands) std::printf("FAIL: the too small window crept\n");
                ok = ok && stands;
                ImGui::SetWindowSize("GW2Chatlator###kbmain", s2);
                ImGui::SetWindowPos("GW2Chatlator###kbmain", p2);
                frames(3);
            }
            // Minimise when not in use: the input had the focus, a click into the game, then nothing for the set time –
            // the window goes down to the small button; the focus key opens it again.
            if (auto autoMin = reinterpret_cast<int (*)(int)>(GetProcAddress(dll, "Gw2KbTestAutoMinimize"))) {
                autoMin(1);
                *th->focus = true;
                frames(4);
                ImGuiIO& io = ImGui::GetIO();
                io.MousePos = ImVec2(1200, 300);  // the game, far from our window
                io.MouseDown[0] = true;
                frames(1);
                io.MouseDown[0] = false;
                frames(3);
                const int right = autoMin(-1);  // not yet: the time has not passed
                Sleep(1300);
                frames(4);
                const int later = autoMin(-1);
                io.MousePos = ImVec2(-1000, -1000);
                *th->focus = true;
                frames(6);
                const int key = autoMin(-1);
                autoMin(0);
                std::printf("minimise when not in use: at once %d  after the time %d  focus key %d\n", right, later, key);
                const bool mins = right == 0 && later == 1 && key == 0;
                if (!mins) std::printf("FAIL: the window did not minimise when not in use / open again\n");
                ok = ok && mins;
            }
            *th->focus = true;  // back from the map the window does not take the keyboard by itself
            frames(3);
        }
        // Learning mode: a word you retype yourself after its space ("Gildne " -> "Gilden ") is noticed.
        type("wir Gildne ");
        frames(40);
        for (int i = 0; i < 7; ++i) press(VK_BACK);
        frames(40);
        type("Gilden ");
        frames(10);
        std::printf("self-correction: %d noticed, newest %s (input: %s)\n", th->selfFixes, th->lastFix, th->input);
        ok = ok && th->selfFixes >= 1;
        // Answering in their language: a French line, then a click on the name – that tab sends in French.
        bridge::ChatEventsMessage fr{};
        fr.type = bridge::ChatEventsType::Map;
        fr.generic.characterName = "Jean Invented";
        fr.generic.content = "Bonjour tout le monde, est-ce que quelqu'un peut m'aider avec cette mission s'il vous plait ?";
        g_chatEvents(&fr);
        frames(80);  // the worker detects the language
        if (auto whisper = reinterpret_cast<void (*)(const char*)>(GetProcAddress(dll, "Gw2KbTestWhisper"))) whisper("Jean Invented");
        frames(5);
        std::printf("reply language for Jean Invented: %s\n", th->sendLang);
        ok = ok && std::strcmp(th->sendLang, "FR") == 0;
    }
    std::printf("text font built by the stand-in Nexus: %d\n", g_fontsBuilt);
    ok = ok && g_fontsBuilt >= 1;
    std::printf("unload\n");
    def->Unload();
    ok = ok && g_chatEvents == nullptr;
    FreeLibrary(dll);
    ImGui::DestroyContext(ctx);
    std::printf(ok ? "PASS\n" : "FAIL\n");
    return ok ? 0 : 1;
}
