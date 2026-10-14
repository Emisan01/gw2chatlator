// extras_install.hpp — installs arcdps "Unofficial Extras" (Krappa322) the way the Nexus library installs add-ons:
// the newest GitHub release, only arcdps_unofficial_extras.dll from that repository, checked against the SHA-256
// GitHub reports, into <GW2>\addons (Nexus loads it and hands it to ArcDPS). Only ever on the user's click.
// Used by GW2 Keyboard (Settings → General) and by the Nexus add-on GW2 Chat Bridge (its options page).
#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace gct {

constexpr const wchar_t* kExtrasDllName = L"arcdps_unofficial_extras.dll";
constexpr const wchar_t* kExtrasPage = L"https://github.com/Krappa322/arcdps_unofficial_extras_releases";
// Vonsh's "Events: Chat" (in the Nexus library): every chat channel as a Nexus event.
constexpr const wchar_t* kChatEventsDllName = L"Events_Chat.dll";
constexpr const wchar_t* kChatEventsPage = L"https://github.com/jsantorek/GW2-Chat";

enum class ExtrasInstallCode { Ok, NoAnswer, NotChecked, DownloadFailed };

struct ExtrasInstallResult {
    ExtrasInstallCode code = ExtrasInstallCode::NoAnswer;
    std::wstring detail;  // status code or the download's error text
    uint64_t bytes = 0;   // size of the file (known once the release was read)
};

// Blocking (call it on a worker thread). `addonsDir` ends with a backslash. `cancelled` may be empty.
ExtrasInstallResult InstallUnofficialExtras(const std::wstring& addonsDir, const std::function<bool()>& cancelled);
// The same for "Events: Chat" (newest release of jsantorek/GW2-Chat, Events_Chat.dll, SHA-256 checked).
ExtrasInstallResult InstallChatEvents(const std::wstring& addonsDir, const std::function<bool()>& cancelled);

// <GW2>\addons\ of the installed game (registry / Steam / usual folders, never the game process), empty if none.
std::wstring Gw2AddonsDir();

// Our Nexus add-on as it comes with the program package: <exe folder>\nexus\gw2chat_bridge.dll (empty if absent).
constexpr const wchar_t* kBridgeDllName = L"gw2chat_bridge.dll";
std::wstring BridgeInPackage();

}  // namespace gct
