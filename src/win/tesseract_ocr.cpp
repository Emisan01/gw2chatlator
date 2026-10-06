#include "win/tesseract_ocr.hpp"

#include <windows.h>

#include <cwchar>
#include <thread>

#include "core/i18n.hpp"
#include "core/text.hpp"
#include "win/files.hpp"

namespace gct {
namespace {

constexpr DWORD kTimeoutMs = 8000;

bool FileExists(const std::wstring& p) {
    const DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool DirExists(const std::wstring& p) {
    const DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring Env(const wchar_t* name) {
    wchar_t buf[MAX_PATH * 2] = {};
    const DWORD n = GetEnvironmentVariableW(name, buf, MAX_PATH * 2);
    return (n > 0 && n < MAX_PATH * 2) ? std::wstring(buf, n) : std::wstring();
}

std::vector<std::string> ListModels(const std::wstring& tessdata) {
    std::vector<std::string> out;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((tessdata + L"\\*.traineddata").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        std::wstring name = fd.cFileName;
        name.resize(name.size() - 12);  // ".traineddata"
        out.push_back(ToUtf8(name));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return out;
}

bool Probe(const std::wstring& candidate, TesseractInfo* info) {
    std::wstring exe = Trim(candidate);
    if (exe.empty()) return false;
    if (exe.front() == L'"' && exe.back() == L'"' && exe.size() >= 2) exe = exe.substr(1, exe.size() - 2);
    if (DirExists(exe)) exe += L"\\tesseract.exe";
    if (!FileExists(exe)) return false;
    const std::wstring dir = exe.substr(0, exe.find_last_of(L"\\/"));
    std::wstring tessdata = dir + L"\\tessdata";
    if (!DirExists(tessdata)) {
        const std::wstring env = Env(L"TESSDATA_PREFIX");
        if (!env.empty() && DirExists(env)) tessdata = env;
    }
    TesseractInfo r;
    r.exe = exe;
    r.tessdata = tessdata;
    r.models = ListModels(tessdata);
    if (r.models.empty()) return false;
    *info = std::move(r);
    return true;
}

}  // namespace

bool FindTesseract(const std::wstring& configured, TesseractInfo* info) {
    std::vector<std::wstring> candidates;
    if (!Trim(configured).empty()) candidates.push_back(configured);
    candidates.push_back(ExeDir() + L"\\tesseract");
    for (const wchar_t* var : {L"ProgramW6432", L"ProgramFiles", L"ProgramFiles(x86)"}) {
        const std::wstring v = Env(var);
        if (!v.empty()) candidates.push_back(v + L"\\Tesseract-OCR");
    }
    const std::wstring local = Env(L"LOCALAPPDATA");
    if (!local.empty()) {
        candidates.push_back(local + L"\\Programs\\Tesseract-OCR");
        candidates.push_back(local + L"\\Tesseract-OCR");
    }
    for (const std::wstring& c : candidates)
        if (Probe(c, info)) return true;
    wchar_t found[MAX_PATH] = {};
    if (SearchPathW(nullptr, L"tesseract.exe", nullptr, MAX_PATH, found, nullptr) && Probe(found, info)) return true;
    return false;
}

bool TesseractOcr::Init(const TesseractInfo& info, const std::string& langs, std::wstring* error) {
    info_ = TesseractInfo();
    if (info.exe.empty() || langs.empty()) {
        if (error) *error = Tr(L"Tesseract not found");
        return false;
    }
    info_ = info;
    langs_ = langs;
    return true;
}

std::wstring TesseractOcr::Language() const { return FromUtf8(langs_); }

bool TesseractOcr::Recognize(const Image& img, std::vector<TsvLine>& out, std::wstring* error) {
    out.clear();
    auto fail = [&](const std::wstring& e) {
        if (error) *error = e;
        return false;
    };
    if (!Ready()) return fail(Tr(L"Tesseract not found"));
    const std::string pgm = EncodePgm(img, lightText_);
    if (pgm.empty()) return fail(Tr(L"Empty image"));

    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE inRead = nullptr, inWrite = nullptr, outRead = nullptr, outWrite = nullptr;
    if (!CreatePipe(&inRead, &inWrite, &sa, 1 << 16)) return fail(Tr(L"Tesseract could not be started"));
    if (!CreatePipe(&outRead, &outWrite, &sa, 1 << 16)) {
        CloseHandle(inRead);
        CloseHandle(inWrite);
        return fail(Tr(L"Tesseract could not be started"));
    }
    SetHandleInformation(inWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = inRead;
    si.hStdOutput = outWrite;
    si.hStdError = nul != INVALID_HANDLE_VALUE ? nul : outWrite;
    std::wstring cmd = L"\"" + info_.exe + L"\" stdin stdout --tessdata-dir \"" + info_.tessdata +
                       L"\" --psm 6 -l " + FromUtf8(langs_) + L" -c tessedit_create_tsv=1" +
                       (lightText_ ? L" -c tessedit_do_invert=0" : L"");
    // One thread per process: several cores fighting over a tiny picture only makes it slower.
    std::wstring env;
    if (wchar_t* block = GetEnvironmentStringsW()) {
        for (const wchar_t* p = block; *p; p += wcslen(p) + 1) {
            const std::wstring entry = p;
            if (_wcsnicmp(entry.c_str(), L"OMP_THREAD_LIMIT=", 17) == 0) continue;
            env += entry;
            env += L'\0';
        }
        FreeEnvironmentStringsW(block);
    }
    env += L"OMP_THREAD_LIMIT=1";
    env += L'\0';
    env += L'\0';
    PROCESS_INFORMATION pi{};
    const BOOL started = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | BELOW_NORMAL_PRIORITY_CLASS,
                                        env.data(), nullptr, &si, &pi);
    CloseHandle(inRead);
    CloseHandle(outWrite);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!started) {
        CloseHandle(inWrite);
        CloseHandle(outRead);
        return fail(Tr(L"Tesseract could not be started"));
    }

    // Feed the picture on a helper thread while we read the answer.
    std::thread writer([inWrite, &pgm] {
        size_t off = 0;
        while (off < pgm.size()) {
            DWORD n = 0;
            const DWORD chunk = static_cast<DWORD>(std::min<size_t>(pgm.size() - off, 1 << 16));
            if (!WriteFile(inWrite, pgm.data() + off, chunk, &n, nullptr) || n == 0) break;
            off += n;
        }
        CloseHandle(inWrite);
    });
    std::string tsv;
    char buf[8192];
    const ULONGLONG deadline = GetTickCount64() + kTimeoutMs;
    bool timedOut = false;
    for (;;) {
        DWORD avail = 0;
        if (!PeekNamedPipe(outRead, nullptr, 0, nullptr, &avail, nullptr)) break;  // closed: process done
        if (avail == 0) {
            if (WaitForSingleObject(pi.hProcess, 15) == WAIT_OBJECT_0) {
                // Drain whatever is left.
                DWORD n = 0;
                while (PeekNamedPipe(outRead, nullptr, 0, nullptr, &avail, nullptr) && avail > 0 &&
                       ReadFile(outRead, buf, sizeof(buf), &n, nullptr) && n > 0)
                    tsv.append(buf, n);
                break;
            }
            if (GetTickCount64() > deadline) {
                timedOut = true;
                break;
            }
            continue;
        }
        DWORD n = 0;
        if (!ReadFile(outRead, buf, sizeof(buf), &n, nullptr) || n == 0) break;
        tsv.append(buf, n);
        if (tsv.size() > (8u << 20)) break;
    }
    if (timedOut || WaitForSingleObject(pi.hProcess, 2000) != WAIT_OBJECT_0) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 1000);
        timedOut = true;
    }
    writer.join();
    CloseHandle(outRead);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (timedOut) return fail(Tr(L"Tesseract did not answer in time"));
    if (code != 0 && tsv.empty()) return fail(TrF(L"Tesseract failed (exit code {1})", {std::to_wstring(code)}));
    out = ParseTesseractTsv(tsv);
    return true;
}

}  // namespace gct
