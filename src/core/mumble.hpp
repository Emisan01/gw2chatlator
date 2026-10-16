// mumble.hpp — GW2's MumbleLink data (the official shared-memory interface
// documented on the GW2 wiki, API:MumbleLink). Portable parsing part.
#pragma once

#include <cstdint>
#include <string>

namespace gct {

// uiState bits of the GW2 context block.
constexpr uint32_t kUiMapOpen = 1u << 0;
constexpr uint32_t kUiGameHasFocus = 1u << 3;
constexpr uint32_t kUiCompetitive = 1u << 4;
constexpr uint32_t kUiTextboxHasFocus = 1u << 5;
constexpr uint32_t kUiInCombat = 1u << 6;

struct MumbleIdentity {
    std::wstring name;     // character name
    int uiSize = -1;       // 0 small .. 3 larger
    uint32_t mapId = 0;
    bool commander = false;
};

// Parses the identity JSON GW2 writes into LinkedMem::identity.
MumbleIdentity ParseMumbleIdentity(const std::wstring& json);

// Returns the height (in pixels) of the native GW2 bottom bar (EXP / mastery bar up to the
// bottom edge of the native chat minimal button) for a given screen display height and GW2
// uiSize (0=small, 1=normal, 2=large, 3=larger, or -1=unknown).
float Gw2BottomBarOffset(float displayH, int uiSize = -1);

struct MumbleState {
    bool live = false;          // GW2 is running and updating the block
    // GW2 writes the block every frame only while you are on a map: the tick
    // stands still in the character selection and on loading screens. The
    // chat reader runs only then (that screen shows character data, no chat).
    bool inMap = false;
    MumbleIdentity identity;
    uint32_t uiState = 0;
    uint32_t processId = 0;
    uint32_t uiTick = 0;  // GW2 adds 1 every frame: ticks per second = the game's frame rate
    // The map (continent coordinates, as the GW2 API gives them): where the open world map looks, its scale, you.
    float mapCenterX = 0, mapCenterY = 0, mapScale = 0, playerX = 0, playerY = 0;

    bool GameHasFocus() const { return live && (uiState & kUiGameHasFocus); }
    bool TextboxHasFocus() const { return live && (uiState & kUiTextboxHasFocus); }
    bool MapOpen() const { return live && (uiState & kUiMapOpen); }
};

}  // namespace gct
