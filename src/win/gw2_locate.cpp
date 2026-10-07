#include "win/gw2_locate.hpp"

#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <cwchar>
#include <fstream>
#include <iterator>
#include <vector>

namespace gct {
namespace {

constexpr const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr const wchar_t* kRunValue = L"GW2ChatTranslator";
constexpr const wchar_t* kIniName = L"gw2-chat-translator.ini";

std::wstring ReadRegString(HKEY root, const wchar_t* subKey, const wchar_t* value, REGSAM view) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subKey, 0, KEY_QUERY_VALUE | view, &key) != ERROR_SUCCESS) return {};
    std::wstring out;
    DWORD type = 0, bytes = 0;
    if (RegQueryValueExW(key, value, nullptr, &type, nullptr, &bytes) == ERROR_SUCCESS &&
        (type == REG_SZ || type == REG_EXPAND_SZ) && bytes >= sizeof(wchar_t)) {
        std::vector<wchar_t> buf(bytes / sizeof(wchar_t) + 1, L'\0');
        if (RegQueryValueExW(key, value, nullptr, &type, reinterpret_cast<BYTE*>(buf.data()), &bytes) == ERROR_SUCCESS) {
            out.assign(buf.data());
            if (type == REG_EXPAND_SZ) {
                wchar_t expanded[MAX_PATH * 2] = {};
                if (ExpandEnvironmentStringsW(out.c_str(), expanded, static_cast<DWORD>(std::size(expanded))))
                    out = expanded;
            }
        }
    }
    RegCloseKey(key);
    return out;
}

std::string ReadSmallFile(const std::wstring& path) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return {};
    std::string data;
    LARGE_INTEGER size{};
    if (GetFileSizeEx(f, &size) && size.QuadPart > 0 && size.QuadPart < (1 << 20)) {
        data.resize(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        if (!ReadFile(f, data.data(), static_cast<DWORD>(data.size()), &read, nullptr)) read = 0;
        data.resize(read);
    }
    CloseHandle(f);
    return data;
}

bool Exists(const std::wstring& path) {
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::wstring EnvVar(const wchar_t* name) {
    wchar_t buf[MAX_PATH * 2] = {};
    DWORD n = GetEnvironmentVariableW(name, buf, static_cast<DWORD>(std::size(buf)));
    if (n == 0 || n >= std::size(buf)) return {};
    return buf;
}

std::wstring KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p)) && p) out = p;
    CoTaskMemFree(p);
    return out;
}

bool CreateDirs(const std::wstring& dir) {
    return SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr) == ERROR_SUCCESS || Exists(dir);
}

bool CanWriteTo(const std::wstring& dir) {
    std::wstring probe = JoinPath(dir, L".gct_write_test.tmp");
    HANDLE f = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    CloseHandle(f);
    return true;
}

std::wstring LastErrorText(DWORD err) {
    wchar_t* msg = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                   err, 0, reinterpret_cast<wchar_t*>(&msg), 0, nullptr);
    std::wstring out = msg ? msg : L"";
    if (msg) LocalFree(msg);
    while (!out.empty() && (out.back() == L'\n' || out.back() == L'\r' || out.back() == L' ')) out.pop_back();
    if (out.empty()) out = L"error " + std::to_wstring(err);
    return out;
}

}  // namespace

bool IsGw2Dir(const std::wstring& dir) {
    return !dir.empty() && (Exists(JoinPath(dir, L"Gw2-64.exe")) || Exists(JoinPath(dir, L"Gw2.exe")));
}

std::wstring ResolveGw2Dir(const std::wstring& path) {
    if (path.empty()) return {};
    std::wstring p = path;
    for (auto& c : p)
        if (c == L'/') c = L'\\';
    while (p.size() > 3 && p.back() == L'\\') p.pop_back();

    // If pointing directly to an executable:
    if (p.size() > 4 && _wcsicmp(p.c_str() + p.size() - 4, L".exe") == 0) {
        size_t slash = p.find_last_of(L'\\');
        if (slash != std::wstring::npos) p = p.substr(0, slash);
    }
    if (IsGw2Dir(p)) return p;

    // If pointing to a subfolder (e.g. "addons", "addons\GW2ChatTranslator", "bin64"):
    std::wstring cur = p;
    for (int depth = 0; depth < 3; ++depth) {
        size_t slash = cur.find_last_of(L'\\');
        if (slash == std::wstring::npos || slash < 2) break;
        cur = cur.substr(0, slash);
        if (IsGw2Dir(cur)) return cur;
    }
    return {};
}

void ScanShortcuts(std::vector<std::wstring>& out) {
    const KNOWNFOLDERID folders[] = {FOLDERID_Desktop, FOLDERID_PublicDesktop, FOLDERID_Programs, FOLDERID_CommonPrograms};
    for (const auto& fid : folders) {
        std::wstring dir = KnownFolder(fid);
        if (dir.empty()) continue;
        WIN32_FIND_DATAW fd{};
        HANDLE h = FindFirstFileW(JoinPath(dir, L"*.lnk").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            std::wstring name = fd.cFileName;
            for (auto& c : name) c = static_cast<wchar_t>(towlower(c));
            if (name.find(L"guild wars 2") != std::wstring::npos || name.find(L"gw2") != std::wstring::npos) {
                IShellLinkW* link = nullptr;
                if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) {
                    IPersistFile* pf = nullptr;
                    if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&pf)))) {
                        if (SUCCEEDED(pf->Load(JoinPath(dir, fd.cFileName).c_str(), STGM_READ))) {
                            wchar_t target[MAX_PATH * 2] = {};
                            if (SUCCEEDED(link->GetPath(target, static_cast<int>(std::size(target)), nullptr, 0)) && target[0]) {
                                std::wstring gameDir = GameDirFromRegistryValue(target);
                                if (!gameDir.empty()) out.push_back(gameDir);
                            }
                        }
                        pf->Release();
                    }
                    link->Release();
                }
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
}

std::wstring FindGw2Dir() {
    std::vector<std::wstring> registry;
    ScanShortcuts(registry);
    const REGSAM views[] = {KEY_WOW64_64KEY, KEY_WOW64_32KEY};
    for (REGSAM view : views) {
        for (HKEY root : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER}) {
            registry.push_back(ReadRegString(root, L"SOFTWARE\\ArenaNet\\Guild Wars 2", L"Path", view));
            registry.push_back(ReadRegString(root, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Guild Wars 2",
                                             L"InstallLocation", view));
            registry.push_back(ReadRegString(root, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Guild Wars 2",
                                             L"DisplayIcon", view));
        }
    }
    // Steam: steamapps\libraryfolders.vdf lists every library.
    std::vector<std::wstring> steamLibs;
    std::wstring steam = ReadRegString(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", 0);
    if (steam.empty()) steam = ReadRegString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Valve\\Steam", L"InstallPath", KEY_WOW64_32KEY);
    if (!steam.empty()) {
        for (auto& c : steam)
            if (c == L'/') c = L'\\';
        steamLibs.push_back(steam);
        for (const auto& lib : ParseSteamLibraryPaths(ReadSmallFile(JoinPath(steam, L"steamapps\\libraryfolders.vdf"))))
            steamLibs.push_back(lib);
    }
    std::vector<std::wstring> roots;
    for (const wchar_t* var : {L"ProgramFiles", L"ProgramW6432", L"ProgramFiles(x86)"}) {
        std::wstring v = EnvVar(var);
        if (!v.empty()) roots.push_back(v);
    }
    DWORD drives = GetLogicalDrives();
    for (int i = 2; i < 26; ++i) {  // C: .. Z:
        if (!(drives & (1u << i))) continue;
        std::wstring root = std::wstring(1, static_cast<wchar_t>(L'A' + i)) + L":\\";
        if (GetDriveTypeW(root.c_str()) != DRIVE_FIXED) continue;
        roots.push_back(root.substr(0, 2));
        roots.push_back(root + L"Games");
        roots.push_back(root + L"Program Files");
    }
    for (const auto& dir : Gw2DirCandidates(L"", registry, steamLibs, roots))
        if (IsGw2Dir(dir)) return dir;
    return {};
}

std::wstring FileDescription(const std::wstring& path) {
    DWORD handle = 0;
    DWORD size = GetFileVersionInfoSizeW(path.c_str(), &handle);
    if (size == 0) return {};
    std::vector<BYTE> data(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data())) return {};
    struct LangCp {
        WORD lang;
        WORD cp;
    };
    LangCp* langs = nullptr;
    UINT langBytes = 0;
    std::vector<LangCp> tries;
    if (VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&langs), &langBytes) && langs)
        for (UINT i = 0; i < langBytes / sizeof(LangCp); ++i) tries.push_back(langs[i]);
    tries.push_back({0x0409, 0x04B0});
    tries.push_back({0x0409, 0x04E4});
    std::wstring out;
    for (const wchar_t* field : {L"FileDescription", L"ProductName", L"CompanyName"}) {
        for (const auto& t : tries) {
            wchar_t sub[96];
            swprintf(sub, std::size(sub), L"\\StringFileInfo\\%04x%04x\\%ls", t.lang, t.cp, field);
            wchar_t* text = nullptr;
            UINT len = 0;
            if (VerQueryValueW(data.data(), sub, reinterpret_cast<void**>(&text), &len) && text && len > 1) {
                std::wstring v(text);
                if (!v.empty() && out.find(v) == std::wstring::npos) out += (out.empty() ? L"" : L" / ") + v;
                break;
            }
        }
    }
    return out;
}

AddonEnvironment ScanAddons(const std::wstring& gameDir) {
    if (gameDir.empty()) return {};
    return DetectAddons([&](const std::wstring& rel) { return Exists(JoinPath(gameDir, rel)); },
                        [&](const std::wstring& rel) { return FileDescription(JoinPath(gameDir, rel)); });
}

std::wstring CurrentExePath() {
    std::vector<wchar_t> buf(MAX_PATH);
    for (;;) {
        DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0) return {};
        if (n < buf.size()) return std::wstring(buf.data(), n);
        buf.resize(buf.size() * 2);
    }
}

std::wstring CurrentExeDir() {
    std::wstring p = CurrentExePath();
    size_t slash = p.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : p.substr(0, slash);
}

std::wstring UserInstallDir() {
    std::wstring local = KnownFolder(FOLDERID_LocalAppData);
    if (local.empty()) return {};
    return JoinPath(JoinPath(local, L"Programs"), kInstallFolderName);
}

InstallResult InstallTo(const std::wstring& targetDir) {
    InstallResult r;
    std::wstring self = CurrentExePath();
    std::wstring selfDir = CurrentExeDir();
    if (self.empty() || targetDir.empty()) {
        r.error = L"no target folder";
        return r;
    }
    if (CompareStringOrdinal(selfDir.c_str(), -1, targetDir.c_str(), -1, TRUE) == CSTR_EQUAL) {
        r.ok = r.alreadyThere = true;
        r.exePath = self;
        return r;
    }
    if (!CreateDirs(targetDir) || !CanWriteTo(targetDir)) {
        r.error = LastErrorText(ERROR_ACCESS_DENIED);
        return r;
    }
    size_t slash = self.find_last_of(L"\\/");
    std::wstring exeName = slash == std::wstring::npos ? self : self.substr(slash + 1);
    std::wstring dest = JoinPath(targetDir, exeName);
    if (!CopyFileW(self.c_str(), dest.c_str(), FALSE)) {
        r.error = LastErrorText(GetLastError());
        return r;
    }
    // Take the settings and your words along, but never overwrite files already there.
    for (const wchar_t* name : {kIniName, L"my-gw2-words.txt", L"gw2-woerter.txt"}) {
        const std::wstring from = JoinPath(selfDir, name), to = JoinPath(targetDir, name);
        if (Exists(from) && !Exists(to)) CopyFileW(from.c_str(), to.c_str(), TRUE);
    }
    const std::wstring learnedFrom = JoinPath(selfDir, L"learned"), learnedTo = JoinPath(targetDir, L"learned");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(JoinPath(learnedFrom, L"*.txt").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        CreateDirs(learnedTo);
        do {
            const std::wstring to = JoinPath(learnedTo, fd.cFileName);
            if (!Exists(to)) CopyFileW(JoinPath(learnedFrom, fd.cFileName).c_str(), to.c_str(), TRUE);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    r.ok = true;
    r.exePath = dest;
    return r;
}

bool SetAutostart(bool enable, const std::wstring& exePath) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return false;
    LSTATUS st;
    if (enable) {
        std::wstring cmd = AutostartCommand(exePath);
        st = RegSetValueExW(key, kRunValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()),
                            static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        st = RegDeleteValueW(key, kRunValue);
        if (st == ERROR_FILE_NOT_FOUND) st = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    return st == ERROR_SUCCESS;
}

std::wstring AutostartTarget() {
    std::wstring cmd = ReadRegString(HKEY_CURRENT_USER, kRunKey, kRunValue, 0);
    if (cmd.empty()) return {};
    if (cmd[0] == L'"') {
        size_t close = cmd.find(L'"', 1);
        return close == std::wstring::npos ? cmd.substr(1) : cmd.substr(1, close - 1);
    }
    size_t space = cmd.find(L' ');
    return space == std::wstring::npos ? cmd : cmd.substr(0, space);
}

bool IsAutostartEnabled() {
    std::wstring target = AutostartTarget();
    return !target.empty() && Exists(target);
}

std::wstring PickFolder(void* owner, const std::wstring& title, const std::wstring& initial) {
    std::wstring out;
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg))) || !dlg)
        return out;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    if (!title.empty()) dlg->SetTitle(title.c_str());
    if (!initial.empty()) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(initial.c_str(), nullptr, IID_PPV_ARGS(&item))) && item) {
            dlg->SetFolder(item);
            item->Release();
        }
    }
    if (SUCCEEDED(dlg->Show(static_cast<HWND>(owner)))) {
        IShellItem* result = nullptr;
        if (SUCCEEDED(dlg->GetResult(&result)) && result) {
            PWSTR path = nullptr;
            if (SUCCEEDED(result->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) out = path;
            CoTaskMemFree(path);
            result->Release();
        }
    }
    dlg->Release();
    return out;
}

std::wstring PickTextFile(void* owner, const std::wstring& title, bool save, const std::wstring& name) {
    std::wstring out;
    IFileDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dlg))) ||
        !dlg)
        return out;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_FORCEFILESYSTEM | (save ? FOS_OVERWRITEPROMPT : FOS_FILEMUSTEXIST));
    const COMDLG_FILTERSPEC types[] = {{L"Text", L"*.txt"}, {L"*", L"*.*"}};
    dlg->SetFileTypes(2, types);
    dlg->SetDefaultExtension(L"txt");
    if (!title.empty()) dlg->SetTitle(title.c_str());
    if (save && !name.empty()) dlg->SetFileName(name.c_str());
    if (SUCCEEDED(dlg->Show(static_cast<HWND>(owner)))) {
        IShellItem* result = nullptr;
        if (SUCCEEDED(dlg->GetResult(&result)) && result) {
            PWSTR path = nullptr;
            if (SUCCEEDED(result->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) out = path;
            CoTaskMemFree(path);
            result->Release();
        }
    }
    dlg->Release();
    return out;
}

}  // namespace gct
