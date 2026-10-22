// Exercises the real add-on translator/process boundary with a deterministic fake helper.
#include <windows.h>

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "core/translator.hpp"
#include "win/local_mt_translator.hpp"

namespace {
bool Check(bool ok, const char* label) {
    if (!ok) std::cerr << "[FAIL] " << label << '\n';
    return ok;
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
    std::filesystem::remove_all(data, ec);

    if (!ok) return 1;
    std::cout << "Local translator process self-test: PASS (segments, validation, crash, restart, timeout)\n";
    return 0;
}
