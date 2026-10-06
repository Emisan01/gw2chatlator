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

struct MumbleState {
    bool live = false;          // GW2 is running and updating the block
    MumbleIdentity identity;
    uint32_t uiState = 0;
    uint32_t processId = 0;

    bool GameHasFocus() const { return live && (uiState & kUiGameHasFocus); }
    bool TextboxHasFocus() const { return live && (uiState & kUiTextboxHasFocus); }
};

}  // namespace gct
