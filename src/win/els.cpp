// els.cpp
#include "els.hpp"

#include <windows.h>
#include <elscore.h>

#include <mutex>

#include "core/langs.hpp"
#include "core/text.hpp"

namespace gct {
namespace {

// Service GUIDs (elssrvc.h), defined here to stay independent of SDK headers.
const GUID kLanguageDetection = {0xcf7e00b1, 0x909b, 0x4d95, {0xa8, 0xf4, 0x61, 0x1f, 0x7c, 0x37, 0x77, 0x02}};
const GUID kCyrillicToLatin = {0x3dd12a98, 0x5afd, 0x4903, {0xa1, 0x3f, 0xe1, 0x7e, 0x6c, 0x0b, 0xfe, 0x01}};
const GUID kDevanagariToLatin = {0xc4a4dcfe, 0x2661, 0x4d02, {0x98, 0x35, 0xf4, 0x81, 0x87, 0x10, 0x98, 0x03}};

using FnGetServices = HRESULT(WINAPI*)(PMAPPING_ENUM_OPTIONS, PMAPPING_SERVICE_INFO*, DWORD*);
using FnRecognize = HRESULT(WINAPI*)(PMAPPING_SERVICE_INFO, LPCWSTR, DWORD, DWORD, PMAPPING_OPTIONS,
                                     PMAPPING_PROPERTY_BAG);
using FnFreeBag = HRESULT(WINAPI*)(PMAPPING_PROPERTY_BAG);

struct Els {
    HMODULE dll = nullptr;
    FnGetServices getServices = nullptr;
    FnRecognize recognize = nullptr;
    FnFreeBag freeBag = nullptr;
    PMAPPING_SERVICE_INFO detect = nullptr, cyrillic = nullptr, devanagari = nullptr;
    std::mutex lock;

    Els() {
        dll = LoadLibraryW(L"elscore.dll");
        if (!dll) return;
        getServices = reinterpret_cast<FnGetServices>(reinterpret_cast<void*>(GetProcAddress(dll, "MappingGetServices")));
        recognize = reinterpret_cast<FnRecognize>(reinterpret_cast<void*>(GetProcAddress(dll, "MappingRecognizeText")));
        freeBag = reinterpret_cast<FnFreeBag>(reinterpret_cast<void*>(GetProcAddress(dll, "MappingFreePropertyBag")));
        if (!getServices || !recognize || !freeBag) return;
        detect = Service(kLanguageDetection);
        cyrillic = Service(kCyrillicToLatin);
        devanagari = Service(kDevanagariToLatin);
    }

    // Services stay allocated for the lifetime of the process (tiny, freed at exit).
    PMAPPING_SERVICE_INFO Service(const GUID& g) {
        MAPPING_ENUM_OPTIONS opt{};
        opt.Size = sizeof(opt);
        GUID copy = g;
        opt.pGuid = &copy;
        PMAPPING_SERVICE_INFO services = nullptr;
        DWORD count = 0;
        if (FAILED(getServices(&opt, &services, &count)) || count == 0) return nullptr;
        return services;
    }

    // Runs a service; returns the first result range's data as text.
    bool Run(PMAPPING_SERVICE_INFO svc, const std::wstring& text, std::wstring& out) {
        if (!svc || text.empty()) return false;
        std::lock_guard<std::mutex> guard(lock);
        MAPPING_PROPERTY_BAG bag{};
        bag.Size = sizeof(bag);
        if (FAILED(recognize(svc, text.c_str(), static_cast<DWORD>(text.size()), 0, nullptr, &bag))) return false;
        bool ok = false;
        if (bag.dwRangesCount > 0 && bag.prgResultRanges && bag.prgResultRanges[0].pData) {
            const auto* data = static_cast<const wchar_t*>(bag.prgResultRanges[0].pData);
            const size_t chars = bag.prgResultRanges[0].dwDataSize / sizeof(wchar_t);
            out.assign(data, chars);
            while (!out.empty() && out.back() == L'\0') out.pop_back();
            ok = true;
        }
        freeBag(&bag);
        return ok;
    }
};

Els& Instance() {
    static Els els;
    return els;
}

}  // namespace

bool ElsAvailable() { return Instance().detect != nullptr; }

std::wstring DetectLanguage(const std::wstring& text) {
    const std::wstring t = Trim(text);
    if (CodePointCount(t) < 12) return {};  // too short to be reliable
    std::wstring list;
    if (!Instance().Run(Instance().detect, t, list)) return {};
    // Double-null-terminated list, most likely first ("de\0en\0").
    const std::wstring first = list.substr(0, list.find(L'\0'));
    if (first.empty() || first == L"und") return {};
    return PrimaryLang(first);
}

std::wstring TransliterateToLatin(const std::wstring& text) {
    Els& e = Instance();
    std::wstring out = text, tmp;
    bool cyr = false, dev = false;
    for (wchar_t c : text) {
        cyr = cyr || (c >= 0x0400 && c <= 0x052F);
        dev = dev || (c >= 0x0900 && c <= 0x097F);
    }
    if (cyr && e.Run(e.cyrillic, out, tmp) && !tmp.empty()) out = tmp;
    if (dev && e.Run(e.devanagari, out, tmp) && !tmp.empty()) out = tmp;
    return out;
}

}  // namespace gct
