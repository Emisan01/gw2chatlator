// mic_sessions.cpp
#include "win/mic_sessions.hpp"

#include <windows.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>

#include <functional>

namespace gct {
namespace {

const CLSID kMMDeviceEnumerator = {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
const IID kIMMDeviceEnumerator = {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
const IID kIAudioSessionManager2 = {0x77AA99A0, 0x1BD6, 0x484F, {0x8B, 0xC7, 0x2C, 0x65, 0x4C, 0x9A, 0x9B, 0x6F}};
const IID kIAudioSessionControl2 = {0xBFB7FF88, 0x7239, 0x4FC9, {0x8F, 0xA2, 0x07, 0xC9, 0x50, 0xBE, 0x9C, 0x6D}};
const IID kISimpleAudioVolume = {0x87CE5498, 0x68D6, 0x44E5, {0x92, 0x15, 0x6D, 0xA4, 0x7E, 0xF8, 0x83, 0xD8}};

template <class T>
void Release(T*& p) {
    if (p) p->Release();
    p = nullptr;
}

// The program's exe name from the session's identifier ("...|\Device\HarddiskVolume3\...\Discord.exe%b{...}") –
// no handle to any other process is opened (invariant 1).
std::wstring ExeOf(IAudioSessionControl2* ctl2) {
    std::wstring name;
    LPWSTR id = nullptr;
    if (SUCCEEDED(ctl2->GetSessionIdentifier(&id)) && id) {
        std::wstring s = id;
        CoTaskMemFree(id);
        const size_t end = s.find(L"%b");
        if (end != std::wstring::npos) s.resize(end);
        const size_t slash = s.find_last_of(L"\\/|");
        name = slash == std::wstring::npos ? s : s.substr(slash + 1);
    }
    return name;
}

// Calls `fn(pid, exe, volume)` for every active capture session on the device (this process left out).
void ForEachSession(const std::wstring& deviceId,
                    const std::function<void(DWORD, const std::wstring&, ISimpleAudioVolume*)>& fn) {
    const HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IMMDeviceEnumerator* en = nullptr;
    IMMDevice* dev = nullptr;
    IAudioSessionManager2* mgr = nullptr;
    IAudioSessionEnumerator* list = nullptr;
    if (SUCCEEDED(CoCreateInstance(kMMDeviceEnumerator, nullptr, CLSCTX_ALL, kIMMDeviceEnumerator,
                                   reinterpret_cast<void**>(&en))) &&
        SUCCEEDED(deviceId.empty() ? en->GetDefaultAudioEndpoint(eCapture, eConsole, &dev)
                                   : en->GetDevice(deviceId.c_str(), &dev)) &&
        SUCCEEDED(dev->Activate(kIAudioSessionManager2, CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&mgr))) &&
        SUCCEEDED(mgr->GetSessionEnumerator(&list))) {
        int count = 0;
        list->GetCount(&count);
        for (int i = 0; i < count; ++i) {
            IAudioSessionControl* ctl = nullptr;
            IAudioSessionControl2* ctl2 = nullptr;
            ISimpleAudioVolume* vol = nullptr;
            if (SUCCEEDED(list->GetSession(i, &ctl)) &&
                SUCCEEDED(ctl->QueryInterface(kIAudioSessionControl2, reinterpret_cast<void**>(&ctl2))) &&
                SUCCEEDED(ctl->QueryInterface(kISimpleAudioVolume, reinterpret_cast<void**>(&vol)))) {
                AudioSessionState state = AudioSessionStateInactive;
                DWORD pid = 0;
                ctl->GetState(&state);
                ctl2->GetProcessId(&pid);
                if (pid && pid != GetCurrentProcessId() && state != AudioSessionStateExpired) fn(pid, ExeOf(ctl2), vol);
            }
            Release(vol);
            Release(ctl2);
            Release(ctl);
        }
    }
    Release(list);
    Release(mgr);
    Release(dev);
    Release(en);
    if (SUCCEEDED(co)) CoUninitialize();
}

bool Listed(const std::vector<std::wstring>& names, const std::wstring& exe) {
    for (const std::wstring& n : names)
        if (!n.empty() && CompareStringOrdinal(n.c_str(), -1, exe.c_str(), -1, TRUE) == CSTR_EQUAL) return true;
    return false;
}

}  // namespace

std::vector<std::wstring> AppsUsingMicrophone(const std::wstring& deviceId) {
    std::vector<std::wstring> out;
    ForEachSession(deviceId, [&](DWORD, const std::wstring& exe, ISimpleAudioVolume*) {
        if (!exe.empty() && !Listed(out, exe)) out.push_back(exe);
    });
    return out;
}

}  // namespace gct
