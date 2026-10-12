// voice_session.hpp — one push-to-talk recording at a time for GW2 Keyboard: the microphone (AudioCapture), the
// recognizer (WhisperEngine) on one low-priority worker thread, live passes while speaking (optional) and the final
// pass after release. Results reach the window only by PostMessage (VoiceEvent, owned by the receiver); nothing here
// touches the UI, the input box or the game, and nothing is ever sent – the user checks the text and presses Enter.
#pragma once

#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/voice.hpp"
#include "win/audio_capture.hpp"

namespace gct {

class WhisperEngine;

struct VoiceSettings {
    std::wstring modelPath;     // installed model file
    std::string language;       // whisper code; "" = detect
    bool translate = false;     // into English (whisper)
    int threads = 2;            // recognizer threads (VoiceThreads)
    bool live = true;           // live passes while speaking
    int maxSeconds = 30;        // then the recording ends by itself (Final follows, `maxReached`)
    std::wstring deviceId;      // "" = Windows default microphone
    std::string initialPrompt;  // UTF-8, may be empty
};

struct VoiceEvent {
    enum class Kind {
        Partial,  // live text while speaking: `live`
        Final,    // after release: `text` (may be empty: nothing recognized / too short / silence -> `empty`)
        Error,    // `message` (+ `audioError` for the microphone); the session is over
    } kind = Kind::Final;
    unsigned session = 0;  // drop events of older sessions
    LiveText live;
    std::wstring text;
    bool empty = false;       // Final without text (too short, silence, nothing recognized)
    bool maxReached = false;  // Final because the maximum length was reached
    AudioError audioError = AudioError::None;
    std::wstring message;
    int recordMs = 0;     // length of the recording
    int recognizeMs = 0;  // time of the final pass (whisper_full)
    std::string language; // whisper code that was used / detected
};

class VoiceSession {
public:
    // Events are posted to `notify` as (message, 0, VoiceEvent*).
    VoiceSession(HWND notify, UINT message);
    ~VoiceSession();  // cancels, joins the worker, frees the model
    VoiceSession(const VoiceSession&) = delete;
    VoiceSession& operator=(const VoiceSession&) = delete;

    // The recognizer DLLs are there (WhisperEngine::RuntimeAvailable).
    static bool Available(std::wstring* error = nullptr);

    // Opens the microphone and starts recording at once; the model loads meanwhile on the worker (if not loaded).
    // False + message (and audioError) when the microphone cannot be opened or the model file is missing; then no
    // event follows. Returns the new session id via `session`.
    bool Begin(const VoiceSettings& s, unsigned* session, std::wstring* message, AudioError* audioError);
    // Stops recording; the final pass runs on the worker and posts exactly one Final or Error event.
    void End();
    // Stops and drops everything of the current session; no further event of it.
    void Cancel();

    bool Recording() const { return recording_; }
    bool Busy() const { return recording_ || recognizing_; }  // recording or final pass still running
    float Level() const;        // microphone level 0..1 while recording
    int RecordedMs() const;     // so far
    unsigned Session() const { return session_; }

    // Frees the model after `minutes` without use (call from a UI timer; cheap).
    void UnloadIfIdle(int minutes);

    struct Stats {
        bool loaded = false;
        std::wstring model;       // file name
        int lastRecordMs = 0, lastRecognizeMs = 0;
        double lastRealtimeFactor = 0;  // recognizeMs / recordMs
        int loadMs = 0;
        std::string systemInfo;  // whisper_print_system_info
    };
    Stats GetStats() const;

    // Settings → Voice → "Test": records `seconds` from the microphone, recognizes it with `s`, and posts one
    // std::wstring* (owned by the receiver) with the text, latency and real-time factor to (notify, message).
    // Runs on its own thread; the session must not be recording.
    void RunTest(const VoiceSettings& s, int seconds, HWND notify, UINT message);

private:
    void Worker();
    void Post(std::unique_ptr<VoiceEvent> e);

    HWND notify_;
    UINT message_;
    AudioCapture capture_;
    std::unique_ptr<WhisperEngine> engine_;
    VoiceSettings settings_;

    mutable std::mutex mu_;  // guards everything below that is not atomic
    std::condition_variable cv_;
    std::vector<float> audio_;  // the current recording (16 kHz mono)
    std::thread worker_;
    bool quit_ = false;
    bool finalWanted_ = false;
    bool liveRunning_ = false;
    std::wstring lastLive_;
    std::atomic<bool> recording_{false};
    std::atomic<bool> recognizing_{false};
    std::atomic<bool> abort_{false};
    std::atomic<unsigned> session_{0};
    unsigned long long beginTick_ = 0, lastUseTick_ = 0;
    Stats stats_;
};

}  // namespace gct
