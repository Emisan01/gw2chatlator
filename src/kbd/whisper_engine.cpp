// whisper_engine.cpp — whisper.cpp on the CPU (see whisper_engine.hpp). whisper.dll and ggml.dll are delay-loaded from
// <exe>\voice: RuntimeAvailable loads them by full path and binds every import at once, so no later call can fail
// in the delay-load helper. Built without GCT_HAVE_WHISPER it is a placeholder: no runtime, nothing loads.
#include "kbd/whisper_engine.hpp"

#ifdef GCT_HAVE_WHISPER

#include <windows.h>
#include <delayimp.h>

#include <algorithm>
#include <chrono>
#include <climits>
#include <cstdio>
#include <mutex>
#include <vector>

#include "core/i18n.hpp"
#include "core/text.hpp"
#include "core/voice.hpp"
#include "ggml-backend.h"
#include "whisper.h"
#include "win/files.hpp"

namespace gct {

namespace {

enum class RuntimeState { Missing, WrongVersion, NoBackend, Ok };
std::once_flag g_once;
RuntimeState g_state = RuntimeState::Missing;  // written once inside call_once, read after it

void QuietLog(ggml_log_level, const char*, void*) {}  // whisper/ggml log to stderr otherwise

int DelayLoadFilter(unsigned long code) {
    return code == VcppException(ERROR_SEVERITY_ERROR, ERROR_MOD_NOT_FOUND) ||
                   code == VcppException(ERROR_SEVERITY_ERROR, ERROR_PROC_NOT_FOUND)
               ? EXCEPTION_EXECUTE_HANDLER
               : EXCEPTION_CONTINUE_SEARCH;
}

// Binds all delay-loaded imports now and registers the best CPU backend found in `dirUtf8`. Returns the number of
// devices, -1 when an import is missing (DLLs of another version). Only plain locals: __try cannot be used in a
// function that unwinds objects.
long long BindAndRegister(const char* dirUtf8) {
    __try {
        if (FAILED(__HrLoadAllImportsForDll("whisper.dll")) || FAILED(__HrLoadAllImportsForDll("ggml.dll"))) return -1;
        whisper_log_set(QuietLog, nullptr);
        // Only our folder: ggml_backend_load_all() would also search the exe folder and the current directory.
        ggml_backend_load_all_from_path(dirUtf8);
        return static_cast<long long>(ggml_backend_dev_count());
    } __except (DelayLoadFilter(GetExceptionCode())) {
        return -1;
    }
}

struct FileLoader {
    static size_t Read(void* f, void* out, size_t n) { return fread(out, 1, n, static_cast<FILE*>(f)); }
    static bool Eof(void* f) { return feof(static_cast<FILE*>(f)) != 0; }
    static void Close(void* f) { fclose(static_cast<FILE*>(f)); }
};

// whisper checks the abort callback only between graphs (after the whole encoder, per decoder step); the encoder
// itself cannot be stopped, so a cancel waits at most one encoder pass. Checked before the encoder starts as well.
bool AbortRequested(void* flag) { return static_cast<const std::atomic<bool>*>(flag)->load(); }
bool EncoderMayBegin(whisper_context*, whisper_state*, void* flag) { return !AbortRequested(flag); }

// The recognizer must not take CPU time from the game: the calling thread runs below normal while it works.
// ggml's own helper threads (threads - 1, created per graph) start at normal priority; Windows does not inherit it.
class LowerPriority {
public:
    LowerPriority() : old_(GetThreadPriority(GetCurrentThread())) {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    }
    ~LowerPriority() {
        if (old_ != THREAD_PRIORITY_ERROR_RETURN) SetThreadPriority(GetCurrentThread(), old_);
    }
    LowerPriority(const LowerPriority&) = delete;
    LowerPriority& operator=(const LowerPriority&) = delete;

private:
    int old_;
};

}  // namespace

WhisperEngine::~WhisperEngine() { Unload(); }

std::wstring WhisperEngine::RuntimeDir() { return ExeDir() + L"\\voice"; }

bool WhisperEngine::RuntimeAvailable(std::wstring* error) {
    std::call_once(g_once, [] {
        // By full path, with the voice folder as search path for its own imports (ggml.dll, ggml-base.dll); the
        // delay-load helper then finds the modules already loaded. Never unloaded.
        const UINT oldMode = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
        HMODULE whisper =
            LoadLibraryExW((RuntimeDir() + L"\\whisper.dll").c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        SetErrorMode(oldMode);
        if (!whisper) {
            g_state = RuntimeState::Missing;
            return;
        }
        const long long devices = BindAndRegister(ToUtf8(RuntimeDir()).c_str());
        g_state = devices < 0 ? RuntimeState::WrongVersion : devices == 0 ? RuntimeState::NoBackend : RuntimeState::Ok;
    });
    if (g_state == RuntimeState::Ok) return true;
    if (error) {  // translated here, not cached: the UI language can change while the program runs
        if (g_state == RuntimeState::Missing)
            *error = TrF(L"The voice recognizer is missing (folder {1}).", {RuntimeDir()});
        else if (g_state == RuntimeState::WrongVersion)
            *error = TrF(L"The voice recognizer in {1} does not fit this program version.", {RuntimeDir()});
        else
            *error = Tr(L"The voice recognizer found no backend for this processor.");
    }
    return false;
}

std::string WhisperEngine::SystemInfo() {
    if (!RuntimeAvailable()) return {};
    const char* s = whisper_print_system_info();
    return s ? s : "";
}

bool WhisperEngine::Load(const std::wstring& modelPath, std::wstring* error) {
    if (ctx_ && modelPath == modelPath_) return true;
    Unload();
    if (!RuntimeAvailable(error)) return false;
    FILE* f = nullptr;  // any Unicode path (whisper's own loader takes UTF-8, BMP only)
    if (_wfopen_s(&f, modelPath.c_str(), L"rb") != 0 || !f) {
        if (error) *error = TrF(L"The voice model cannot be opened: {1}", {modelPath});
        return false;
    }
    whisper_model_loader loader{};
    loader.context = f;
    loader.read = &FileLoader::Read;
    loader.eof = &FileLoader::Eof;
    loader.close = &FileLoader::Close;  // whisper closes it, also on failure
    whisper_context_params cp = whisper_context_default_params();
    cp.use_gpu = false;
    whisper_context* ctx = whisper_init_with_params(&loader, cp);
    if (!ctx) {
        if (error) *error = TrF(L"The voice model is damaged or not a whisper model: {1}", {modelPath});
        return false;
    }
    ctx_ = ctx;
    modelPath_ = modelPath;
    return true;
}

void WhisperEngine::Unload() {
    if (ctx_) whisper_free(static_cast<whisper_context*>(ctx_));
    ctx_ = nullptr;
    modelPath_.clear();
}

WhisperEngine::Result WhisperEngine::Transcribe(const float* pcm, size_t samples, const Options& o,
                                                const std::atomic<bool>* abort) {
    Result r;
    if (!ctx_) {
        r.error = Tr(L"No voice model is loaded.");
        return r;
    }
    whisper_context* ctx = static_cast<whisper_context*>(ctx_);

    // whisper refuses less than 100 ms and guesses wildly on very short clips: pad with silence to 1 s.
    std::vector<float> padded;
    if (!pcm || samples < static_cast<size_t>(kVoiceRate)) {
        padded.assign(static_cast<size_t>(kVoiceRate), 0.0f);
        if (pcm) std::copy(pcm, pcm + samples, padded.begin());
        pcm = padded.data();
        samples = padded.size();
    }

    whisper_full_params p = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    p.n_threads = std::max(1, o.threads);
    p.translate = o.translate;
    p.no_context = true;
    p.no_timestamps = true;
    p.print_special = false;
    p.print_progress = false;
    p.print_realtime = false;
    p.print_timestamps = false;
    p.suppress_blank = true;
    p.suppress_nst = true;
    p.language = o.language.empty() ? "auto" : o.language.c_str();  // "auto" = detect and transcribe
    p.initial_prompt = o.initialPrompt.empty() ? nullptr : o.initialPrompt.c_str();
    p.audio_ctx = std::clamp(o.audioCtx, 0, 1500);
    if (abort) {
        void* flag = const_cast<std::atomic<bool>*>(abort);
        p.abort_callback = &AbortRequested;
        p.abort_callback_user_data = flag;
        p.encoder_begin_callback = &EncoderMayBegin;
        p.encoder_begin_callback_user_data = flag;
    }

    const auto start = std::chrono::steady_clock::now();
    int rc;
    {
        LowerPriority low;
        rc = whisper_full(ctx, p, pcm, static_cast<int>(std::min<size_t>(samples, INT_MAX)));
    }
    r.ms = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());

    if (abort && abort->load()) {
        r.aborted = true;
        return r;
    }
    if (rc != 0) {
        r.error = TrF(L"Voice recognition failed (code {1}).", {std::to_wstring(rc)});
        return r;
    }
    const int n = whisper_full_n_segments(ctx);
    for (int i = 0; i < n; ++i)
        if (const char* t = whisper_full_get_segment_text(ctx, i)) r.text += t;
    const int lang = whisper_full_lang_id(ctx);
    if (const char* code = lang >= 0 ? whisper_lang_str(lang) : nullptr) r.language = code;
    r.ok = true;
    return r;
}

}  // namespace gct

#else  // !GCT_HAVE_WHISPER

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

#endif
