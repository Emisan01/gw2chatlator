// text_prediction.cpp — Windows.Data.Text.TextPredictionGenerator through the raw WinRT ABI (like ocr.cpp; no
// C++/WinRT). Interface IDs and vtable order from the Windows SDK header windows.data.text.h.
#include "text_prediction.hpp"

#include <windows.h>
#include <inspectable.h>
#include <roapi.h>
#include <winstring.h>

#include <cstring>

#ifdef _MSC_VER
#pragma comment(lib, "runtimeobject.lib")
#endif

namespace gct {
namespace {

struct ITextPredictionGenerator : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_ResolvedLanguage(HSTRING* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_LanguageAvailableButNotInstalled(boolean* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCandidatesAsync(HSTRING input, IInspectable** result) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCandidatesWithMaxCountAsync(HSTRING input, UINT32 maxCandidates,
                                                                      IInspectable** result) = 0;
};
struct ITextPredictionGeneratorFactory : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE Create(HSTRING languageTag, ITextPredictionGenerator** result) = 0;
};
struct IAsyncOperationRaw : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE put_Completed(IUnknown* handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Completed(IUnknown** handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetResults(void** results) = 0;
};
struct IAsyncInfoRaw : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Id(UINT32* id) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Status(INT32* status) = 0;  // 0 started, 1 completed, 2 canceled, 3 error
    virtual HRESULT STDMETHODCALLTYPE get_ErrorCode(HRESULT* code) = 0;
    virtual HRESULT STDMETHODCALLTYPE Cancel() = 0;
    virtual HRESULT STDMETHODCALLTYPE Close() = 0;
};
struct IVectorViewHString : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE GetAt(UINT32 index, HSTRING* item) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Size(UINT32* size) = 0;
};

const IID IID_Factory = {0x7257b416, 0x8ba2, 0x4751, {0x9d, 0x30, 0x9d, 0x85, 0x43, 0x56, 0x53, 0xa2}};
const IID IID_AsyncInfo = {0x00000036, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

template <typename T>
struct Ptr {
    T* p = nullptr;
    ~Ptr() {
        if (p) p->Release();
    }
    void** OutVoid() { return reinterpret_cast<void**>(&p); }
};

struct HStr {
    HSTRING h = nullptr;
    explicit HStr(const std::wstring& s) { WindowsCreateString(s.c_str(), static_cast<UINT32>(s.size()), &h); }
    ~HStr() {
        if (h) WindowsDeleteString(h);
    }
};

}  // namespace

struct TextPrediction::Impl {
    ITextPredictionGenerator* gen = nullptr;
    ~Impl() {
        if (gen) gen->Release();
    }
};

TextPrediction::TextPrediction() : impl_(std::make_unique<Impl>()) {}
TextPrediction::~TextPrediction() = default;

bool TextPrediction::Ready() const { return impl_->gen != nullptr; }

bool TextPrediction::Init(const std::wstring& languageTag) {
    cache_.clear();
    if (impl_->gen) {
        impl_->gen->Release();
        impl_->gen = nullptr;
    }
    if (languageTag.empty()) return false;
    HStr cls(L"Windows.Data.Text.TextPredictionGenerator");
    Ptr<ITextPredictionGeneratorFactory> factory;
    HRESULT hr = RoGetActivationFactory(cls.h, IID_Factory, factory.OutVoid());
    if (hr == CO_E_NOTINITIALIZED) {  // a thread without COM (the tools): WinRT for this thread
        RoInitialize(RO_INIT_MULTITHREADED);
        hr = RoGetActivationFactory(cls.h, IID_Factory, factory.OutVoid());
    }
    if (FAILED(hr) || !factory.p) return false;
    HStr tag(languageTag);
    if (FAILED(factory.p->Create(tag.h, &impl_->gen)) || !impl_->gen) return false;
    boolean notInstalled = 0;
    impl_->gen->get_LanguageAvailableButNotInstalled(&notInstalled);
    // A generator for a language that is not installed answers nothing: treat it as not there.
    if (notInstalled) {
        impl_->gen->Release();
        impl_->gen = nullptr;
    }
    return Ready();
}

const std::vector<std::wstring>& TextPrediction::Candidates(const std::wstring& input) const {
    static const std::vector<std::wstring> none;
    if (!impl_->gen || input.empty()) return none;
    if (auto it = cache_.find(input); it != cache_.end()) return it->second;
    if (cache_.size() > 4000) cache_.clear();
    std::vector<std::wstring>& out = cache_[input];
    HStr in(input);
    Ptr<IInspectable> opObj;
    if (FAILED(impl_->gen->GetCandidatesWithMaxCountAsync(in.h, 20, &opObj.p)) || !opObj.p) return out;
    Ptr<IAsyncInfoRaw> info;
    if (FAILED(opObj.p->QueryInterface(IID_AsyncInfo, info.OutVoid())) || !info.p) return out;
    INT32 status = 0;
    for (int waited = 0; waited < 150; waited += 2) {  // usually a few milliseconds
        if (FAILED(info.p->get_Status(&status)) || status != 0) break;
        Sleep(2);
    }
    if (status != 1) {
        info.p->Cancel();
        return out;
    }
    auto* op = reinterpret_cast<IAsyncOperationRaw*>(opObj.p);  // the operation's own vtable follows IInspectable
    Ptr<IVectorViewHString> list;
    if (FAILED(op->GetResults(list.OutVoid())) || !list.p) return out;
    UINT32 n = 0;
    list.p->get_Size(&n);
    for (UINT32 i = 0; i < n; ++i) {
        HSTRING h = nullptr;
        if (FAILED(list.p->GetAt(i, &h)) || !h) continue;
        UINT32 len = 0;
        const wchar_t* raw = WindowsGetStringRawBuffer(h, &len);
        out.emplace_back(raw ? raw : L"", len);
        WindowsDeleteString(h);
    }
    return out;
}

}  // namespace gct
