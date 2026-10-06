#pragma once
// Portable helpers for finding the GW2 installation, describing the add-on
// environment (arcdps, Nexus, unofficial extras) and building the autostart
// command line. No windows.h here: the Windows side (win/gw2_locate) supplies
// registry values, file existence and file descriptions.
//
// Everything in this file is read-only detection by file names. Nothing here
// touches the game process; that keeps the tool hook-free.

#include <functional>
#include <string>
#include <vector>

namespace gct {

// Folder name used inside "<GW2>\addons\" for a clean, self-contained install.
inline constexpr const wchar_t* kInstallFolderName = L"GW2ChatTranslator";

// Command line switch: start hidden and show the window only while GW2 runs.
inline constexpr const wchar_t* kWaitForGw2Switch = L"--wait-for-gw2";

// "D:\\SteamLibrary" style paths from Steam's libraryfolders.vdf (UTF-8 text).
// Escaped backslashes are unescaped, duplicates removed, order kept.
std::vector<std::wstring> ParseSteamLibraryPaths(const std::string& vdfUtf8);

// Turns a path that may point at Gw2-64.exe (or quoted, or with arguments, as
// found in registry values) into the game directory. Empty if nothing usable.
std::wstring GameDirFromRegistryValue(const std::wstring& value);

// Candidate game directories in the order they should be tried. `running` is
// the directory of a running Gw2-64.exe (best source), then registry values,
// Steam libraries and common install locations. Duplicates removed
// (case-insensitive, trailing separators ignored).
std::vector<std::wstring> Gw2DirCandidates(const std::wstring& running,
                                           const std::vector<std::wstring>& registryValues,
                                           const std::vector<std::wstring>& steamLibraries,
                                           const std::vector<std::wstring>& programFilesDirs);

std::wstring JoinPath(const std::wstring& dir, const std::wstring& name);
std::wstring InstallDirFor(const std::wstring& gameDir);

// What is installed next to the game. Detection only; we never load or call
// any of it.
struct AddonEnvironment {
    bool proxyDll = false;          // <GW2>\d3d11.dll exists (some loader is installed)
    bool chainload = false;         // <GW2>\d3d11_chainload.dll (Nexus chain-loading arcdps)
    bool nexus = false;
    bool arcdps = false;
    bool unofficialExtras = false;
    bool addonLoader = false;       // gw2-addon-loader (older loader)
    std::wstring proxyDescription;  // FileDescription/ProductName of d3d11.dll
    std::vector<std::wstring> evidence;  // relative paths that matched, for the status view
};

// `exists(rel)` answers whether "<GW2>\rel" exists (file or folder).
// `describe(rel)` returns the version-resource description of a DLL (may be empty).
AddonEnvironment DetectAddons(const std::function<bool(const std::wstring&)>& exists,
                              const std::function<std::wstring(const std::wstring&)>& describe);

// Quoted autostart command: "\"C:\\...\\GW2ChatTranslator.exe\" --wait-for-gw2".
std::wstring AutostartCommand(const std::wstring& exePath);

// True if the command line (as passed to wWinMain) contains `sw` as a whole token.
bool HasSwitch(const std::wstring& cmdLine, const std::wstring& sw);

}  // namespace gct
