// ocr.cpp — Windows.Media.Ocr through the raw WinRT ABI (no C++/WinRT, so
// MSVC and MinGW both build it). Interface IDs and vtable order follow the
// Windows metadata (cross-checked against the windows-rs bindings).
#include "ocr.hpp"

#include <windows.h>
#include <inspectable.h>
#include <roapi.h>
#include <winstring.h>

#include <cstring>

#ifdef _MSC_VER
#pragma comment(lib, "runtimeobject.lib")
#endif

namespace gct {
namespace abi {

struct RectF {
    float X, Y, Width, Height;
};
struct PlaneDescription {
    INT32 StartIndex, Width, Height, Stride;
};

struct IVectorViewRaw : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE GetAt(UINT32 index, void** item) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Size(UINT32* size) = 0;
    virtual HRESULT STDMETHODCALLTYPE IndexOf(void* value, UINT32* index, boolean* found) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetMany(UINT32 start, UINT32 capacity, void** items, UINT32* actual) = 0;
};
struct IOcrWord : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_BoundingRect(RectF* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Text(HSTRING* value) = 0;
};
struct IOcrLine : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Words(IVectorViewRaw** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Text(HSTRING* value) = 0;
};
struct IOcrResult : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Lines(IVectorViewRaw** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_TextAngle(IInspectable** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Text(HSTRING* value) = 0;
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
struct IOcrEngine : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE RecognizeAsync(IInspectable* bitmap, IAsyncOperationRaw** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_RecognizerLanguage(IInspectable** language) = 0;
};
struct IOcrEngineStatics : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_MaxImageDimension(UINT32* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AvailableRecognizerLanguages(IInspectable** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE IsLanguageSupported(IInspectable* language, boolean* result) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryCreateFromLanguage(IInspectable* language, IOcrEngine** engine) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryCreateFromUserProfileLanguages(IOcrEngine** engine) = 0;
};
struct ISoftwareBitmapFactory : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE Create(INT32 format, INT32 width, INT32 height, IInspectable** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateWithAlpha(INT32 format, INT32 width, INT32 height, INT32 alpha,
                                                      IInspectable** value) = 0;
};
struct ISoftwareBitmap : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_BitmapPixelFormat(INT32* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_BitmapAlphaMode(INT32* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_PixelWidth(INT32* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_PixelHeight(INT32* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsReadOnly(boolean* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_DpiX(DOUBLE value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_DpiX(DOUBLE* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_DpiY(DOUBLE value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_DpiY(DOUBLE* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE LockBuffer(INT32 mode, IInspectable** value) = 0;
};
struct IBitmapBuffer : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE GetPlaneCount(INT32* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPlaneDescription(INT32 index, PlaneDescription* value) = 0;
};
struct IMemoryBuffer : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE CreateReference(IInspectable** reference) = 0;
};
struct IMemoryBufferByteAccess : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetBuffer(BYTE** value, UINT32* capacity) = 0;
};
struct IClosable : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE Close() = 0;
};
struct ILanguageFactory : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE CreateLanguage(HSTRING tag, IInspectable** language) = 0;
};
struct ILanguage : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_LanguageTag(HSTRING* value) = 0;
};

const IID IID_IOcrEngineStatics = {0x5bffa85a, 0x3384, 0x3540, {0x99, 0x40, 0x69, 0x91, 0x20, 0xd4, 0x28, 0xa8}};
const IID IID_ISoftwareBitmapFactory = {0xc99feb69, 0x2d62, 0x4d47, {0xa6, 0xb3, 0x4f, 0xdb, 0x6a, 0x07, 0xfd, 0xf8}};
const IID IID_ISoftwareBitmap = {0x689e0708, 0x7eef, 0x483f, {0x96, 0x3f, 0xda, 0x93, 0x88, 0x18, 0xe0, 0x73}};
const IID IID_IBitmapBuffer = {0xa53e04c4, 0x399c, 0x438c, {0xb2, 0x8f, 0xa6, 0x3a, 0x6b, 0x83, 0xd1, 0xa1}};
const IID IID_IMemoryBuffer = {0xfbc4dd2a, 0x245b, 0x11e4, {0xaf, 0x98, 0x68, 0x94, 0x23, 0x26, 0x0c, 0xf8}};
const IID IID_IMemoryBufferByteAccess = {0x5b0d3235, 0x4dba, 0x4d44, {0x86, 0x5e, 0x8f, 0x1d, 0x0e, 0x4f, 0xd0, 0x4d}};
const IID IID_IClosable = {0x30d5a829, 0x7fa4, 0x4026, {0x83, 0xbb, 0xd7, 0x5b, 0xae, 0x4e, 0xa9, 0x9e}};
const IID IID_ILanguageFactory = {0x9b0252ac, 0x0c27, 0x44f8, {0xb7, 0x92, 0x97, 0x93, 0xfb, 0x66, 0xc6, 0x3e}};
const IID IID_ILanguage = {0xea79a752, 0xf7c2, 0x4265, {0xb1, 0xbd, 0xc4, 0xde, 0xc4, 0xe4, 0xf0, 0x80}};
const IID IID_IAsyncInfo = {0x00000036, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

constexpr INT32 kPixelFormatBgra8 = 87;
constexpr INT32 kBufferAccessWrite = 2;

}  // namespace abi

namespace {

template <typename T>
struct Ptr {  // minimal COM smart pointer
    T* p = nullptr;
    Ptr() = default;
    Ptr(const Ptr&) = delete;
    Ptr& operator=(const Ptr&) = delete;
    ~Ptr() { Reset(); }
    void Reset() {
        if (p) p->Release();
        p = nullptr;
    }
    T** Out() {
        Reset();
        return &p;
    }
    void** OutVoid() { return reinterpret_cast<void**>(Out()); }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

template <typename T>
bool As(IUnknown* from, const IID& iid, Ptr<T>& to) {
    return from && SUCCEEDED(from->QueryInterface(iid, to.OutVoid())) && to.p;
}

struct HStr {
    HSTRING h = nullptr;
    explicit HStr(const wchar_t* s) { WindowsCreateString(s, static_cast<UINT32>(wcslen(s)), &h); }
    ~HStr() {
        if (h) WindowsDeleteString(h);
    }
};

std::wstring TakeString(HSTRING h) {
    if (!h) return {};
    UINT32 len = 0;
    const wchar_t* raw = WindowsGetStringRawBuffer(h, &len);
    std::wstring s(raw ? raw : L"", len);
    WindowsDeleteString(h);
    return s;
}

template <typename T>
bool Factory(const wchar_t* className, const IID& iid, Ptr<T>& out) {
    HStr name(className);
    return SUCCEEDED(RoGetActivationFactory(name.h, iid, out.OutVoid())) && out.p;
}

}  // namespace

struct OcrEngine::Impl {
    Ptr<abi::IOcrEngine> engine;
    Ptr<abi::ISoftwareBitmapFactory> bitmaps;
    std::wstring language;
    UINT32 maxDimension = 0;
    bool roInitialized = false;

    ~Impl() {
        engine.Reset();
        bitmaps.Reset();
        if (roInitialized) RoUninitialize();
    }
};

OcrEngine::OcrEngine() : impl_(std::make_unique<Impl>()) {}
OcrEngine::~OcrEngine() = default;

bool OcrEngine::Ready() const { return impl_->engine.p != nullptr; }
std::wstring OcrEngine::Language() const { return impl_->language; }
int OcrEngine::MaxImageDimension() const { return static_cast<int>(impl_->maxDimension); }

bool OcrEngine::Init(const std::wstring& languageTag, std::wstring* error) {
    auto fail = [&](const wchar_t* msg) {
        if (error) *error = msg;
        return false;
    };
    const HRESULT ro = RoInitialize(RO_INIT_MULTITHREADED);
    impl_->roInitialized = SUCCEEDED(ro);  // S_FALSE counts too; RPC_E_CHANGED_MODE does not

    Ptr<abi::IOcrEngineStatics> statics;
    if (!Factory(L"Windows.Media.Ocr.OcrEngine", abi::IID_IOcrEngineStatics, statics))
        return fail(L"Windows-Texterkennung nicht verf\u00fcgbar (ab Windows 10)");
    statics->get_MaxImageDimension(&impl_->maxDimension);

    if (languageTag.empty()) {
        statics->TryCreateFromUserProfileLanguages(impl_->engine.Out());
    } else {
        Ptr<abi::ILanguageFactory> langs;
        Ptr<IInspectable> lang;
        if (Factory(L"Windows.Globalization.Language", abi::IID_ILanguageFactory, langs)) {
            HStr tag(languageTag.c_str());
            if (SUCCEEDED(langs->CreateLanguage(tag.h, lang.Out())) && lang) {
                boolean supported = 0;
                if (SUCCEEDED(statics->IsLanguageSupported(lang.p, &supported)) && supported)
                    statics->TryCreateFromLanguage(lang.p, impl_->engine.Out());
            }
        }
    }
    if (!impl_->engine)
        return fail(L"Keine Texterkennung f\u00fcr diese Sprache installiert (Windows-Einstellungen \u2192 Sprache)");

    Ptr<IInspectable> langObj;
    Ptr<abi::ILanguage> lang;
    if (SUCCEEDED(impl_->engine->get_RecognizerLanguage(langObj.Out())) && As(langObj.p, abi::IID_ILanguage, lang)) {
        HSTRING h = nullptr;
        if (SUCCEEDED(lang->get_LanguageTag(&h))) impl_->language = TakeString(h);
    }
    if (!Factory(L"Windows.Graphics.Imaging.SoftwareBitmap", abi::IID_ISoftwareBitmapFactory, impl_->bitmaps))
        return fail(L"SoftwareBitmap nicht verf\u00fcgbar");
    return true;
}

bool OcrEngine::Recognize(const Image& img, std::vector<OcrTextLine>& out, std::wstring* error) {
    out.clear();
    auto fail = [&](const wchar_t* msg) {
        if (error) *error = msg;
        return false;
    };
    if (!Ready()) return fail(L"Texterkennung nicht bereit");
    if (img.Empty()) return fail(L"Leeres Bild");
    if (impl_->maxDimension && (img.width > static_cast<int>(impl_->maxDimension) ||
                                img.height > static_cast<int>(impl_->maxDimension)))
        return fail(L"Chat-Bereich zu gro\u00df f\u00fcr die Texterkennung");

    // 1. Pixels into a SoftwareBitmap.
    Ptr<IInspectable> bitmapObj;
    Ptr<abi::ISoftwareBitmap> bitmap;
    if (FAILED(impl_->bitmaps->Create(abi::kPixelFormatBgra8, img.width, img.height, bitmapObj.Out())) ||
        !As(bitmapObj.p, abi::IID_ISoftwareBitmap, bitmap))
        return fail(L"Bild konnte nicht angelegt werden");
    {
        Ptr<IInspectable> bufferObj;
        Ptr<abi::IBitmapBuffer> buffer;
        Ptr<abi::IMemoryBuffer> memory;
        Ptr<IInspectable> reference;
        Ptr<abi::IMemoryBufferByteAccess> bytes;
        abi::PlaneDescription plane{};
        BYTE* data = nullptr;
        UINT32 capacity = 0;
        if (FAILED(bitmap->LockBuffer(abi::kBufferAccessWrite, bufferObj.Out())) ||
            !As(bufferObj.p, abi::IID_IBitmapBuffer, buffer) || FAILED(buffer->GetPlaneDescription(0, &plane)) ||
            !As(bufferObj.p, abi::IID_IMemoryBuffer, memory) || FAILED(memory->CreateReference(reference.Out())) ||
            !As(reference.p, abi::IID_IMemoryBufferByteAccess, bytes) || FAILED(bytes->GetBuffer(&data, &capacity)) ||
            !data)
            return fail(L"Bildpuffer nicht verf\u00fcgbar");
        const size_t rowBytes = static_cast<size_t>(img.width) * 4;
        for (int y = 0; y < img.height; ++y) {
            const size_t offset = static_cast<size_t>(plane.StartIndex) + static_cast<size_t>(y) * plane.Stride;
            if (offset + rowBytes > capacity) break;
            std::memcpy(data + offset, &img.bgra[static_cast<size_t>(y) * rowBytes], rowBytes);
        }
        bytes.Reset();
        Ptr<abi::IClosable> close;
        if (As(reference.p, abi::IID_IClosable, close)) close->Close();
        if (As(bufferObj.p, abi::IID_IClosable, close)) close->Close();  // unlock before use
    }

    // 2. Recognize (async operation, polled — we are on a worker thread).
    Ptr<abi::IAsyncOperationRaw> op;
    if (FAILED(impl_->engine->RecognizeAsync(bitmap.p, op.Out())) || !op) return fail(L"Texterkennung fehlgeschlagen");
    Ptr<abi::IAsyncInfoRaw> info;
    if (!As(op.p, abi::IID_IAsyncInfo, info)) return fail(L"Texterkennung fehlgeschlagen");
    INT32 status = 0;
    for (int waited = 0; waited < 5000; waited += 5) {
        if (FAILED(info->get_Status(&status)) || status != 0) break;
        Sleep(5);
    }
    if (status != 1) {
        info->Cancel();
        return fail(L"Texterkennung hat nicht geantwortet");
    }
    Ptr<abi::IOcrResult> result;
    if (FAILED(op->GetResults(result.OutVoid())) || !result) return fail(L"Texterkennung ohne Ergebnis");

    // 3. Lines and word boxes.
    Ptr<abi::IVectorViewRaw> lines;
    UINT32 lineCount = 0;
    if (FAILED(result->get_Lines(lines.Out())) || !lines || FAILED(lines->get_Size(&lineCount))) return true;
    for (UINT32 i = 0; i < lineCount; ++i) {
        Ptr<abi::IOcrLine> line;
        if (FAILED(lines->GetAt(i, line.OutVoid())) || !line) continue;
        OcrTextLine l;
        HSTRING h = nullptr;
        if (SUCCEEDED(line->get_Text(&h))) l.text = TakeString(h);
        Ptr<abi::IVectorViewRaw> words;
        UINT32 wordCount = 0;
        if (SUCCEEDED(line->get_Words(words.Out())) && words && SUCCEEDED(words->get_Size(&wordCount))) {
            for (UINT32 j = 0; j < wordCount; ++j) {
                Ptr<abi::IOcrWord> word;
                if (FAILED(words->GetAt(j, word.OutVoid())) || !word) continue;
                OcrWordBox w;
                abi::RectF r{};
                word->get_BoundingRect(&r);
                w.rect = {static_cast<int>(r.X), static_cast<int>(r.Y), static_cast<int>(r.Width + 0.5f),
                          static_cast<int>(r.Height + 0.5f)};
                HSTRING wh = nullptr;
                if (SUCCEEDED(word->get_Text(&wh))) w.text = TakeString(wh);
                l.words.push_back(std::move(w));
            }
        }
        out.push_back(std::move(l));
    }
    return true;
}

}  // namespace gct
