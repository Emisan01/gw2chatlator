// voice_models.cpp — placeholder; the implementation follows (see voice_models.hpp).
#include "win/voice_models.hpp"

namespace gct {

std::wstring VoiceModelPath(const std::wstring& dir, const VoiceModelInfo&) { return dir; }
bool VoiceModelInstalled(const std::wstring&, const VoiceModelInfo&) { return false; }
bool DownloadVoiceModel(const std::wstring&, const VoiceModelInfo&, const std::function<bool(uint64_t, uint64_t)>&,
                        const std::atomic<bool>*, std::wstring* error) {
    if (error) *error = L"not built yet";
    return false;
}
bool DeleteVoiceModel(const std::wstring&, const VoiceModelInfo&) { return false; }
VoiceCatalog LoadVoiceCatalog(const std::wstring&) { return {}; }
VoiceHardware ProbeVoiceHardware() { return {}; }
uint64_t FreeDiskBytes(const std::wstring&) { return 0; }

}  // namespace gct
