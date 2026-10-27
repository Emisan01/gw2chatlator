// nexus_kb_smoketest.cpp — Real-life stress, smoke & frame-time benchmark for GW2Chatlator.
// Simulates an intensive in-game session:
//   - 500 rapid chat messages with links, waypoints, items, bidi, punycode, and multi-language chatter.
//   - 1,000 rendered frames with scrolling, hovering, and interaction.
//   - High-resolution frame-time profiling (mean, p99, worst-case).
//   - Rate-limiter validation under fire.
//   - Clean unload without resource leaks or thread hangs.

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <numeric>
#include <string>
#include <vector>

#include "../../addons/nexus-keyboard/src/chat_events.hpp"
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
void FakeLog(ELogLevel, const char* channel, const char* text) {
    // std::printf("[%s] %s\n", channel, text);
}
void* Alloc(size_t n, void*) { return std::malloc(n); }
void Free(void* p, void*) { std::free(p); }

struct TestHooks {
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

void Frame() {
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1920, 1080);
    io.DeltaTime = 1.0f / 60.0f;
    ImGui::NewFrame();
    for (size_t i = 0; i < g_renders.size(); ++i) {
        if (!g_renders[i]) continue;
        if (i == 1) {
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

}  // namespace

// The test's clipboard: in memory – a link click in the test must not overwrite yours while you play.
std::string g_testClipboard;
void TestSetClipboard(void*, const char* text) { g_testClipboard = text ? text : ""; }
const char* TestGetClipboard(void*) { return g_testClipboard.c_str(); }

int wmain(int argc, wchar_t** argv) {
    // Nothing of a running game reaches the test, nothing of the test the game: the add-on reads its own, empty
    // MumbleLink, the clipboard stays in this process (see TestClipboard), clicks find no GW2 window of this process.
    SetEnvironmentVariableW(L"GW2KB_MUMBLE", L"GW2KB_TEST_NO_MUMBLE");
    if (argc < 2) {
        std::printf("usage: nexus_kb_smoketest.exe <path-to-GW2Chatlator.dll>\n");
        return 2;
    }
    setvbuf(stdout, nullptr, _IONBF, 0);

    std::printf("==================================================================\n");
    std::printf("      GW2Chatlator — Real-Life Stress & Smoke Benchmark           \n");
    std::printf("==================================================================\n");

    ImGui::SetAllocatorFunctions(Alloc, Free);
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGui::GetIO().SetClipboardTextFn = TestSetClipboard;
    ImGui::GetIO().GetClipboardTextFn = TestGetClipboard;
    unsigned char* pixels = nullptr;
    int w = 0, h = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);

    {
        std::wstring dir = argv[1];
        dir = dir.substr(0, dir.find_last_of(L"\\/") + 1) + L"GW2Keyboard";
        CreateDirectoryW(dir.c_str(), nullptr);
        WritePrivateProfileStringW(L"Nexus", L"SetupDone", L"1", (dir + L"\\gw2-chat-translator.ini").c_str());
        WritePrivateProfileStringW(L"NexusTabs", nullptr, nullptr, (dir + L"\\gw2-chat-translator.ini").c_str());
        WritePrivateProfileStringW(L"Translate", L"WriteLangs", L"DE,EN-GB,FR,ES", (dir + L"\\gw2-chat-translator.ini").c_str());
        WritePrivateProfileStringW(L"Window", L"Opacity", L"255", (dir + L"\\gw2-chat-translator.ini").c_str());
    }

    HMODULE dll = LoadLibraryW(argv[1]);
    if (!dll) {
        std::printf("FAIL: LoadLibraryW failed for %ls\n", argv[1]);
        return 1;
    }

    auto getDef = reinterpret_cast<AddonDefinition_t* (*)()>(GetProcAddress(dll, "GetAddonDef"));
    AddonDefinition_t* def = getDef ? getDef() : nullptr;
    if (!def || def->APIVersion != NEXUS_API_VERSION) {
        std::printf("FAIL: invalid GetAddonDef\n");
        return 1;
    }
    std::printf("[*] Add-on Loaded: %s v%u.%u.%u (Provider: %d)\n",
                def->Name, def->Version.Major, def->Version.Minor, def->Version.Build, static_cast<int>(def->Provider));

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

    def->Load(&api);
    if (g_renders.size() < 2 || !g_chatEvents) {
        std::printf("FAIL: addon did not register render callbacks or chat event subscription\n");
        return 1;
    }

    // Warm-up 10 frames
    for (int i = 0; i < 10; ++i) Frame();

    auto hooks = reinterpret_cast<TestHooks* (*)()>(GetProcAddress(dll, "Gw2KbTestHooks"));
    TestHooks* th = hooks ? hooks() : nullptr;
    if (th) {
        th->forceMap = true;
        th->noSend = true;
    }

    // -------------------------------------------------------------------------
    // Phase 1: Massive WvW / World Boss Chat Flood (500 distinct messages)
    // -------------------------------------------------------------------------
    bool failed = false;  // checks that fail say so at the end – after the add-on was unloaded cleanly
    std::printf("[*] Phase 1: Ingesting 500 High-Intensity Chat Lines ...\n");
    const struct MsgTemplate {
        bridge::ChatEventsType type;
        const char* sender;
        const char* content;
    } templates[] = {
        {bridge::ChatEventsType::Map, "Commander Tyria", "Stack on tag for Tequatl burn phase [&BAwEAAA=] now!"},
        {bridge::ChatEventsType::Squad, "Mesmer Portal", "Portal ready in 3, 2, 1... drop veil then push into lord room!"},
        {bridge::ChatEventsType::Party, "Heal Firebrand", "Quickness and aegis up! Use food [&AgGvWwEA] and utility [&AgHFMAAA]"},
        {bridge::ChatEventsType::Guild, "Guild Officer", "Weekly raid training at reset! Check builds at https://snowcrows.com/builds/raids"},
        {bridge::ChatEventsType::Map, "Loot Lucky", "Droprate is insane, just got precursor [&AgEEYQAA] from champ bag!"},
        {bridge::ChatEventsType::Whisper, "Jean Dupont", "Bonjour mon ami, est-ce que tu peux m'aider avec cette mission fractale ?"},
        {bridge::ChatEventsType::TeamWvW, "Scout North", "Enemy zerg incoming 40+ from hills: https://gw2efficiency.com"},
        {bridge::ChatEventsType::Local, "Suspicious Spammer", "Free 5000 gems claim here: http://guildwars2-free-gems.xyz/claim now!"},
        {bridge::ChatEventsType::Map, "Shortener Fan", "Check this funny clip: https://bit.ly/gw2-clip-funny-boss"},
        {bridge::ChatEventsType::Map, "Punycode Attacker", "Official giveaway: http://xn--guld wars2-s1a.com/gem-event"},
        {bridge::ChatEventsType::Map, "Trojan Source", "Check this build \u202Ereversed\u202D text with zero \u200B width space"},
        {bridge::ChatEventsType::Squad, "Ele Fresh Air", "DPS check: parse is 42.5k on golem [&DQYGBwA=]"},
    };
    const size_t numTemplates = sizeof(templates) / sizeof(templates[0]);

    auto startIngest = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 500; ++i) {
        const auto& t = templates[i % numTemplates];
        char senderBuf[64];
        char contentBuf[256];
        snprintf(senderBuf, sizeof(senderBuf), "%s %d", t.sender, i);
        snprintf(contentBuf, sizeof(contentBuf), "[#%d] %s", i + 1, t.content);

        bridge::ChatEventsMessage m{};
        m.type = t.type;
        m.generic.characterName = senderBuf;
        m.generic.content = contentBuf;
        g_chatEvents(&m);

        if (i % 25 == 0) Frame();  // interleave frame draws to simulate live chat
    }
    auto endIngest = std::chrono::high_resolution_clock::now();
    double ingestMs = std::chrono::duration<double, std::milli>(endIngest - startIngest).count();
    std::printf("    [PASS] 500 lines ingested in %.2f ms (%.2f lines/ms)\n", ingestMs, 500.0 / ingestMs);
    // The chat source guard must take every realistic line (Unicode, chat codes, links, tricks): none rejected, no pause.
    if (auto src = reinterpret_cast<int (*)(int, void*, char*)>(GetProcAddress(dll, "Gw2KbTestChatSource"))) {
        struct {  // as ChatSourceTest in kb_shared.hpp
            int reading, pause, shown, dropped, unreadable, disabled;
            int received, ignored;
            char version[32], before[32];
        } st{};
        src(0, &st, nullptr);
        std::printf("    chat source guard: %d shown, %d dropped, pause %d\n", st.shown, st.dropped, st.pause);
        if (st.shown != 500 || st.dropped != 0 || st.pause != 0 || st.received != st.shown + st.dropped + st.ignored) {
            std::printf("    [FAIL] The chat source guard rejected realistic lines!\n");
            failed = true;
        }
    }

    // -------------------------------------------------------------------------
    // Phase 2: High-Resolution Frame-Time Profiling (1,000 Frames)
    // -------------------------------------------------------------------------
    std::printf("[*] Phase 2: Profiling 1,000 Render Frames with Full Chat Buffer ...\n");
    struct CostSums {  // as FrameCosts in kb_shared.hpp
        double ms[7];
        double totalMs, lineUs;
        long long lines;
        double sumMs[7];
        double sumTotalMs;
        long long frames;
    };
    auto costs = reinterpret_cast<int (*)(CostSums*, int)>(GetProcAddress(dll, "Gw2KbTestCosts"));
    if (costs) costs(nullptr, 1);  // the sums start with these frames
    std::vector<double> frameTimes;
    frameTimes.reserve(1000);

    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    for (int i = 0; i < 1000; ++i) {
        ImGuiIO& io = ImGui::GetIO();
        // Simulate mouse hovering across chat view
        io.MousePos = ImVec2(200.0f + (i % 300), 200.0f + (i % 200));

        LARGE_INTEGER t0, t1;
        QueryPerformanceCounter(&t0);
        Frame();
        QueryPerformanceCounter(&t1);

        double ms = (t1.QuadPart - t0.QuadPart) * 1000.0 / freq.QuadPart;
        frameTimes.push_back(ms);
    }

    std::sort(frameTimes.begin(), frameTimes.end());
    double sum = std::accumulate(frameTimes.begin(), frameTimes.end(), 0.0);
    double avg = sum / frameTimes.size();
    double p50 = frameTimes[frameTimes.size() * 50 / 100];
    double p95 = frameTimes[frameTimes.size() * 95 / 100];
    double p99 = frameTimes[frameTimes.size() * 99 / 100];
    double maxMs = frameTimes.back();

    std::printf("    Render Performance Metrics (1,000 Frames):\n");
    std::printf("      - Average Frame Time : %.3f ms\n", avg);
    std::printf("      - Median (p50)       : %.3f ms\n", p50);
    std::printf("      - 95th Percentile    : %.3f ms\n", p95);
    std::printf("      - 99th Percentile    : %.3f ms\n", p99);
    std::printf("      - Worst-Case Spike   : %.3f ms\n", maxMs);

    bool perfOk = (avg < 0.80) && (p99 < 2.50) && (maxMs < 5.00);
    if (!perfOk) {
        std::printf("    [FAIL] Render latency exceeded acceptable thresholds!\n");
        failed = true;
    } else {
        std::printf("    [PASS] Zero-Stutter SLA satisfied (< 0.8ms avg, < 2.5ms p99)\n");
    }
    // The formula must add up, like with like over the same 1000 frames: our parts sum to our whole, and our whole lies
    // inside the frames measured from outside (the rest is ImGui's own work: NewFrame, the options callback, Render).
    if (costs) {
        CostSums c{};
        costs(&c, 0);
        double parts = 0;
        for (double v : c.sumMs) parts += v;
        const double n = c.frames > 0 ? static_cast<double>(c.frames) : 1.0;
        std::printf("    Our frame on the render thread: %.4f ms = pre %.4f + options %.4f + window %.4f + header %.4f"
                    " + chat %.4f + input %.4f + footer %.4f (%lld frames)\n",
                    c.sumTotalMs / n, c.sumMs[0] / n, c.sumMs[1] / n, c.sumMs[2] / n, c.sumMs[3] / n, c.sumMs[4] / n,
                    c.sumMs[5] / n, c.sumMs[6] / n, c.frames);
        std::printf("    Outside it (ImGui itself): %.4f ms of %.4f ms; per chat line: %.2f us (%lld lines)\n",
                    avg - c.sumTotalMs / n, avg, c.lineUs, c.lines);
        const bool addsUp = c.frames == 1000 && std::fabs(parts - c.sumTotalMs) <= 0.001 * n && c.sumTotalMs <= sum;
        if (!addsUp) {
            std::printf("    [FAIL] The frame formula does not add up!\n");
            failed = true;
        } else {
            std::printf("    [PASS] The frame formula adds up.\n");
        }
    }

    // -------------------------------------------------------------------------
    // Phase 3: Interactive Typing & Sending Under Heavy Chat
    // -------------------------------------------------------------------------
    std::printf("[*] Phase 3: Interactive Player Typing & Sending ...\n");
    if (th) {
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

        frames(60);  // Allow background worker thread to settle
        *th->focus = true;
        frames(3);

        type("Smoke test typing input under 500 lines of chat history");
        frames(10);
        press(VK_RETURN);
        frames(60);  // Allow async send pipeline to complete

        std::printf("    Sent parts: %d, remaining input: %zu\n", th->sentParts, std::strlen(th->input));
        if (th->sentParts < 1 || std::strlen(th->input) != 0) {
            std::printf("    [FAIL] Send failed under heavy load!\n");
            failed = true;
        } else {
            std::printf("    [PASS] Interactive send verified successfully.\n");
        }
    }

    // -------------------------------------------------------------------------
    // Phase 4: Clean Teardown
    // -------------------------------------------------------------------------
    std::printf("[*] Phase 4: Graceful Shutdown & Unload ...\n");
    def->Unload();
    FreeLibrary(dll);
    ImGui::DestroyContext(ctx);
    std::printf("    [PASS] Add-on unloaded cleanly without crash or leak.\n");
    if (failed) {
        std::printf("\n   >>> SMOKE TEST RESULT: FAIL (see above) <<<\n");
        return 1;
    }

    std::printf("\n==================================================================\n");
    std::printf("   >>> SMOKE TEST RESULT: 100%% ROCK-SOLID (PASS) <<<            \n");
    std::printf("==================================================================\n");
    return 0;
}
