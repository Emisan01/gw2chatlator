// nexus_kb_selftest.cpp — loads GW2 Keyboard for Nexus (gw2keyboard_nexus.dll) outside the game: a real ImGui
// context, a stand-in Nexus API, a few frames drawn, a chat line raised the way "Events: Chat" does, then unloaded.
// Catches crashes before they could happen inside GW2. Usage: nexus_kb_selftest.exe <path to gw2keyboard_nexus.dll>

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "../../addons/nexus/src/chat_events.hpp"
#include "imgui/imgui.h"
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
};
void Free(void* p, void*) { std::free(p); }

void Frame() {
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1920, 1080);
    io.DeltaTime = 1.0f / 60.0f;
    ImGui::NewFrame();
    for (GUI_RENDER r : g_renders)
        if (r) r();
    ImGui::Render();
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
    unsigned char* pixels = nullptr;
    int w = 0, h = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);

    {  // no first start: nothing is downloaded during the test
        std::wstring dir = argv[1];
        dir = dir.substr(0, dir.find_last_of(L"\\/") + 1) + L"GW2Keyboard";
        CreateDirectoryW(dir.c_str(), nullptr);
        WritePrivateProfileStringW(L"Nexus", L"SetupDone", L"1", (dir + L"\\gw2-chat-translator.ini").c_str());
    }
    HMODULE dll = LoadLibraryW(argv[1]);
    auto getDef = dll ? reinterpret_cast<AddonDefinition_t* (*)()>(GetProcAddress(dll, "GetAddonDef")) : nullptr;
    AddonDefinition_t* def = getDef ? getDef() : nullptr;
    if (!def || def->APIVersion != NEXUS_API_VERSION) {
        std::printf("FAIL: no valid GetAddonDef\n");
        return 1;
    }
    std::printf("add-on: %s %u.%u.%u\n", def->Name, def->Version.Major, def->Version.Minor, def->Version.Build);
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
        // Learning mode: a word you retype yourself after its space ("Gildne " -> "Gilden ") is noticed.
        type("wir Gildne ");
        frames(40);
        for (int i = 0; i < 7; ++i) press(VK_BACK);
        frames(40);
        type("Gilden ");
        frames(10);
        std::printf("self-correction: %d noticed, newest %s (input: %s)\n", th->selfFixes, th->lastFix, th->input);
        ok = ok && th->selfFixes >= 1;
    }
    std::printf("unload\n");
    def->Unload();
    ok = ok && g_chatEvents == nullptr;
    FreeLibrary(dll);
    ImGui::DestroyContext(ctx);
    std::printf(ok ? "PASS\n" : "FAIL\n");
    return ok ? 0 : 1;
}
