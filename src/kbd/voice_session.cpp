// voice_session.cpp — placeholder; the implementation follows (see voice_session.hpp).
#include "kbd/voice_session.hpp"

#include "kbd/whisper_engine.hpp"

namespace gct {

VoiceSession::VoiceSession(HWND notify, UINT message) : notify_(notify), message_(message) {}
VoiceSession::~VoiceSession() = default;
bool VoiceSession::Available(std::wstring* error) { return WhisperEngine::RuntimeAvailable(error); }
bool VoiceSession::Begin(const VoiceSettings&, unsigned*, std::wstring* message, AudioError* audioError) {
    if (message) *message = L"not built yet";
    if (audioError) *audioError = AudioError::None;
    return false;
}
void VoiceSession::End() {}
void VoiceSession::Cancel() {}
float VoiceSession::Level() const { return 0; }
int VoiceSession::RecordedMs() const { return 0; }
void VoiceSession::UnloadIfIdle(int) {}
VoiceSession::Stats VoiceSession::GetStats() const { return {}; }
void VoiceSession::RunTest(const VoiceSettings&, int, HWND, UINT) {}
void VoiceSession::Worker() {}
void VoiceSession::Post(std::unique_ptr<VoiceEvent>) {}

}  // namespace gct
