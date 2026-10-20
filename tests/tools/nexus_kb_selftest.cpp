// nexus_kb_selftest.cpp — loads GW2 Keyboard for Nexus (gw2keyboard_nexus.dll) outside the game: a real ImGui
// context, a stand-in Nexus API, a few frames drawn, a chat line raised the way "Events: Chat" does, then unloaded.
// Catches crashes before they could happen inside GW2. Usage: nexus_kb_selftest.exe <path to gw2keyboard_nexus.dll>

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "../../addons/nexus-keyboard/src/chat_events.hpp"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "nexus/Nexus.h"

#include "../../addons/nexus-keyboard/src/chat/DecoderRingApi.h"

// Events: Chat as the add-on finds it: this program exports GetAddonDef with Events: Chat's signature and a version the
// test changes – a new version of the other add-on, as Nexus pulls it over night.
AddonVersion_t g_fakeChatVersion{9, 9, 9, 0};
extern "C" __declspec(dllexport) AddonDefinition_t* GetAddonDef() {
    static AddonDefinition_t d{};
    d.Signature = bridge::kChatEventsSignature;
    d.Version = g_fakeChatVersion;
    d.Name = "Events: Chat (stand-in)";
    return &d;
}

namespace {

// Decoder Ring stood in, the way a broken update could behave: 1 = a longer record than ours, without string ends;
// 2 = it crashes inside Resolve.
int g_decoderMode = 0;
DecoderStatus FakeResolve(uint8_t type, uint32_t id, const char*, DecoderRecord* out) {
    if (g_decoderMode == 2) {
        volatile int* p = nullptr;
        *p = 1;
    }
    std::memset(out, 0xC3, sizeof(DecoderRecord) + sizeof(DecoderRecord) / 2);
    out->schemaVersion = 9;
    out->linkType = type;
    out->status = DR_Resolved;
    out->id = id;
    out->factCount = 255;
    return DR_Resolved;
}
DecoderRingApi g_fakeDecoder{DECODER_RING_API_VERSION, FakeResolve, nullptr, nullptr};
void* FakeDataLink(const char* id) {
    return g_decoderMode && std::strcmp(id, DECODER_RING_DATALINK) == 0 ? &g_fakeDecoder : nullptr;
}

struct ChatSourceTest {  // as in kb_shared.hpp
    int reading = 0, pause = 0, shown = 0, dropped = 0, unreadable = 0, disabled = 0, decoderFault = 0;
    int received = 0, ignored = 0;
    char version[32] = {}, before[32] = {};
};
std::wstring g_iniPath;  // <GW2Keyboard>\gw2-chat-translator.ini

}  // namespace

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
int g_gameKeys = 0;  // GW2 keys the add-on asked Nexus to press (GameBinds_InvokeAsync)
void FakeGameBind(EGameBinds, int32_t) { ++g_gameKeys; }
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
    if (FindWindowW(L"ArenaNet_Gr_Window_Class", nullptr)) {  // GW2 runs: its MumbleLink (chat line, map, loading
        // screens) would steer the add-on under test, and nothing of a test may ever reach a game
        std::printf("GW2 is running - close it first (the test would read its MumbleLink)\n");
        return 3;
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
        // A fresh ini every run: a run that failed half-way skipped its restoring steps (the mini mode, the layout) and the
        // next run started from that – and failed somewhere else.
        DeleteFileW((dir + L"\\gw2-chat-translator.ini").c_str());
        WritePrivateProfileStringW(L"Nexus", L"SetupDone", L"1", (dir + L"\\gw2-chat-translator.ini").c_str());
        WritePrivateProfileStringW(L"NexusTabs", nullptr, nullptr, (dir + L"\\gw2-chat-translator.ini").c_str());  // no old tabs
        WritePrivateProfileStringW(L"Translate", L"WriteLangs", L"DE,EN-GB,FR,ES", (dir + L"\\gw2-chat-translator.ini").c_str());
        wchar_t alpha[16] = {};  // GW2KB_SHOT_ALPHA: the window's opacity for the picture (60-255)
        WritePrivateProfileStringW(L"Window", L"Opacity", GetEnvironmentVariableW(L"GW2KB_SHOT_ALPHA", alpha, 16) ? alpha : L"255",
                                   (dir + L"\\gw2-chat-translator.ini").c_str());
    }
    {  // The chat source guard: nothing of an earlier run. Outside picture mode the last session had Events: Chat 9.9.8.0,
       // so this one's 9.9.9.0 is new since then: a note once, the technical details name the one before.
        std::wstring dir = argv[1];
        dir = dir.substr(0, dir.find_last_of(L"\\/") + 1) + L"GW2Keyboard";
        g_iniPath = dir + L"\\gw2-chat-translator.ini";
        WritePrivateProfileStringW(L"ChatSource", nullptr, nullptr, g_iniPath.c_str());
        if (!GetEnvironmentVariableW(L"GW2KB_SHOT", nullptr, 0))
            WritePrivateProfileStringW(L"ChatSource", L"LastVersion", L"9.9.8.0", g_iniPath.c_str());
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
    api.DataLink_Get = FakeDataLink;
    api.GameBinds_InvokeAsync = FakeGameBind;
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

    // The chat source guard (1): Events: Chat 9.9.9.0 is new since the last session (9.9.8.0) – the details name the one
    // before; reading goes on as always (updates are Nexus' business, ours is never to crash and never to show garbage).
    const bool shotMode = GetEnvironmentVariableW(L"GW2KB_SHOT", nullptr, 0) > 0;
    auto src = reinterpret_cast<int (*)(int, ChatSourceTest*, char*)>(GetProcAddress(dll, "Gw2KbTestChatSource"));
    auto srcState = [&] {
        ChatSourceTest st{};
        if (src) src(0, &st, nullptr);
        return st;
    };
    if (ok && !src) {
        std::printf("FAIL: no Gw2KbTestChatSource\n");
        ok = false;
    }
    if (ok && !shotMode) {
        const ChatSourceTest st = srcState();
        std::printf("chat source at start: Events: Chat %s (before: %s), shown %d, reading %d, pause %d\n", st.version,
                    st.before, st.shown, st.reading, st.pause);
        const bool fine = std::strcmp(st.version, "9.9.9.0") == 0 && std::strcmp(st.before, "9.9.8.0") == 0 &&
                          st.shown == 1 && st.reading == 1 && st.pause == 0;
        if (!fine) std::printf("FAIL: the chat source at start (new version noted, the first line shown)\n");
        ok = ok && fine;
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
        if (src && GetEnvironmentVariableW(L"GW2KB_SHOT_TECH", nullptr, 0)) src(8, nullptr, nullptr);  // Options > Technical
        const bool shotSnippets = src && GetEnvironmentVariableW(L"GW2KB_SHOT_SNIPPETS", nullptr, 0) > 0;  // text blocks
        wchar_t optTab[8] = {};  // GW2KB_SHOT_OPTIONS=<tab>: the options open on that tab (0 General … 6 Technical)
        if (src && GetEnvironmentVariableW(L"GW2KB_SHOT_OPTIONS", optTab, 8)) {
            char tab[8] = {};
            WideCharToMultiByte(CP_UTF8, 0, optTab, -1, tab, sizeof tab, nullptr, nullptr);
            src(8, nullptr, tab);
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
            if (shotSnippets && i == 10) src(10, nullptr, nullptr);  // opened once the windows stand (as by a click)
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
        if (src) src(9, nullptr, nullptr);  // the options closed again (they count as "in use")
        if (GetEnvironmentVariableW(L"GW2KB_SHOT_SNIPPETS", nullptr, 0)) {  // the text blocks closed: a click beside them
            ImGui::GetIO().MousePos = ImVec2(1800, 100);
            ImGui::GetIO().MouseDown[0] = true;
            Frame();
            ImGui::GetIO().MouseDown[0] = false;
            Frame();
            ImGui::GetIO().MousePos = ImVec2(-1000, -1000);
        }
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
        {  // a word longer than the line (a theme code): broken inside the word, sent whole
            const char* code = " GW2C1:111111-1E1E1E-0D2B45-4A73A3-163D5E-DDDDDD-8A8A8A-B8D8F8-111111-1E1E1E-0D2B45";
            type(code);
            frames(10);
            std::string flat;
            for (const char* p = th->input; *p; ++p) {
                if (*p == '\r') { if (p[1] == '\n') ++p; continue; }
                flat += *p == '\n' ? ' ' : *p;
            }
            const int breaks = static_cast<int>(std::count(th->input, th->input + std::strlen(th->input), '\r'));
            const bool whole = flat.size() >= std::strlen(code) && flat.compare(flat.size() - std::strlen(code), std::string::npos, code) == 0;
            std::printf("long word: %d break(s) inside it, flat text whole: %s\n", breaks, whole ? "yes" : "no");
            if (breaks < 1 || !whole) std::printf("FAIL: a word longer than the line was not broken (or not kept whole)\n");
            ok = ok && breaks >= 1 && whole;
            for (size_t k = 0; k < std::strlen(code); ++k) press(VK_BACK);  // back to the sentence
            frames(5);
        }
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
                // A waypoint never opens GW2's chat line (typing mode stops the map flight) nor the map, and our window
                // stays (in game the old way "every link opens GW2's chat line" took it whenever Decoder Ring had not
                // named it yet).
                {
                    const int keys = g_gameKeys;
                    const ImVec2 w0 = size();
                    waypoint(4);
                    frames(6);
                    const ImVec2 w1 = size();
                    std::printf("waypoint click: game keys %d, window %.0fx%.0f -> %.0fx%.0f\n", g_gameKeys - keys, w0.x, w0.y,
                                w1.x, w1.y);
                    const bool quiet = g_gameKeys == keys && std::fabs(w1.x - w0.x) < 1 && std::fabs(w1.y - w0.y) < 1;
                    if (!quiet) std::printf("FAIL: a waypoint click pressed a GW2 key or moved our window\n");
                    ok = ok && quiet;
                }
                // The minimised button stands still – also with a frame height in fractional pixels (Nexus' font in the
                // game): ImGui floors a window's size, and a size not in whole pixels let the button creep up a pixel a frame.
                {
                    ImGuiStyle& st = ImGui::GetStyle();
                    const float padY = st.FramePadding.y;
                    st.FramePadding.y += 3.3f;  // the button above ImGui's 32 px minimum, not in whole pixels
                    waypoint(0);
                    frames(4);
                    const ImVec2 b0 = pos();
                    frames(60);
                    const ImVec2 b1 = pos();
                    st.FramePadding.y = padY;
                    waypoint(1);
                    frames(6);
                    std::printf("minimised button over 60 frames: %.0f,%.0f -> %.0f,%.0f\n", b0.x, b0.y, b1.x, b1.y);
                    const bool still = std::fabs(b1.x - b0.x) < 0.5f && std::fabs(b1.y - b0.y) < 0.5f;
                    if (!still) std::printf("FAIL: the minimised button moved by itself\n");
                    ok = ok && still;
                }
                // A press on the chat that minimises the window (a link) must not carry ImGui's window move into the button:
                // the mouse moved a few pixels before it let go, and the corner went up by the big window's height (in the
                // game: "after a few clicks the window stood at the top").
                {
                    ImGuiIO& io = ImGui::GetIO();
                    const ImVec2 q0 = pos(), z0 = size();
                    io.MousePos = ImVec2(q0.x + 200, q0.y + 80);
                    frames(2);
                    io.MouseDown[0] = true;
                    frames(2);
                    waypoint(0);  // the link minimises the window while the mouse is still held
                    frames(3);
                    io.MousePos = ImVec2(q0.x + 204, q0.y + 77);
                    frames(3);
                    io.MouseDown[0] = false;
                    frames(3);
                    io.MousePos = ImVec2(-1000, -1000);
                    waypoint(1);
                    frames(6);
                    const ImVec2 q1 = pos(), z1 = size();
                    std::printf("press, minimise, move, let go, open: corner %.0f,%.0f -> %.0f,%.0f\n", q0.x, q0.y + z0.y,
                                q1.x, q1.y + z1.y);
                    const bool kept = std::fabs(q1.x - q0.x) < 0.5f && std::fabs((q1.y + z1.y) - (q0.y + z0.y)) < 0.5f;
                    if (!kept) std::printf("FAIL: a press that minimised the window moved its corner\n");
                    ok = ok && kept;
                    Sleep(800);  // longer than the three quick clicks' 700 ms: this press is no first click of the next test
                }
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
                // Minimise and open five times by three quick clicks, near (not at) the screen edges: the window must come
                // back to exactly where it stood every time (a click remembered from before minimising snapped it).
                {
                    const ImVec2 cycleAt(7, 1080 - 28 - s2.y - 6);
                    ImGui::SetWindowPos("GW2Chatlator###kbmain", cycleAt);
                    frames(3);
                    const ImVec2 c0 = pos();
                    ImVec2 cN = c0;
                    std::printf("cycles near the edge: start %.0f,%.0f", c0.x, c0.y);
                    for (int c = 0; c < 5; ++c) {
                        const ImVec2 inside(cN.x + 200, cN.y + 80);
                        click(inside);
                        click(inside);
                        click(inside);
                        frames(3);
                        ImGui::GetIO().MousePos = ImVec2(-1000, -1000);
                        frames(2);
                        waypoint(1);
                        frames(4);
                        cN = pos();
                        std::printf("  %d: %.0f,%.0f", c + 1, cN.x, cN.y);
                    }
                    std::printf("\n");
                    const bool stays = std::fabs(cN.x - c0.x) < 1 && std::fabs(cN.y - c0.y) < 1;
                    if (!stays) std::printf("FAIL: the window wandered over minimise / open cycles\n");
                    ok = ok && stays;
                    ImGui::SetWindowPos("GW2Chatlator###kbmain", p2);
                    frames(3);
                }
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
            // The mini mode: held down for a moment and dragged, the whole window moves (not only at its edges).
            if (auto mini = reinterpret_cast<void (*)(int)>(GetProcAddress(dll, "Gw2KbTestMini"))) {
                auto wpos = [] {
                    ImGuiWindow* w = ImGui::FindWindowByName("GW2Chatlator###kbmain");
                    return w ? w->Pos : ImVec2(-1, -1);
                };
                auto wsize = [] {
                    ImGuiWindow* w = ImGui::FindWindowByName("GW2Chatlator###kbmain");
                    return w ? w->Size : ImVec2(-1, -1);
                };
                {  // mini mode on, off, on, off …: the window must not crawl (its bottom stays, its size comes back)
                    ImGui::SetWindowPos("GW2Chatlator###kbmain", ImVec2(300, 500));
                    frames(4);
                    const ImVec2 t0 = wpos(), ts0 = wsize();
                    std::printf("mini on / off: start %.0f,%.0f %.0fx%.0f", t0.x, t0.y, ts0.x, ts0.y);
                    for (int c = 0; c < 4; ++c) {
                        mini(1);
                        frames(4);
                        const ImVec2 on = wpos(), son = wsize();
                        mini(0);
                        frames(4);
                        const ImVec2 off = wpos(), soff = wsize();
                        std::printf("  [on %.0f,%.0f %.0f / off %.0f,%.0f %.0f]", on.x, on.y, son.y, off.x, off.y, soff.y);
                    }
                    const ImVec2 t1 = wpos(), ts1 = wsize();
                    std::printf("\n");
                    const bool still = std::fabs(t1.y - t0.y) < 1 && std::fabs(ts1.y - ts0.y) < 1 && std::fabs(t1.x - t0.x) < 1;
                    if (!still) std::printf("FAIL: the window crawled while the mini mode went on and off\n");
                    ok = ok && still;
                }
                {  // a long input made the big window taller; mini on, sent there, mini off: back to its normal height
                    const ImVec2 keepSize = wsize();
                    ImGui::SetWindowSize("GW2Chatlator###kbmain", ImVec2(keepSize.x, 250));  // small: a long input must grow it
                    ImGui::SetWindowPos("GW2Chatlator###kbmain", ImVec2(300, 500));
                    frames(4);
                    const ImVec2 g0 = wpos(), gs0 = wsize();
                    *th->focus = true;
                    frames(3);
                    type("Wer kommt mit in die Fraktale heute Abend, wir brauchen noch zwei Leute fuer die Bosse und danach "
                         "wollen wir noch eine Runde im WvW drehen, Treffpunkt ist wie immer am Wegpunkt vor der Festung, "
                         "bringt bitte Essen und Werkzeug mit und sagt kurz Bescheid wenn ihr spaeter kommt, wir warten "
                         "gern ein paar Minuten auf euch, bis dann und viel Spass beim Spielen euch allen heute Abend");
                    frames(10);
                    const float grownH = wsize().y;
                    mini(1);
                    frames(4);
                    *th->focus = true;
                    frames(3);
                    const int partsBefore = th->sentParts;
                    press(VK_RETURN);
                    // Until it is really sent (the input empty again) – a fixed 60 frames were too few for the first run
                    // of a fresh build (the virus scanner reads the new DLL, nothing is cached yet).
                    for (int k = 0; k < 400 && (th->sentParts == partsBefore || th->input[0]); ++k) frames(1);
                    frames(10);
                    mini(0);
                    frames(10);
                    const ImVec2 g1 = wpos(), gs1 = wsize();
                    std::printf("long input, mini, sent, back: %.0f high -> grown %.0f -> sent %d part(s) -> %.0f,%.0f %.0f high\n",
                                gs0.y, grownH, th->sentParts - partsBefore, g1.x, g1.y, gs1.y);
                    const bool back = grownH > gs0.y && th->sentParts > partsBefore && std::fabs(gs1.y - gs0.y) < 1 &&
                                      std::fabs((g1.y + gs1.y) - (g0.y + gs0.y)) < 1;
                    if (!back) std::printf("FAIL: the big window did not come back to its normal size after the mini mode\n");
                    ok = ok && back;
                    ImGui::SetWindowSize("GW2Chatlator###kbmain", keepSize);
                    frames(3);
                }
                mini(1);
                frames(5);
                ImGui::SetWindowPos("GW2Chatlator###kbmain", ImVec2(300, 600));
                frames(3);
                const ImVec2 m0 = wpos(), ms = wsize();
                ImGuiIO& io = ImGui::GetIO();
                io.MousePos = ImVec2(m0.x + ms.x * 0.5f, m0.y + ms.y * 0.5f);
                io.MouseDown[0] = true;
                frames(1);
                Sleep(320);  // held a moment
                frames(1);
                for (int k = 0; k < 10; ++k) {
                    io.MousePos.x += 6;
                    io.MousePos.y -= 4;
                    frames(1);
                }
                io.MouseDown[0] = false;
                frames(2);
                const ImVec2 m1 = wpos();
                io.MousePos = ImVec2(-1000, -1000);
                mini(0);
                frames(5);
                std::printf("mini mode, hold and drag: %.0f,%.0f -> %.0f,%.0f (%.0fx%.0f)\n", m0.x, m0.y, m1.x, m1.y, ms.x, ms.y);
                const bool moved = std::fabs((m1.x - m0.x) - 60) < 8 && std::fabs((m1.y - m0.y) + 40) < 8;
                if (!moved) std::printf("FAIL: the mini window did not follow the mouse\n");
                ok = ok && moved;
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
            // The corner against everything that is no move of yours (user, 2026-10-20: "such window bugs never again"):
            // 400 random steps – minimise, open, the mini mode on / off, a click on the window, a press held while the
            // window changes its shape and a mouse that moves before it lets go, frame heights in fractional pixels, idle
            // frames – and after every step the bottom-left corner stands exactly where it stood. A fixed seed: a
            // failure comes back the same way.
            {
                auto shape = reinterpret_cast<void (*)(int)>(GetProcAddress(dll, "Gw2KbTestWaypoint"));
                auto miniMode = reinterpret_cast<void (*)(int)>(GetProcAddress(dll, "Gw2KbTestMini"));
                auto rect = [] {
                    ImGuiWindow* w = ImGui::FindWindowByName("GW2Chatlator###kbmain");
                    return w ? ImVec4(w->Pos.x, w->Pos.y, w->SizeFull.x, w->SizeFull.y) : ImVec4(-1, -1, 0, 0);
                };
                auto corner = [&] {
                    const ImVec4 r = rect();
                    return ImVec2(r.x, r.y + r.w);
                };
                if (shape && miniMode) {
                    ImGuiIO& io = ImGui::GetIO();
                    ImGuiStyle& st = ImGui::GetStyle();
                    const float padY = st.FramePadding.y;
                    const float pads[] = {0.0f, 0.3f, 1.7f, 3.3f};
                    ImGui::SetWindowPos("GW2Chatlator###kbmain", ImVec2(300, 800 - rect().w));
                    frames(5);
                    const ImVec2 c0 = corner();
                    ImVec2 cFail = c0;
                    wchar_t seedText[16] = {};  // GW2KB_TEST_SEED: another random walk (the default one in every build)
                    const unsigned seed = GetEnvironmentVariableW(L"GW2KB_TEST_SEED", seedText, 16)
                                              ? static_cast<unsigned>(_wtoi(seedText)) : 20261020u;
                    std::mt19937 rnd(seed);
                    bool miniOn = false;
                    int failedAt = -1;
                    const char* what = "";
                    const char* from = "";
                    for (int step = 0; step < 400 && failedAt < 0; ++step) {
                        const ImVec4 r = rect();
                        const bool button = r.z < 80;  // the minimised button
                        from = button ? "the button" : miniOn ? "the mini window" : "the big window";
                        const ImVec2 inside(r.x + std::min(200.0f, r.z * 0.5f), r.y + std::min(80.0f, r.w * 0.5f));
                        switch (rnd() % 7) {
                            case 0:
                                what = "minimise";
                                if (!button) shape(0);
                                break;
                            case 1:
                                what = "open";
                                if (button) shape(1);
                                break;
                            case 2:
                                what = "mini mode on / off";
                                if (!button) {
                                    miniOn = !miniOn;
                                    miniMode(miniOn ? 1 : 0);
                                }
                                break;
                            case 3:  // down and up on the spot (three of them in a row minimise: a shape, no move)
                                what = "click";
                                io.MousePos = inside;
                                frames(1);
                                io.MouseDown[0] = true;
                                frames(1);
                                io.MouseDown[0] = false;
                                frames(1);
                                break;
                            case 4:  // held while the window changes its shape; the mouse moves a little before it lets go
                                what = "press, new shape, move, let go";
                                io.MousePos = inside;
                                frames(1);
                                io.MouseDown[0] = true;
                                frames(2);
                                shape(button ? 1 : 0);
                                frames(2);
                                io.MousePos = ImVec2(inside.x + static_cast<float>(static_cast<int>(rnd() % 17) - 8),
                                                     inside.y + static_cast<float>(static_cast<int>(rnd() % 17) - 8));
                                frames(2);
                                io.MouseDown[0] = false;
                                frames(1);
                                break;
                            case 5:
                                what = "frame height in fractional pixels";
                                st.FramePadding.y = padY + pads[rnd() % 4];
                                break;
                            default:
                                what = "idle";
                                break;
                        }
                        io.MousePos = ImVec2(-1000, -1000);
                        frames(3);
                        const ImVec2 c = corner();
                        if (std::fabs(c.x - c0.x) > 0.5f || std::fabs(c.y - c0.y) > 0.5f) {
                            failedAt = step;
                            cFail = c;
                        }
                    }
                    st.FramePadding.y = padY;
                    if (rect().z < 80) shape(1);
                    frames(3);
                    if (miniOn) miniMode(0);
                    frames(5);
                    std::printf("corner over 400 random steps: %.0f,%.0f%s\n", c0.x, c0.y, failedAt < 0 ? " – stood still" : "");
                    if (failedAt >= 0)
                        std::printf("FAIL: step %d (%s, from %s) moved the window's corner to %.0f,%.0f\n", failedAt, what, from,
                                    cFail.x, cFail.y);
                    ok = ok && failedAt < 0;
                }
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
        // "Translate what I say": spoken and the switch on has priority – with "Send" at "as written" it goes into the
        // first of your languages; spoken with the switch off, and typed, exactly as before. A voice result in an empty
        // input marks it spoken; sent, the mark is gone.
        {
            auto route = reinterpret_cast<int (*)(int, int, char*)>(GetProcAddress(dll, "Gw2KbTestVoiceRoute"));
            auto voiceFinal = reinterpret_cast<int (*)(const char*)>(GetProcAddress(dll, "Gw2KbTestVoiceFinal"));
            if (route && voiceFinal) {
                char lang[16] = {};
                const int spokenOn = route(1, 1, lang);
                const std::string into = lang;
                const int spokenOff = route(1, 0, lang);
                const int typedOn = route(0, 1, lang);
                for (int i = 0; i < 16; ++i) press(VK_BACK);  // the input empty
                frames(3);
                voiceFinal("hello there");
                frames(3);
                const int marked = voiceFinal(nullptr);
                const std::string said = th->input;
                press(VK_RETURN);
                frames(10);
                const int afterSend = voiceFinal(nullptr);
                std::printf("translate what I say: spoken+on %d (into %s)  spoken+off %d  typed+on %d  marked %d (%s)  after sending %d\n",
                            spokenOn, into.c_str(), spokenOff, typedOn, marked, said.c_str(), afterSend);
                const bool voiceOk = spokenOn == 1 && !into.empty() && spokenOff == 0 && typedOn == 0 && marked == 1 &&
                                     said.find("hello there") != std::string::npos && afterSend == 0;
                if (!voiceOk) std::printf("FAIL: \"Translate what I say\" does not decide as it should\n");
                ok = ok && voiceOk;
            }
        }
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
    // The chat source guard (2): what an update of Events: Chat could hand over – a changed layout, unreadable memory,
    // a guard page, endless strings, thousands of random lines – and a broken Decoder Ring. Nothing may take the process
    // down; garbage is never shown; reading pauses by itself and comes back on "Resume".
    if (ok && src && !shotMode) {
        auto frames4 = [](int n) {
            for (int i = 0; i < n; ++i) {
                Frame();
                Sleep(15);
            }
        };
        auto raise = [&](int type, const char* who, const char* say) {
            bridge::ChatEventsMessage e{};
            e.type = static_cast<bridge::ChatEventsType>(type);
            e.generic.characterName = who;
            e.generic.content = say;
            g_chatEvents(&e);
        };
        auto newVersion = [&](uint16_t build) {
            g_fakeChatVersion = AddonVersion_t{9, 9, build, 0};
            src(1, nullptr, nullptr);
        };
        constexpr int kMap = 4;
        bool good = true;
        auto expect = [&](bool cond, const char* what) {
            if (!cond) std::printf("FAIL: chat source - %s\n", what);
            good = good && cond;
        };
        // A. A changed layout (a new field moved every pointer by 8 bytes): its lines are never shown; 5 of them among
        // the last 20 pause reading; "Resume" reads again.
        newVersion(11);
        ChatSourceTest st = srcState();
        expect(std::strcmp(st.before, "9.9.9.0") == 0, "a new version of Events: Chat was not noted");
        const int shownA = st.shown;
        auto shifted = [&] {
            alignas(8) unsigned char raw[72] = {};
            const char* name = "Bo Shifted";
            const char* account = "Bo.1234";
            const char* text2 = "a line in the new layout";
            raw[8] = kMap;  // type at 8, flags at 12, then the new field (8 bytes), the GUID (16), the three pointers
            for (int k = 0; k < 16; ++k) raw[24 + k] = static_cast<unsigned char>(0x9A + 13 * k);
            std::memcpy(raw + 40, &name, 8);
            std::memcpy(raw + 48, &account, 8);
            std::memcpy(raw + 56, &text2, 8);
            g_chatEvents(raw);
        };
        for (int i = 0; i < 4; ++i) shifted();
        st = srcState();
        expect(st.shown == shownA && st.pause == 0, "a line of a changed layout was shown (or 4 paused reading)");
        shifted();
        raise(kMap, "Ana Sample", "a good line while paused");
        st = srcState();
        std::printf("chat source, changed layout: shown +%d, pause %d\n", st.shown - shownA, st.pause);
        expect(st.shown == shownA && st.pause == 4, "a changed layout did not pause reading");
        src(2, nullptr, nullptr);  // "Resume"
        raise(kMap, "Ana Sample", "plain again");
        expect(srcState().shown == shownA + 1 && srcState().pause == 0, "\"Resume\" did not read again");
        // B. The shield: memory that cannot be read pauses reading at once; "Resume" reads again.
        void* reserved = VirtualAlloc(nullptr, 1 << 16, MEM_RESERVE, PAGE_NOACCESS);
        auto* pages = static_cast<unsigned char*>(VirtualAlloc(nullptr, 2 * 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        DWORD oldProt = 0;
        VirtualProtect(pages + 4096, 4096, PAGE_NOACCESS, &oldProt);
        std::memset(pages + 4096 - 100, 'A', 100);  // a string running into a page that cannot be read: no end
        int trips = 0;
        auto tripped = [&] {
            const ChatSourceTest t = srcState();
            if (t.pause != 3) return false;
            ++trips;
            src(2, nullptr, nullptr);
            return true;
        };
        raise(kMap, static_cast<const char*>(reserved) + 64, "a name in memory nobody owns");
        expect(tripped(), "a name in unreadable memory did not stop reading");
        raise(kMap, "Ana Sample", reinterpret_cast<const char*>(pages + 4096 - 100));
        expect(tripped(), "a text running into an unreadable page did not stop reading");
        g_chatEvents(reinterpret_cast<void*>(static_cast<uintptr_t>(0x1234)));
        expect(tripped(), "a line pointer outside user memory did not stop reading");
        g_chatEvents(static_cast<unsigned char*>(reserved) + 256);
        expect(tripped(), "a line in unreadable memory did not stop reading");
        // A guard page (as at the end of a thread's stack): looked at first, never touched – its guard must survive.
        auto* guard = static_cast<char*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE | PAGE_GUARD));
        raise(kMap, guard + 16, "a name on a guard page");
        expect(tripped(), "a name on a guard page did not stop reading");
        MEMORY_BASIC_INFORMATION gmi{};
        VirtualQuery(guard, &gmi, sizeof(gmi));
        std::printf("chat source, guard page: still guarded %s\n", (gmi.Protect & PAGE_GUARD) ? "yes" : "NO");
        expect((gmi.Protect & PAGE_GUARD) != 0, "reading touched a guard page (another thread's stack could die later)");
        g_chatEvents(nullptr);  // no line at all
        expect(srcState().pause == 0, "an empty event paused reading");
        const std::string endless(5000, 'A');  // readable, but no end within the bound: never shown
        const int shownB = srcState().shown;
        raise(kMap, "Ana Sample", endless.c_str());
        raise(77, "Ana Sample", "a type nobody knows");
        raise(kMap, "Ana \x01 Sample", "a control character in the name");
        raise(kMap, "Ana Sample", "\xFF\xFE not utf-8");
        st = srcState();
        expect(st.shown == shownB && st.pause == 0, "garbage was shown (or 4 bad lines paused reading)");
        // C. Thousands of random lines: random heads, pointers into random bytes, unreadable memory, nowhere.
        std::vector<std::vector<char>> pool(64);
        uint64_t rng = 0x9E3779B97F4A7C15ull;
        auto next = [&rng] {
            rng ^= rng << 13;
            rng ^= rng >> 7;
            rng ^= rng << 17;
            return rng;
        };
        for (auto& b : pool) {
            b.resize(16 + next() % 3000);
            for (char& c : b) c = static_cast<char>(next() % 3 ? 0x20 + next() % 95 : next());
            if (next() % 2) b.back() = 0;
        }
        auto pick = [&]() -> const char* {
            switch (next() % 6) {
                case 0: return nullptr;
                case 1: return reinterpret_cast<const char*>(static_cast<uintptr_t>(next() % 0x10000));
                case 2: return static_cast<const char*>(reserved) + next() % 0x8000;
                case 3: return reinterpret_cast<const char*>(static_cast<uintptr_t>(next()));
                default: {
                    const auto& b = pool[next() % pool.size()];
                    return b.data() + next() % b.size();
                }
            }
        };
        const int shownC = srcState().shown;
        int pauses = 0;
        for (int k = 0; k < 3000; ++k) {
            alignas(8) unsigned char raw[64];
            for (auto& c : raw) c = static_cast<unsigned char>(next());
            auto* e = reinterpret_cast<bridge::ChatEventsMessage*>(raw);
            if (next() % 4) e->type = static_cast<bridge::ChatEventsType>(next() % 14);
            if (next() % 4) {
                e->generic.characterName = pick();
                e->generic.accountName = pick();
                e->generic.content = pick();
            }
            g_chatEvents(raw);
            if (srcState().pause != 0) {
                ++pauses;
                src(2, nullptr, nullptr);
            }
            if (k % 100 == 0) Frame();
        }
        frames4(5);
        st = srcState();
        std::printf("chat source, 3000 random lines: shown %d, %d unreadable, reading paused and resumed %d times, add-on %s\n",
                    st.shown - shownC, st.unreadable, pauses, st.disabled ? "OFF" : "alive");
        raise(kMap, "Ana Sample", "still here after the noise");
        expect(srcState().shown == st.shown + 1 && !st.disabled, "reading did not come back after the random lines");
        // D. Lines stop fitting (5 of the last 20): reading pauses; "Resume" reads again.
        newVersion(13);
        for (int i = 0; i < 5; ++i) raise(kMap, "Ana Sample", "plain");
        for (int i = 0; i < 5; ++i) raise(77, "Ana Sample", "garbage type");
        st = srcState();
        std::printf("chat source, lines stopped fitting: pause %d\n", st.pause);
        expect(st.pause == 4, "bad lines did not pause reading");
        src(2, nullptr, nullptr);
        const int shownD = srcState().shown;
        raise(kMap, "Ana Sample", "back again");
        expect(srcState().shown == shownD + 1, "\"Resume\" did not read again");
        // E. By hand: reading off – not a byte is touched (an unreadable line does not even pause it); on again.
        src(4, nullptr, nullptr);
        const int shownE = srcState().shown;
        raise(kMap, "Ana Sample", "while off");
        g_chatEvents(static_cast<unsigned char*>(reserved) + 512);
        st = srcState();
        expect(st.shown == shownE && st.pause == 0 && st.reading == 0, "reading off still read");
        src(5, nullptr, nullptr);
        raise(kMap, "Ana Sample", "on again");
        expect(srcState().shown == shownE + 1, "reading on did not read");
        // F. The protocol: versions and the rules counted – never a name, a text, a channel or a length of a line.
        std::vector<char> prot(4096);
        src(6, nullptr, prot.data());
        const std::string p(prot.data());
        std::printf("chat source protocol: %zu bytes, first line: %s\n", p.size(), p.substr(0, p.find('\n')).c_str());
        expect(p.find("9.9.13.0") != std::string::npos && p.find("lines, counted") != std::string::npos &&
                   p.find("flags 0x") == std::string::npos,  // the old list of single lines: "type 1 flags 0x0 name 9 …"
               "the protocol lacks versions or the counts – or lists single lines");
        expect(p.find("Ana") == std::string::npos && p.find("Sample") == std::string::npos &&
                   p.find("plain") == std::string::npos && p.find("garbage") == std::string::npos &&
                   p.find("Kai") == std::string::npos,
               "the protocol holds names or text");
        // G. A broken Decoder Ring: a longer record without string ends, then a crash inside it – the chat codes stay
        // plain, our add-on stays on. The chat tab in front, so the codes are drawn (and resolved).
        if (hooks0) hooks0()->forceMap = true;
        src(7, nullptr, nullptr);
        frames4(5);
        g_decoderMode = 1;
        raise(kMap, "Ana Sample", "look at [&AgGvWwEA] and [&BAwEAAA=]");
        frames4(10);
        st = srcState();
        expect(!st.disabled && !st.decoderFault, "a longer Decoder Ring record broke something");
        g_decoderMode = 2;
        raise(kMap, "Ana Sample", "again [&AgEEYQAA]");
        frames4(10);
        st = srcState();
        std::printf("Decoder Ring broken: fault caught %d, add-on %s\n", st.decoderFault, st.disabled ? "OFF" : "alive");
        expect(st.decoderFault && !st.disabled, "a crash inside Decoder Ring was not caught");
        g_decoderMode = 0;
        // G2. The chat draws only what is in view (the height cache): the content height – so every line's place – is
        // the same whether a line was measured this frame or comes from the cache: at the bottom, scrolled to the
        // middle, frames later; after a width change it settles at once and stays.
        src(7, nullptr, nullptr);
        for (int i = 0; i < 300; ++i)
            raise(kMap, "Ana Sample", i % 3 ? "a short line"
                                            : "a long line that wraps over the width of the chat window at least once or "
                                              "twice when the window is narrow enough to make it wrap");
        auto chatWin = []() -> ImGuiWindow* {
            for (ImGuiWindow* w : static_cast<ImGuiContext*>(ImGui::GetCurrentContext())->Windows)
                if (std::strncmp(w->Name, "GW2Chatlator###kbmain/chat", 26) == 0) return w;
            return nullptr;
        };
        frames4(2);
        ImGuiWindow* cw = chatWin();
        expect(cw != nullptr, "the chat area was not found");
        if (cw) {
            const float hBottom = cw->ScrollMax.y;
            frames4(5);
            const float hLater = cw->ScrollMax.y;
            cw->ScrollTarget.y = cw->ScrollMax.y * 0.5f;  // scrolled up to the middle
            cw->ScrollTargetCenterRatio.y = 0.0f;
            frames4(5);
            const float hMiddle = cw->ScrollMax.y, atY = cw->Scroll.y;
            const ImVec2 mainSize = ImGui::FindWindowByName("GW2Chatlator###kbmain")->Size;
            ImGui::SetWindowSize("GW2Chatlator###kbmain", ImVec2(mainSize.x + 140, mainSize.y));  // wider: other wraps
            frames4(2);
            const float hWide = cw->ScrollMax.y;
            frames4(6);
            const float hWideLater = cw->ScrollMax.y;
            ImGui::SetWindowSize("GW2Chatlator###kbmain", mainSize);
            frames4(3);
            std::printf("chat height cache: content %.0f bottom / %.0f later / %.0f in the middle (at %.0f), wider %.0f / %.0f\n",
                        hBottom, hLater, hMiddle, atY, hWide, hWideLater);
            expect(hBottom > 0 && hBottom == hLater && hLater == hMiddle && atY > 0 && atY < hMiddle,
                   "the chat's content height changed with the height cache (lines would jump)");
            expect(hWide == hWideLater && hWide < hMiddle, "after a width change the chat did not settle at once");
        }
        // H. For the developer: Options > Technical draws the version, the counts and the last lines.
        src(8, nullptr, nullptr);
        frames4(6);
        st = srcState();
        expect(!st.disabled, "drawing the technical details switched the add-on off");
        src(9, nullptr, nullptr);
        frames4(2);
        // I. The version this session ends with is what the next one compares with (checked after the unload).
        newVersion(14);
        expect(std::strcmp(srcState().before, "9.9.13.0") == 0, "the version before was not named");
        {  // E5: every line in exactly one pot
            const ChatSourceTest c = srcState();
            std::printf("chat source pots: %d received = %d shown + %d dropped + %d ignored\n", c.received, c.shown,
                        c.dropped, c.ignored);
            expect(c.received > 0 && c.received == c.shown + c.dropped + c.ignored, "a line disappeared unnoticed");
        }
        std::printf("chat source guard: %s (the shield caught %d unreadable lines)\n", good ? "ok" : "FAILED",
                    srcState().unreadable);
        ok = ok && good;
    }
    // The waypoint quantum macro: in the test there is no GW2 window and no swap chain – it must never click, give up
    // with a note, and its thread must end cleanly, also when the add-on unloads while it still runs.
    auto waypointHook = reinterpret_cast<void (*)(int)>(GetProcAddress(dll, "Gw2KbTestWaypoint"));
    auto synthetic = reinterpret_cast<int (*)()>(GetProcAddress(dll, "Gw2KbTestSyntheticInputs"));
    if (ok && waypointHook && synthetic && !shotMode) {
        const int before = synthetic();
        waypointHook(3);  // the test key: no GW2 in front – no click
        Sleep(400);
        waypointHook(2);  // the macro: no chat, no frame – no click
        for (int i = 0; i < 120; ++i) {
            Frame();
            Sleep(20);
        }
        const int sent = synthetic() - before;
        std::printf("quantum macro without GW2: %d synthetic mouse inputs sent\n", sent);
        if (sent != 0) std::printf("FAIL: the macro sent mouse input without GW2 in front\n");
        ok = ok && sent == 0;
        waypointHook(2);  // started again: the unload below must end it
    }
    std::printf("text font built by the stand-in Nexus: %d\n", g_fontsBuilt);
    ok = ok && g_fontsBuilt >= 1;
    // GW2KB_TEST_REMOVEALL=1 (only on a copy of the add-on – it deletes its GW2Keyboard folder): "remove everything",
    // then unload – nothing may come back (no ini, no learned words written on unload).
    std::wstring removeDir;
    const bool removeAll = GetEnvironmentVariableW(L"GW2KB_TEST_REMOVEALL", nullptr, 0) > 0;
    if (removeAll) {
        removeDir = argv[1];
        removeDir = removeDir.substr(0, removeDir.find_last_of(L"\\/") + 1) + L"GW2Keyboard";
        if (auto removeFn = reinterpret_cast<int (*)(int)>(GetProcAddress(dll, "Gw2KbTestRemoveAll"))) {
            removeFn(1);
            for (int k = 0; k < 400 && removeFn(0) == 0; ++k) {
                Frame();
                Sleep(15);
            }
            std::printf("remove everything: state %d, folder there before unload: %s\n", removeFn(0),
                        GetFileAttributesW(removeDir.c_str()) != INVALID_FILE_ATTRIBUTES ? "yes" : "no");
        }
    }
    std::printf("unload\n");
    const ULONGLONG unloadAt = GetTickCount64();
    def->Unload();
    const ULONGLONG unloadMs = GetTickCount64() - unloadAt;  // a thread that does not end would hang it
    std::printf("unload took %llu ms\n", unloadMs);
    if (unloadMs > 3000) std::printf("FAIL: the unload hung (a thread did not end)\n");
    ok = ok && unloadMs <= 3000;
    ok = ok && g_chatEvents == nullptr;
    if (src && !shotMode && !removeAll) {  // the next session compares with the version this one ended with
        wchar_t last[64] = {};
        GetPrivateProfileStringW(L"ChatSource", L"LastVersion", L"", last, 64, g_iniPath.c_str());
        std::printf("chat source after the unload: last version %ls\n", last);
        if (std::wcscmp(last, L"9.9.14.0") != 0) std::printf("FAIL: the version of Events: Chat was not remembered\n");
        ok = ok && std::wcscmp(last, L"9.9.14.0") == 0;
    }
    if (removeAll) {
        const bool back = GetFileAttributesW(removeDir.c_str()) != INVALID_FILE_ATTRIBUTES;
        std::printf("remove everything: folder there after unload: %s\n", back ? "yes" : "no");
        if (back) std::printf("FAIL: something wrote into the removed folder again\n");
        ok = ok && !back;
    }
    FreeLibrary(dll);
    ImGui::DestroyContext(ctx);
    std::printf(ok ? "PASS\n" : "FAIL\n");
    return ok ? 0 : 1;
}
