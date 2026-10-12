// audio_capture.cpp — WASAPI shared-mode capture on an own MTA thread, 16 kHz mono float to a callback.
#include "win/audio_capture.hpp"

#include "core/i18n.hpp"
#include "core/voice.hpp"

#include <windows.h>

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <propidl.h>
#include <shellapi.h>

#include <cstdio>
#include <cstring>
#include <numeric>

#ifndef AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM
#define AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM 0x80000000
#endif
#ifndef AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY
#define AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY 0x08000000
#endif
#ifndef E_NOTFOUND
#define E_NOTFOUND HRESULT_FROM_WIN32(ERROR_NOT_FOUND)
#endif

namespace gct {
namespace {

// Own copies of the GUIDs: no initguid.h / ksmedia.h / uuid.lib differences between MSVC and MinGW.
const CLSID kMMDeviceEnumerator = {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
const IID kIMMDeviceEnumerator = {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
const IID kIAudioClient = {0x1CB9AD4C, 0xDBFA, 0x4C32, {0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2}};
const IID kIAudioCaptureClient = {0xC8ADBD64, 0xE71E, 0x48A0, {0xA4, 0xDE, 0x18, 0x5C, 0x39, 0x5C, 0xD3, 0x17}};
const GUID kSubtypePcm = {0x00000001, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};
const GUID kSubtypeFloat = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};
const PROPERTYKEY kFriendlyName = {{0xA45C254E, 0xDF1C, 0x4EFD, {0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0}}, 14};

template <typename T>
struct Com {
    T* p = nullptr;
    Com() = default;
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    ~Com() { Reset(); }
    void Reset() {
        if (p) p->Release();
        p = nullptr;
    }
    T* operator->() const { return p; }
    void** Out() {
        Reset();
        return reinterpret_cast<void**>(&p);
    }
};

// COM for a call from any thread: an STA caller (UI thread) keeps its apartment; only what was initialized here is
// uninitialized.
struct ComScope {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ComScope() = default;
    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;
    ~ComScope() {
        if (SUCCEEDED(hr)) CoUninitialize();
    }
};

AudioError MapError(HRESULT hr) {
    if (hr == E_ACCESSDENIED) return AudioError::AccessDenied;
    if (hr == AUDCLNT_E_DEVICE_IN_USE) return AudioError::InUse;
    if (hr == E_NOTFOUND || hr == AUDCLNT_E_DEVICE_INVALIDATED) return AudioError::NoDevice;
    return AudioError::Failed;
}

std::wstring ErrorText(AudioError e, HRESULT hr) {
    switch (e) {
        case AudioError::None: return {};
        case AudioError::NoDevice: return Tr(L"No microphone found.");
        case AudioError::AccessDenied: return Tr(L"Windows blocks the microphone for this program (privacy settings).");
        case AudioError::InUse: return Tr(L"The microphone is used exclusively by another program.");
        case AudioError::Failed: break;
    }
    wchar_t code[32];
    swprintf(code, 32, L"WASAPI error 0x%08X", static_cast<unsigned>(hr));
    return TrF(L"The microphone cannot be opened ({1}).", {code});
}

// The device format when Windows does not convert it for us.
struct InFormat {
    bool isFloat = false;
    int bytes = 0;  // container size of one sample
    int channels = 0;
    int rate = 0;
    int blockAlign = 0;
    bool Supported() const {
        return channels > 0 && rate > 0 && blockAlign >= bytes * channels &&
               (isFloat ? (bytes == 4 || bytes == 8) : (bytes >= 1 && bytes <= 4));
    }
};

InFormat Describe(const WAVEFORMATEX& w) {
    InFormat f;
    bool pcm = w.wFormatTag == WAVE_FORMAT_PCM;
    f.isFloat = w.wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
    if (w.wFormatTag == WAVE_FORMAT_EXTENSIBLE && w.cbSize >= 22) {
        const auto& x = reinterpret_cast<const WAVEFORMATEXTENSIBLE&>(w);
        f.isFloat = IsEqualGUID(x.SubFormat, kSubtypeFloat) != 0;
        pcm = IsEqualGUID(x.SubFormat, kSubtypePcm) != 0;
    }
    if (!f.isFloat && !pcm) return {};
    f.bytes = w.wBitsPerSample / 8;
    f.channels = w.nChannels;
    f.rate = static_cast<int>(w.nSamplesPerSec);
    f.blockAlign = w.nBlockAlign;
    return f;
}

// Raw frames -> interleaved float. Integer samples are left-justified in their container (24-in-32 included), so
// dividing by the container's full scale is right for every valid-bit count.
void AppendDecoded(const BYTE* p, UINT32 frames, const InFormat& f, std::vector<float>& out) {
    const size_t start = out.size();
    out.resize(start + static_cast<size_t>(frames) * f.channels);
    float* o = out.data() + start;
    for (UINT32 i = 0; i < frames; ++i) {
        const BYTE* s = p + static_cast<size_t>(i) * f.blockAlign;
        for (int c = 0; c < f.channels; ++c, s += f.bytes) {
            float v = 0;
            if (f.isFloat && f.bytes == 4) {
                std::memcpy(&v, s, 4);
            } else if (f.isFloat) {
                double d;
                std::memcpy(&d, s, 8);
                v = static_cast<float>(d);
            } else if (f.bytes == 1) {
                v = (static_cast<int>(s[0]) - 128) / 128.0f;  // 8-bit PCM is unsigned
            } else {
                // Little-endian bytes into the top of an int32.
                uint32_t u = 0;
                for (int b = 0; b < f.bytes; ++b) u |= static_cast<uint32_t>(s[b]) << (8 * (4 - f.bytes + b));
                v = static_cast<float>(static_cast<int32_t>(u)) / 2147483648.0f;
            }
            *o++ = v;
        }
    }
}

}  // namespace

AudioCapture::~AudioCapture() { Stop(); }

bool AudioCapture::Start(const std::wstring& deviceId, SamplesFn onSamples, AudioError* error, std::wstring* message) {
    Stop();
    auto fail = [&](AudioError e, const std::wstring& m) {
        if (error) *error = e;
        if (message) *message = m;
        return false;
    };
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    readyEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stopEvent_ || !readyEvent_) {
        const HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        Stop();
        return fail(AudioError::Failed, ErrorText(AudioError::Failed, hr));
    }
    onSamples_ = std::move(onSamples);
    stop_ = false;
    startError_ = AudioError::None;
    startMessage_.clear();
    thread_ = std::thread(&AudioCapture::Run, this, deviceId);
    if (WaitForSingleObject(readyEvent_, 5000) != WAIT_OBJECT_0) {
        Stop();
        return fail(AudioError::Failed, Tr(L"The microphone did not answer in time."));
    }
    if (startError_ != AudioError::None) {
        const AudioError e = startError_;
        const std::wstring m = startMessage_;
        Stop();
        return fail(e, m);
    }
    if (error) *error = AudioError::None;
    if (message) message->clear();
    return true;
}

void AudioCapture::Stop() {
    stop_ = true;
    if (stopEvent_) SetEvent(stopEvent_);
    if (thread_.joinable()) {
        if (thread_.get_id() == std::this_thread::get_id()) return;  // from the callback: the thread ends by itself
        thread_.join();
    }
    if (stopEvent_) CloseHandle(stopEvent_);
    if (readyEvent_) CloseHandle(readyEvent_);
    stopEvent_ = readyEvent_ = nullptr;
    onSamples_ = nullptr;
    running_ = false;
    level_ = 0;
}

void AudioCapture::Run(std::wstring deviceId) {
    const ComScope com;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);  // light work; must not starve under the game
    HRESULT hr = S_OK;
    auto report = [&](AudioError e, const std::wstring& m) {
        startError_ = e;
        startMessage_ = m;
        SetEvent(readyEvent_);
    };
    auto failHr = [&](HRESULT code) {
        const AudioError e = MapError(code);
        report(e, ErrorText(e, code));
    };

    HANDLE audioEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    Com<IMMDeviceEnumerator> enumerator;
    Com<IMMDevice> device;
    Com<IAudioClient> client;
    Com<IAudioCaptureClient> capture;
    WAVEFORMATEX* mix = nullptr;
    InFormat in;
    bool convert = false;
    bool started = false;

    do {
        if (FAILED(com.hr)) { failHr(com.hr); break; }
        if (!audioEvent) { failHr(HRESULT_FROM_WIN32(GetLastError())); break; }
        hr = CoCreateInstance(kMMDeviceEnumerator, nullptr, CLSCTX_ALL, kIMMDeviceEnumerator, enumerator.Out());
        if (FAILED(hr)) { failHr(hr); break; }
        hr = deviceId.empty() ? enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &device.p)
                              : enumerator->GetDevice(deviceId.c_str(), &device.p);
        if (FAILED(hr) || !device.p) { failHr(FAILED(hr) ? hr : E_NOTFOUND); break; }
        DWORD state = 0;
        if (SUCCEEDED(device->GetState(&state)) && state != DEVICE_STATE_ACTIVE) { failHr(E_NOTFOUND); break; }

        // 16 kHz mono float, converted by Windows (AUTOCONVERTPCM) — the format the recognizer wants.
        WAVEFORMATEXTENSIBLE want{};
        want.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        want.Format.nChannels = 1;
        want.Format.nSamplesPerSec = kVoiceRate;
        want.Format.wBitsPerSample = 32;
        want.Format.nBlockAlign = 4;
        want.Format.nAvgBytesPerSec = kVoiceRate * 4;
        want.Format.cbSize = 22;  // the extensible part
        want.Samples.wValidBitsPerSample = 32;
        want.dwChannelMask = SPEAKER_FRONT_CENTER;
        want.SubFormat = kSubtypeFloat;
        const REFERENCE_TIME buffer = 2000000;  // 200 ms (100 ns units)
        hr = device->Activate(kIAudioClient, CLSCTX_ALL, nullptr, client.Out());
        if (FAILED(hr)) { failHr(hr); break; }
        hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                                    AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                                buffer, 0, &want.Format, nullptr);
        if (FAILED(hr)) {
            if (MapError(hr) != AudioError::Failed) { failHr(hr); break; }
            // Refused: open the device in its own format (a fresh client — a failed Initialize may leave it unusable)
            // and convert here.
            hr = device->Activate(kIAudioClient, CLSCTX_ALL, nullptr, client.Out());
            if (SUCCEEDED(hr)) hr = client->GetMixFormat(&mix);
            if (FAILED(hr)) { failHr(hr); break; }
            in = Describe(*mix);
            if (!in.Supported()) { report(AudioError::Failed, Tr(L"The microphone's sound format is not supported.")); break; }
            hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, buffer, 0, mix, nullptr);
            if (FAILED(hr)) { failHr(hr); break; }
            convert = true;
        }
        hr = client->SetEventHandle(audioEvent);
        if (SUCCEEDED(hr)) hr = client->GetService(kIAudioCaptureClient, capture.Out());
        if (SUCCEEDED(hr)) hr = client->Start();
        if (FAILED(hr)) { failHr(hr); break; }
        started = true;
        running_ = true;
        report(AudioError::None, {});
    } while (false);

    if (started) {
        std::vector<float> out, pending, tail;
        // Conversion in whole resampling periods (48 kHz: 3 frames -> 1 sample; 44.1 kHz: 441 -> 160), so every chunk
        // starts at the same phase; odd rates with long periods are converted as they come.
        size_t period = 1;
        if (convert) {
            period = static_cast<size_t>(in.rate / std::gcd(in.rate, kVoiceRate));
            if (period > static_cast<size_t>(in.rate / 10)) period = 1;
        }
        const size_t tailMax = kVoiceRate / 20;  // 50 ms for the level meter
        HANDLE waits[2] = {stopEvent_, audioEvent};
        bool lost = false;
        while (!lost) {
            const DWORD w = WaitForMultipleObjects(2, waits, FALSE, 2000);
            if (w == WAIT_OBJECT_0 || w == WAIT_FAILED) break;
            out.clear();
            for (;;) {
                UINT32 packet = 0;
                hr = capture->GetNextPacketSize(&packet);
                if (FAILED(hr)) { lost = true; break; }  // device unplugged / invalidated
                if (packet == 0) break;
                BYTE* data = nullptr;
                UINT32 frames = 0;
                DWORD flags = 0;
                hr = capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
                if (FAILED(hr)) { lost = true; break; }
                if (hr == AUDCLNT_S_BUFFER_EMPTY) break;
                const bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
                if (!convert) {
                    const float* f = reinterpret_cast<const float*>(data);
                    if (silent) out.insert(out.end(), frames, 0.0f);
                    else out.insert(out.end(), f, f + frames);
                } else if (silent) {
                    pending.insert(pending.end(), static_cast<size_t>(frames) * in.channels, 0.0f);
                } else {
                    AppendDecoded(data, frames, in, pending);
                }
                capture->ReleaseBuffer(frames);
            }
            if (convert) {
                const size_t use = pending.size() / in.channels / period * period;
                if (use > 0) {
                    const std::vector<float> mono = ToMono16k(pending.data(), use, in.channels, in.rate);
                    out.insert(out.end(), mono.begin(), mono.end());
                    pending.erase(pending.begin(), pending.begin() + static_cast<ptrdiff_t>(use * in.channels));
                }
            }
            if (out.empty()) continue;
            tail.insert(tail.end(), out.begin(), out.end());
            if (tail.size() > tailMax) tail.erase(tail.begin(), tail.end() - static_cast<ptrdiff_t>(tailMax));
            level_ = LevelMeter(tail.data(), tail.size(), 50);
            if (!stop_ && onSamples_) onSamples_(out.data(), out.size());
        }
        client->Stop();
    }
    running_ = false;
    level_ = 0;

    if (mix) CoTaskMemFree(mix);
    capture.Reset();
    client.Reset();
    device.Reset();
    enumerator.Reset();
    if (audioEvent) CloseHandle(audioEvent);
}

std::vector<AudioDevice> AudioCapture::Devices() {
    std::vector<AudioDevice> list;
    const ComScope com;  // an STA caller (RPC_E_CHANGED_MODE) can create the enumerator just as well
    Com<IMMDeviceEnumerator> enumerator;
    Com<IMMDeviceCollection> devices;
    if (FAILED(CoCreateInstance(kMMDeviceEnumerator, nullptr, CLSCTX_ALL, kIMMDeviceEnumerator, enumerator.Out())) ||
        FAILED(enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &devices.p)))
        return list;
    UINT count = 0;
    devices->GetCount(&count);
    for (UINT i = 0; i < count; ++i) {
        Com<IMMDevice> device;
        if (FAILED(devices->Item(i, &device.p))) continue;
        AudioDevice d;
        LPWSTR id = nullptr;
        if (FAILED(device->GetId(&id)) || !id) continue;
        d.id = id;
        CoTaskMemFree(id);
        Com<IPropertyStore> props;
        if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &props.p))) {
            PROPVARIANT v;
            PropVariantInit(&v);
            if (SUCCEEDED(props->GetValue(kFriendlyName, &v)) && v.vt == VT_LPWSTR && v.pwszVal) d.name = v.pwszVal;
            PropVariantClear(&v);
        }
        if (d.name.empty()) d.name = d.id;
        list.push_back(std::move(d));
    }
    return list;
}

void OpenMicrophonePrivacySettings() {
    ShellExecuteW(nullptr, L"open", L"ms-settings:privacy-microphone", nullptr, nullptr, SW_SHOWNORMAL);
}

}  // namespace gct
