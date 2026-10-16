// d3d11_hook.cpp — Inactive stub.
// DirectX 11 scissor hooking was deactivated because GW2's native 2D UI does not use
// D3D11 scissor rects, preventing false positive suppression of add-on UI.

#include "d3d11_hook.hpp"

namespace gct {

void D3D11Hook_Init(AddonAPI_t*) {}
void D3D11Hook_Shutdown() {}
void D3D11Hook_SetHideChat(bool) {}
void D3D11Hook_FrameReset() {}

}  // namespace gct
