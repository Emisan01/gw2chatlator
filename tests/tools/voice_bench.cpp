// voice_bench — measures the voice recognizer (WhisperEngine, the app's own code) on recordings.
//   voice_bench.exe <model.bin> <lang|auto> <threads> <file.wav>...
// WAVs: 16 kHz mono 16-bit PCM. Each file runs twice: full encoder context (audio_ctx 0) and the reduced context the
// app uses for short clips (seconds * 50 + 64, 256..1500). Prints text, time and real-time factor (time / audio).
// VOICE_BENCH_REPEAT=n runs each pass n times and reports the fastest.
// The DLLs are expected in <exe>\voice (copied by the build). Recordings stay in local\ (never committed).
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "core/text.hpp"
#include "kbd/whisper_engine.hpp"

namespace {

bool ReadWav16kMono(const std::wstring& path, std::vector<float>* out, std::string* error) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) {
        *error = "cannot open";
        return false;
    }
    std::vector<unsigned char> b;
    unsigned char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) b.insert(b.end(), buf, buf + n);
    fclose(f);
    auto u16 = [&](size_t i) { return static_cast<uint32_t>(b[i] | (b[i + 1] << 8)); };
    auto u32 = [&](size_t i) { return u16(i) | (u16(i + 2) << 16); };
    if (b.size() < 12 || memcmp(b.data(), "RIFF", 4) != 0 || memcmp(b.data() + 8, "WAVE", 4) != 0) {
        *error = "not a WAV file";
        return false;
    }
    bool fmtOk = false;
    for (size_t i = 12; i + 8 <= b.size();) {
        const uint32_t size = u32(i + 4);
        const size_t body = i + 8;
        if (body + size > b.size()) break;
        if (memcmp(&b[i], "fmt ", 4) == 0 && size >= 16) {
            const uint32_t format = u16(body), channels = u16(body + 2), rate = u32(body + 4), bits = u16(body + 14);
            if (format != 1 || channels != 1 || rate != 16000 || bits != 16) {
                *error = "need 16 kHz mono 16-bit PCM (format " + std::to_string(format) + ", " +
                         std::to_string(channels) + " ch, " + std::to_string(rate) + " Hz, " + std::to_string(bits) +
                         " bit)";
                return false;
            }
            fmtOk = true;
        } else if (memcmp(&b[i], "data", 4) == 0 && fmtOk) {
            out->resize(size / 2);
            for (size_t k = 0; k < out->size(); ++k)
                (*out)[k] = static_cast<int16_t>(u16(body + 2 * k)) / 32768.0f;
            return true;
        }
        i = body + size + (size & 1);
    }
    *error = "no fmt/data chunk";
    return false;
}

void Print(const std::string& utf8) { fwrite(utf8.data(), 1, utf8.size(), stdout); }

std::string FileName(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return gct::ToUtf8(slash == std::wstring::npos ? path : path.substr(slash + 1));
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    if (argc < 5) {
        Print("usage: voice_bench <model.bin> <lang|auto> <threads> <file.wav>...\n");
        return 2;
    }
    std::wstring error;
    if (!gct::WhisperEngine::RuntimeAvailable(&error)) {
        Print("runtime: " + gct::ToUtf8(error) + "\n");
        return 1;
    }
    Print("system: " + gct::WhisperEngine::SystemInfo() + "\n");
    // Which CPU variant ggml chose (the one that stays loaded).
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((gct::WhisperEngine::RuntimeDir() + L"\\ggml-cpu-*.dll").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (GetModuleHandleW(fd.cFileName)) Print("backend: " + gct::ToUtf8(fd.cFileName) + "\n");
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }

    gct::WhisperEngine engine;
    auto t0 = std::chrono::steady_clock::now();
    if (!engine.Load(argv[1], &error)) {
        Print("model: " + gct::ToUtf8(error) + "\n");
        return 1;
    }
    const auto loadMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    Print("model: " + FileName(argv[1]) + " loaded in " + std::to_string(loadMs) + " ms\n");

    gct::WhisperEngine::Options o;
    const std::string lang = gct::ToUtf8(argv[2]);
    o.language = lang == "auto" ? "" : lang;
    o.threads = std::max(1, _wtoi(argv[3]));
    wchar_t rep[16] = L"1";  // n runs per pass, the fastest counts
    GetEnvironmentVariableW(L"VOICE_BENCH_REPEAT", rep, 16);
    const int repeat = std::max(1, _wtoi(rep));

    // Warm-up (first run allocates buffers): 1 s of silence, not counted.
    std::vector<float> silence(16000, 0.0f);
    std::atomic<bool> abort{false};
    gct::WhisperEngine::Result warm = engine.Transcribe(silence.data(), silence.size(), o, &abort);
    Print("warm-up: " + std::to_string(warm.ms) + " ms\n\n");

    char line[256];
    for (int i = 4; i < argc; ++i) {
        std::vector<float> pcm;
        std::string err;
        const std::string name = FileName(argv[i]);
        if (!ReadWav16kMono(argv[i], &pcm, &err)) {
            Print(name + ": " + err + "\n");
            continue;
        }
        const int audioMs = static_cast<int>(pcm.size() * 1000 / 16000);
        const int reduced = std::clamp(static_cast<int>(pcm.size() * 50 / 16000) + 64, 256, 1500);
        Print(name + " (" + std::to_string(audioMs) + " ms)\n");
        std::string texts[2];
        int ms[2] = {0, 0};
        for (int pass = 0; pass < 2; ++pass) {
            o.audioCtx = pass == 0 ? 0 : reduced;
            gct::WhisperEngine::Result r = engine.Transcribe(pcm.data(), pcm.size(), o, &abort);
            for (int k = 1; k < repeat; ++k)  // fastest of n: other programs on the PC only ever add time
                r.ms = std::min(r.ms, engine.Transcribe(pcm.data(), pcm.size(), o, &abort).ms);
            ms[pass] = r.ms;
            texts[pass] = r.ok ? r.text : "ERROR " + gct::ToUtf8(r.error);
            snprintf(line, sizeof line, "  ctx %4d: %5d ms  RTF %.3f  [%s] ", pass == 0 ? 1500 : reduced, r.ms,
                     audioMs > 0 ? static_cast<double>(r.ms) / audioMs : 0.0, r.language.c_str());
            Print(line + texts[pass] + "\n");
        }
        // One tab-separated line per file for tables: name, audio ms, full ms, reduced ctx, reduced ms, texts.
        Print("RESULT\t" + name + "\t" + std::to_string(audioMs) + "\t" + std::to_string(ms[0]) + "\t" +
              std::to_string(reduced) + "\t" + std::to_string(ms[1]) + "\t" + texts[0] + "\t" + texts[1] + "\n\n");
    }

    // Cancel: how fast a running pass stops (abort set 200 ms into a pass on the first file, both contexts).
    std::vector<float> pcm;
    std::string err;
    if (ReadWav16kMono(argv[4], &pcm, &err)) {
        for (int ctx : {0, std::clamp(static_cast<int>(pcm.size() * 50 / 16000) + 64, 256, 1500)}) {
            o.audioCtx = ctx;
            abort = false;
            std::thread stopper([&abort] {
                Sleep(200);
                abort = true;
            });
            const gct::WhisperEngine::Result r = engine.Transcribe(pcm.data(), pcm.size(), o, &abort);
            stopper.join();
            Print("abort ctx " + std::to_string(ctx ? ctx : 1500) + ": aborted=" + std::to_string(r.aborted) +
                  " after " + std::to_string(r.ms) + " ms (set at 200 ms)\n");
        }
    }
    return 0;
}
