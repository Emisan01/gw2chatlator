// voice.cpp — placeholder; the implementation follows (see voice.hpp).
#include "core/voice.hpp"

namespace gct {

int VoiceModelInfo::QualityFor(const std::string&) const { return 0; }
const VoiceModelInfo* VoiceCatalog::Find(const std::string&) const { return nullptr; }
const VoiceModelInfo* VoiceCatalog::Recommended() const { return nullptr; }
const char* DefaultVoiceCatalogJson() { return "{}"; }
bool ParseVoiceCatalog(const std::string&, VoiceCatalog*) { return false; }
SpeechSpan FindSpeech(const float*, size_t, const SpeechOptions&) { return {}; }
float LevelMeter(const float*, size_t, int) { return 0; }
std::wstring CleanTranscript(const std::string&) { return {}; }
LiveText SplitLive(const std::wstring&, const std::wstring& newest) { return {L"", newest}; }
std::string WhisperLang(const std::wstring&) { return {}; }
int WhisperAudioCtx(size_t) { return 0; }
int VoiceThreads(int setting, unsigned) { return setting > 0 ? setting : 1; }
std::vector<float> ToMono16k(const float*, size_t, int, int) { return {}; }

}  // namespace gct
