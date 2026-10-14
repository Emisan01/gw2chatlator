// voice_module.hpp — the voice module of GW2 Keyboard for Nexus: gw2kb_voice.exe and the whisper DLLs in
// <addons>\GW2Keyboard\ (voice\*.dll). When only the add-on DLL was copied, it is loaded from this version's GitHub
// release (GW2Keyboard-Nexus.zip, SHA-256 as GitHub reports it), unpacked with Windows' tar.exe in %TEMP%, and only
// those files are copied over.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "win/extras_install.hpp"

namespace gct {

// `kbDir` = <addons>\GW2Keyboard (no trailing backslash).
bool VoiceModulePresent(const std::wstring& kbDir);

// Blocking (own thread). `progress` gets the download's bytes and returns false to cancel.
ExtrasInstallResult InstallVoiceModule(const std::wstring& kbDir, const std::wstring& version,
                                       const std::function<bool(uint64_t done, uint64_t total)>& progress);

}  // namespace gct
