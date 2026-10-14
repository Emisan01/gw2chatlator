// nexus_kb_selftest.cpp — loads GW2 Keyboard for Nexus (gw2keyboard_nexus.dll) outside the game: a real ImGui
// context, a stand-in Nexus API, a few frames drawn, a chat line raised the way "Events: Chat" does, then unloaded.
// Catches crashes before they could happen inside GW2. Usage: nexus_kb_selftest.exe <path to gw2keyboard_nexus.dll>

#include <windows.h>

#include <cstdio>
#include <cstdlib>
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
    std::printf("unload\n");
    def->Unload();
    ok = ok && g_chatEvents == nullptr;
    FreeLibrary(dll);
    ImGui::DestroyContext(ctx);
    std::printf(ok ? "PASS\n" : "FAIL\n");
    return ok ? 0 : 1;
}
