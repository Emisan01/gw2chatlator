// kb_voice_helper.hpp — the voice helper process gw2kb_voice.exe: pipes, its events read on our own thread.
// Included by kb_shared.hpp at its place (it relies on what stands before it there) – include that, not this.
#pragma once

namespace kb {

// ---------------------------------------------------------------------------------------------------------------
// The voice helper: a hidden process with pipes; its events are read on our own thread.
class VoiceHelper {
public:
    bool Start(const std::wstring& exe, const std::wstring& dataDir) {
        Stop();
        SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
        HANDLE inRead = nullptr, outWrite = nullptr;
        if (!CreatePipe(&inRead, &inWrite_, &sa, 0)) return false;
        if (!CreatePipe(&outRead_, &outWrite, &sa, 0)) {
            CloseHandle(inRead);
            Close();
            return false;
        }
        SetHandleInformation(inWrite_, HANDLE_FLAG_INHERIT, 0);  // our ends stay ours
        SetHandleInformation(outRead_, HANDLE_FLAG_INHERIT, 0);
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = inRead;
        si.hStdOutput = outWrite;
        si.hStdError = outWrite;
        std::wstring cmd = L"\"" + exe + L"\" --data \"" + dataDir + L"\"";
        PROCESS_INFORMATION pi{};
        const BOOL ok = CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, TRUE,
                                       CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, nullptr, nullptr, &si, &pi);
        CloseHandle(inRead);
        CloseHandle(outWrite);
        if (!ok) {
            Close();
            return false;
        }
        CloseHandle(pi.hThread);
        process_ = pi.hProcess;
        reader_ = std::thread([this] { Read(); });
        return true;
    }
    void Send(const char* cmd) {
        if (!inWrite_) return;
        const std::string line = std::string(cmd) + "\n";
        DWORD n = 0;
        WriteFile(inWrite_, line.data(), static_cast<DWORD>(line.size()), &n, nullptr);
    }
    void Stop() {
        if (process_) {
            Send("quit");
            if (WaitForSingleObject(process_, 2000) != WAIT_OBJECT_0) TerminateProcess(process_, 0);
        }
        Close();
        if (reader_.joinable()) reader_.join();  // its pipe is closed: ReadFile returns
    }
    bool Running() const { return process_ != nullptr; }

private:
    void Close() {
        if (inWrite_) CloseHandle(inWrite_);
        if (process_) CloseHandle(process_);
        inWrite_ = process_ = nullptr;
        if (outRead_ && !reader_.joinable()) {
            CloseHandle(outRead_);
            outRead_ = nullptr;
        }
    }
    void Read() {
        std::string pending;
        char buf[2048];
        DWORD n = 0;
        while (ReadFile(outRead_, buf, sizeof(buf), &n, nullptr) && n > 0) {
            pending.append(buf, n);
            size_t nl;
            while ((nl = pending.find('\n')) != std::string::npos) {
                const std::string line = pending.substr(0, nl);
                pending.erase(0, nl + 1);
                try {
                    OnLine(line);
                } catch (...) {
                }
            }
        }
        CloseHandle(outRead_);
        outRead_ = nullptr;
        std::lock_guard<std::mutex> lock(g_mu);
        g_voiceReady = g_recording = false;
        g_voiceLive.clear();
    }
    static void OnLine(const std::string& line);
    HANDLE process_ = nullptr, inWrite_ = nullptr, outRead_ = nullptr;
    std::thread reader_;
};

extern VoiceHelper g_voice;

}  // namespace kb
