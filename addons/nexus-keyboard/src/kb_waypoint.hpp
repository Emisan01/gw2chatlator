// kb_waypoint.hpp — the "quantum macro" for waypoints (kb_waypoint.cpp).
// Included by kb_shared.hpp at its place (it relies on what stands before it there) – include that, not this.
#pragma once

namespace kb {

void WaypointFrameTick();                              // render thread, every frame: the frame the macro asked for
void StartWaypointClick(const ChatLineHint& line);     // g_mu held: click the original in GW2's chat
void OnClickTestKey(const char*, bool release);        // the test key: one click where the mouse is
void StopWaypointMacro();                              // unload
int SyntheticInputs();                                 // the self-test: how many synthetic mouse inputs were sent
bool WaypointPassThrough();                            // while the click runs our window lets the mouse through

}  // namespace kb
