// audio_capture.hpp — the microphone via WASAPI (shared mode, event driven) as 16 kHz mono float.
// Own thread with its own COM apartment (MTA); the callback runs on that thread and must only copy. Nothing is stored
// on disk. Windows converts the format (AUTOCONVERTPCM); if that is refused, the device format is converted here.
#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace gct {

struct AudioDevice {
    std::wstring id;    // endpoint id ("" = Windows default)
    std::wstring name;  // friendly name
};

enum class AudioError {
    None,
    NoDevice,       // no microphone (or the chosen one is gone)
    AccessDenied,   // Windows privacy settings block the microphone (E_ACCESSDENIED) -> ms-settings:privacy-microphone
    InUse,          // the device is used exclusively by another program
    Failed,         // anything else (text in `message`)
};

class AudioCapture {
public:
    using SamplesFn = std::function<void(const float* pcm16k, size_t samples)>;

    AudioCapture() = default;
    ~AudioCapture();
    AudioCapture(const AudioCapture&) = delete;
    AudioCapture& operator=(const AudioCapture&) = delete;

    // Starts the capture thread and waits until the device is open (or failed, then false + error). deviceId "" =
    // Windows default capture device (console role).
    bool Start(const std::wstring& deviceId, SamplesFn onSamples, AudioError* error, std::wstring* message);
    // Stops and joins the thread; after it returns no callback runs any more. Safe to call twice.
    void Stop();
    bool Running() const { return running_; }
    // Level of the newest audio, 0..1 (see LevelMeter), for the meter on the microphone button.
    float Level() const { return level_; }

    // Capture devices that are active, the default first is not guaranteed; empty on failure.
    static std::vector<AudioDevice> Devices();

private:
    void Run(std::wstring deviceId);
    SamplesFn onSamples_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_{false};
    std::atomic<float> level_{0};
    void* stopEvent_ = nullptr;  // HANDLE, wakes the thread
    // Start() handshake
    void* readyEvent_ = nullptr;  // HANDLE
    AudioError startError_ = AudioError::None;
    std::wstring startMessage_;
};


}  // namespace gct
