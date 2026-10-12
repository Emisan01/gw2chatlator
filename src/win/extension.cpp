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

std::wstring LoadChatExtension(std::wstring* error) {
    std::wstring dummy;
    if (!error) error = &dummy;
    const HttpResponse r = HttpRequestUrl(L"GET", kExtensionUrl, L"", "", 180000, 64u << 20);
    if (!r.transportOk || r.status != 200 || r.body.size() < 4096 || r.body.compare(0, 2, "PK") != 0) {
        *error = !r.transportOk ? r.error : TrF(L"Download failed (HTTP {1}).", {std::to_wstring(r.status)});
        return {};
    }
    wchar_t tmp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmp);
    const std::wstring work = Join(tmp, L"gw2ct-extension-" + std::to_wstring(GetTickCount64()));
    if (!EnsureDir(work) || !WriteFileAtomic(Join(work, L"package.zip"), r.body)) {
        *error = Tr(L"Could not write the file.");
        return {};
    }
    std::wstring exe;
    const std::wstring unpacked = Join(work, L"x");
    if (EnsureDir(unpacked) && Unpack(Join(work, L"package.zip"), unpacked, error)) {
        const std::wstring root = FindPackageRoot(unpacked);
        const std::wstring dest = UserInstallDir();
        if (root.empty()) {
            *error = Tr(L"GW2ChatTranslator.exe is not in the download.");
        } else if (dest.empty() || !EnsureDir(dest) || !EnsureDir(Join(dest, L"rapid"))) {
            *error = Tr(L"Could not write the file.");
        } else {
            bool ok = true;
            for (const wchar_t* f : {L"GW2ChatTranslator.exe", L"onnxruntime.dll", L"THIRD_PARTY_NOTICES.txt"})
                if (Exists(Join(root, f)) && !CopyFileW(Join(root, f).c_str(), Join(dest, f).c_str(), FALSE)) ok = false;
            WIN32_FIND_DATAW fd;
            HANDLE h = FindFirstFileW(Join(Join(root, L"rapid"), L"*").c_str(), &fd);
            if (h != INVALID_HANDLE_VALUE) {
                do {
                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                    if (!CopyFileW(Join(Join(root, L"rapid"), fd.cFileName).c_str(),
                                   Join(Join(dest, L"rapid"), fd.cFileName).c_str(), FALSE))
                        ok = false;
                } while (FindNextFileW(h, &fd));
                FindClose(h);
            }
            if (ok) exe = Join(dest, L"GW2ChatTranslator.exe");
            else *error = Tr(L"Could not copy the files (is GW2 Chat Translator still running?).");
        }
    }
    RemoveTree(work);
    return exe;
}

}  // namespace gct
