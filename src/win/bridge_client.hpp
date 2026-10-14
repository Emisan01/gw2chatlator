// bridge_client.hpp — the sending side of GW2 Keyboard's pipe, for the OCR reader (GW2 Chat Translator --reader,
// docs/READER_PLAN.md). Connects whenever the keyboard runs, writes one JSON line per message (core/bridge_protocol),
// never reads anything back. Sending never blocks the caller: a small queue and its own thread.
#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

#include "core/bridge_protocol.hpp"

namespace gct {

class BridgeClient {
public:
    ~BridgeClient() { Stop(); }
    // `src` = "ocr"; `version` goes into the hello of every connection.
    void Start(const std::string& src, const std::string& version);
    void Stop();
    void Send(const BridgeMessage& m);  // dropped (oldest first) while the keyboard is not there
    bool Connected() const { return connected_; }

private:
    void Run();

    std::string src_, version_;
    std::mutex mu_;
    std::condition_variable wake_;
    std::deque<std::string> queue_;
    std::atomic<bool> stop_{false}, connected_{false};
    std::thread thread_;
};

}  // namespace gct
