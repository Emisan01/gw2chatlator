// bridge_main.cpp — "GW2 Chat Bridge", the optional Nexus add-on (see docs/NEXUS_PLAN.md).
//
// Listens to the party/squad chat that Unofficial Extras reports (relayed by Raidcore's "ArcDPS Integration" as the
// Nexus event EV_UNOFFICIAL_EXTRAS_CHAT_MESSAGE) and writes each line, as exact text, into the named pipe of GW2 Chat
// Translator / GW2 Keyboard. That is all it does:
//   - it only reads: no key, no chat line, nothing is ever sent to the game;
//   - it only writes into the pipe: it never reads from it, so nothing from outside can steer it;
//   - the game thread only copies the message into a small queue; the pipe is written by our own thread.
// This add-on lives in the game process – it is a separate, optional download. The programs themselves stay outside
// the game (CLAUDE.md invariant 1).

#include <windows.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

#include "core/bridge_protocol.hpp"
#include "nexus/Nexus.h"
#include "unofficial_extras/Definitions.h"
#include "version.h"

namespace {

constexpr const char* kAddonName = "GW2 Chat Bridge";
constexpr size_t kQueueConnected = 256;    // lines waiting while the program is busy
constexpr size_t kQueueDisconnected = 20;  // without a program only the newest few are kept for its start

// Raidcore relays the V2 callback's ChatMessageInfo; newer Definitions.h call the same struct SquadMessageInfo.
static_assert(sizeof(void*) != 8 || sizeof(SquadMessageInfo) == 72, "Unofficial Extras chat struct changed");

AddonDefinition_t g_def = {};
AddonAPI_t* g_api = nullptr;

std::mutex g_mutex;
std::condition_variable g_wake;
std::deque<std::string> g_queue;
std::atomic<bool> g_stop{false};
std::atomic<bool> g_connected{false};
std::thread g_writer;

void Log(ELogLevel level, const char* text) {
    if (g_api && g_api->Log) g_api->Log(level, kAddonName, text);
}

std::string Str(const char* s, uint64_t n) { return s && n ? std::string(s, static_cast<size_t>(n)) : std::string(); }

std::string VersionString() {
    return std::to_string(g_def.Version.Major) + "." + std::to_string(g_def.Version.Minor) + "." +
           std::to_string(g_def.Version.Build);
}

void Enqueue(std::string line) {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_queue.push_back(std::move(line));
        const size_t cap = g_connected ? kQueueConnected : kQueueDisconnected;
        while (g_queue.size() > cap) g_queue.pop_front();
    }
    g_wake.notify_one();
}

// Game thread: copy, nothing else (the strings are only valid during the call).
void OnChatMessage(void* args) {
    const auto* m = static_cast<const SquadMessageInfo*>(args);
    if (!m || !m->Text || m->TextLength == 0) return;
    try {
        gct::BridgeMessage b;
        b.channel = m->Type == ChannelType::Squad ? "squad" : "party";
        b.subgroup = m->Subgroup == UINT8_MAX ? -1 : m->Subgroup;
        b.broadcast = (m->IsBroadcast & 1) != 0;
        b.time = Str(m->Timestamp, m->TimestampLength);
        b.account = Str(m->AccountName, m->AccountNameLength);
        b.character = Str(m->CharacterName, m->CharacterNameLength);
        b.text = Str(m->Text, m->TextLength);
        Enqueue(gct::EncodeBridgeMessage(b));
    } catch (...) {
        // never let anything escape into the game
    }
}

bool WriteLine(HANDLE pipe, const std::string& line) {
    DWORD written = 0;
    return WriteFile(pipe, line.data(), static_cast<DWORD>(line.size()), &written, nullptr) &&
           written == line.size();
}

// Our thread: connect to the program's pipe (it may start later or not at all), then write what arrives.
void WriterLoop() {
    HANDLE pipe = INVALID_HANDLE_VALUE;
    while (!g_stop) {
        if (pipe == INVALID_HANDLE_VALUE) {
            // SECURITY_IDENTIFICATION: whoever owns the pipe cannot act as us.
            pipe = CreateFileW(gct::kBridgePipeName, GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                               SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
            if (pipe == INVALID_HANDLE_VALUE) {
                std::unique_lock<std::mutex> lock(g_mutex);
                g_wake.wait_for(lock, std::chrono::seconds(2), [] { return g_stop.load(); });
                continue;
            }
            gct::BridgeMessage hello;
            hello.kind = gct::BridgeMessage::Kind::Hello;
            hello.addonVersion = VersionString();
            if (!WriteLine(pipe, gct::EncodeBridgeMessage(hello))) {
                CloseHandle(pipe);
                pipe = INVALID_HANDLE_VALUE;
                continue;
            }
            g_connected = true;
            Log(LOGL_INFO, "connected to GW2 Chat Translator / GW2 Keyboard");
        }
        std::string line;
        {
            std::unique_lock<std::mutex> lock(g_mutex);
            g_wake.wait_for(lock, std::chrono::seconds(5), [] { return g_stop.load() || !g_queue.empty(); });
            if (g_stop) break;
            if (g_queue.empty()) continue;
            line = std::move(g_queue.front());
            g_queue.pop_front();
        }
        if (!WriteLine(pipe, line)) {  // program closed: the line is lost, try again later
            CloseHandle(pipe);
            pipe = INVALID_HANDLE_VALUE;
            g_connected = false;
            Log(LOGL_INFO, "disconnected");
        }
    }
    if (pipe != INVALID_HANDLE_VALUE) CloseHandle(pipe);
    g_connected = false;
}

void AddonLoad(AddonAPI_t* api) {
    g_api = api;
    g_stop = false;
    g_writer = std::thread(WriterLoop);
    if (g_api->Events_Subscribe) g_api->Events_Subscribe("EV_UNOFFICIAL_EXTRAS_CHAT_MESSAGE", OnChatMessage);
    Log(LOGL_INFO, "loaded – party/squad chat needs arcdps + Unofficial Extras + the ArcDPS Integration add-on");
}

void AddonUnload() {
    if (g_api && g_api->Events_Unsubscribe) g_api->Events_Unsubscribe("EV_UNOFFICIAL_EXTRAS_CHAT_MESSAGE", OnChatMessage);
    g_stop = true;
    g_wake.notify_all();
    if (g_writer.joinable()) {
        CancelSynchronousIo(g_writer.native_handle());  // a write blocked on a full pipe
        g_writer.join();
    }
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_queue.clear();
    }
    g_api = nullptr;
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) { return TRUE; }

extern "C" __declspec(dllexport) AddonDefinition_t* GetAddonDef() {
    g_def.Signature = static_cast<uint32_t>(-0x47324342);  // random negative: not hosted on Raidcore
    g_def.APIVersion = NEXUS_API_VERSION;
    g_def.Name = kAddonName;
    g_def.Version.Major = GCT_VER_MAJOR;
    g_def.Version.Minor = GCT_VER_MINOR;
    g_def.Version.Build = GCT_VER_PATCH;
    g_def.Version.Revision = 0;
    g_def.Author = "Emisan01";
    g_def.Description =
        "Hands party and squad chat (from Unofficial Extras) to GW2 Chat Translator / GW2 Keyboard as exact text, so "
        "they can translate it. Reads only - it never sends anything to the game.";
    g_def.Load = AddonLoad;
    g_def.Unload = AddonUnload;
    g_def.Flags = AF_None;
    // No Nexus auto-update yet: the repo's releases are the programs' (Nexus would compare their tags with us).
    g_def.Provider = UP_None;
    return &g_def;
}
