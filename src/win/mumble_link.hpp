// mumble_link.hpp — reads GW2's MumbleLink shared memory (read-only).
// This is the interface ArenaNet provides for external tools; it is not
// memory reading of the game process.
#pragma once

#include <windows.h>

#include <string>

#include "core/mumble.hpp"

namespace gct {

class MumbleLink {
public:
    explicit MumbleLink(std::wstring name = L"MumbleLink") : name_(std::move(name)) {}
    ~MumbleLink();
    MumbleLink(const MumbleLink&) = delete;
    MumbleLink& operator=(const MumbleLink&) = delete;

    // Cheap; call as often as needed. `live` is false when GW2 is not
    // running or the block has not been updated for a while.
    MumbleState Read();

private:
    bool Open();

    std::wstring name_;
    HANDLE map_ = nullptr;
    const void* view_ = nullptr;
    unsigned long lastTick_ = 0;
    ULONGLONG lastTickChange_ = 0;
};

}  // namespace gct
