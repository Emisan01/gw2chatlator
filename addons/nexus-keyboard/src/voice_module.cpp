#include "voice_module.hpp"

#include <windows.h>

#include <vector>

namespace gct {
namespace {

constexpr const char* kDownloadPrefix = "https://github.com/Emisan01/gw2chatlator/releases/download/";
constexpr const wchar_t* kPackage = L"GW2Keyboard-Nexus.zip";

bool Exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

std::vector<std::wstring> FilesIn(const std::wstring& dir, const wchar_t* pattern) {
    std::vector<std::wstring> out;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\" + pattern).c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) out.push_back(fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return out;
}

// Removes our temporary folder (only what we unpacked into it).
void RemoveTree(const std::wstring& dir) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            const std::wstring name = fd.cFileName;
            if (name == L"." || name == L"..") continue;
            const std::wstring p = dir + L"\\" + name;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) RemoveTree(p);
            else DeleteFileW(p.c_str());
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir.c_str());
}

bool Unpack(const std::wstring& zip, const std::wstring& to) {
    wchar_t sys[MAX_PATH] = {};
    GetSystemDirectoryW(sys, MAX_PATH);
    const std::wstring tar = std::wstring(sys) + L"\\tar.exe";  // in Windows since 10 (1803)
    if (!Exists(tar)) return false;
    std::wstring cmd = L"\"" + tar + L"\" -xf \"" + zip + L"\" -C \"" + to + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS,
                        nullptr, nullptr, &si, &pi))
        return false;
    const bool done = WaitForSingleObject(pi.hProcess, 120000) == WAIT_OBJECT_0;
    DWORD code = 1;
    if (done) GetExitCodeProcess(pi.hProcess, &code);
    else TerminateProcess(pi.hProcess, 1);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code == 0;
}

}  // namespace

bool RemoveVoiceModule(const std::wstring& kbDir) {
    DeleteFileW((kbDir + L"\\gw2kb_voice.exe").c_str());
    RemoveTree(kbDir + L"\\voice");
    for (const wchar_t* text : {L"README.txt", L"THIRD_PARTY_NOTICES.txt"}) DeleteFileW((kbDir + L"\\" + text).c_str());
    return !Exists(kbDir + L"\\gw2kb_voice.exe") && !Exists(kbDir + L"\\voice");
}

bool RemoveFolder(const std::wstring& dir) {
    RemoveTree(dir);
    return !Exists(dir);
}

bool VoiceModulePresent(const std::wstring& kbDir) {
    return Exists(kbDir + L"\\gw2kb_voice.exe") && Exists(kbDir + L"\\voice\\whisper.dll");
}

ExtrasInstallResult InstallVoiceModule(const std::wstring& kbDir, const std::wstring& version,
                                       const std::function<bool(uint64_t done, uint64_t total)>& progress) {
    wchar_t temp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, temp);
    const std::wstring work = std::wstring(temp) + L"gw2kb-voice-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                              std::to_wstring(GetTickCount64());
    CreateDirectoryW(work.c_str(), nullptr);
    const std::wstring zip = work + L"\\package.zip";
    // 1. Dedicated extras release container (keeps main version releases lightweight)
    ExtrasInstallResult r = DownloadReleaseAsset(L"/repos/Emisan01/gw2chatlator/releases/tags/extras",
                                                 kDownloadPrefix, kPackage, zip, progress);
    // 2. Fallback to release tag matching the add-on version
    if (r.code == ExtrasInstallCode::NoAnswer && r.detail == L"HTTP 404")
        r = DownloadReleaseAsset(L"/repos/Emisan01/gw2chatlator/releases/tags/v" + version,
                                 kDownloadPrefix, kPackage, zip, progress);
    // 3. Fallback to latest release
    if (r.code == ExtrasInstallCode::NoAnswer && r.detail == L"HTTP 404")
        r = DownloadReleaseAsset(L"/repos/Emisan01/gw2chatlator/releases/latest", kDownloadPrefix, kPackage, zip, progress);
    if (r.code != ExtrasInstallCode::Ok) {
        RemoveTree(work);
        return r;
    }
    const std::wstring unpacked = work + L"\\x";
    CreateDirectoryW(unpacked.c_str(), nullptr);
    const std::wstring from = unpacked + L"\\GW2Keyboard";
    if (!Unpack(zip, unpacked) || !Exists(from + L"\\gw2kb_voice.exe")) {
        RemoveTree(work);
        r.code = ExtrasInstallCode::DownloadFailed;
        r.detail = L"the package could not be unpacked";
        return r;
    }
    // Only the voice module: the helper, its DLLs, and the texts that come with it.
    CreateDirectoryW(kbDir.c_str(), nullptr);
    CreateDirectoryW((kbDir + L"\\voice").c_str(), nullptr);
    bool ok = CopyFileW((from + L"\\gw2kb_voice.exe").c_str(), (kbDir + L"\\gw2kb_voice.exe").c_str(), FALSE) != 0;
    for (const std::wstring& dll : FilesIn(from + L"\\voice", L"*.dll"))
        ok = CopyFileW((from + L"\\voice\\" + dll).c_str(), (kbDir + L"\\voice\\" + dll).c_str(), FALSE) && ok;
    for (const wchar_t* text : {L"README.txt", L"THIRD_PARTY_NOTICES.txt"})
        if (Exists(from + L"\\" + text)) CopyFileW((from + L"\\" + text).c_str(), (kbDir + L"\\" + text).c_str(), FALSE);
    RemoveTree(work);
    if (!ok || !VoiceModulePresent(kbDir)) {
        r.code = ExtrasInstallCode::DownloadFailed;
        r.detail = L"the files could not be copied";
    }
    return r;
}

}  // namespace gct
