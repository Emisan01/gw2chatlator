// Exercises the real add-on translator/process boundary with a deterministic fake helper.
#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "core/translator.hpp"
#include "win/local_mt_packages.hpp"
#include "win/local_mt_translator.hpp"

namespace {
bool Check(bool ok, const char* label) {
    if (!ok) std::cerr << "[FAIL] " << label << '\n';
    return ok;
}

uint64_t FileBytes(const std::filesystem::path& path) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    return ec ? 0 : static_cast<uint64_t>(size);
}

// A helper package as it comes out of GW2Keyboard-LocalMT.zip: the exe plus its four texts.
void MakePackage(const std::filesystem::path& dir, const std::filesystem::path& exe) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::filesystem::copy_file(exe, dir / L"gw2kb_translate.exe", std::filesystem::copy_options::overwrite_existing, ec);
    for (const wchar_t* text : {L"THIRD_PARTY_NOTICES_LOCAL_MT.txt", L"LICENSE-CTranslate2-MIT.txt",
                                L"LICENSE-oneDNN-Apache-2.0.txt", L"LICENSE-Apache-2.0.txt"})
        std::ofstream(dir / text) << "notice";
}

// The installed old helper: the fake helper with a few bytes appended (still runs), so its size tells it apart.
void InstallOldHelper(const std::filesystem::path& kbDir, const std::filesystem::path& exe) {
    std::error_code ec;
    std::filesystem::create_directories(kbDir, ec);
    std::filesystem::copy_file(exe, kbDir / L"gw2kb_translate.exe", std::filesystem::copy_options::overwrite_existing, ec);
    std::ofstream(kbDir / L"gw2kb_translate.exe", std::ios::binary | std::ios::app) << "OLD-HELPER";
}

// Starts exe with a stdin pipe held open, so the fake helper keeps running like the add-on's live helper.
HANDLE StartRunning(const std::filesystem::path& exe, HANDLE& stdinWrite) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE childIn = nullptr;
    if (!CreatePipe(&childIn, &stdinWrite, &sa, 0)) return nullptr;
    SetHandleInformation(stdinWrite, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = childIn;
    si.hStdOutput = GetStdHandle(STD_ERROR_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    std::wstring command = L"\"" + exe.wstring() + L"\"";
    PROCESS_INFORMATION pi{};
    const BOOL created = CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                        nullptr, &si, &pi);
    CloseHandle(childIn);
    if (!created) return nullptr;
    CloseHandle(pi.hThread);
    return pi.hProcess;
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) {
        std::cerr << "Usage: local_mt_translator_selftest <fake-helper.exe>\n";
        return 2;
    }
    wchar_t temp[MAX_PATH] = {};
    if (!GetTempPathW(MAX_PATH, temp)) return 2;
    const std::filesystem::path data = std::filesystem::path(temp) /
        (L"gw2kb-localmt-selftest-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
         std::to_wstring(GetTickCount64()));
    std::error_code ec;
    std::filesystem::create_directories(data / L"translate" / L"en-fr", ec);
    if (ec) {
        std::cerr << "[FAIL] could not create temporary test data\n";
        return 2;
    }

    bool ok = true;

    // 1. Probe & capabilities tests using deterministic modes
    SetEnvironmentVariableW(L"GW2KB_FAKE_HELPER_MODE", L"full");
    const auto probeFull = gct::ProbeLocalMtHelper(argv[1], data.wstring());
    ok &= Check(probeFull.status == gct::HelperStatus::Ok && probeFull.proto == 1 &&
                probeFull.caps.size() == 2 && probeFull.build == "test-build",
                "probe full helper returns Status::Ok, proto 1, caps, build");

    SetEnvironmentVariableW(L"GW2KB_FAKE_HELPER_MODE", L"old");
    const auto probeOld = gct::ProbeLocalMtHelper(argv[1], data.wstring());
    ok &= Check(probeOld.status == gct::HelperStatus::Outdated && probeOld.proto == 0,
                "probe old helper returns Status::Outdated with proto 0");

    auto translatorOld = gct::MakeLocalMtTranslator(argv[1], data.wstring());
    const auto resOld = translatorOld->Translate({{L"hello", false}}, L"en", L"fr");
    ok &= Check(!resOld.ok && !resOld.error.empty(),
                "translation with outdated helper fails with clear error");
    gct::StopLocalMtTranslator(translatorOld);
    translatorOld.reset();

    SetEnvironmentVariableW(L"GW2KB_FAKE_HELPER_MODE", L"no-nllb");
    const auto probeNoNllbForced = gct::ProbeLocalMtHelper(argv[1], data.wstring(), true /* forceNllb */);
    ok &= Check(probeNoNllbForced.status == gct::HelperStatus::Outdated &&
                probeNoNllbForced.outdatedReason.find(L"nllb") != std::wstring::npos,
                "probe helper without nllb when forceNllb=true returns Outdated(nllb)");

    const auto probeNoNllbNormal = gct::ProbeLocalMtHelper(argv[1], data.wstring(), false /* forceNllb */);
    ok &= Check(probeNoNllbNormal.status == gct::HelperStatus::Ok,
                "probe helper without nllb when forceNllb=false returns Ok");

    auto translatorNoNllbForced = gct::MakeLocalMtTranslator(argv[1], data.wstring(), true /* forceNllb */);
    const auto resNoNllbForced = translatorNoNllbForced->Translate({{L"hello", false}}, L"en", L"fr");
    ok &= Check(!resNoNllbForced.ok && !resNoNllbForced.error.empty(),
                "translation with forceNllb fails when helper lacks nllb");
    gct::StopLocalMtTranslator(translatorNoNllbForced);
    translatorNoNllbForced.reset();

    // Reset environment mode back to full for remaining functional tests
    SetEnvironmentVariableW(L"GW2KB_FAKE_HELPER_MODE", L"full");

    // 2. Normal lifecycle tests with full helper
    auto translator = gct::MakeLocalMtTranslator(argv[1], data.wstring());
    const auto normal = translator->Translate({{L"LFG ", true}, {L"hello", false}}, L"en", L"fr");
    ok &= Check(normal.ok && normal.text == L"LFG Bonjour", "translated result preserves protected segments");

    const auto cleaned = translator->Translate({{L"symbols", false}}, L"en", L"fr");
    ok &= Check(cleaned.ok && cleaned.text == L"Bonjour", "unsupported decorative symbols are stripped");

    const auto implausible = translator->Translate({{L"only symbols", false}}, L"en", L"fr");
    ok &= Check(!implausible.ok && !implausible.error.empty(), "decorative-only output is rejected");

    const auto crashed = translator->Translate({{L"crash", false}}, L"en", L"fr");
    ok &= Check(!crashed.ok && !crashed.error.empty(), "helper crash becomes an error");

    const auto restarted = translator->Translate({{L"hello again", false}}, L"en", L"fr");
    ok &= Check(restarted.ok && restarted.text == L"Bonjour", "helper restarts after a crash");

    const ULONGLONG started = GetTickCount64();
    const auto timedOut = translator->Translate({{L"timeout", false}}, L"en", L"fr");
    const ULONGLONG elapsed = GetTickCount64() - started;
    ok &= Check(!timedOut.ok && elapsed < 7500, "a stalled helper returns an error within the timeout bound");
    gct::StopLocalMtTranslator(translator);
    translator.reset();

    // 3. Model package marker corruption detection
    const auto modelDir = data / L"translate" / L"test-pair";
    std::filesystem::create_directories(modelDir, ec);
    std::string modelData = "dummy-model-content";
    {
        std::ofstream bin(modelDir / L"model.bin", std::ios::binary);
        bin << modelData;
    }
    {
        std::ofstream pkg(modelDir / L".package.json", std::ios::binary);
        pkg << "{\"pair\":\"test-pair\",\"files\":{\"model.bin\":"
            << modelData.size() << "}}";
    }
    ok &= Check(!gct::IsLocalMtModelCorrupted(data.wstring(), "test-pair"),
                "valid model matches package marker size");

    // Tamper with model.bin size
    {
        std::ofstream bin(modelDir / L"model.bin", std::ios::binary | std::ios::app);
        bin << "-extra-tampered-bytes";
    }
    ok &= Check(gct::IsLocalMtModelCorrupted(data.wstring(), "test-pair"),
                "tampered model file size is detected as corrupted");

    // 4. The real swap (SwapLocalMtHelper) in a folder with a space and an umlaut
    const auto kbDir = data / L"add ons ä" / L"GW2Keyboard";
    const auto liveExe = kbDir / L"gw2kb_translate.exe";
    const auto oldExe = kbDir / L"gw2kb_translate.exe.old";
    const auto package = data / L"package" / L"GW2Keyboard";
    MakePackage(package, argv[1]);
    const uint64_t newBytes = FileBytes(package / L"gw2kb_translate.exe");

    // success: the new helper answers proto 1 -> it stays, .old is gone, the texts are copied
    InstallOldHelper(kbDir, argv[1]);
    const uint64_t oldBytes = FileBytes(liveExe);
    SetEnvironmentVariableW(L"GW2KB_FAKE_HELPER_MODE", L"full");
    auto swap = gct::SwapLocalMtHelper(kbDir.wstring(), package.wstring());
    ok &= Check(swap.code == gct::ExtrasInstallCode::Ok && FileBytes(liveExe) == newBytes && !std::filesystem::exists(oldExe) &&
                    std::filesystem::exists(kbDir / L"LICENSE-Apache-2.0.txt"),
                "swap: a working new helper replaces the old one and no .old is left");

    // handshake failure: the new helper still says the old "ready" -> rollback to the old exe
    InstallOldHelper(kbDir, argv[1]);
    SetEnvironmentVariableW(L"GW2KB_FAKE_HELPER_MODE", L"old");
    swap = gct::SwapLocalMtHelper(kbDir.wstring(), package.wstring());
    ok &= Check(swap.code != gct::ExtrasInstallCode::Ok && FileBytes(liveExe) == oldBytes && !std::filesystem::exists(oldExe) &&
                    swap.detail.find(L"old one is back") != std::wstring::npos,
                "swap: a new helper with an outdated handshake is rolled back to the old exe");

    // incomplete package (what a failed download leaves): nothing is touched
    const auto broken = data / L"broken" / L"GW2Keyboard";
    MakePackage(broken, argv[1]);
    std::filesystem::remove(broken / L"LICENSE-Apache-2.0.txt", ec);
    SetEnvironmentVariableW(L"GW2KB_FAKE_HELPER_MODE", L"full");
    swap = gct::SwapLocalMtHelper(kbDir.wstring(), broken.wstring());
    ok &= Check(swap.code != gct::ExtrasInstallCode::Ok && FileBytes(liveExe) == oldBytes && !std::filesystem::exists(oldExe),
                "swap: an incomplete package leaves the old helper untouched");

    // the old helper is still running: it is ended first, then replaced
    HANDLE stdinWrite = nullptr;
    HANDLE running = StartRunning(liveExe, stdinWrite);
    ok &= Check(running && WaitForSingleObject(running, 300) == WAIT_TIMEOUT, "swap: the old helper runs before the swap");
    swap = gct::SwapLocalMtHelper(kbDir.wstring(), package.wstring());
    const bool swapOk = swap.code == gct::ExtrasInstallCode::Ok, ended = running && WaitForSingleObject(running, 0) == WAIT_OBJECT_0,
               replaced = FileBytes(liveExe) == newBytes, noOld = !std::filesystem::exists(oldExe);
    if (!(swapOk && ended && replaced && noOld))  // which part, and the swap's own words (CI showed only the FAIL line)
        std::wcout << L"  swap ok " << swapOk << L", old helper ended " << ended << L", replaced " << replaced
                   << L", no .old " << noOld << L" – " << swap.detail << L"\n";
    ok &= Check(swapOk && ended && replaced && noOld, "swap: a running old helper is ended first and then replaced");
    if (running) CloseHandle(running);
    if (stdinWrite) CloseHandle(stdinWrite);

    // the same through the folder's short (8.3) path – GitHub's temp folder is "C:\Users\RUNNER~1\...": the running
    // helper is still found (the same file, however the path is written) and replaced
    wchar_t shortKb[MAX_PATH * 2] = {};
    if (GetShortPathNameW(kbDir.c_str(), shortKb, MAX_PATH * 2) && _wcsicmp(shortKb, kbDir.c_str()) != 0) {
        InstallOldHelper(kbDir, argv[1]);
        HANDLE stdinShort = nullptr;
        HANDLE runningShort = StartRunning(liveExe, stdinShort);
        swap = gct::SwapLocalMtHelper(shortKb, package.wstring());
        ok &= Check(swap.code == gct::ExtrasInstallCode::Ok && runningShort &&
                        WaitForSingleObject(runningShort, 0) == WAIT_OBJECT_0 && FileBytes(liveExe) == newBytes,
                    "swap: a running helper is found through the folder's short (8.3) path too");
        if (runningShort) CloseHandle(runningShort);
        if (stdinShort) CloseHandle(stdinShort);
    } else {
        std::cout << "(no short 8.3 names on this volume: the short path step is skipped)\n";
    }
    SetEnvironmentVariableW(L"GW2KB_FAKE_HELPER_MODE", nullptr);

    std::filesystem::remove_all(data, ec);

    if (!ok) return 1;
    std::cout << "Local translator process self-test: PASS (handshake, caps, segments, validation, crash, timeout, markers, "
                 "swap, rollback, running helper)\n";
    return 0;
}
