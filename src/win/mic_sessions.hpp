// mic_sessions.hpp — which programs have a microphone stream open (Windows' audio sessions; no handle to any other
// process, nothing in them is touched). GW2 Keyboard pauses voice activation while a voice chat (Discord, TeamSpeak ...)
// listens: what you say then is meant for the voice chat.
#pragma once

#include <string>
#include <vector>

namespace gct {

// Programs with an open microphone stream now (exe names, without this program), on the default or given device.
std::vector<std::wstring> AppsUsingMicrophone(const std::wstring& deviceId);

}  // namespace gct
