// bridge_pipe.hpp — receives the lines of the optional Nexus add-on "GW2 Chat Bridge" (docs/NEXUS_PLAN.md).
// A named pipe server on its own thread: inbound only, local clients only, DACL for the current user only. We never
// write into it – nothing can go back into the game process. Every decoded line is posted to the window.
#pragma once

#include <windows.h>

#include <atomic>
#include <thread>

#include "core/bridge_protocol.hpp"

namespace gct {

class BridgePipe {
public:
    // Posted messages: wParam 1 = a client connected, 2 = it disconnected (lParam 0 for both);
    // wParam 0 = a message, lParam = new BridgeMessage* (the receiver deletes it).
    enum : WPARAM { kMessage = 0, kConnected = 1, kDisconnected = 2 };

    ~BridgePipe() { Stop(); }
    void Start(HWND target, UINT msg);
    void Stop();

private:
    void Run();

    HWND target_ = nullptr;
    UINT msg_ = 0;
    HANDLE stop_ = nullptr;
    std::thread thread_;
};

}  // namespace gct
