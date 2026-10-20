// extras_install.hpp — loads add-on files the way the Nexus library does: an asset of a GitHub release, checked
// against the SHA-256 GitHub reports for it. Used by GW2 Keyboard for Nexus for Vonsh's "Events: Chat" (into
// <GW2>\addons, Nexus loads it) and for its own voice module (voice_module.cpp).
#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace gct {

// Vonsh's "Events: Chat" (in the Nexus library): every chat channel as a Nexus event.
constexpr const wchar_t* kChatEventsDllName = L"Events_Chat.dll";
constexpr const wchar_t* kChatEventsPage = L"https://github.com/jsantorek/GW2-Chat";
// PieOrCake's "Decoder Ring" (in the Nexus library): names, maps and details of chat codes, through a DataLink.
constexpr const wchar_t* kDecoderRingDllName = L"DecoderRing.dll";

enum class ExtrasInstallCode { Ok, NoAnswer, NotChecked, DownloadFailed };

struct ExtrasInstallResult {
    ExtrasInstallCode code = ExtrasInstallCode::NoAnswer;
    std::wstring detail;  // status code or the download's error text
    uint64_t bytes = 0;   // size of the file (known once the release was read)
};

// The newest release of jsantorek/GW2-Chat, Events_Chat.dll, SHA-256 checked, into `addonsDir` (ends with a
// backslash). Blocking (call it on a worker thread). `cancelled` may be empty.
ExtrasInstallResult InstallChatEvents(const std::wstring& addonsDir, const std::function<bool()>& cancelled);
// The newest release of PieOrCake/decoder_ring, DecoderRing.dll, SHA-256 checked, into `addonsDir`. Blocking.
ExtrasInstallResult InstallDecoderRing(const std::wstring& addonsDir, const std::function<bool()>& cancelled);

// One asset of a GitHub release (`releaseApiPath` e.g. "/repos/o/r/releases/latest") into `dest`: only from
// `downloadPrefix`, only with the SHA-256 GitHub reports for it. `progress` returns false to cancel. Blocking.
ExtrasInstallResult DownloadReleaseAsset(const std::wstring& releaseApiPath, const char* downloadPrefix,
                                         const std::wstring& asset, const std::wstring& dest,
                                         const std::function<bool(uint64_t done, uint64_t total)>& progress);

}  // namespace gct
