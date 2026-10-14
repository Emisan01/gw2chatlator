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

// Waits for an overlapped operation or the stop event. False = stopped or failed.
bool Await(HANDLE pipe, OVERLAPPED& ov, HANDLE stop, DWORD* bytes) {
    const HANDLE events[2] = {ov.hEvent, stop};
    if (WaitForMultipleObjects(2, events, FALSE, INFINITE) != WAIT_OBJECT_0) {
        CancelIoEx(pipe, &ov);
        GetOverlappedResult(pipe, &ov, bytes, TRUE);
        return false;
    }
    return GetOverlappedResult(pipe, &ov, bytes, FALSE) != FALSE;
}

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
    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    while (ov.hEvent && WaitForSingleObject(stop_, 0) != WAIT_OBJECT_0) {
        // FIRST_PIPE_INSTANCE: if another process (or the other program of ours) holds the name, we wait and retry.
        HANDLE pipe = CreateNamedPipeW(kBridgePipeName,
                                       PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                       PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                       1, 0, 64 * 1024, 0, &sa);
        if (pipe == INVALID_HANDLE_VALUE) {
            WaitForSingleObject(stop_, 5000);
            continue;
        }
        ResetEvent(ov.hEvent);
        DWORD n = 0;
        bool connected = ConnectNamedPipe(pipe, &ov) != FALSE;
        if (!connected) {
            const DWORD err = GetLastError();
            connected = err == ERROR_PIPE_CONNECTED || (err == ERROR_IO_PENDING && Await(pipe, ov, stop_, &n));
        }
        if (connected) {
            PostMessageW(target_, msg_, kConnected, 0);
            std::string pending;
            char buf[4096];
            for (;;) {
                ResetEvent(ov.hEvent);
                n = 0;
                BOOL ok = ReadFile(pipe, buf, sizeof(buf), &n, &ov);
                if (!ok && (GetLastError() != ERROR_IO_PENDING || !Await(pipe, ov, stop_, &n))) break;
                if (ok && !GetOverlappedResult(pipe, &ov, &n, FALSE)) break;
                if (n == 0) continue;
                pending.append(buf, n);
                size_t nl;
                while ((nl = pending.find('\n')) != std::string::npos) {
                    BridgeMessage m;
                    if (DecodeBridgeMessage(pending.substr(0, nl), m)) {
                        auto* copy = new BridgeMessage(std::move(m));
                        if (!PostMessageW(target_, msg_, kMessage, reinterpret_cast<LPARAM>(copy))) delete copy;
                    }
                    pending.erase(0, nl + 1);
                }
                if (pending.size() > kBridgeMaxLine) pending.clear();  // garbage without line ends
            }
            PostMessageW(target_, msg_, kDisconnected, 0);
        }
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
    if (ov.hEvent) CloseHandle(ov.hEvent);
    LocalFree(sd);
}

}  // namespace gct
