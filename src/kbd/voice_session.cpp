// voice_session.cpp — push-to-talk recordings for GW2 Keyboard (see voice_session.hpp).
//
// Threads: the UI thread calls Begin / End / Cancel / RunTest; the capture thread only appends samples (under mu_);
// one worker thread owns the WhisperEngine and does every recognizer call (load, live passes, final pass, test,
// unload). The audio lives only in memory and is dropped after the final pass.
#include "kbd/voice_session.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cwchar>

#include "core/i18n.hpp"
#include "core/text.hpp"
#include "kbd/whisper_engine.hpp"

namespace gct {
namespace {

constexpr size_t kMinLiveSamples = kVoiceRate;  // 1 s of audio before the first live pass
constexpr ULONGLONG kLiveGapMs = 400;           // from the end of one live pass to the next
constexpr ULONGLONG kLiveRecheckMs = 150;       // no speech yet: look again after this

int SamplesToMs(size_t n) { return static_cast<int>(static_cast<unsigned long long>(n) * 1000ull / kVoiceRate); }

std::wstring FileName(const std::wstring& path) {
    const size_t cut = path.find_last_of(L"\\/");
    return cut == std::wstring::npos ? path : path.substr(cut + 1);
}

bool FileExists(const std::wstring& path) {
    const DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

WhisperEngine::Options OptionsFor(const VoiceSettings& s, size_t samples) {
    WhisperEngine::Options o;
    o.language = s.language;
    o.translate = s.translate;
    o.threads = std::max(1, s.threads);
    o.initialPrompt = s.initialPrompt;
    o.audioCtx = WhisperAudioCtx(samples);  // always: the full 30 s context is 4-6x slower for short clips
    return o;
}

std::wstring Decimal(double v, int digits) {
    wchar_t b[32];
    swprintf(b, 32, L"%.*f", digits, v);
    return b;
}

void PostString(HWND notify, UINT message, const std::wstring& text) {
    auto* s = new std::wstring(text);
    if (!PostMessageW(notify, message, 0, reinterpret_cast<LPARAM>(s))) delete s;
}

// Worker thread only. No-op when that model is loaded already; `ms` = load time.
bool LoadModel(WhisperEngine& engine, const std::wstring& path, std::wstring* error, int* ms) {
    *ms = 0;
    if (engine.Loaded() && engine.ModelPath() == path) return true;
    const auto t0 = std::chrono::steady_clock::now();
    const bool ok = engine.Load(path, error);
    *ms = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count());
    return ok;
}

// While whisper runs: only on the chosen (least busy) cores and below normal priority – its helper threads included, so
// GW2 always wins when both want a core. Restored afterwards.
class CpuBudget {
public:
    explicit CpuBudget(unsigned long long mask) {
        HANDLE self = GetCurrentProcess();
        DWORD_PTR sys = 0;
        if (mask && GetProcessAffinityMask(self, &oldMask_, &sys) && (mask & sys))
            setMask_ = SetProcessAffinityMask(self, static_cast<DWORD_PTR>(mask & sys)) != FALSE;
        oldClass_ = GetPriorityClass(self);
        if (oldClass_ == NORMAL_PRIORITY_CLASS) SetPriorityClass(self, BELOW_NORMAL_PRIORITY_CLASS);
    }
    ~CpuBudget() {
        HANDLE self = GetCurrentProcess();
        if (setMask_) SetProcessAffinityMask(self, oldMask_);
        if (oldClass_ == NORMAL_PRIORITY_CLASS) SetPriorityClass(self, NORMAL_PRIORITY_CLASS);
    }
    CpuBudget(const CpuBudget&) = delete;
    CpuBudget& operator=(const CpuBudget&) = delete;

private:
    DWORD_PTR oldMask_ = 0;
    DWORD oldClass_ = 0;
    bool setMask_ = false;
};

WhisperEngine::Result Recognize(WhisperEngine& engine, const VoiceSettings& s, const float* pcm, size_t n,
                                const std::atomic<bool>* abort) {
    const CpuBudget budget(s.affinity);
    if (s.levelSpeech && n > 0) {  // automatic boost: the loudest part of the speech to about -18 dBFS (at most x8)
        const SpeechSpan sp = FindSpeech(pcm, n);
        const float gain = std::clamp(std::pow(10.0f, (-18.0f - sp.peakDb) / 20.0f), 1.0f, 8.0f);
        if (gain > 1.05f) {
            std::vector<float> loud(pcm, pcm + n);
            for (float& v : loud) v = std::clamp(v * gain, -1.0f, 1.0f);
            return engine.Transcribe(loud.data(), n, OptionsFor(s, n), abort);
        }
    }
    return engine.Transcribe(pcm, n, OptionsFor(s, n), abort);
}

}  // namespace

VoiceSession::VoiceSession(HWND notify, UINT message)
    : notify_(notify), message_(message), engine_(std::make_unique<WhisperEngine>()) {}

VoiceSession::~VoiceSession() {
    Cancel();
    testCancel_ = true;
    if (testThread_.joinable()) testThread_.join();
    {
        std::lock_guard<std::mutex> lk(mu_);
        quit_ = true;
        abort_ = true;  // a running pass (test included) stops at the next graph
        jobs_.clear();
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();  // the worker frees the model on its way out
}

bool VoiceSession::Available(std::wstring* error) { return WhisperEngine::RuntimeAvailable(error); }

void VoiceSession::EnsureWorker() {
    if (!worker_.joinable()) worker_ = std::thread([this] { Worker(); });
}

void VoiceSession::Post(std::unique_ptr<VoiceEvent> e) {
    if (!e || !notify_) return;
    if (PostMessageW(notify_, message_, 0, reinterpret_cast<LPARAM>(e.get()))) e.release();
}

bool VoiceSession::Begin(const VoiceSettings& s, unsigned* session, std::wstring* message, AudioError* audioError) {
    auto fail = [&](AudioError e, const std::wstring& m) {
        if (message) *message = m;
        if (audioError) *audioError = e;
        return false;
    };
    if (message) message->clear();
    if (audioError) *audioError = AudioError::None;
    std::wstring why;
    if (!Available(&why)) return fail(AudioError::None, why);
    if (s.modelPath.empty() || !FileExists(s.modelPath))
        return fail(AudioError::None, TrF(L"The voice model is missing: {1}", {s.modelPath}));
    Cancel();  // whatever ran before ends here, without an event
    EnsureWorker();

    std::lock_guard<std::mutex> cap(captureMu_);
    unsigned id;
    {
        std::lock_guard<std::mutex> lk(mu_);
        id = ++session_;
        settings_ = s;
        gain_ = std::pow(10.0f, std::clamp(s.gainDb, 0.0f, 24.0f) / 20.0f);
        autoEndMs_ = s.autoEndMs;
        settings_.maxSeconds = std::clamp(s.maxSeconds, 1, 600);
        audio_.clear();
        audio_.reserve(static_cast<size_t>(settings_.maxSeconds) * kVoiceRate);
        lastLive_.clear();
        finalWanted_ = false;
        maxReached_ = false;
        recognizing_ = false;
        recording_ = true;  // before Start: the first packets count
        beginTick_ = lastUseTick_ = GetTickCount64();
    }
    AudioError err = AudioError::None;
    std::wstring msg;
    // The capture thread only copies; the maximum length is noticed here, the worker ends the recording.
    const bool ok = capture_.Start(
        s.deviceId,
        [this, id](const float* pcm, size_t n) {
            std::lock_guard<std::mutex> lk(mu_);
            if (!recording_ || session_ != id || maxReached_) return;
            const size_t limit = static_cast<size_t>(settings_.maxSeconds) * kVoiceRate;
            const size_t take = std::min(n, limit - std::min(limit, audio_.size()));
            const size_t at = audio_.size();
            audio_.insert(audio_.end(), pcm, pcm + take);
            if (gain_ != 1.0f)  // microphone boost
                for (size_t i = at; i < audio_.size(); ++i) audio_[i] = std::clamp(audio_[i] * gain_, -1.0f, 1.0f);
            if (audio_.size() >= limit) {
                maxReached_ = true;
                cv_.notify_one();
            }
        },
        &err, &msg);
    if (!ok) {
        std::lock_guard<std::mutex> lk(mu_);
        if (session_ == id) {
            recording_ = false;
            audio_.clear();
            audio_.shrink_to_fit();
        }
        return fail(err == AudioError::None ? AudioError::Failed : err, msg);
    }
    if (session) *session = id;
    cv_.notify_one();  // the worker loads the model meanwhile
    return true;
}

void VoiceSession::End() {
    unsigned id;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (!recording_) return;  // never started, cancelled, or ended by itself (maximum length)
        recording_ = false;
        recognizing_ = true;      // Busy() stays true until the Final / Error event
        finalWanted_ = true;
        if (liveRunning_) abort_ = true;  // the final pass should not wait for a live one
        id = session_;
    }
    cv_.notify_one();
    StopCapture(id);
}

void VoiceSession::Cancel() {
    {
        std::lock_guard<std::mutex> cap(captureMu_);
        {
            std::lock_guard<std::mutex> lk(mu_);
            ++session_;  // any event still on its way is stale now
            if (liveRunning_ || recognizing_) abort_ = true;
            recording_ = false;
            recognizing_ = false;
            finalWanted_ = false;
            maxReached_ = false;
            audio_.clear();
            audio_.shrink_to_fit();
            lastLive_.clear();
        }
        capture_.Stop();
    }
    cv_.notify_one();
}

void VoiceSession::StopCapture(unsigned id) {
    std::lock_guard<std::mutex> cap(captureMu_);
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (session_ != id) return;  // a newer recording owns the microphone (session_ changes only under captureMu_)
    }
    capture_.Stop();
}

float VoiceSession::Level() const {  // the meter shows the boosted level (60 dB span)
    return recording_ ? std::clamp(capture_.Level() + 20.0f * std::log10(gain_) / 60.0f, 0.0f, 1.0f) : 0.0f;
}

int VoiceSession::RecordedMs() const {
    std::lock_guard<std::mutex> lk(mu_);
    return SamplesToMs(audio_.size());
}

void VoiceSession::UnloadIfIdle(int minutes) {
    if (minutes <= 0 || !worker_.joinable()) return;
    const ULONGLONG limit = static_cast<ULONGLONG>(minutes) * 60000ull;
    std::lock_guard<std::mutex> lk(mu_);
    if (!stats_.loaded || recording_ || recognizing_ || !jobs_.empty()) return;
    if (GetTickCount64() - lastUseTick_ < limit) return;
    jobs_.push_back([this, limit] {
        {
            std::lock_guard<std::mutex> g(mu_);  // used again since it was queued: keep it
            if (recording_ || recognizing_ || GetTickCount64() - lastUseTick_ < limit) return;
        }
        engine_->Unload();
        std::lock_guard<std::mutex> g(mu_);
        stats_.loaded = false;
    });
    cv_.notify_one();
}

VoiceSession::Stats VoiceSession::GetStats() const {
    Stats s;
    {
        std::lock_guard<std::mutex> lk(mu_);
        s = stats_;
    }
    if (WhisperEngine::RuntimeAvailable()) s.systemInfo = WhisperEngine::SystemInfo();
    return s;
}

void VoiceSession::RunTest(const VoiceSettings& s, int seconds, HWND notify, UINT message) {
    if (recording_) {
        PostString(notify, message, L"[!] " + Tr(L"Finish the recording in the window first, then test again."));
        return;
    }
    if (testRunning_) {
        PostString(notify, message, L"[!] " + Tr(L"A test is already running."));
        return;
    }
    std::wstring why;
    if (!Available(&why)) {
        PostString(notify, message, L"[!] " + why);
        return;
    }
    if (s.modelPath.empty() || !FileExists(s.modelPath)) {
        PostString(notify, message, L"[!] " + Tr(L"The voice model is not downloaded yet."));
        return;
    }
    if (testThread_.joinable()) testThread_.join();  // the last test has finished (testRunning_ is false)
    EnsureWorker();
    testRunning_ = true;
    testCancel_ = false;
    const int secs = std::clamp(seconds, 1, 30);
    testThread_ = std::thread([this, s, secs, notify, message] {
        // 1. Record on this helper thread (its own microphone stream; the window's recordings are untouched).
        std::mutex bufMu;
        std::vector<float> buf;
        AudioCapture cap;
        AudioError err = AudioError::None;
        std::wstring msg;
        const bool ok = cap.Start(
            s.deviceId,
            [&bufMu, &buf](const float* pcm, size_t n) {
                std::lock_guard<std::mutex> lk(bufMu);
                buf.insert(buf.end(), pcm, pcm + n);
            },
            &err, &msg);
        if (!ok) {
            PostString(notify, message, L"[!] " + msg);  // AudioCapture says why (privacy settings, no device, ...)
            testRunning_ = false;
            return;
        }
        const ULONGLONG until = GetTickCount64() + static_cast<ULONGLONG>(secs) * 1000ull;
        while (!testCancel_ && GetTickCount64() < until) Sleep(50);
        cap.Stop();
        if (testCancel_) {
            testRunning_ = false;
            return;
        }
        std::vector<float> audio;
        {
            std::lock_guard<std::mutex> lk(bufMu);
            audio.swap(buf);
        }
        // 2. Recognize on the worker: it owns the recognizer.
        std::lock_guard<std::mutex> lk(mu_);
        if (quit_) {
            testRunning_ = false;
            return;
        }
        jobs_.push_back([this, s, notify, message, audio = std::move(audio)] {
            {
                std::lock_guard<std::mutex> g(mu_);
                if (quit_) {
                    testRunning_ = false;
                    return;
                }
                abort_ = false;
            }
            const int recordMs = SamplesToMs(audio.size());
            const SpeechSpan span = FindSpeech(audio.data(), audio.size());
            std::wstring report;
            if (!span.speech) {
                report = L"[!] " + TrF(L"No speech heard in {1} s (loudest {2} dB) – check the microphone and speak "
                                       L"during the test.",
                                       {Decimal(recordMs / 1000.0, 1), std::to_wstring(std::lround(span.peakDb))});
            } else {
                std::wstring error;
                int loadMs = 0;
                const bool loaded = LoadModel(*engine_, s.modelPath, &error, &loadMs);
                {
                    std::lock_guard<std::mutex> g(mu_);
                    stats_.loaded = loaded;
                    stats_.model = loaded ? FileName(s.modelPath) : std::wstring();
                    if (loaded && loadMs > 0) stats_.loadMs = loadMs;
                }
                if (!loaded) {
                    report = L"[!] " + error;
                } else {
                    const size_t n = span.end - span.begin;
                    const WhisperEngine::Result r =
                        Recognize(*engine_, s, audio.data() + span.begin, n, &abort_);
                    if (r.aborted) {
                        testRunning_ = false;
                        return;  // the program is closing
                    }
                    if (!r.ok) {
                        report = L"[!] " + r.error;
                    } else {
                        const std::wstring text = CleanTranscript(r.text);
                        const double rtf = recordMs > 0 ? static_cast<double>(r.ms) / recordMs : 0.0;
                        {
                            std::lock_guard<std::mutex> g(mu_);
                            stats_.lastRecordMs = recordMs;
                            stats_.lastRecognizeMs = r.ms;
                            stats_.lastRealtimeFactor = rtf;
                            lastUseTick_ = GetTickCount64();
                        }
                        report = text.empty()
                                     ? L"[!] " + TrF(L"{1} s recorded, but nothing was recognized.",
                                                     {Decimal(recordMs / 1000.0, 1)})
                                     : L"[OK] " + TrF(L"{1} s recorded, recognized in {2} s ({3}x real time): {4}",
                                                      {Decimal(recordMs / 1000.0, 1), Decimal(r.ms / 1000.0, 1),
                                                       Decimal(rtf, rtf < 0.1 ? 2 : 1), text});
                    }
                }
            }
            PostString(notify, message, report);
            testRunning_ = false;
        });
        cv_.notify_one();
    });
}

void VoiceSession::Worker() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    ULONGLONG nextLive = 0;  // earliest tick of the next live pass
    ULONGLONG nextEndCheck = 0;  // auto end: when to look at the trailing silence again
    unsigned loadFailed = 0;  // the session whose model did not load (not tried again)
    std::unique_lock<std::mutex> lk(mu_);
    while (!quit_) {
        // ---- A finished recording: the final pass (nothing to recognize -> an empty Final).
        if (finalWanted_) {
            finalWanted_ = false;
            const unsigned id = session_;
            const VoiceSettings s = settings_;
            auto e = std::make_unique<VoiceEvent>();
            e->kind = VoiceEvent::Kind::Final;
            e->session = id;
            e->maxReached = maxReached_;
            maxReached_ = false;
            std::vector<float> audio;
            audio.swap(audio_);
            abort_ = false;
            lk.unlock();

            e->recordMs = SamplesToMs(audio.size());
            const SpeechSpan span = FindSpeech(audio.data(), audio.size());  // too short / silence: nothing
            bool loaded = false;
            int loadMs = 0;
            if (!span.speech) {
                e->empty = true;
            } else {
                std::wstring error;
                loaded = LoadModel(*engine_, s.modelPath, &error, &loadMs);
                if (!loaded) {
                    e->kind = VoiceEvent::Kind::Error;
                    e->message = error;
                } else {
                    const size_t n = span.end - span.begin;
                    const WhisperEngine::Result r =
                        Recognize(*engine_, s, audio.data() + span.begin, n, &abort_);
                    if (r.aborted) {
                        e.reset();  // cancelled meanwhile: no event
                    } else if (!r.ok) {
                        e->kind = VoiceEvent::Kind::Error;
                        e->message = r.error;
                    } else {
                        e->text = CleanTranscript(r.text);
                        e->empty = e->text.empty();
                        e->recognizeMs = r.ms;
                        e->language = r.language;
                    }
                }
            }
            std::vector<float>().swap(audio);  // the recording is gone now

            lk.lock();
            if (span.speech) {
                stats_.loaded = engine_->Loaded();
                stats_.model = engine_->Loaded() ? FileName(engine_->ModelPath()) : std::wstring();
                if (loaded && loadMs > 0) stats_.loadMs = loadMs;
            }
            if (session_ == id) {
                recognizing_ = false;  // before the event: the window may send right away
                lastUseTick_ = GetTickCount64();
                if (e && e->kind == VoiceEvent::Kind::Final && !e->empty) {
                    stats_.lastRecordMs = e->recordMs;
                    stats_.lastRecognizeMs = e->recognizeMs;
                    stats_.lastRealtimeFactor = e->recordMs > 0 ? static_cast<double>(e->recognizeMs) / e->recordMs : 0;
                }
                Post(std::move(e));
            }
            continue;
        }

        // ---- The recording reached its maximum length, or the microphone went away: it ends here.
        if (recording_) {
            bool lost = false;
            if (!maxReached_) {
                std::unique_lock<std::mutex> cap(captureMu_, std::try_to_lock);  // Begin may still be starting it
                lost = cap.owns_lock() && !capture_.Running();
            }
            // Locked / press-to-toggle: a pause after speech ends the message (the text still waits for your Enter).
            bool paused = false;
            const int endMs = autoEndMs_;
            if (!maxReached_ && !lost && endMs > 0 && GetTickCount64() >= nextEndCheck) {
                nextEndCheck = GetTickCount64() + 150;
                const SpeechSpan sp = FindSpeech(audio_.data(), audio_.size());  // end includes 200 ms padding
                paused = sp.speech && audio_.size() > sp.end &&
                         static_cast<int>((audio_.size() - sp.end) * 1000 / kVoiceRate) + 200 >= endMs;
            }
            if (maxReached_ || lost || paused) {
                const unsigned id = session_;
                recording_ = false;
                recognizing_ = true;
                finalWanted_ = true;
                lk.unlock();
                StopCapture(id);
                lk.lock();
                continue;
            }
        }

        // ---- Jobs (Settings → Test, unloading) while nothing is recorded.
        if (!jobs_.empty() && !recording_) {
            std::function<void()> job = std::move(jobs_.front());
            jobs_.pop_front();
            lk.unlock();
            job();
            lk.lock();
            continue;
        }

        // ---- Load the model while the user speaks.
        if (recording_ && session_ != loadFailed &&
            !(engine_->Loaded() && engine_->ModelPath() == settings_.modelPath)) {
            const unsigned id = session_;
            const std::wstring path = settings_.modelPath;
            lk.unlock();
            std::wstring error;
            int loadMs = 0;
            const bool ok = LoadModel(*engine_, path, &error, &loadMs);
            lk.lock();
            stats_.loaded = ok;
            stats_.model = ok ? FileName(path) : std::wstring();
            if (ok) stats_.loadMs = loadMs;
            if (!ok) {
                loadFailed = id;
                if (session_ == id && recording_) {  // the session is over
                    recording_ = false;
                    recognizing_ = false;
                    finalWanted_ = false;
                    audio_.clear();
                    audio_.shrink_to_fit();
                    auto e = std::make_unique<VoiceEvent>();
                    e->kind = VoiceEvent::Kind::Error;
                    e->session = id;
                    e->message = error;
                    Post(std::move(e));
                    lk.unlock();
                    StopCapture(id);
                    lk.lock();
                }
            }
            continue;
        }

        // ---- A live pass over what was said so far (not while the final one is wanted).
        if (recording_ && settings_.live && engine_->Loaded() && audio_.size() >= kMinLiveSamples &&
            GetTickCount64() >= nextLive) {
            const unsigned id = session_;
            const VoiceSettings s = settings_;
            std::vector<float> snapshot(audio_);
            liveRunning_ = true;
            abort_ = false;
            lk.unlock();
            const SpeechSpan span = FindSpeech(snapshot.data(), snapshot.size());
            std::wstring text;
            bool done = false;
            if (span.speech) {
                const size_t n = snapshot.size() - span.begin;  // up to now: the last word may still be spoken
                const WhisperEngine::Result r =
                    Recognize(*engine_, s, snapshot.data() + span.begin, n, &abort_);
                done = r.ok && !r.aborted;
                if (done) text = CleanTranscript(r.text);
            }
            std::vector<float>().swap(snapshot);
            lk.lock();
            liveRunning_ = false;
            nextLive = GetTickCount64() + (span.speech ? kLiveGapMs : kLiveRecheckMs);
            if (done && session_ == id) {
                lastUseTick_ = GetTickCount64();
                if (recording_ && !text.empty()) {
                    auto e = std::make_unique<VoiceEvent>();
                    e->kind = VoiceEvent::Kind::Partial;
                    e->session = id;
                    e->live = SplitLive(lastLive_, text);
                    lastLive_ = text;
                    Post(std::move(e));
                }
            }
            continue;
        }

        // ---- Nothing to do.
        if (recording_) cv_.wait_for(lk, std::chrono::milliseconds(50));
        else cv_.wait(lk);
    }
    lk.unlock();
    engine_->Unload();
    std::lock_guard<std::mutex> g(mu_);
    stats_.loaded = false;
}

}  // namespace gct
