// voice.hpp — portable logic of the voice input (GW2 Keyboard): the model catalog, finding speech in a recording,
// cleaning what the recognizer returns, the live text's stable / unsure parts and the recognizer language.
// No windows.h; tested in tests/core_tests.cpp. Audio is 16 kHz mono float in [-1, 1] everywhere.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gct {

constexpr int kVoiceRate = 16000;  // samples per second the recognizer wants

// ---- model catalog ("voice_models.json"; a built-in copy, an updated file next to the data wins) ----

struct VoiceLangQuality {
    std::string lang;  // whisper code: "de", "en", "ar", "zh", ...
    int quality = 0;   // 0 unusable, 1 weak, 2 good, 3 very good
};

struct VoiceModelInfo {
    std::string id;        // "small-q5_1"
    std::string name;      // "Whisper small (q5_1)"
    std::string provider;  // "whisper" (later "vosk", "moonshine", ...)
    std::string file;      // "ggml-small-q5_1.bin"
    std::string url;       // https only
    std::string sha256;    // lower-case hex, 64 characters
    uint64_t bytes = 0;    // exact file size
    int ramMb = 0;         // memory while loaded (estimate)
    bool streaming = false;  // real partial results (whisper: false; live text = repeated passes)
    bool englishOnly = false;
    bool recommended = false;  // the default choice
    std::string licence;   // "MIT"
    std::vector<VoiceLangQuality> langs;  // empty = every whisper language at quality 2
    // Quality for a whisper language code; 2 when the model lists none, 0 for other languages of an English model.
    int QualityFor(const std::string& lang) const;
};

struct VoiceCatalog {
    std::string date;  // "2026-10-12": when sizes and notes were checked
    std::vector<VoiceModelInfo> models;
    const VoiceModelInfo* Find(const std::string& id) const;
    const VoiceModelInfo* Recommended() const;  // the one marked recommended, else the first
};

// The catalog shipped with the program (JSON text, verified sizes and SHA-256 of whisper.cpp v1.9.5's models).
const char* DefaultVoiceCatalogJson();
// Parses a catalog. Entries with a non-https URL, a SHA-256 that is not 64 hex characters, no file name, a file name
// with a path separator or "..", or no size are skipped (a broken update never offers a bad download). False when
// nothing usable is left.
bool ParseVoiceCatalog(const std::string& json, VoiceCatalog* out);

// ---- speech in a recording (energy based; runs once on the whole recording and on live snapshots) ----

struct SpeechSpan {
    bool speech = false;  // enough voiced audio (>= minSpeechMs) to give to the recognizer
    size_t begin = 0;     // first sample to keep (with padding), in samples
    size_t end = 0;       // one past the last sample to keep
    int voicedMs = 0;     // voiced 20 ms frames in total
    float peakDb = -120;  // loudest frame (dBFS, RMS of 20 ms)
    float noiseDb = -120; // estimated noise floor (dBFS)
};

struct SpeechOptions {
    int minRecordingMs = 300;  // shorter recordings are dropped (concept: < 0.3 s inserts nothing)
    int minSpeechMs = 250;     // voiced frames needed
    float thresholdDb = 12;    // a frame is voiced when it is this far above the noise floor ...
    float absoluteFloorDb = -50;  // ... and louder than this (dBFS)
    int padMs = 200;           // kept before the first and after the last voiced frame
};

SpeechSpan FindSpeech(const float* pcm, size_t samples, const SpeechOptions& o = {});

// RMS level of the last `ms` milliseconds as 0..1 for a level meter (-60 dBFS = 0, 0 dBFS = 1).
float LevelMeter(const float* pcm, size_t samples, int ms = 50);

// ---- what the recognizer returns ----

// Removes whisper's non-speech markers ("[BLANK_AUDIO]", "[Musik]", "(Applaus)", "*lacht*", "♪"), collapses spaces,
// trims, and returns "" for the sentences whisper invents on silence or noise ("Untertitel der Amara.org-Gemeinschaft",
// "Thanks for watching!", ...) when they are the whole result. UTF-8 in, UTF-16 out (the input box text).
std::wstring CleanTranscript(const std::string& utf8);

// The live text: the part both of the last two passes agree on (stable, shown normally) and the rest of the newest
// pass (unsure, shown grey). Compared word by word, case- and punctuation-insensitive; the newest pass's last word is
// always unsure while speaking.
struct LiveText {
    std::wstring stable;
    std::wstring unsure;
};
LiveText SplitLive(const std::wstring& previous, const std::wstring& newest);

// ---- recognizer settings ----

// Whisper language for a language code of this program ("DE", "EN-GB", "ZH-HANS", "PT-BR", "" ...): "de", "en", "zh",
// "pt"; "" (= let whisper detect) for empty or unknown codes.
std::string WhisperLang(const std::wstring& code);

// Whisper's encoder context for a recording: 50 frames per second plus a margin, at least 256, at most 1500 (= the
// full 30 s window). Shorter contexts are much faster for short push-to-talk clips.
int WhisperAudioCtx(size_t samples);

// Threads for the recognizer: half the logical processors, at most 4, at least 1 (setting 0 = automatic).
int VoiceThreads(int setting, unsigned logicalProcessors);

// Where the recognizer runs: the least busy physical cores right now (GW2 keeps one or two cores busy, Windows moves
// them around – so the choice is made fresh for every recording). `busy` = share 0..1 per logical processor,
// `cores` = the logical processors of each physical core (hyper-threading siblings together). One thread per chosen
// physical core, on its less busy sibling. `want` 0 = automatic: every core under ~35 % load, 2..6 (more brings little:
// 4 -> 6 threads was only 18 % faster in voice_bench); else exactly `want` (siblings added when there are too few cores).
struct VoiceCorePick {
    std::vector<int> logical;  // logical processor indices to run on (empty = no data: let Windows choose)
    int threads = 0;
};
VoiceCorePick PickVoiceCores(const std::vector<float>& busy, const std::vector<std::vector<int>>& cores, int want);

// ---- audio format ----

// Interleaved float frames of any rate / channel count -> 16 kHz mono (channels averaged, linear interpolation after a
// simple low-pass when downsampling). Used only when Windows cannot convert the microphone format itself.
std::vector<float> ToMono16k(const float* interleaved, size_t frames, int channels, int rate);

}  // namespace gct
