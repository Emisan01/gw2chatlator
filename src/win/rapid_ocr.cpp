// rapid_ocr.cpp
#include "rapid_ocr.hpp"

#include <windows.h>

#include "core/text.hpp"
#include "win/files.hpp"

#include <bcrypt.h>

#include "core/rapid_models.hpp"
#include "win/http.hpp"

#ifdef GCT_HAVE_RAPIDOCR
// No static initialisation: the DLL is delay-loaded and may be missing (InitApi after LoadLibrary).
#define ORT_API_MANUAL_INIT
#include <onnxruntime_cxx_api.h>
#endif

namespace gct {

#ifdef GCT_HAVE_RAPIDOCR

namespace {

Ort::Env& Env() {
    static Ort::Env env(ORT_LOGGING_LEVEL_ERROR, "gw2chatlator");
    return env;
}

}  // namespace

struct RapidRecognizer::Impl {
    std::unique_ptr<Ort::Session> session;
    std::vector<std::wstring> dict;
    std::string inputName, outputName;
};

RapidRecognizer::RapidRecognizer() : impl_(std::make_unique<Impl>()) {}
RapidRecognizer::~RapidRecognizer() = default;

bool RapidRecognizer::RuntimeAvailable(std::wstring* error) {
    static int state = 0;  // 0 unknown, 1 ok, 2 missing
    if (state == 0) {
        // Loaded by full path before the first ONNX Runtime call, so the delay load finds this one.
        const std::wstring dll = ExeDir() + L"\\onnxruntime.dll";
        state = LoadLibraryExW(dll.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH) ? 1 : 2;
        if (state == 1) {
            const OrtApi* api = OrtGetApiBase()->GetApi(ORT_API_VERSION);  // null: older runtime than the headers
            if (api) Ort::InitApi(api);
            else state = 2;
        }
    }
    if (state == 2 && error) *error = L"onnxruntime.dll not found next to the program";
    return state == 1;
}

bool RapidRecognizer::Load(const std::wstring& modelPath, const std::wstring& dictPath, std::wstring* error) {
    impl_->session.reset();
    if (!RuntimeAvailable(error)) return false;
    std::string dict;
    if (!ReadFileBytes(dictPath, dict)) {
        if (error) *error = L"dictionary not found: " + dictPath;
        return false;
    }
    impl_->dict = ParseRecDictionary(dict);
    try {
        Ort::SessionOptions so;
        so.SetIntraOpNumThreads(2);  // leave the game its cores
        so.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        impl_->session = std::make_unique<Ort::Session>(Env(), modelPath.c_str(), so);
        Ort::AllocatorWithDefaultOptions alloc;
        impl_->inputName = impl_->session->GetInputNameAllocated(0, alloc).get();
        impl_->outputName = impl_->session->GetOutputNameAllocated(0, alloc).get();
    } catch (const Ort::Exception& e) {
        impl_->session.reset();
        if (error) *error = FromUtf8(e.what());
        return false;
    }
    return true;
}

bool RapidRecognizer::Ready() const { return impl_->session != nullptr; }

RecResult RapidRecognizer::Recognize(const Image& line, bool invert, std::wstring* error) {
    RecResult r;
    if (!impl_->session || line.Empty()) return r;
    RecInput in = PrepareRecInput(line, invert);
    try {
        const int64_t shape[4] = {1, 3, kRecHeight, in.width};
        Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value input = Ort::Value::CreateTensor<float>(mem, in.data.data(), in.data.size(), shape, 4);
        const char* inNames[] = {impl_->inputName.c_str()};
        const char* outNames[] = {impl_->outputName.c_str()};
        auto out = impl_->session->Run(Ort::RunOptions{nullptr}, inNames, &input, 1, outNames, 1);
        const auto info = out[0].GetTensorTypeAndShapeInfo();
        const std::vector<int64_t> dims = info.GetShape();
        if (dims.size() != 3) return r;
        r = CtcDecode(out[0].GetTensorData<float>(), static_cast<int>(dims[1]), static_cast<int>(dims[2]), impl_->dict);
        r.inputWidth = in.width;
    } catch (const Ort::Exception& e) {
        if (error) *error = FromUtf8(e.what());
    }
    return r;
}

#else  // built without RapidOCR

struct RapidRecognizer::Impl {};
RapidRecognizer::RapidRecognizer() : impl_(std::make_unique<Impl>()) {}
RapidRecognizer::~RapidRecognizer() = default;
bool RapidRecognizer::RuntimeAvailable(std::wstring* error) {
    if (error) *error = L"built without RapidOCR";
    return false;
}
bool RapidRecognizer::Load(const std::wstring&, const std::wstring&, std::wstring* error) {
    return RuntimeAvailable(error);
}
bool RapidRecognizer::Ready() const { return false; }
RecResult RapidRecognizer::Recognize(const Image&, bool, std::wstring*) { return {}; }

#endif

namespace {

std::string Sha256Hex(const std::string& data) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    unsigned char hash[32] = {};
    std::string hex;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0) {
        if (BCryptHash(alg, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())),
                       static_cast<ULONG>(data.size()), hash, sizeof(hash)) == 0) {
            static const char* digits = "0123456789abcdef";
            for (unsigned char b : hash) {
                hex += digits[b >> 4];
                hex += digits[b & 15];
            }
        }
        BCryptCloseAlgorithmProvider(alg, 0);
    }
    return hex;
}

}  // namespace

bool RapidGroupInstalled(const RapidModelGroup& g, const std::wstring& dir) {
    return GetFileAttributesW((dir + L"\\" + RapidModelFile(g)).c_str()) != INVALID_FILE_ATTRIBUTES &&
           GetFileAttributesW((dir + L"\\" + RapidDictFile(g)).c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::wstring RapidGroupDir(const RapidModelGroup& g, const std::wstring& userDir) {
    if (!userDir.empty() && RapidGroupInstalled(g, userDir)) return userDir;
    const std::wstring shipped = ExeDir() + L"\\rapid";
    return RapidGroupInstalled(g, shipped) ? shipped : L"";
}

bool DownloadRapidGroup(const RapidModelGroup& g, const std::wstring& dir, std::wstring* error) {
    if (!EnsureDir(dir)) {
        if (error) *error = L"cannot create " + dir;
        return false;
    }
    const HttpResponse model = HttpRequestUrl(L"GET", g.recUrl, L"", "", 120000, 64u << 20);
    if (!model.transportOk || model.status != 200) {
        if (error) *error = model.transportOk ? L"HTTP " + std::to_wstring(model.status) : model.error;
        return false;
    }
    if (Sha256Hex(model.body) != g.recSha256) {  // never use a file that is not the published one
        if (error) *error = L"checksum does not match";
        return false;
    }
    const HttpResponse dict = HttpRequestUrl(L"GET", g.dictUrl, L"", "", 60000);
    if (!dict.transportOk || dict.status != 200 || dict.body.empty()) {
        if (error) *error = dict.transportOk ? L"HTTP " + std::to_wstring(dict.status) : dict.error;
        return false;
    }
    return WriteFileAtomic(dir + L"\\" + RapidDictFile(g), dict.body) &&
           WriteFileAtomic(dir + L"\\" + RapidModelFile(g), model.body);
}

}  // namespace gct
