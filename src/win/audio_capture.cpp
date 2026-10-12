// audio_capture.cpp — placeholder; the implementation follows (see audio_capture.hpp).
#include "win/audio_capture.hpp"

namespace gct {

AudioCapture::~AudioCapture() { Stop(); }
bool AudioCapture::Start(const std::wstring&, SamplesFn, AudioError* error, std::wstring* message) {
    if (error) *error = AudioError::Failed;
    if (message) *message = L"not built yet";
    return false;
}
void AudioCapture::Stop() {}
void AudioCapture::Run(std::wstring) {}
std::vector<AudioDevice> AudioCapture::Devices() { return {}; }
void OpenMicrophonePrivacySettings() {}

}  // namespace gct
