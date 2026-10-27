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
    unsigned long long affinity = 0;  // logical processors to recognize on (PickVoiceCores); 0 = any
    float gainDb = 0;           // microphone boost applied to what is recorded
    bool levelSpeech = false;
    int autoEndMs = 0;          // > 0: the recording ends by itself after this much silence following speech
    float thresholdDb = -40;    // voice activation: a 20 ms frame above this level (dBFS) counts as speech ...
    int minSpeechMs = 250;      // ... this much of it starts a recording ...
    int preRollMs = 300;        // ... with this much from before it, so the first word is not lost   // automatic boost: quiet speech is raised to a good level before recognition
};

struct VoiceEvent {
    enum class Kind {
        Partial,  // live text while speaking: `live`
        Final,    // after release: `text` (may be empty: nothing recognized / too short / silence -> `empty`)
        Error,    // `message` (+ `audioError` for the microphone); the session is over
        Started,  // voice activation heard speech: a recording runs now (`session` is its id)
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
    // Voice activation: the microphone stays open; a recording starts by itself when speech is heard (with the
    // pre-roll), ends after the pause (settings.autoEndMs), posts Started / Partial / Final like any other, and then it
    // listens again – until Cancel(). False + message when the microphone or the model cannot be used.
    bool Listen(const VoiceSettings& s, std::wstring* message, AudioError* audioError);
    bool Listening() const { return listening_; }
    // Ends the running recording by itself after `ms` of silence once something was said (0 = only by End()).
    void SetAutoEnd(int ms) { autoEndMs_ = ms; }

    bool Recording() const { return recording_; }
    bool Busy() const { return recording_ || recognizing_; }  // recording or final pass still running
    float Level() const;        // microphone level 0..1 while recording
    unsigned Session() const { return session_; }


    struct Stats {
        bool loaded = false;
        std::wstring model;       // file name
        int lastRecordMs = 0, lastRecognizeMs = 0;
        double lastRealtimeFactor = 0;  // recognizeMs / recordMs
        int loadMs = 0;
        std::string systemInfo;  // whisper_print_system_info
    };

    // Settings → Voice → "Test": records `seconds` from the microphone, recognizes it with `s`, and posts one
    // std::wstring* (owned by the receiver) with the text, latency and real-time factor to (notify, message).
    // Runs on its own thread; the session must not be recording.
    void RunTest(const VoiceSettings& s, int seconds, HWND notify, UINT message);

private:
    void Worker();
    void Post(std::unique_ptr<VoiceEvent> e);
    void EnsureWorker();                // UI thread: starts the worker on first use
    void StopCapture(unsigned session);  // stops the microphone if `session` still owns it

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
    float gain_ = 1.0f;
    std::atomic<int> autoEndMs_{0};
    std::atomic<bool> listening_{false};
    std::vector<float> ring_;  // voice activation: the last moments before speech (under mu_)  // microphone boost as a factor (set in Begin)
    std::atomic<bool> recording_{false};
    std::atomic<bool> recognizing_{false};
    std::atomic<bool> abort_{false};
    std::atomic<unsigned> session_{0};
    unsigned long long beginTick_ = 0, lastUseTick_ = 0;
    Stats stats_;
    bool maxReached_ = false;                 // the recording hit settings_.maxSeconds (the worker ends it)
    std::deque<std::function<void()>> jobs_;  // run on the worker while nothing is recorded (test, unloading)
    // capture_ is started / stopped by the UI thread and the worker: always under captureMu_, which is taken before
    // mu_ and never while holding it (the capture callback takes mu_; Stop() waits for the callback).
    std::mutex captureMu_;
    std::thread testThread_;                  // RunTest's recording
    std::atomic<bool> testRunning_{false}, testCancel_{false};
};

}  // namespace gct
