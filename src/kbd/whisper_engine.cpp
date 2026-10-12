// whisper_engine.cpp — placeholder until whisper.cpp is built (see whisper_engine.hpp): no runtime, nothing loads.
#include "kbd/whisper_engine.hpp"

namespace gct {

WhisperEngine::~WhisperEngine() { Unload(); }
bool WhisperEngine::RuntimeAvailable(std::wstring* error) {
    if (error) *error = L"voice recognizer not built";
    return false;
}
std::wstring WhisperEngine::RuntimeDir() { return {}; }
std::string WhisperEngine::SystemInfo() { return {}; }
bool WhisperEngine::Load(const std::wstring&, std::wstring* error) {
    if (error) *error = L"voice recognizer not built";
    return false;
}
void WhisperEngine::Unload() { ctx_ = nullptr; modelPath_.clear(); }
WhisperEngine::Result WhisperEngine::Transcribe(const float*, size_t, const Options&, const std::atomic<bool>*) {
    Result r;
    r.error = L"voice recognizer not built";
    return r;
}

}  // namespace gct
