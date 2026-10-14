// bridge_selftest.cpp — the whole Nexus bridge chain without the game: loads gw2chat_bridge.dll with a stand-in
// Nexus API, raises an invented squad message the way Raidcore's relay does, and receives it through the real
// BridgePipe (our programs' side). Prints PASS / FAIL. Usage: bridge_selftest.exe <path to gw2chat_bridge.dll>

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "nexus/Nexus.h"
#include "unofficial_extras/Definitions.h"
#include "win/bridge_pipe.hpp"

using namespace gct;

namespace {

EVENT_CONSUME g_chat = nullptr;

void FakeSubscribe(const char* id, EVENT_CONSUME cb) {
    if (std::strcmp(id, "EV_UNOFFICIAL_EXTRAS_CHAT_MESSAGE") == 0) g_chat = cb;
}
void FakeUnsubscribe(const char* id, EVENT_CONSUME) {
    if (std::strcmp(id, "EV_UNOFFICIAL_EXTRAS_CHAT_MESSAGE") == 0) g_chat = nullptr;
}
void FakeLog(ELogLevel, const char* channel, const char* text) { std::printf("[%s] %s\n", channel, text); }

struct Received {
    bool connected = false, hello = false, chat = false;
    BridgeMessage last;
} g_rx;

LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_APP) {
        if (BridgePipe::KindOf(wp) == BridgePipe::kConnected) g_rx.connected = true;
        auto* m = reinterpret_cast<BridgeMessage*>(lp);
        if (m) {
            if (m->kind == BridgeMessage::Kind::Hello) g_rx.hello = true;
            else {
                g_rx.chat = true;
                g_rx.last = *m;
            }
            delete m;
        }
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// Pumps messages until `done` or the time is up.
template <class F>
bool PumpUntil(F done, DWORD ms) {
    const ULONGLONG end = GetTickCount64() + ms;
    while (!done() && GetTickCount64() < end) {
        MSG m;
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&m);
        Sleep(20);
    }
    return done();
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        std::printf("usage: bridge_selftest <gw2chat_bridge.dll>\n");
        return 2;
    }
    WNDCLASSW wc{};
    wc.lpfnWndProc = Proc;
    wc.lpszClassName = L"BridgeSelfTest";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
    BridgePipe pipe;
    pipe.Start(hwnd, WM_APP);

    HMODULE dll = LoadLibraryW(argv[1]);
    auto getDef = dll ? reinterpret_cast<AddonDefinition_t* (*)()>(GetProcAddress(dll, "GetAddonDef")) : nullptr;
    AddonDefinition_t* def = getDef ? getDef() : nullptr;
    if (!def || def->APIVersion != NEXUS_API_VERSION) {
        std::printf("FAIL: no valid GetAddonDef\n");
        return 1;
    }
    std::printf("add-on: %s %u.%u.%u\n", def->Name, def->Version.Major, def->Version.Minor, def->Version.Build);
    AddonAPI_t api{};
    api.Events_Subscribe = FakeSubscribe;
    api.Events_Unsubscribe = FakeUnsubscribe;
    api.Log = FakeLog;
    def->Load(&api);
    bool ok = g_chat != nullptr;
    ok = ok && PumpUntil([] { return g_rx.connected && g_rx.hello; }, 5000);  // it retries every 2 s

    // Invented line, exactly as Unofficial Extras hands it over (strings only valid during the call).
    std::string text = "Sammeln am Kommandeur \xE2\x80\x93 jetzt!";
    SquadMessageInfo info{};
    info.ChannelId = 7;
    info.Type = ChannelType::Squad;
    info.Subgroup = UINT8_MAX;
    info.IsBroadcast = 1;
    info.Timestamp = "2026-10-14T11:45:24.888Z";
    info.TimestampLength = std::strlen(info.Timestamp);
    info.AccountName = ":Invented.1234";
    info.AccountNameLength = std::strlen(info.AccountName);
    info.CharacterName = "Nora Testwind";
    info.CharacterNameLength = std::strlen(info.CharacterName);
    info.Text = text.c_str();
    info.TextLength = text.size();
    if (ok) g_chat(&info);
    text.assign(text.size(), 'x');  // the add-on must have copied it already
    ok = ok && PumpUntil([] { return g_rx.chat; }, 3000);
    ok = ok && g_rx.last.channel == "squad" && g_rx.last.subgroup == -1 && g_rx.last.broadcast &&
         g_rx.last.character == "Nora Testwind" && g_rx.last.account == ":Invented.1234" &&
         g_rx.last.text == "Sammeln am Kommandeur \xE2\x80\x93 jetzt!";

    def->Unload();
    ok = ok && g_chat == nullptr;
    FreeLibrary(dll);
    pipe.Stop();
    std::printf(ok ? "PASS\n" : "FAIL (connected %d, hello %d, chat %d)\n", g_rx.connected, g_rx.hello, g_rx.chat);
    return ok ? 0 : 1;
}
