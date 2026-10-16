// d3d11_hook.cpp — D3D11 scissor-rect filter for hiding native GW2 chat without pattern scanning.
//
// In Guild Wars 2, the native chat window is docked in the bottom-left corner of the screen.
// By intercepting DirectX 11 scissor rects and draw calls on the immediate context via Nexus's
// MinHook interface, we completely skip rendering of the native chat window whenever hideNativeChat
// is enabled. Because this hooks the standard Microsoft D3D11 COM interface, it requires ZERO
// byte pattern scanning and is 100% stable across game updates.

#include "d3d11_hook.hpp"

#include <d3d11.h>
#include <dxgi.h>
#include <windows.h>

#include <atomic>
#include <cstdio>

#include "imgui/imgui.h"

namespace gct {

namespace {

typedef void (__stdcall *PFN_RSSetScissorRects)(ID3D11DeviceContext*, UINT, const D3D11_RECT*);
typedef void (__stdcall *PFN_DrawIndexed)(ID3D11DeviceContext*, UINT, UINT, INT);
typedef void (__stdcall *PFN_Draw)(ID3D11DeviceContext*, UINT, UINT);
typedef void (__stdcall *PFN_DrawIndexedInstanced)(ID3D11DeviceContext*, UINT, UINT, UINT, INT, UINT);
typedef void (__stdcall *PFN_DrawInstanced)(ID3D11DeviceContext*, UINT, UINT, UINT, UINT);
typedef void (__stdcall *PFN_RSSetState)(ID3D11DeviceContext*, ID3D11RasterizerState*);
typedef void (__stdcall *PFN_RSSetViewports)(ID3D11DeviceContext*, UINT, const D3D11_VIEWPORT*);

AddonAPI_t* s_api = nullptr;
ID3D11Device* s_device = nullptr;
ID3D11DeviceContext* s_context = nullptr;
void** s_vtbl = nullptr;

std::atomic<bool> s_hideNativeChat{false};
bool s_inChatScissor = false;
bool s_loggedHit = false;

PFN_RSSetScissorRects s_origRSSetScissorRects = nullptr;
PFN_DrawIndexed s_origDrawIndexed = nullptr;
PFN_Draw s_origDraw = nullptr;
PFN_DrawIndexedInstanced s_origDrawIndexedInstanced = nullptr;
PFN_DrawInstanced s_origDrawInstanced = nullptr;
PFN_RSSetState s_origRSSetState = nullptr;
PFN_RSSetViewports s_origRSSetViewports = nullptr;

void __stdcall Hooked_RSSetScissorRects(ID3D11DeviceContext* pContext, UINT NumRects, const D3D11_RECT* pRects) {
    if (NumRects > 0 && pRects) {
        float screenW = 1920.0f;
        float screenH = 1080.0f;
        if (ImGui::GetCurrentContext() && ImGui::GetIO().DisplaySize.x > 0 && ImGui::GetIO().DisplaySize.y > 0) {
            screenW = ImGui::GetIO().DisplaySize.x;
            screenH = ImGui::GetIO().DisplaySize.y;
        }

        const LONG l = pRects[0].left;
        const LONG t = pRects[0].top;
        const LONG r = pRects[0].right;
        const LONG b = pRects[0].bottom;

        // GW2 chat window is anchored in the bottom-left corner of the window.
        // It spans horizontally from x = [0..60] to [80..screenW*0.65],
        // and vertically from y = [screenH*0.20..screenH+50].
        // Full-screen scissor rects (0, 0, screenW, screenH) have t == 0 and r >= screenW - 10, so they never match.
        // Child elements (tabs, text history, editbox) are also contained within this quadrant.
        if (l >= 0 && l <= 60 &&
            r > 80 && r <= static_cast<LONG>(screenW * 0.65f) &&
            t >= static_cast<LONG>(screenH * 0.20f) &&
            b <= static_cast<LONG>(screenH + 50)) {
            s_inChatScissor = true;
            if (!s_loggedHit && s_api && s_api->Log) {
                s_loggedHit = true;
                char buf[128];
                snprintf(buf, sizeof(buf), "Native chat scissor detected: [%ld, %ld, %ld, %ld]", l, t, r, b);
                s_api->Log(LOGL_INFO, "GW2Chatlator", buf);
            }
        } else {
            s_inChatScissor = false;
        }
    } else {
        s_inChatScissor = false;
    }

    s_origRSSetScissorRects(pContext, NumRects, pRects);
}

void __stdcall Hooked_DrawIndexed(ID3D11DeviceContext* pContext, UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation) {
    if (s_hideNativeChat.load(std::memory_order_relaxed) && s_inChatScissor) {
        return;
    }
    s_origDrawIndexed(pContext, IndexCount, StartIndexLocation, BaseVertexLocation);
}

void __stdcall Hooked_Draw(ID3D11DeviceContext* pContext, UINT VertexCount, UINT StartVertexLocation) {
    if (s_hideNativeChat.load(std::memory_order_relaxed) && s_inChatScissor) {
        return;
    }
    s_origDraw(pContext, VertexCount, StartVertexLocation);
}

void __stdcall Hooked_DrawIndexedInstanced(ID3D11DeviceContext* pContext, UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation) {
    if (s_hideNativeChat.load(std::memory_order_relaxed) && s_inChatScissor) {
        return;
    }
    s_origDrawIndexedInstanced(pContext, IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation, StartInstanceLocation);
}

void __stdcall Hooked_DrawInstanced(ID3D11DeviceContext* pContext, UINT VertexCountPerInstance, UINT InstanceCount, UINT StartVertexLocation, UINT StartInstanceLocation) {
    if (s_hideNativeChat.load(std::memory_order_relaxed) && s_inChatScissor) {
        return;
    }
    s_origDrawInstanced(pContext, VertexCountPerInstance, InstanceCount, StartVertexLocation, StartInstanceLocation);
}

void __stdcall Hooked_RSSetState(ID3D11DeviceContext* pContext, ID3D11RasterizerState* pRasterizerState) {
    s_inChatScissor = false;
    s_origRSSetState(pContext, pRasterizerState);
}

void __stdcall Hooked_RSSetViewports(ID3D11DeviceContext* pContext, UINT NumViewports, const D3D11_VIEWPORT* pViewports) {
    s_inChatScissor = false;
    s_origRSSetViewports(pContext, NumViewports, pViewports);
}

}  // namespace

void D3D11Hook_Init(AddonAPI_t* api) {
    if (!api || !api->SwapChain || !api->MinHook_Create || !api->MinHook_Enable) return;
    if (s_context) return;  // already initialized

    s_api = api;
    IDXGISwapChain* swapChain = static_cast<IDXGISwapChain*>(api->SwapChain);

    HRESULT hr = swapChain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&s_device));
    if (FAILED(hr) || !s_device) {
        if (s_api->Log) s_api->Log(LOGL_WARNING, "GW2Chatlator", "Failed to get D3D11 device from SwapChain");
        return;
    }

    s_device->GetImmediateContext(&s_context);
    if (!s_context) {
        if (s_api->Log) s_api->Log(LOGL_WARNING, "GW2Chatlator", "Failed to get D3D11 immediate context");
        s_device->Release();
        s_device = nullptr;
        return;
    }

    s_vtbl = *reinterpret_cast<void***>(s_context);
    if (!s_vtbl) {
        s_context->Release();
        s_context = nullptr;
        s_device->Release();
        s_device = nullptr;
        return;
    }

    auto hook = [&](int index, void* detour, void** orig) {
        void* target = s_vtbl[index];
        EMHStatus st = s_api->MinHook_Create(target, detour, orig);
        if (st == MH_OK) {
            s_api->MinHook_Enable(target);
        } else if (s_api->Log) {
            char buf[96];
            snprintf(buf, sizeof(buf), "MinHook_Create failed for vtbl[%d]: status %d", index, static_cast<int>(st));
            s_api->Log(LOGL_WARNING, "GW2Chatlator", buf);
        }
    };

    hook(45, reinterpret_cast<void*>(Hooked_RSSetScissorRects), reinterpret_cast<void**>(&s_origRSSetScissorRects));
    hook(12, reinterpret_cast<void*>(Hooked_DrawIndexed), reinterpret_cast<void**>(&s_origDrawIndexed));
    hook(13, reinterpret_cast<void*>(Hooked_Draw), reinterpret_cast<void**>(&s_origDraw));
    hook(20, reinterpret_cast<void*>(Hooked_DrawIndexedInstanced), reinterpret_cast<void**>(&s_origDrawIndexedInstanced));
    hook(21, reinterpret_cast<void*>(Hooked_DrawInstanced), reinterpret_cast<void**>(&s_origDrawInstanced));
    hook(43, reinterpret_cast<void*>(Hooked_RSSetState), reinterpret_cast<void**>(&s_origRSSetState));
    hook(44, reinterpret_cast<void*>(Hooked_RSSetViewports), reinterpret_cast<void**>(&s_origRSSetViewports));

    if (s_api->Log) s_api->Log(LOGL_INFO, "GW2Chatlator", "D3D11 native chat filter installed");
}

void D3D11Hook_Shutdown() {
    if (s_api && s_api->MinHook_Disable && s_api->MinHook_Remove && s_vtbl) {
        auto unhook = [&](int index, void* orig) {
            if (orig) {
                void* target = s_vtbl[index];
                s_api->MinHook_Disable(target);
                s_api->MinHook_Remove(target);
            }
        };
        unhook(45, s_origRSSetScissorRects);
        unhook(12, s_origDrawIndexed);
        unhook(13, s_origDraw);
        unhook(20, s_origDrawIndexedInstanced);
        unhook(21, s_origDrawInstanced);
        unhook(43, s_origRSSetState);
        unhook(44, s_origRSSetViewports);
    }

    s_origRSSetScissorRects = nullptr;
    s_origDrawIndexed = nullptr;
    s_origDraw = nullptr;
    s_origDrawIndexedInstanced = nullptr;
    s_origDrawInstanced = nullptr;
    s_origRSSetState = nullptr;
    s_origRSSetViewports = nullptr;
    s_vtbl = nullptr;

    if (s_context) {
        s_context->Release();
        s_context = nullptr;
    }
    if (s_device) {
        s_device->Release();
        s_device = nullptr;
    }
    s_api = nullptr;
}

void D3D11Hook_SetHideChat(bool hide) {
    s_hideNativeChat.store(hide, std::memory_order_relaxed);
}

void D3D11Hook_FrameReset() {
    s_inChatScissor = false;
}

}  // namespace gct
