// voice_models.hpp — the voice models on disk: where they are, whether they are complete, downloading them (streamed
// to disk, SHA-256 checked, only on the user's click) and what this PC can carry (cores, AVX2, memory, free space).
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

#include "core/voice.hpp"

namespace gct {

// <dir>\<file>
std::wstring VoiceModelPath(const std::wstring& dir, const VoiceModelInfo& m);
// The file is there, has the catalog's exact size and starts with the GGML magic ("lmgg"). Cheap: no hashing.
bool VoiceModelInstalled(const std::wstring& dir, const VoiceModelInfo& m);
// Blocking (worker thread). Streams the model into <dir>\<file>.tmp, checks size and SHA-256, then renames it.
// progress(done, total) may return false to cancel; `cancel` is checked too. False + error on failure (the partial
// file is deleted).
bool DownloadVoiceModel(const std::wstring& dir, const VoiceModelInfo& m,
                        const std::function<bool(uint64_t done, uint64_t total)>& progress,
                        const std::atomic<bool>* cancel, std::wstring* error);
// Deletes the model file (only ours: <dir>\<file>).
bool DeleteVoiceModel(const std::wstring& dir, const VoiceModelInfo& m);

// The catalog: <dataDir>\voice_models.json when it parses, else the built-in one.
VoiceCatalog LoadVoiceCatalog(const std::wstring& dataDir);

struct VoiceHardware {
    unsigned logicalProcessors = 0;
    bool avx2 = false;
    uint64_t ramTotal = 0, ramAvailable = 0;  // bytes
};
VoiceHardware ProbeVoiceHardware();
// Free bytes on the drive of `dir` (0 if unknown).
uint64_t FreeDiskBytes(const std::wstring& dir);

}  // namespace gct
