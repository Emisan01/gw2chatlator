// extension.cpp
#include "win/extension.hpp"

#include <windows.h>

#include <vector>

#include "core/i18n.hpp"
#include "win/files.hpp"
#include "win/gw2_locate.hpp"
#include "win/http.hpp"

namespace gct {
namespace {

bool Exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

std::wstring Join(const std::wstring& dir, const std::wstring& name) {
    return dir.empty() || dir.back() == L'\\' ? dir + name : dir + L"\\" + name;
}

// The folder in the unpacked ZIP that holds GW2ChatTranslator.exe ("GW2ChatTranslator-1.3.0" or the top itself).
std::wstring FindPackageRoot(const std::wstring& top) {
    if (Exists(Join(top, L"GW2ChatTranslator.exe"))) return top;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(Join(top, L"*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return {};
    std::wstring found;
    do {
        const std::wstring name = fd.cFileName;
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || name == L"." || name == L"..") continue;
        if (Exists(Join(Join(top, name), L"GW2ChatTranslator.exe"))) found = Join(top, name);
    } while (found.empty() && FindNextFileW(h, &fd));
    FindClose(h);
    return found;
}

// Removes the temporary folder we made (only that one, recursively).
void RemoveTree(const std::wstring& dir) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(Join(dir, L"*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            const std::wstring name = fd.cFileName;
            if (name == L"." || name == L"..") continue;
            const std::wstring p = Join(dir, name);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) RemoveTree(p);
            else DeleteFileW(p.c_str());
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir.c_str());
}

bool Unpack(const std::wstring& zip, const std::wstring& to, std::wstring* error) {
    wchar_t sys[MAX_PATH] = {};
    GetSystemDirectoryW(sys, MAX_PATH);
    const std::wstring tar = Join(sys, L"tar.exe");  // in Windows since 10 (1803)
    if (!Exists(tar)) {
        *error = Tr(L"Windows cannot unpack the file here (tar.exe missing) – download the ZIP from GitHub instead.");
        return false;
    }
    std::wstring cmd = L"\"" + tar + L"\" -xf \"" + zip + L"\" -C \"" + to + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        *error = Tr(L"The file could not be unpacked.");
        return false;
    }
    const bool done = WaitForSingleObject(pi.hProcess, 120000) == WAIT_OBJECT_0;
    DWORD code = 1;
    if (done) GetExitCodeProcess(pi.hProcess, &code);
    else TerminateProcess(pi.hProcess, 1);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (code != 0) *error = Tr(L"The file could not be unpacked.");
    return code == 0;
}

}  // namespace

std::wstring FindChatTranslatorExe() {
    const std::wstring here = Join(CurrentExeDir(), L"GW2ChatTranslator.exe");
    if (Exists(here)) return here;
    const std::wstring installed = Join(UserInstallDir(), L"GW2ChatTranslator.exe");
    if (Exists(installed)) return installed;
    const std::wstring other = FindInstalledExe();  // an older install in the GW2 add-ons folder
    if (other.size() >= 21 && _wcsicmp(other.c_str() + other.size() - 21, L"GW2ChatTranslator.exe") == 0) return other;
    return {};
}

std::wstring LoadChatExtension(const std::wstring& dataDir, std::wstring* error) {
    std::wstring dummy;
    if (!error) error = &dummy;
    const HttpResponse r = HttpRequestUrl(L"GET", kExtensionUrl, L"", "", 180000, 64u << 20);
    if (!r.transportOk || r.status != 200 || r.body.size() < 4096 || r.body.compare(0, 2, "PK") != 0) {
        *error = !r.transportOk ? r.error : TrF(L"Download failed (HTTP {1}).", {std::to_wstring(r.status)});
        return {};
    }
    wchar_t tmp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmp);
    const std::wstring work = Join(tmp, L"GW2ChatTranslator-download");
    RemoveTree(work);  // an older download (files in use stay – a new name below avoids them)
    const std::wstring dir = Join(work, std::to_wstring(GetTickCount64()));
    const std::wstring zip = Join(dir, L"package.zip");
    if (!EnsureDir(work) || !EnsureDir(dir) || !WriteFileAtomic(zip, r.body)) {
        *error = Tr(L"Could not write the file.");
        return {};
    }
    if (!Unpack(zip, dir, error)) return {};
    DeleteFileW(zip.c_str());
    const std::wstring root = FindPackageRoot(dir);
    if (root.empty()) {
        *error = Tr(L"GW2ChatTranslator.exe is not in the download.");
        return {};
    }
    // Your settings and words go along; the setup carries them into the install folder.
    for (const wchar_t* f : {L"gw2-chat-translator.ini", L"my-words.txt", L"my-names.txt", L"corrections.txt"}) {
        const std::wstring from = Join(dataDir, f);
        if (Exists(from)) CopyFileW(from.c_str(), Join(root, f).c_str(), FALSE);
    }
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(Join(Join(dataDir, L"learned"), L"*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        EnsureDir(Join(root, L"learned"));
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                CopyFileW(Join(Join(dataDir, L"learned"), fd.cFileName).c_str(), Join(Join(root, L"learned"), fd.cFileName).c_str(),
                          FALSE);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    // The chat translation is what was asked for; the setup runs as for every new download.
    const std::wstring ini = Join(root, L"gw2-chat-translator.ini");
    WritePrivateProfileStringW(L"Basic", L"KeyboardOnly", L"0", ini.c_str());
    WritePrivateProfileStringW(L"General", L"SetupDone", L"0", ini.c_str());
    return Join(root, L"GW2ChatTranslator.exe");
}

}  // namespace gct
