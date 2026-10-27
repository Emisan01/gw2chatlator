// local_mt_packages.cpp — installs only the helper or the selected model pair, on the user's click.
#include "win/local_mt_packages.hpp"

#include <windows.h>
#include <tlhelp32.h>

#include <fstream>
#include <sstream>
#include <vector>

#include "core/json.hpp"
#include "core/local_mt_protocol.hpp"
#include "core/text.hpp"
#include "win/extras_install.hpp"
#include "win/local_mt_translator.hpp"

namespace gct {
namespace {

constexpr const char* kDownloadPrefix = "https://github.com/Emisan01/gw2chatlator/releases/download/";
constexpr const wchar_t* kHelperAsset = L"GW2Keyboard-LocalMT.zip";

bool Exists(const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

bool ExistsFile(const std::wstring& path) {
    const DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

uint64_t GetFileBytes(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return 0;
    return (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
}

bool CheckModelPackageSizes(const std::wstring& dir) {
    const std::wstring pkgPath = dir + L"\\.package.json";
    if (!ExistsFile(pkgPath)) return true;
    std::ifstream f(pkgPath, std::ios::binary);
    if (!f.is_open()) return true;
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    JsonValue root;
    if (!ParseJson(content, root) || !root.IsObject()) return false;
    const JsonValue* files = root.Get("files");
    if (!files || !files->IsObject()) return false;
    for (const auto& kv : files->obj) {
        const std::wstring filePath = dir + L"\\" + FromUtf8(kv.first);
        if (!ExistsFile(filePath)) return false;
        if (kv.second.type == JsonValue::Type::Number) {
            uint64_t expected = static_cast<uint64_t>(kv.second.n);
            if (GetFileBytes(filePath) != expected) return false;
        }
    }
    return true;
}

void WriteModelPackageMarker(const std::wstring& dir, const std::string& pair, const std::vector<std::wstring>& fileNames) {
    std::string json = "{\"pair\":\"" + JsonEscape(pair) + "\",\"files\":{";
    bool first = true;
    for (const auto& fn : fileNames) {
        if (!first) json += ",";
        first = false;
        uint64_t sz = GetFileBytes(dir + L"\\" + fn);
        json += "\"" + JsonEscape(ToUtf8(fn)) + "\":" + std::to_string(sz);
    }
    json += "}}";
    std::ofstream out(dir + L"\\.package.json", std::ios::binary | std::ios::trunc);
    if (out.is_open()) {
        out.write(json.data(), json.size());
    }
}

std::wstring FullPath(const std::wstring& path) {  // absolute and in long form, as Windows names a process image
    wchar_t full[MAX_PATH * 2] = {};
    const DWORD n = GetFullPathNameW(path.c_str(), MAX_PATH * 2, full, nullptr);
    if (!n || n >= MAX_PATH * 2) return path;
    wchar_t longPath[MAX_PATH * 2] = {};
    const DWORD m = GetLongPathNameW(full, longPath, MAX_PATH * 2);
    return m && m < MAX_PATH * 2 ? std::wstring(longPath, m) : std::wstring(full, n);
}

// The same file, however its path is written: volume and file ID. On GitHub's runner the image path a running helper
// reports did not match ours as text – the helper went on running and its exe could not be removed.
bool SameFile(const std::wstring& a, const std::wstring& b) {
    const auto id = [](const std::wstring& path, BY_HANDLE_FILE_INFORMATION& info) {
        HANDLE h = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        const bool ok = GetFileInformationByHandle(h, &info) != 0;
        CloseHandle(h);
        return ok;
    };
    BY_HANDLE_FILE_INFORMATION x{}, y{};
    if (!id(a, x) || !id(b, y)) return _wcsicmp(a.c_str(), b.c_str()) == 0;
    return x.dwVolumeSerialNumber == y.dwVolumeSerialNumber && x.nFileIndexHigh == y.nFileIndexHigh &&
           x.nFileIndexLow == y.nFileIndexLow;
}

// Ends only the helpers started from exactly this exe (a second game client with its own add-on folder keeps
// its helper) and waits until they are gone, so the file can be moved.
void TerminateHelperProcesses(const std::wstring& exePath) {
    const std::wstring wanted = FullPath(exePath);
    const size_t slash = wanted.find_last_of(L"\\/");
    const std::wstring exeName = slash == std::wstring::npos ? wanted : wanted.substr(slash + 1);
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, exeName.c_str()) != 0) continue;
            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE | SYNCHRONIZE, FALSE,
                                         pe.th32ProcessID);
            if (!process) continue;
            wchar_t image[MAX_PATH * 2] = {};
            DWORD size = MAX_PATH * 2;
            if (QueryFullProcessImageNameW(process, 0, image, &size) && SameFile(image, wanted)) {
                TerminateProcess(process, 0);
                WaitForSingleObject(process, 5000);
            }
            CloseHandle(process);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
}

constexpr const wchar_t* kHelperTexts[] = {L"THIRD_PARTY_NOTICES_LOCAL_MT.txt", L"LICENSE-CTranslate2-MIT.txt",
                                           L"LICENSE-oneDNN-Apache-2.0.txt", L"LICENSE-Apache-2.0.txt"};

void CleanupObsoleteFiles(const std::wstring& kbDir) {
    const wchar_t* obsolete[] = {
        L"gw2keyboard_nexus.dll",
        L"gw2kb_local_mt.exe",
        L"ctranslate2.dll"
    };
    for (const wchar_t* name : obsolete) {
        const std::wstring path = kbDir + L"\\" + name;
        if (ExistsFile(path)) {
            DeleteFileW(path.c_str());
        }
    }
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
    // 1. Dedicated extras release container (keeps main version releases lightweight)
    ExtrasInstallResult result = DownloadReleaseAsset(L"/repos/Emisan01/gw2chatlator/releases/tags/extras",
                                                      kDownloadPrefix, asset, zip, progress);
    // 2. Fallback to release tag matching the add-on version
    if (result.code == ExtrasInstallCode::NoAnswer && result.detail == L"HTTP 404") {
        const std::wstring release = L"/repos/Emisan01/gw2chatlator/releases/tags/v" + version;
        result = DownloadReleaseAsset(release, kDownloadPrefix, asset, zip, progress);
    }
    // 3. Fallback to latest release
    if (result.code == ExtrasInstallCode::NoAnswer && result.detail == L"HTTP 404") {
        result = DownloadReleaseAsset(L"/repos/Emisan01/gw2chatlator/releases/latest", kDownloadPrefix, asset, zip,
                                      progress);
    }
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
    if (pair == "nllb-200") {
        const std::wstring dir = dataDir + L"\\translate\\nllb-200";
        return ExistsFile(dir + L"\\model.bin") && ExistsFile(dir + L"\\sentencepiece.bpe.model") &&
               CheckModelPackageSizes(dir);
    }
    if (!FindModel(pair)) return false;
    const std::wstring dir = dataDir + L"\\translate\\" + FromUtf8(pair);
    return ExistsFile(dir + L"\\model.bin") && ExistsFile(dir + L"\\source.spm") &&
           ExistsFile(dir + L"\\target.spm") && CheckModelPackageSizes(dir);
}

bool IsLocalMtModelCorrupted(const std::wstring& dataDir, const std::string& pair) {
    const std::wstring dir = (pair == "nllb-200")
                                 ? (dataDir + L"\\translate\\nllb-200")
                                 : (dataDir + L"\\translate\\" + FromUtf8(pair));
    if (!ExistsFile(dir + L"\\model.bin")) return false;
    const std::wstring pkgPath = dir + L"\\.package.json";
    if (!ExistsFile(pkgPath)) return false;
    return !CheckModelPackageSizes(dir);
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
    if (pair != "nllb-200" && !FindModel(pair)) return false;
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
    const bool isNllb = (pair == "nllb-200");
    if (!isNllb && !FindModel(pair)) {
        result.code = ExtrasInstallCode::NotChecked;
        result.detail = L"unknown language pair";
        return result;
    }
    if (LocalMtModelPresent(dataDir, pair)) {
        result.code = ExtrasInstallCode::DownloadFailed;
        result.detail = L"this language model is already installed";
        return result;
    }
    const std::wstring work = WorkDir();
    if (work.empty() || !CreateDirectoryW(work.c_str(), nullptr)) {
        result.code = ExtrasInstallCode::DownloadFailed;
        result.detail = L"could not create a temporary install folder";
        return result;
    }
    const std::wstring asset = isNllb ? L"GW2Keyboard-NLLB-200.zip"
                                      : (L"GW2Keyboard-OPUS-MT-" + FromUtf8(pair) + L".zip");
    result = DownloadAsset(asset, version, work + L"\\package.zip", progress);
    const std::wstring unpacked = work + L"\\x";
    CreateDirectoryW(unpacked.c_str(), nullptr);
    const std::wstring source = unpacked + L"\\translate\\" + FromUtf8(pair);
    const auto validFiles = [&]() {
        return isNllb
            ? (ExistsFile(source + L"\\model.bin") && ExistsFile(source + L"\\sentencepiece.bpe.model"))
            : (ExistsFile(source + L"\\model.bin") && ExistsFile(source + L"\\source.spm") &&
               ExistsFile(source + L"\\target.spm") && ExistsFile(source + L"\\pair.json"));
    };
    if (result.code == ExtrasInstallCode::Ok &&
        (!Unpack(work + L"\\package.zip", unpacked) || !validFiles())) {
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
        } else {
            std::vector<std::wstring> filesToMark;
            if (isNllb) {
                filesToMark = {L"model.bin", L"sentencepiece.bpe.model"};
            } else {
                filesToMark = {L"model.bin", L"source.spm", L"target.spm", L"pair.json"};
            }
            WriteModelPackageMarker(target, pair, filesToMark);
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

ExtrasInstallResult SwapLocalMtHelper(const std::wstring& kbDir, const std::wstring& packageDir) {
    ExtrasInstallResult result;
    bool complete = ExistsFile(packageDir + L"\\gw2kb_translate.exe");
    for (const wchar_t* text : kHelperTexts) complete = complete && ExistsFile(packageDir + L"\\" + text);
    if (!complete) {
        result.code = ExtrasInstallCode::DownloadFailed;
        result.detail = L"the local translation package is incomplete";
        return result;
    }
    CreateDirectoryW(kbDir.c_str(), nullptr);
    const std::wstring targetExe = kbDir + L"\\gw2kb_translate.exe";
    const std::wstring oldExe = kbDir + L"\\gw2kb_translate.exe.old";

    TerminateHelperProcesses(targetExe);
    bool hadOld = false;
    if (ExistsFile(targetExe)) {
        DeleteFileW(oldExe.c_str());
        if (!MoveFileExW(targetExe.c_str(), oldExe.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            result.code = ExtrasInstallCode::DownloadFailed;
            result.detail = L"the old helper could not be set aside – nothing was changed";
            return result;
        }
        hadOld = true;
    }

    const bool copied = CopyFileW((packageDir + L"\\gw2kb_translate.exe").c_str(), targetExe.c_str(), FALSE) != 0;
    const HelperInfo probe = copied ? ProbeLocalMtHelper(targetExe, kbDir) : HelperInfo{};
    if (!copied || probe.status != HelperStatus::Ok) {
        DeleteFileW(targetExe.c_str());
        if (hadOld) MoveFileExW(oldExe.c_str(), targetExe.c_str(), MOVEFILE_REPLACE_EXISTING);
        result.code = ExtrasInstallCode::DownloadFailed;
        result.detail = !copied ? L"could not copy the new helper, the old one is back"
                        : probe.status == HelperStatus::Outdated
                            ? L"the new helper was rejected (" + probe.outdatedReason + L"), the old one is back"
                            : L"the new helper did not start, the old one is back";
        return result;
    }
    if (hadOld) DeleteFileW(oldExe.c_str());
    for (const wchar_t* text : kHelperTexts)
        CopyFileW((packageDir + L"\\" + text).c_str(), (kbDir + L"\\" + text).c_str(), FALSE);
    CleanupObsoleteFiles(kbDir);
    result.code = ExtrasInstallCode::Ok;
    return result;
}

ExtrasInstallResult UpdateLocalMtModule(const std::wstring& kbDir, const std::wstring& version,
                                        const std::function<bool(uint64_t, uint64_t)>& progress) {
    ExtrasInstallResult result;
    const std::wstring work = WorkDir();
    if (work.empty() || !CreateDirectoryW(work.c_str(), nullptr)) {
        result.code = ExtrasInstallCode::DownloadFailed;
        result.detail = L"could not create a temporary install folder";
        return result;
    }
    // The old helper keeps running during the download; it is only ended right before the swap.
    const std::wstring zip = work + L"\\package.zip";
    result = DownloadAsset(kHelperAsset, version, zip, progress);
    const std::wstring unpacked = work + L"\\x";
    CreateDirectoryW(unpacked.c_str(), nullptr);
    if (result.code == ExtrasInstallCode::Ok && !Unpack(zip, unpacked)) {
        result.code = ExtrasInstallCode::DownloadFailed;
        result.detail = L"the local translation package is incomplete";
    }
    if (result.code == ExtrasInstallCode::Ok) result = SwapLocalMtHelper(kbDir, unpacked + L"\\GW2Keyboard");
    RemoveTree(work);
    return result;
}

}  // namespace gct
