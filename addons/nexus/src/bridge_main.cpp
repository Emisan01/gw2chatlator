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
#include <shellapi.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

#include "core/bridge_protocol.hpp"
#include "chat_events.hpp"
#include "extras_setup.hpp"
#include "win/extras_install.hpp"
#include "imgui/imgui.h"
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
HMODULE g_self = nullptr;
std::atomic<uint64_t> g_forwarded{0};  // lines written into the pipe (options page)
std::atomic<bool> g_passParty{true}, g_passSquad{true}, g_passOthers{true};  // the options page switches
// "Events: Chat" delivers (it reports party/squad too): the Unofficial Extras copies are then left out.
std::atomic<bool> g_chatEventsLive{false};

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
    if (g_chatEventsLive) return;  // the same line comes from "Events: Chat"
    const bool squad = m->Type == ChannelType::Squad;
    if (!(squad ? g_passSquad.load() : g_passParty.load())) return;  // switched off on the options page
    try {
        gct::BridgeMessage b;
        b.channel = squad ? "squad" : "party";
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

// Game thread: a line of any channel from "Events: Chat" – copy, nothing else.
void OnChatEvents(void* args) {
    const auto* m = static_cast<const bridge::ChatEventsMessage*>(args);
    if (!m) return;
    g_chatEventsLive = true;
    using T = bridge::ChatEventsType;
    const char* channel = nullptr;
    switch (m->type) {
        case T::Party: channel = "party"; break;
        case T::Squad:
        case T::SquadMessage:
        case T::SquadBroadcast: channel = "squad"; break;
        case T::Map: channel = "map"; break;
        case T::Local: channel = "say"; break;
        case T::Whisper: channel = "whisper"; break;
        case T::Guild: channel = "guild"; break;
        case T::TeamPvP:
        case T::TeamWvW: channel = "team"; break;
        default: return;  // errors, message of the day, emotes
    }
    const bool isParty = m->type == T::Party;
    const bool isSquad = m->type == T::Squad || m->type == T::SquadMessage || m->type == T::SquadBroadcast;
    if (isParty ? !g_passParty.load() : isSquad ? !g_passSquad.load() : !g_passOthers.load()) return;  // switched off
    try {
        gct::BridgeMessage b;
        b.src = "chat";
        b.channel = channel;
        if (m->type == T::SquadMessage || m->type == T::SquadBroadcast) {
            if (!m->squadMessage) return;
            b.text = m->squadMessage;
            b.broadcast = m->type == T::SquadBroadcast || (m->flags & bridge::kChatEventsBroadcast) != 0;
        } else {
            if (!m->generic.content) return;
            b.text = m->generic.content;
            if (m->generic.characterName) b.character = m->generic.characterName;
            if (m->generic.accountName) b.account = m->generic.accountName;
            b.outgoing = m->type == T::Whisper && (m->flags & bridge::kChatEventsWhisperFromMe) != 0;
        }
        if (b.text.empty()) return;
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
            const bridge::Presence present = bridge::DetectPresence(g_self);
            hello.extras = present.extras;
            hello.chatEvents = present.chatEvents || g_chatEventsLive;
            if (present.chatEvents) g_chatEventsLive = true;
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
        if (WriteLine(pipe, line)) {
            ++g_forwarded;
        } else {  // program closed: the line is lost, try again later
            CloseHandle(pipe);
            pipe = INVALID_HANDLE_VALUE;
            g_connected = false;
            Log(LOGL_INFO, "disconnected");
        }
    }
    if (pipe != INVALID_HANDLE_VALUE) CloseHandle(pipe);
    g_connected = false;
}

// Nexus → Addons → GW2 Chat Bridge: what is there, what is missing, and Unofficial Extras on a click.
void OptionsRender() {
    static ULONGLONG checked = 0;
    static bridge::Presence p;
    if (GetTickCount64() - checked > 1000) {  // the module list once a second while this page is open
        p = bridge::DetectPresence(g_self);
        checked = GetTickCount64();
    }
    const ImVec4 good(0.45f, 0.85f, 0.45f, 1.f), bad(0.95f, 0.55f, 0.35f, 1.f);
    auto row = [&](const char* what, bool ok, const char* state) {
        ImGui::TextUnformatted(what);
        ImGui::SameLine(260);
        ImGui::TextColored(ok ? good : bad, "%s", state);
    };
    ImGui::TextWrapped("Hands the chat to GW2 Keyboard / GW2 Chat Translator as exact text, so they can translate it: "
                       "every channel from Events: Chat (Vonsh), or party and squad from Unofficial Extras. It only "
                       "reads - it never sends anything to the game.");
    ImGui::Separator();
    row("Events: Chat (all channels)", p.chatEvents,
        p.chatEvents ? "loaded" : p.chatEventsFile ? "installed - restart the game" : "missing");
    row("ArcDPS", p.arcdps, p.arcdps ? "found" : "missing");
    row("Unofficial Extras", p.extras,
        p.extras ? "loaded" : p.extrasFile ? "installed - restart the game" : "missing");
    row("ArcDPS Integration (Nexus)", p.relay, p.relay ? "found" : "missing");
    row("GW2 Keyboard / Chat Translator", g_connected, g_connected ? "connected" : "not running");
    ImGui::Text("Lines passed on: %llu", static_cast<unsigned long long>(g_forwarded.load()));
    bool party = g_passParty, squad = g_passSquad, others = g_passOthers;
    const bool changed = ImGui::Checkbox("Pass on party chat", &party) | ImGui::Checkbox("Pass on squad chat", &squad) |
                         ImGui::Checkbox("Pass on every other channel (Events: Chat)", &others);
    if (changed) {
        g_passParty = party;
        g_passSquad = squad;
        g_passOthers = others;
        bridge::SaveSettings(g_self, {party, squad, others});
    }

    std::string msg;
    const bridge::InstallState st = bridge::GetInstallState(&msg);
    if (!p.chatEvents && !p.chatEventsFile && st != bridge::InstallState::Done && st != bridge::InstallState::Running) {
        ImGui::Separator();
        ImGui::TextWrapped("Every channel (map, whispers, guild ...) comes from Events: Chat by Vonsh, an add-on in the "
                           "Nexus library: Nexus -> Addons -> Library -> \"Events: Chat\" - or install it here the same "
                           "way (its newest GitHub release, checksum checked).");
        if (ImGui::Button("Install Events: Chat")) bridge::StartChatEventsInstall(g_self);
        ImGui::SameLine();
        if (ImGui::Button("Open its GitHub page##chat"))
            ShellExecuteW(nullptr, L"open", gct::kChatEventsPage, nullptr, nullptr, SW_SHOWNORMAL);
    }
    if (!p.chatEvents && !p.extras && !p.extrasFile && st != bridge::InstallState::Done) {
        ImGui::Separator();
        if (!p.arcdps) {
            ImGui::TextWrapped("Party and squad chat need ArcDPS first: Nexus -> Addons -> Library -> ArcDPS.");
        } else {
            ImGui::TextWrapped("Party and squad chat come from Unofficial Extras, a free ArcDPS extension by Krappa322. "
                               "It can be installed here like the Nexus library installs add-ons: the newest release "
                               "from its GitHub page, checksum checked, into your addons folder.");
            if (st != bridge::InstallState::Running && ImGui::Button("Install Unofficial Extras"))
                bridge::StartExtrasInstall(g_self);
            ImGui::SameLine();
            if (ImGui::Button("Open its GitHub page"))
                ShellExecuteW(nullptr, L"open", gct::kExtrasPage, nullptr, nullptr, SW_SHOWNORMAL);
        }
    }
    if (!msg.empty()) ImGui::TextWrapped("%s", msg.c_str());
}

void AddonLoad(AddonAPI_t* api) {
    g_api = api;
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(api->ImguiContext));
    ImGui::SetAllocatorFunctions(reinterpret_cast<void* (*)(size_t, void*)>(api->ImguiMalloc),
                                 reinterpret_cast<void (*)(void*, void*)>(api->ImguiFree));
    if (api->GUI_Register) api->GUI_Register(RT_OptionsRender, OptionsRender);
    const bridge::Settings s = bridge::LoadSettings(g_self);
    g_passParty = s.party;
    g_passSquad = s.squad;
    g_passOthers = s.others;
    g_chatEventsLive = false;
    g_stop = false;
    g_writer = std::thread(WriterLoop);
    if (g_api->Events_Subscribe) {
        g_api->Events_Subscribe("EV_UNOFFICIAL_EXTRAS_CHAT_MESSAGE", OnChatMessage);
        g_api->Events_Subscribe(bridge::kChatEventsEvent, OnChatEvents);
    }
    Log(LOGL_INFO, "loaded – party/squad chat needs arcdps + Unofficial Extras + the ArcDPS Integration add-on");
}

void AddonUnload() {
    if (g_api && g_api->GUI_Deregister) g_api->GUI_Deregister(OptionsRender);
    bridge::StopExtrasInstall();
    if (g_api && g_api->Events_Unsubscribe) {
        g_api->Events_Unsubscribe("EV_UNOFFICIAL_EXTRAS_CHAT_MESSAGE", OnChatMessage);
        g_api->Events_Unsubscribe(bridge::kChatEventsEvent, OnChatEvents);
    }
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

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) g_self = module;
    return TRUE;
}

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
