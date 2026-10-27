// local_mt_packages.hpp — click-to-install, SHA-256 checked OPUS-MT assets.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "win/extras_install.hpp"

namespace gct {

bool LocalMtModulePresent(const std::wstring& kbDir);
bool LocalMtModelPresent(const std::wstring& dataDir, const std::string& pair);
bool IsLocalMtModelCorrupted(const std::wstring& dataDir, const std::string& pair);
bool RemoveLocalMtModule(const std::wstring& kbDir);
bool RemoveLocalMtModel(const std::wstring& dataDir, const std::string& pair);

ExtrasInstallResult InstallLocalMtModule(
    const std::wstring& kbDir, const std::wstring& version,
    const std::function<bool(uint64_t done, uint64_t total)>& progress);
ExtrasInstallResult UpdateLocalMtModule(
    const std::wstring& kbDir, const std::wstring& version,
    const std::function<bool(uint64_t done, uint64_t total)>& progress);
// Replaces kbDir's helper by the one in packageDir: ends the running helper, keeps the old exe as .old until the
// new one answered "ready" with a sufficient proto, and puts the old one back on any failure (no half state).
ExtrasInstallResult SwapLocalMtHelper(const std::wstring& kbDir, const std::wstring& packageDir);
ExtrasInstallResult InstallLocalMtModel(
    const std::wstring& dataDir, const std::string& pair, const std::wstring& version,
    const std::function<bool(uint64_t done, uint64_t total)>& progress);

}  // namespace gct
