#include "win/bridge_client.hpp"

#include <windows.h>

#include <chrono>

namespace gct {
namespace {

constexpr size_t kQueueConnected = 256;    // lines waiting while the keyboard is busy
constexpr size_t kQueueDisconnected = 20;  // without the keyboard only the newest few are kept for its start

bool WriteLine(HANDLE pipe, const std::string& line) {
    DWORD written = 0;
    return WriteFile(pipe, line.data(), static_cast<DWORD>(line.size()), &written, nullptr) && written == line.size();
}

}  // namespace

void BridgeClient::Start(const std::string& src, const std::string& version) {
    if (thread_.joinable()) return;
    src_ = src;
    version_ = version;
    stop_ = false;
    thread_ = std::thread([this] { Run(); });
}

void BridgeClient::Stop() {
    if (!thread_.joinable()) return;
    stop_ = true;
    wake_.notify_all();
    CancelSynchronousIo(thread_.native_handle());  // a write blocked on a full pipe
    thread_.join();
}

void BridgeClient::Send(const BridgeMessage& m) {
    BridgeMessage copy = m;
    copy.src = src_;
    {
        std::lock_guard<std::mutex> lock(mu_);
        queue_.push_back(EncodeBridgeMessage(copy));
        const size_t cap = connected_ ? kQueueConnected : kQueueDisconnected;
        while (queue_.size() > cap) queue_.pop_front();
    }
    wake_.notify_one();
}

void BridgeClient::Run() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    HANDLE pipe = INVALID_HANDLE_VALUE;
    while (!stop_) {
        if (pipe == INVALID_HANDLE_VALUE) {
            // SECURITY_IDENTIFICATION: whoever owns the pipe cannot act as us.
            pipe = CreateFileW(kBridgePipeName, GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                               SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
            if (pipe == INVALID_HANDLE_VALUE) {
                std::unique_lock<std::mutex> lock(mu_);
                wake_.wait_for(lock, std::chrono::seconds(2), [this] { return stop_.load(); });
                continue;
            }
            BridgeMessage hello;
            hello.kind = BridgeMessage::Kind::Hello;
            hello.src = src_;
            hello.addonVersion = version_;
            if (!WriteLine(pipe, EncodeBridgeMessage(hello))) {
                CloseHandle(pipe);
                pipe = INVALID_HANDLE_VALUE;
                continue;
            }
            connected_ = true;
        }
        std::string line;
        {
            std::unique_lock<std::mutex> lock(mu_);
            wake_.wait_for(lock, std::chrono::seconds(5), [this] { return stop_.load() || !queue_.empty(); });
            if (stop_) break;
            if (queue_.empty()) continue;
            line = std::move(queue_.front());
            queue_.pop_front();
        }
        if (!WriteLine(pipe, line)) {  // the keyboard closed: the line is lost, connect again later
            CloseHandle(pipe);
            pipe = INVALID_HANDLE_VALUE;
            connected_ = false;
        }
    }
    if (pipe != INVALID_HANDLE_VALUE) CloseHandle(pipe);
    connected_ = false;
}

}  // namespace gct
