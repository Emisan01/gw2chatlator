#include "win/bridge_pipe.hpp"

#include <sddl.h>

#include <string>
#include <vector>

namespace gct {
namespace {

// "D:P(A;;GA;;;<SID of the current user>)" – nobody else may open the pipe.
std::wstring OwnUserSddl() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return {};
    std::wstring sddl;
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<BYTE> buf(size);
    if (size && GetTokenInformation(token, TokenUser, buf.data(), size, &size)) {
        wchar_t* sid = nullptr;
        if (ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buf.data())->User.Sid, &sid)) {
            sddl = std::wstring(L"D:P(A;;GA;;;") + sid + L")";
            LocalFree(sid);
        }
    }
    CloseHandle(token);
    return sddl;
}

// One pipe instance: waiting for a sender, or reading from it.
struct Slot {
    HANDLE pipe = INVALID_HANDLE_VALUE;
    OVERLAPPED ov{};
    bool reading = false;  // false: waiting for a connection
    char buf[4096];
    std::string pending;   // bytes of a line not yet complete
};

}  // namespace

void BridgePipe::Start(HWND target, UINT msg) {
    if (thread_.joinable()) return;
    target_ = target;
    msg_ = msg;
    stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stop_) return;
    thread_ = std::thread([this] { Run(); });
}

void BridgePipe::Stop() {
    if (!thread_.joinable()) return;
    SetEvent(stop_);
    thread_.join();
    CloseHandle(stop_);
    stop_ = nullptr;
}

void BridgePipe::Run() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    const std::wstring sddl = OwnUserSddl();
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (sddl.empty() ||
        !ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &sd, nullptr))
        return;  // without a user-only DACL no pipe at all
    SECURITY_ATTRIBUTES sa{sizeof(sa), sd, FALSE};
    Slot slots[kMaxClients];
    for (Slot& s : slots) s.ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    bool own = false;  // the name is ours: the first instance was created with FIRST_PIPE_INSTANCE

    auto post = [&](WORD kind, int conn, LPARAM lp) {
        return PostMessageW(target_, msg_, MAKEWPARAM(kind, conn), lp) != FALSE;
    };
    // Waits for the next sender on this slot (a new instance if it has none).
    auto listen = [&](int i) {
        Slot& s = slots[i];
        s.reading = false;
        s.pending.clear();
        if (s.pipe == INVALID_HANDLE_VALUE) {
            // FIRST_PIPE_INSTANCE: if another process holds the name, nothing is created (retried later).
            s.pipe = CreateNamedPipeW(kBridgePipeName,
                                      PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED | (own ? 0 : FILE_FLAG_FIRST_PIPE_INSTANCE),
                                      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                      kMaxClients, 0, 64 * 1024, 0, &sa);
            if (s.pipe == INVALID_HANDLE_VALUE) return false;
            own = true;
        }
        ResetEvent(s.ov.hEvent);
        if (ConnectNamedPipe(s.pipe, &s.ov)) {
            SetEvent(s.ov.hEvent);
        } else {
            const DWORD err = GetLastError();
            if (err == ERROR_PIPE_CONNECTED) SetEvent(s.ov.hEvent);  // connected between create and connect
            else if (err != ERROR_IO_PENDING) return false;
        }
        return true;
    };
    auto read = [&](int i) {
        Slot& s = slots[i];
        s.reading = true;
        ResetEvent(s.ov.hEvent);
        if (!ReadFile(s.pipe, s.buf, sizeof(s.buf), nullptr, &s.ov) && GetLastError() != ERROR_IO_PENDING) return false;
        return true;  // done or pending: the event tells
    };
    auto drop = [&](int i, bool wasConnected) {
        Slot& s = slots[i];
        CancelIoEx(s.pipe, &s.ov);
        DWORD n = 0;
        GetOverlappedResult(s.pipe, &s.ov, &n, TRUE);
        DisconnectNamedPipe(s.pipe);
        if (wasConnected) post(kDisconnected, i, 0);
        s.reading = false;
    };

    std::vector<HANDLE> waits;
    std::vector<int> index;
    while (WaitForSingleObject(stop_, 0) != WAIT_OBJECT_0) {
        // Every slot waits for a sender or reads; a slot without an instance tries again on the next round.
        waits.assign(1, stop_);
        index.assign(1, -1);
        for (int i = 0; i < kMaxClients; ++i) {
            Slot& s = slots[i];
            if (!s.ov.hEvent) continue;
            if (s.pipe == INVALID_HANDLE_VALUE && !listen(i)) continue;
            waits.push_back(s.ov.hEvent);
            index.push_back(i);
        }
        const DWORD r = WaitForMultipleObjects(static_cast<DWORD>(waits.size()), waits.data(), FALSE,
                                               waits.size() > 1 ? INFINITE : 5000);  // no instance yet: retry
        if (r == WAIT_OBJECT_0 || r == WAIT_TIMEOUT || r == WAIT_FAILED) continue;
        const int i = index[r - WAIT_OBJECT_0];
        Slot& s = slots[i];
        DWORD n = 0;
        const bool ok = GetOverlappedResult(s.pipe, &s.ov, &n, FALSE) != FALSE;
        if (!s.reading) {  // a sender connected
            if (!ok) {
                drop(i, false);
                if (!listen(i)) {
                    CloseHandle(s.pipe);
                    s.pipe = INVALID_HANDLE_VALUE;
                }
                continue;
            }
            post(kConnected, i, 0);
            if (!read(i)) {
                drop(i, true);
                listen(i);
            }
            continue;
        }
        if (!ok) {  // the sender went away
            drop(i, true);
            if (!listen(i)) {
                CloseHandle(s.pipe);
                s.pipe = INVALID_HANDLE_VALUE;
            }
            continue;
        }
        s.pending.append(s.buf, n);
        size_t nl;
        while ((nl = s.pending.find('\n')) != std::string::npos) {
            BridgeMessage m;
            if (DecodeBridgeMessage(s.pending.substr(0, nl), m)) {
                auto* copy = new BridgeMessage(std::move(m));
                if (!post(kMessage, i, reinterpret_cast<LPARAM>(copy))) delete copy;
            }
            s.pending.erase(0, nl + 1);
        }
        if (s.pending.size() > kBridgeMaxLine) s.pending.clear();  // garbage without line ends
        if (!read(i)) {
            drop(i, true);
            listen(i);
        }
    }
    for (int i = 0; i < kMaxClients; ++i) {
        Slot& s = slots[i];
        if (s.pipe != INVALID_HANDLE_VALUE) {
            CancelIoEx(s.pipe, &s.ov);
            DWORD n = 0;
            GetOverlappedResult(s.pipe, &s.ov, &n, TRUE);
            DisconnectNamedPipe(s.pipe);
            CloseHandle(s.pipe);
        }
        if (s.ov.hEvent) CloseHandle(s.ov.hEvent);
    }
    LocalFree(sd);
}

}  // namespace gct
