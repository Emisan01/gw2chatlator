#include "core/gw2_install.hpp"

#include <algorithm>
#include <cwctype>

namespace gct {
namespace {

// Minimal UTF-8 -> UTF-16/32 decoder (invalid bytes become U+FFFD).
std::wstring DecodeUtf8(const std::string& s) {
    std::wstring out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        char32_t cp = 0xFFFD;
        size_t len = 1;
        if (c < 0x80) {
            cp = c;
        } else if ((c >> 5) == 0x6 && i + 1 < s.size()) {
            cp = ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3Fu);
            len = 2;
        } else if ((c >> 4) == 0xE && i + 2 < s.size()) {
            cp = ((c & 0x0Fu) << 12) | ((static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 6) |
                 (static_cast<unsigned char>(s[i + 2]) & 0x3Fu);
            len = 3;
        } else if ((c >> 3) == 0x1E && i + 3 < s.size()) {
            cp = ((c & 0x07u) << 18) | ((static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 12) |
                 ((static_cast<unsigned char>(s[i + 2]) & 0x3Fu) << 6) |
                 (static_cast<unsigned char>(s[i + 3]) & 0x3Fu);
            len = 4;
        }
        i += len;
        if constexpr (sizeof(wchar_t) == 2) {
            if (cp >= 0x10000) {
                cp -= 0x10000;
                out.push_back(static_cast<wchar_t>(0xD800 + (cp >> 10)));
                out.push_back(static_cast<wchar_t>(0xDC00 + (cp & 0x3FF)));
                continue;
            }
        }
        out.push_back(static_cast<wchar_t>(cp));
    }
    return out;
}

std::wstring Trim(const std::wstring& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::iswspace(s[b])) ++b;
    while (e > b && std::iswspace(s[e - 1])) --e;
    return s.substr(b, e - b);
}

std::wstring Lower(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(std::towlower(c));
    return s;
}

std::wstring NormalizeDirKey(std::wstring d) {
    for (auto& c : d)
        if (c == L'/') c = L'\\';
    while (d.size() > 3 && d.back() == L'\\') d.pop_back();
    return Lower(d);
}

bool EndsWithNoCase(const std::wstring& s, const std::wstring& suffix) {
    if (s.size() < suffix.size()) return false;
    return Lower(s.substr(s.size() - suffix.size())) == Lower(suffix);
}

}  // namespace

std::vector<std::wstring> ParseSteamLibraryPaths(const std::string& vdfUtf8) {
    std::vector<std::wstring> out;
    std::wstring text = DecodeUtf8(vdfUtf8);
    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find(L'\n', pos);
        if (eol == std::wstring::npos) eol = text.size();
        std::wstring line = Trim(text.substr(pos, eol - pos));
        pos = eol + 1;
        // "path"		"D:\\SteamLibrary"   (new format)
        // "1"		"D:\\SteamLibrary"       (old format: numbered keys)
        if (line.size() < 4 || line[0] != L'"') continue;
        size_t keyEnd = line.find(L'"', 1);
        if (keyEnd == std::wstring::npos) continue;
        std::wstring key = line.substr(1, keyEnd - 1);
        size_t valStart = line.find(L'"', keyEnd + 1);
        if (valStart == std::wstring::npos) continue;
        std::wstring value;
        bool closed = false;
        for (size_t i = valStart + 1; i < line.size(); ++i) {
            wchar_t c = line[i];
            if (c == L'\\' && i + 1 < line.size()) {
                value.push_back(line[++i]);
            } else if (c == L'"') {
                closed = true;
                break;
            } else {
                value.push_back(c);
            }
        }
        if (!closed || value.empty()) continue;
        bool numbered = !key.empty() && std::all_of(key.begin(), key.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; });
        bool looksLikePath = value.size() >= 3 && value[1] == L':';
        if ((key == L"path" || numbered) && looksLikePath) {
            std::wstring k = NormalizeDirKey(value);
            bool dup = std::any_of(out.begin(), out.end(), [&](const std::wstring& o) { return NormalizeDirKey(o) == k; });
            if (!dup) out.push_back(value);
        }
    }
    return out;
}

std::wstring GameDirFromRegistryValue(const std::wstring& raw) {
    std::wstring v = Trim(raw);
    if (v.empty()) return {};
    if (v[0] == L'"') {
        size_t close = v.find(L'"', 1);
        v = close == std::wstring::npos ? v.substr(1) : v.substr(1, close - 1);
    } else {
        // Unquoted value with arguments: cut after ".exe".
        std::wstring low = Lower(v);
        size_t exe = low.find(L".exe");
        if (exe != std::wstring::npos) v = v.substr(0, exe + 4);
    }
    for (auto& c : v)
        if (c == L'/') c = L'\\';
    if (EndsWithNoCase(v, L".exe")) {
        size_t slash = v.find_last_of(L'\\');
        if (slash == std::wstring::npos) return {};
        v = v.substr(0, slash);
    }
    while (v.size() > 3 && v.back() == L'\\') v.pop_back();
    if (v.size() < 3 || v[1] != L':') {
        // UNC paths are fine too.
        if (!(v.size() > 2 && v[0] == L'\\' && v[1] == L'\\')) return {};
    }
    return v;
}

std::wstring JoinPath(const std::wstring& dir, const std::wstring& name) {
    if (dir.empty()) return name;
    if (dir.back() == L'\\' || dir.back() == L'/') return dir + name;
    return dir + L"\\" + name;
}

std::wstring InstallDirFor(const std::wstring& gameDir) {
    return JoinPath(JoinPath(gameDir, L"addons"), kInstallFolderName);
}

std::vector<std::wstring> Gw2DirCandidates(const std::wstring& running,
                                           const std::vector<std::wstring>& registryValues,
                                           const std::vector<std::wstring>& steamLibraries,
                                           const std::vector<std::wstring>& programFilesDirs) {
    std::vector<std::wstring> out;
    std::vector<std::wstring> keys;
    auto add = [&](const std::wstring& d) {
        if (d.empty()) return;
        std::wstring k = NormalizeDirKey(d);
        if (std::find(keys.begin(), keys.end(), k) != keys.end()) return;
        keys.push_back(k);
        std::wstring clean = d;
        while (clean.size() > 3 && (clean.back() == L'\\' || clean.back() == L'/')) clean.pop_back();
        out.push_back(clean);
    };
    add(GameDirFromRegistryValue(running.empty() ? std::wstring() : JoinPath(running, L"Gw2-64.exe")));
    for (const auto& v : registryValues) add(GameDirFromRegistryValue(v));
    for (const auto& lib : steamLibraries) add(JoinPath(JoinPath(JoinPath(lib, L"steamapps"), L"common"), L"Guild Wars 2"));
    for (const auto& pf : programFilesDirs) add(JoinPath(pf, L"Guild Wars 2"));
    return out;
}

AddonEnvironment DetectAddons(const std::function<bool(const std::wstring&)>& exists,
                              const std::function<std::wstring(const std::wstring&)>& describe) {
    AddonEnvironment env;
    auto check = [&](const std::wstring& rel) {
        if (!exists(rel)) return false;
        env.evidence.push_back(rel);
        return true;
    };

    env.proxyDll = check(L"d3d11.dll");
    env.chainload = check(L"d3d11_chainload.dll");
    std::wstring desc = env.proxyDll ? describe(L"d3d11.dll") : std::wstring();
    std::wstring chainDesc = env.chainload ? describe(L"d3d11_chainload.dll") : std::wstring();
    env.proxyDescription = desc;
    std::wstring d = Lower(desc + L" " + chainDesc);

    if (d.find(L"nexus") != std::wstring::npos || d.find(L"raidcore") != std::wstring::npos) env.nexus = true;
    if (d.find(L"arcdps") != std::wstring::npos || d.find(L"deltaconnected") != std::wstring::npos) env.arcdps = true;
    if (d.find(L"addon loader") != std::wstring::npos || d.find(L"addonloader") != std::wstring::npos) env.addonLoader = true;

    if (check(L"addons\\Nexus")) env.nexus = true;
    if (check(L"addons\\arcdps\\arcdps.ini") || check(L"addons\\arcdps.dll")) env.arcdps = true;
    if (check(L"bin64\\d3d9.dll")) env.arcdps = true;  // legacy arcdps location
    if (check(L"addonLoader.dll") || check(L"addons\\addonLoader.dll")) env.addonLoader = true;
    // Chain-loaded proxy next to Nexus is almost always arcdps.
    if (env.chainload && env.nexus && !env.arcdps) env.arcdps = true;

    for (const wchar_t* rel : {L"arcdps_unofficial_extras.dll", L"addons\\arcdps_unofficial_extras.dll",
                               L"addons\\arcdps\\arcdps_unofficial_extras.dll", L"bin64\\arcdps_unofficial_extras.dll"}) {
        if (check(rel)) {
            env.unofficialExtras = true;
            break;
        }
    }
    return env;
}

std::wstring AutostartCommand(const std::wstring& exePath) {
    return L"\"" + exePath + L"\" " + kWaitForGw2Switch;
}

bool HasSwitch(const std::wstring& cmdLine, const std::wstring& sw) {
    size_t pos = 0;
    while ((pos = cmdLine.find(sw, pos)) != std::wstring::npos) {
        bool startOk = pos == 0 || std::iswspace(cmdLine[pos - 1]) || cmdLine[pos - 1] == L'"';
        size_t end = pos + sw.size();
        bool endOk = end == cmdLine.size() || std::iswspace(cmdLine[end]) || cmdLine[end] == L'"';
        if (startOk && endOk) return true;
        pos = end;
    }
    return false;
}

}  // namespace gct
