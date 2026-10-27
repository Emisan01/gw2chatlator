// gw2kb_translate.exe — isolated local OPUS-MT inference process. No model or ML runtime is loaded in GW2.
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include <ctranslate2/translator.h>
#include <sentencepiece_processor.h>

#include "core/json.hpp"
#include "core/local_mt_protocol.hpp"
#include "core/text.hpp"
#include "core/voice.hpp"
#include "win/cpu_cores.hpp"
#include "win/files.hpp"
#include "version.h"

using namespace gct;
namespace {

bool ExistsFile(const std::wstring& path) {
    const DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

constexpr size_t kMaxLoadedPairs = 2;
constexpr size_t kThreadsPerPair = 2;

struct PairModel {
    std::string id;
    std::string sourcePrefix;
    sentencepiece::SentencePieceProcessor source, target;
    std::unique_ptr<ctranslate2::Translator> translator;
    ULONGLONG used = 0;
};

struct NllbModel {
    sentencepiece::SentencePieceProcessor sp;
    std::unique_ptr<ctranslate2::Translator> translator;
    ULONGLONG used = 0;
};

std::wstring g_data;
std::list<std::unique_ptr<PairModel>> g_models;  // most recently used at the front
std::unique_ptr<NllbModel> g_nllb;
int g_unloadMinutes = 10;
ULONGLONG g_lastRequest = 0;

std::string JsonString(const char* key, const std::string& value) {
    return std::string(",\"") + key + "\":\"" + JsonEscape(value) + "\"";
}

void Emit(const std::string& json) {
    std::cout << json << '\n' << std::flush;
}

// How sure the model was: the mean log probability per token of the best hypothesis (CTranslate2 normalises by
// length), as `,"score":-0.42`; nothing when the model gave none.
std::string ScoreField(const ctranslate2::TranslationResult& result) {
    if (!result.has_scores() || result.scores.empty()) return {};
    char buf[32];
    std::snprintf(buf, sizeof(buf), ",\"score\":%.3f", static_cast<double>(result.scores.front()));
    return buf;
}

std::wstring PairPath(const std::string& id) {
    return g_data + L"\\translate\\" + FromUtf8(id);
}

NllbModel* LoadNllb(std::string& error) {
    if (g_nllb) {
        g_nllb->used = GetTickCount64();
        return g_nllb.get();
    }
    const ULONGLONG start = GetTickCount64();
    const std::wstring dir = g_data + L"\\translate\\nllb-200";
    if (!ExistsFile(dir + L"\\model.bin") || !ExistsFile(dir + L"\\sentencepiece.bpe.model")) {
        error = "nllb-200 model files are missing or incomplete";
        return nullptr;
    }
    auto model = std::make_unique<NllbModel>();
    if (!model->sp.Load(ToUtf8(dir + L"\\sentencepiece.bpe.model")).ok()) {
        error = "nllb-200 tokenizer failed to load";
        return nullptr;
    }
    ctranslate2::models::ModelLoader loader(ToUtf8(dir));
    loader.device = ctranslate2::Device::CPU;
    loader.compute_type = ctranslate2::ComputeType::INT8;
    loader.num_replicas_per_device = 1;
    ctranslate2::ReplicaPoolConfig pool;
    pool.num_threads_per_replica = kThreadsPerPair;
    pool.max_queued_batches = 1;
    try {
        model->translator = std::make_unique<ctranslate2::Translator>(loader, pool);
    } catch (const std::exception& e) {
        error = e.what();
        return nullptr;
    }
    // Warm-up call (prevents cold-start latency on user's first chat translation)
    try {
        std::vector<std::vector<std::string>> warmSrc = {{"deu_Latn", "Hallo", "</s>"}};
        std::vector<std::vector<std::string>> warmTgt = {{"eng_Latn"}};
        ctranslate2::TranslationOptions warmOpt;
        warmOpt.beam_size = 1;
        model->translator->translate_batch(warmSrc, warmTgt, warmOpt);
    } catch (...) {}

    model->used = GetTickCount64();
    g_nllb = std::move(model);
    Emit("{\"evt\":\"loaded\"" + JsonString("pair", "nllb-200") + ",\"ms\":" +
         std::to_string(GetTickCount64() - start) + "}");
    return g_nllb.get();
}

PairModel* LoadPair(const std::string& id, std::string& error) {
    for (auto it = g_models.begin(); it != g_models.end(); ++it) {
        if ((*it)->id != id) continue;
        (*it)->used = GetTickCount64();
        g_models.splice(g_models.begin(), g_models, it);
        return g_models.front().get();
    }

    const ULONGLONG start = GetTickCount64();
    const std::wstring dir = PairPath(id);
    auto model = std::make_unique<PairModel>();
    model->id = id;
    std::string metadata;
    if (ReadFileBytes(dir + L"\\pair.json", metadata)) {
        JsonValue json;
        if (ParseJson(metadata, json) && json.IsObject()) model->sourcePrefix = json.GetString("source_prefix");
    }
    if (!model->source.Load(ToUtf8(dir + L"\\source.spm")).ok() ||
        !model->target.Load(ToUtf8(dir + L"\\target.spm")).ok()) {
        error = "tokenizer files are missing or invalid";
        return nullptr;
    }
    ctranslate2::models::ModelLoader loader(ToUtf8(dir));
    loader.device = ctranslate2::Device::CPU;
    loader.compute_type = ctranslate2::ComputeType::INT8;
    loader.num_replicas_per_device = 1;
    ctranslate2::ReplicaPoolConfig pool;
    pool.num_threads_per_replica = kThreadsPerPair;
    pool.max_queued_batches = 1;
    try {
        model->translator = std::make_unique<ctranslate2::Translator>(loader, pool);
    } catch (const std::exception& e) {
        error = e.what();
        return nullptr;
    }
    // Warm-up call (prevents cold-start latency on user's first chat translation with this pair)
    try {
        std::vector<std::string> warmTokens = {"Hello", "</s>"};
        if (!model->sourcePrefix.empty()) warmTokens.insert(warmTokens.begin(), model->sourcePrefix);
        ctranslate2::TranslationOptions warmOpt;
        warmOpt.beam_size = 1;
        model->translator->translate_batch({warmTokens}, warmOpt);
    } catch (...) {}
    model->used = GetTickCount64();
    if (g_models.size() == kMaxLoadedPairs) g_models.pop_back();
    g_models.push_front(std::move(model));
    Emit("{\"evt\":\"loaded\"" + JsonString("pair", id) + ",\"ms\":" +
         std::to_string(GetTickCount64() - start) + "}");
    return g_models.front().get();
}

void Translate(const std::string& line) {
    LocalMtRequest request;
    if (!ParseLocalMtRequest(line, request)) return;
    g_lastRequest = GetTickCount64();
    const std::string pair = request.source + "-" + request.target;
    std::string error;

    const bool hasOpusPair = ExistsFile(PairPath(pair) + L"\\model.bin");
    const bool hasNllb = ExistsFile(g_data + L"\\translate\\nllb-200\\model.bin");
    const std::string srcTag = NllbLanguageTag(request.source);
    const std::string tgtTag = NllbLanguageTag(request.target);
    const bool preferNllb = (request.model == "nllb");
    const bool preferOpus = (request.model == "opus");
    const bool canUseNllb = hasNllb && !srcTag.empty() && !tgtTag.empty();

    if (canUseNllb && (preferNllb || (!hasOpusPair && !preferOpus))) {
        NllbModel* nllb = LoadNllb(error);
        if (!nllb) {
            Emit("{\"evt\":\"error\",\"id\":" + std::to_string(request.id) + JsonString("msg", error) + "}");
            return;
        }
        std::vector<std::string> tokens;
        if (!nllb->sp.Encode(request.text, &tokens).ok()) {
            Emit("{\"evt\":\"error\",\"id\":" + std::to_string(request.id) +
                 JsonString("msg", "nllb source tokenization failed") + "}");
            return;
        }
        tokens.insert(tokens.begin(), srcTag);
        tokens.emplace_back("</s>");
        ctranslate2::TranslationOptions options;
        options.beam_size = request.beam > 0 ? request.beam : 2;
        options.return_end_token = false;
        options.return_scores = true;
        options.max_decoding_length = std::clamp<size_t>(tokens.size() * 2, 32, 256);
        try {
            const std::vector<std::vector<std::string>> srcBatch = {tokens};
            const std::vector<std::vector<std::string>> tgtPrefixBatch = {{tgtTag}};
            const auto results = nllb->translator->translate_batch(srcBatch, tgtPrefixBatch, options);
            if (results.empty() || results.front().hypotheses.empty()) {
                Emit("{\"evt\":\"error\",\"id\":" + std::to_string(request.id) +
                     JsonString("msg", "the model returned no translation") + "}");
                return;
            }
            auto hypo = results.front().hypotheses.front();
            if (!hypo.empty() && hypo.front() == tgtTag) hypo.erase(hypo.begin());
            std::string text;
            if (!nllb->sp.Decode(hypo, &text).ok()) {
                Emit("{\"evt\":\"error\",\"id\":" + std::to_string(request.id) +
                     JsonString("msg", "nllb target detokenization failed") + "}");
                return;
            }
            Emit("{\"evt\":\"result\",\"id\":" + std::to_string(request.id) + JsonString("text", text) +
                 ScoreField(results.front()) + "}");
            return;
        } catch (const std::exception& e) {
            Emit("{\"evt\":\"error\",\"id\":" + std::to_string(request.id) + JsonString("msg", e.what()) + "}");
            return;
        }
    }

    PairModel* model = LoadPair(pair, error);
    if (!model) {
        Emit("{\"evt\":\"error\",\"id\":" + std::to_string(request.id) + JsonString("msg", error) + "}");
        return;
    }

    std::vector<std::string> sourceTokens;
    if (!model->source.Encode(request.text, &sourceTokens).ok()) {
        Emit("{\"evt\":\"error\",\"id\":" + std::to_string(request.id) +
             JsonString("msg", "source tokenization failed") + "}");
        return;
    }
    if (!model->sourcePrefix.empty()) sourceTokens.insert(sourceTokens.begin(), model->sourcePrefix);
    sourceTokens.emplace_back("</s>");
    ctranslate2::TranslationOptions options;
    options.beam_size = request.beam > 0 ? request.beam : 2;
    options.return_end_token = false;
    options.return_scores = true;
    options.max_decoding_length = std::clamp<size_t>(sourceTokens.size() * 2, 32, 256);
    try {
        const auto results = model->translator->translate_batch({sourceTokens}, options);
        if (results.empty() || results.front().hypotheses.empty()) {
            Emit("{\"evt\":\"error\",\"id\":" + std::to_string(request.id) +
                 JsonString("msg", "the model returned no translation") + "}");
            return;
        }
        std::string text;
        if (!model->target.Decode(results.front().hypotheses.front(), &text).ok()) {
            Emit("{\"evt\":\"error\",\"id\":" + std::to_string(request.id) +
                 JsonString("msg", "target detokenization failed") + "}");
            return;
        }
        Emit("{\"evt\":\"result\",\"id\":" + std::to_string(request.id) + JsonString("text", text) +
             ScoreField(results.front()) + "}");
    } catch (const std::exception& e) {
        Emit("{\"evt\":\"error\",\"id\":" + std::to_string(request.id) + JsonString("msg", e.what()) + "}");
    }
}

void SelectCores() {
    CoreLoad load;
    load.Sample();
    Sleep(120);
    load.Sample();
    const VoiceCorePick pick = PickVoiceCores(load.Busy(), PhysicalCores(), 2);
    if (pick.threads > 0) {
        const uint64_t mask = CoreMask(pick.logical);
        if (mask) SetProcessAffinityMask(GetCurrentProcess(), static_cast<DWORD_PTR>(mask));
    }
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
    SelectCores();
    for (int i = 1; i + 1 < argc; ++i)
        if (std::wstring(argv[i]) == L"--data") g_data = argv[++i];
    if (g_data.empty()) return 2;
    const std::wstring ini = g_data + L"\\gw2-chat-translator.ini";
    g_unloadMinutes = std::clamp<int>(static_cast<int>(GetPrivateProfileIntW(L"Translate", L"UnloadMinutes", 10,
                                                                              ini.c_str())), 1, 120);
    g_lastRequest = GetTickCount64();
    std::string readyMsg = "{\"evt\":\"ready\",\"helper\":\"translate\",\"proto\":1,\"caps\":[\"opus\",\"nllb\",\"score\"],\"build\":\"";
    readyMsg += GCT_GIT_BUILD_ID;
    readyMsg += "\"}";
    Emit(readyMsg);

    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    std::string pending;
    char buffer[4096];
    bool quit = false;
    while (!quit && GetTickCount64() - g_lastRequest < static_cast<ULONGLONG>(g_unloadMinutes) * 60'000) {
        DWORD available = 0;
        if (!PeekNamedPipe(in, nullptr, 0, nullptr, &available, nullptr)) break;
        if (!available) { Sleep(5); continue; }
        DWORD read = 0;
        if (!ReadFile(in, buffer, std::min<DWORD>(available, sizeof(buffer)), &read, nullptr) || !read) break;
        pending.append(buffer, read);
        size_t nl = 0;
        while (!quit && (nl = pending.find('\n')) != std::string::npos) {
            std::string line = pending.substr(0, nl);
            pending.erase(0, nl + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line == "{\"cmd\":\"quit\"}") quit = true;
            else Translate(line);
        }
    }
    return 0;
}
