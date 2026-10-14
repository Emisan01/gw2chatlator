// extras_setup.hpp — is Unofficial Extras there, and installing it the way the Nexus library installs add-ons.
#pragma once

#include <windows.h>

#include <string>

namespace bridge {

struct Presence {
    bool arcdps = false;      // a loaded module exports addextension2
    bool extras = false;      // Unofficial Extras is loaded
    bool extrasFile = false;  // arcdps_unofficial_extras.dll lies in our add-ons folder (loaded at the next start)
    bool relay = false;       // Nexus' own ArcDPS Integration (relays the extras chat event)
};

// Looks at the modules of the game process we live in (no other process is touched) and at our add-ons folder.
Presence DetectPresence(HMODULE self);

enum class InstallState { Idle, Running, Done, Failed };

// Only on the user's click: the newest release from Krappa322's GitHub page, its SHA-256 checked, into the add-ons
// folder (where Nexus picks it up and hands it to ArcDPS). Runs on its own thread.
void StartExtrasInstall(HMODULE self);
InstallState GetInstallState(std::string* message);  // message: UTF-8, for the options page
void StopExtrasInstall();                              // on unload: cancel and wait

}  // namespace bridge
