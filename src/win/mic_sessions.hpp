// mic_sessions.hpp — keeps a dictation out of voice chats: while GW2 Keyboard records, the microphone streams of
// chosen other programs (Discord, TeamSpeak, Mumble ...) are muted through Windows' own audio sessions and unmuted
// right after. Only sessions that were not muted before are touched and given back; the names are also written to the
// settings file so the next start can unmute them after a crash. Nothing is installed, nothing runs in any program.
#pragma once

#include <string>
#include <vector>

namespace gct {

// Programs that have an open microphone stream now (exe names, without this program), on the default or given device.
std::vector<std::wstring> AppsUsingMicrophone(const std::wstring& deviceId);

class MicMuter {
public:
    ~MicMuter() { Restore(); }
    // Mutes the microphone streams of the programs in `exeNames` (case-insensitive, e.g. "Discord.exe"). Returns the
    // names that were muted now (what to unmute after a crash).
    std::vector<std::wstring> Mute(const std::wstring& deviceId, const std::vector<std::wstring>& exeNames);
    // Unmutes what Mute() muted. Safe to call twice.
    void Restore();
    bool Active() const { return !muted_.empty(); }

private:
    struct Muted {
        unsigned long pid;
        std::wstring exe;
    };
    std::wstring device_;
    std::vector<Muted> muted_;
};

// After a crash: unmutes the microphone streams of these programs (what was muted when the program ended).
void UnmuteMicrophoneOf(const std::vector<std::wstring>& exeNames);

}  // namespace gct
