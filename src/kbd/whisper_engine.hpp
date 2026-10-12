// whisper_engine.hpp — speech to text with whisper.cpp, CPU only, on this PC.
// whisper.dll and the ggml DLLs live in <exe>\voice and are delay-loaded: GW2 Keyboard starts and types without
// them; voice input is offered only when RuntimeAvailable() is true. Call Load/Transcribe/Unload from one worker
// thread at a time (VoiceSession's); RuntimeAvailable may be called from any thread.
#pragma once

#include <atomic>
#include <cstddef>
#include <string>

namespace gct {

class WhisperEngine {
public:
    struct Options {
        std::string language;       // whisper code ("de"); "" = detect
        bool translate = false;     // speak any language, get English (whisper's own translation)
        int threads = 4;
        std::string initialPrompt;  // UTF-8; steers spelling of names / GW2 words; "" = none
        int audioCtx = 0;           // encoder context (WhisperAudioCtx); 0 = full 30 s window
    };
    struct Result {
        bool ok = false;
        bool aborted = false;
        std::string text;      // UTF-8, all segments joined, not cleaned (CleanTranscript does that)
        std::string language;  // detected / used whisper code
        int ms = 0;            // time of whisper_full
        std::wstring error;
    };

    WhisperEngine() = default;
    ~WhisperEngine();
    WhisperEngine(const WhisperEngine&) = delete;
    WhisperEngine& operator=(const WhisperEngine&) = delete;

    // <exe>\voice\whisper.dll loads (by full path, before any delay-loaded call) and a CPU backend registers
    // (ggml_backend_load_all_from_path(<exe>\voice)). Cached after the first call. False + error when missing.
    static bool RuntimeAvailable(std::wstring* error = nullptr);
    static std::wstring RuntimeDir();  // <exe>\voice
    static std::string SystemInfo();   // whisper_print_system_info() ("" without runtime)

    // Loads the model (no-op when that file is already loaded). False + error when the file is missing or broken.
    bool Load(const std::wstring& modelPath, std::wstring* error);
    void Unload();
    bool Loaded() const { return ctx_ != nullptr; }
    const std::wstring& ModelPath() const { return modelPath_; }

    // Recognizes 16 kHz mono float audio. `abort` (may be null) stops it early (Result.aborted).
    Result Transcribe(const float* pcm, size_t samples, const Options& o, const std::atomic<bool>* abort);

private:
    void* ctx_ = nullptr;  // whisper_context*
    std::wstring modelPath_;
};

}  // namespace gct
