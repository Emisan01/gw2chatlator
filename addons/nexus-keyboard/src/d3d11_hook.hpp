// d3d11_hook.hpp — D3D11 scissor-rect filter for hiding native GW2 chat without pattern scanning.
#pragma once

#include "nexus/Nexus.h"

namespace gct {

void D3D11Hook_Init(AddonAPI_t* api);
void D3D11Hook_Shutdown();
void D3D11Hook_SetHideChat(bool hide);
void D3D11Hook_FrameReset();

}  // namespace gct
