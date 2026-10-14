// bridge_pipe.hpp — receives the lines of the chat senders: the optional Nexus add-on "GW2 Chat Bridge" and the OCR
// reader (docs/NEXUS_PLAN.md, docs/READER_PLAN.md). A named pipe server on its own thread with several instances (the
// add-on and the reader at the same time): inbound only, local clients only, DACL for the current user only. We never
// write into it – nothing can go back into the game process. Every decoded line is posted to the window.
#pragma once

#include <windows.h>

#include <thread>

#include "core/bridge_protocol.hpp"

namespace gct {

class BridgePipe {
public:
    // Posted messages: wParam = MAKEWPARAM(kind, connection) – the connection number tells the senders apart.
    // kConnected / kDisconnected: lParam 0. kMessage: lParam = new BridgeMessage* (the receiver deletes it).
    enum : WORD { kMessage = 0, kConnected = 1, kDisconnected = 2 };
    static constexpr int kMaxClients = 4;
    static WORD KindOf(WPARAM wp) { return LOWORD(wp); }
    static int ConnectionOf(WPARAM wp) { return HIWORD(wp); }

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
