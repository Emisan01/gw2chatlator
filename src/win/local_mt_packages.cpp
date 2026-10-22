// local_mt_packages.cpp — installs only the helper or the selected model pair, on the user's click.
#include "win/local_mt_packages.hpp"

#include <windows.h>

#include <vector>

#include "core/local_mt_protocol.hpp"
#include "core/text.hpp"
#include "win/extras_install.hpp"

namespace gct {
namespace {

constexpr const char* kDownloadPrefix = "https://github.com/Emisan01/gw2chatlator/releases/download/";
constexpr const wchar_t* kHelperAsset = L"GW2Keyboard-LocalMT.zip";

bool Exists(const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

bool ExistsFile(const std::wstring& path) {
    const DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

void RemoveTree(const std::wstring& dir) {
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            const std::wstring name = fd.cFileName;
            if (name == L"." || name == L"..") continue;
            const std::wstring path = dir + L"\\" + name;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) RemoveTree(path);
            else DeleteFileW(path.c_str());
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir.c_str());
}

bool CopyTree(const std::wstring& from, const std::wstring& to) {
    if (!CreateDirectoryW(to.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((from + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool ok = true;
    do {
        const std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        const std::wstring source = from + L"\\" + name;
        const std::wstring target = to + L"\\" + name;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ok = CopyTree(source, target) && ok;
        else ok = CopyFileW(source.c_str(), target.c_str(), TRUE) != 0 && ok;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return ok;
}

bool Unpack(const std::wstring& zip, const std::wstring& to) {
    wchar_t system[MAX_PATH] = {};
    GetSystemDirectoryW(system, MAX_PATH);
    const std::wstring tar = std::wstring(system) + L"\\tar.exe";
    if (!Exists(tar)) return false;
    std::wstring command = L"\"" + tar + L"\" -xf \"" + zip + L"\" -C \"" + to + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, nullptr, nullptr, &si, &pi)) return false;
    const bool finished = WaitForSingleObject(pi.hProcess, 120000) == WAIT_OBJECT_0;
    DWORD code = 1;
    if (finished) GetExitCodeProcess(pi.hProcess, &code);
    else TerminateProcess(pi.hProcess, 1);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return finished && code == 0;
}

std::wstring WorkDir() {
    wchar_t temp[MAX_PATH] = {};
    if (!GetTempPathW(MAX_PATH, temp)) return {};
    return std::wstring(temp) + L"gw2kb-localmt-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
           std::to_wstring(GetTickCount64());
}

ExtrasInstallResult DownloadAsset(const std::wstring& asset, const std::wstring& version, const std::wstring& zip,
                                  const std::function<bool(uint64_t, uint64_t)>& progress) {
    const std::wstring release = L"/repos/Emisan01/gw2chatlator/releases/tags/v" + version;
    ExtrasInstallResult result = DownloadReleaseAsset(release, kDownloadPrefix, asset, zip, progress);
    if (result.code == ExtrasInstallCode::NoAnswer && result.detail == L"HTTP 404")
        result = DownloadReleaseAsset(L"/repos/Emisan01/gw2chatlator/releases/latest", kDownloadPrefix, asset, zip,
                                      progress);
    return result;
}

const LocalMtModelInfo* FindModel(const std::string& pair) {
    for (const auto& model : LocalMtModelCatalog())
        if (pair == model.pair) return &model;
    return nullptr;
}

}  // namespace

bool LocalMtModulePresent(const std::wstring& kbDir) { return ExistsFile(kbDir + L"\\gw2kb_translate.exe"); }

bool LocalMtModelPresent(const std::wstring& dataDir, const std::string& pair) {
    if (!FindModel(pair)) return false;
    const std::wstring dir = dataDir + L"\\translate\\" + FromUtf8(pair);
    return ExistsFile(dir + L"\\model.bin") && ExistsFile(dir + L"\\source.spm") &&
           ExistsFile(dir + L"\\target.spm");
}

bool RemoveLocalMtModule(const std::wstring& kbDir) {
    DeleteFileW((kbDir + L"\\gw2kb_translate.exe").c_str());
    for (const wchar_t* text : {L"THIRD_PARTY_NOTICES_LOCAL_MT.txt", L"LICENSE-CTranslate2-MIT.txt",
                                L"LICENSE-oneDNN-Apache-2.0.txt", L"LICENSE-Apache-2.0.txt"})
        DeleteFileW((kbDir + L"\\" + text).c_str());
    if (LocalMtModulePresent(kbDir)) return false;
    for (const wchar_t* text : {L"THIRD_PARTY_NOTICES_LOCAL_MT.txt", L"LICENSE-CTranslate2-MIT.txt",
                                L"LICENSE-oneDNN-Apache-2.0.txt", L"LICENSE-Apache-2.0.txt"})
        if (ExistsFile(kbDir + L"\\" + text)) return false;
    return true;
}

bool RemoveLocalMtModel(const std::wstring& dataDir, const std::string& pair) {
    if (!FindModel(pair)) return false;
    const std::wstring dir = dataDir + L"\\translate\\" + FromUtf8(pair);
    RemoveTree(dir);
    return !Exists(dir);
}

ExtrasInstallResult InstallLocalMtModule(const std::wstring& kbDir, const std::wstring& version,
                                         const std::function<bool(uint64_t, uint64_t)>& progress) {
    ExtrasInstallResult result;
    const std::wstring work = WorkDir();
    if (work.empty() || !CreateDirectoryW(work.c_str(), nullptr)) {
        result.code = ExtrasInstallCode::DownloadFailed;
        result.detail = L"could not create a temporary install folder";
        return result;
    }
    const std::wstring zip = work + L"\\package.zip";
    result = DownloadAsset(kHelperAsset, version, zip, progress);
    const std::wstring unpacked = work + L"\\x";
    CreateDirectoryW(unpacked.c_str(), nullptr);
    const std::wstring package = unpacked + L"\\GW2Keyboard";
    if (result.code == ExtrasInstallCode::Ok &&
        (!Unpack(zip, unpacked) || !ExistsFile(package + L"\\gw2kb_translate.exe") ||
         !ExistsFile(package + L"\\THIRD_PARTY_NOTICES_LOCAL_MT.txt") ||
         !ExistsFile(package + L"\\LICENSE-CTranslate2-MIT.txt") ||
         !ExistsFile(package + L"\\LICENSE-oneDNN-Apache-2.0.txt") ||
         !ExistsFile(package + L"\\LICENSE-Apache-2.0.txt"))) {
        result.code = ExtrasInstallCode::DownloadFailed;
        result.detail = L"the local translation package is incomplete";
    }
    if (result.code == ExtrasInstallCode::Ok) {
        CreateDirectoryW(kbDir.c_str(), nullptr);
        bool copied = CopyFileW((package + L"\\gw2kb_translate.exe").c_str(),
                                (kbDir + L"\\gw2kb_translate.exe").c_str(), FALSE) != 0;
        for (const wchar_t* text : {L"THIRD_PARTY_NOTICES_LOCAL_MT.txt", L"LICENSE-CTranslate2-MIT.txt",
                                    L"LICENSE-oneDNN-Apache-2.0.txt", L"LICENSE-Apache-2.0.txt"})
            copied = CopyFileW((package + L"\\" + text).c_str(), (kbDir + L"\\" + text).c_str(), FALSE) != 0 && copied;
        if (!copied) {
            result.code = ExtrasInstallCode::DownloadFailed;
            result.detail = L"could not copy the local translation helper";
        }
    }
    RemoveTree(work);
    if (result.code == ExtrasInstallCode::Ok && !LocalMtModulePresent(kbDir)) {
        result.code = ExtrasInstallCode::DownloadFailed;
        result.detail = L"the local translation helper is missing after installation";
    }
    return result;
}

ExtrasInstallResult InstallLocalMtModel(const std::wstring& dataDir, const std::string& pair,
                                        const std::wstring& version,
                                        const std::function<bool(uint64_t, uint64_t)>& progress) {
    ExtrasInstallResult result;
    if (!FindModel(pair)) {
        result.code = ExtrasInstallCode::NotChecked;
        result.detail = L"unknown language pair";
        return result;
    }
    if (LocalMtModelPresent(dataDir, pair)) {
        result.code = ExtrasInstallCode::DownloadFailed;
        result.detail = L"this language pair is already installed";
        return result;
    }
    const std::wstring work = WorkDir();
    if (work.empty() || !CreateDirectoryW(work.c_str(), nullptr)) {
        result.code = ExtrasInstallCode::DownloadFailed;
        result.detail = L"could not create a temporary install folder";
        return result;
    }
    const std::wstring asset = L"GW2Keyboard-OPUS-MT-" + FromUtf8(pair) + L".zip";
    result = DownloadAsset(asset, version, work + L"\\package.zip", progress);
    const std::wstring unpacked = work + L"\\x";
    CreateDirectoryW(unpacked.c_str(), nullptr);
    const std::wstring source = unpacked + L"\\translate\\" + FromUtf8(pair);
    if (result.code == ExtrasInstallCode::Ok &&
        (!Unpack(work + L"\\package.zip", unpacked) || !ExistsFile(source + L"\\model.bin") ||
         !ExistsFile(source + L"\\source.spm") || !ExistsFile(source + L"\\target.spm") ||
         !ExistsFile(source + L"\\pair.json"))) {
        result.code = ExtrasInstallCode::DownloadFailed;
        result.detail = L"the language model package is incomplete";
    }
    if (result.code == ExtrasInstallCode::Ok) {
        const std::wstring parent = dataDir + L"\\translate";
        CreateDirectoryW(parent.c_str(), nullptr);
        const std::wstring target = parent + L"\\" + FromUtf8(pair);
        const std::wstring staging = target + L".installing-" + std::to_wstring(GetTickCount64());
        if (!CopyTree(source, staging) ||
            !MoveFileExW(staging.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH)) {
            RemoveTree(staging);
            result.code = ExtrasInstallCode::DownloadFailed;
            result.detail = L"could not put the model in its final folder";
        }
    }
    RemoveTree(work);
    if (result.code == ExtrasInstallCode::Ok && !LocalMtModelPresent(dataDir, pair)) {
        result.code = ExtrasInstallCode::DownloadFailed;
        result.detail = L"the model files are missing after installation";
        RemoveLocalMtModel(dataDir, pair);
    }
    return result;
}

}  // namespace gct
