// map_focus.hpp — a waypoint clicked in our chat: the world map opens and moves onto it, as the game does for the links
// of its own chat. Nexus presses the game's map key; MumbleLink tells where the open map looks (mapCenterX/Y in
// continent coordinates, the same as the GW2 API's); the map is dragged with the mouse until the spot is in the middle
// (a few steps, each one measured). The level / instance stays as it is – like in the game.
//
// Only on your click, only while GW2 is in front (checked before every step); the cursor goes back where it was.
#pragma once

#include <windows.h>

#include "nexus/Nexus.h"

namespace gct {

// `avoid`: our window in client coordinates (the drag starts outside it). Runs on its own thread; a second click while
// it runs is ignored.
void FocusWorldMap(AddonAPI_t* api, float x, float y, const RECT& avoid);
// Before the DLL goes: ends a running focus and waits for it.
void StopMapFocus();

}  // namespace gct
