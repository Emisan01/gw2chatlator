#pragma once
// Finding the GW2 installation, installing the tool into a clean subfolder
// next to the game, and the optional "start with Windows, show with GW2"
// autostart. Hook-free: only the registry, Steam's library list and the file
// system are read. The game process is never opened.

#include <string>

#include "core/gw2_install.hpp"

namespace gct {

// First directory that contains Gw2-64.exe: registry (ArenaNet key and the
// uninstall entry, 64- and 32-bit views, HKLM and HKCU), Steam libraries,
// Program Files and "<drive>:\Guild Wars 2" / "<drive>:\Games\Guild Wars 2".
// Empty if nothing was found; the caller then lets the user pick the folder.
std::wstring FindGw2Dir();

// Resolves a directory or file path to the root GW2 installation folder.
// Handles subfolders like "addons" or "bin64", directly selected "Gw2-64.exe",
// and trailing slashes. Returns empty if no GW2 installation is found.
std::wstring ResolveGw2Dir(const std::wstring& path);

bool IsGw2Dir(const std::wstring& dir);

// Version-resource text of a DLL/EXE: FileDescription, ProductName and
// CompanyName joined with " / ". Empty if the file has no version resource.
std::wstring FileDescription(const std::wstring& path);

AddonEnvironment ScanAddons(const std::wstring& gameDir);

std::wstring CurrentExePath();
std::wstring CurrentExeDir();

struct InstallResult {
    bool ok = false;
    bool alreadyThere = false;   // we already run from the target folder
    std::wstring exePath;        // installed exe
    std::wstring error;          // user-facing reason when !ok
};

// Copies the running exe (and the ini next to it, when the target has none)
// to `targetDir`. Creates the folder. Never overwrites a newer ini.
InstallResult InstallTo(const std::wstring& targetDir);

// Fallback when the game folder is not writable: %LOCALAPPDATA%\Programs\GW2ChatTranslator.
std::wstring UserInstallDir();

// HKCU\...\Run entry "GW2ChatTranslator" -> "<exe>" --wait-for-gw2.
bool SetAutostart(bool enable, const std::wstring& exePath);
bool IsAutostartEnabled();
// "GW2 Chat Translator" shortcut on the desktop pointing to `exePath` (replaced if there). The user's choice.
bool CreateDesktopShortcut(const std::wstring& exePath);
// The exe the Run entry points to (empty if none).
std::wstring AutostartTarget();

// Folder picker (IFileOpenDialog with FOS_PICKFOLDERS). Empty if cancelled.
std::wstring PickFolder(void* owner, const std::wstring& title, const std::wstring& initial);
// A text file to open (`save` = false) or to save as (`save` = true, suggested `name`). Empty when cancelled.
std::wstring PickTextFile(void* owner, const std::wstring& title, bool save, const std::wstring& name);

}  // namespace gct
