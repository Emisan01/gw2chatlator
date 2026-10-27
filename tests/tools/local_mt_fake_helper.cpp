// Fake local translator child for local_mt_translator_selftest. Never uses a model or network.
#include <windows.h>

#include <iostream>
#include <string>

#include "core/json.hpp"
#include "core/local_mt_protocol.hpp"

int wmain(int argc, wchar_t** argv) {
    std::string mode = "full";
    for (int i = 1; i < argc; ++i) {
        if (std::wstring(argv[i]) == L"--mode=old" || std::wstring(argv[i]) == L"--old") {
            mode = "old";
        } else if (std::wstring(argv[i]) == L"--mode=no-nllb" || std::wstring(argv[i]) == L"--no-nllb") {
            mode = "no-nllb";
        } else if (std::wstring(argv[i]) == L"--mode=full" || std::wstring(argv[i]) == L"--full") {
            mode = "full";
        }
    }
    wchar_t envMode[64] = {};
    if (GetEnvironmentVariableW(L"GW2KB_FAKE_HELPER_MODE", envMode, 64) > 0) {
        if (wcscmp(envMode, L"old") == 0) mode = "old";
        else if (wcscmp(envMode, L"no-nllb") == 0) mode = "no-nllb";
        else if (wcscmp(envMode, L"full") == 0) mode = "full";
    }

    if (mode == "old") {
        std::cout << "{\"evt\":\"ready\"}\n" << std::flush;
    } else if (mode == "no-nllb") {
        std::cout << "{\"evt\":\"ready\",\"helper\":\"translate\",\"proto\":1,\"caps\":[\"opus\"],\"build\":\"test-build\"}\n" << std::flush;
    } else {
        std::cout << "{\"evt\":\"ready\",\"helper\":\"translate\",\"proto\":1,\"caps\":[\"opus\",\"nllb\"],\"build\":\"test-build\"}\n" << std::flush;
    }

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line == "{\"cmd\":\"quit\"}") return 0;
        gct::LocalMtRequest request;
        if (!gct::ParseLocalMtRequest(line, request)) continue;
        if (request.text == "crash") return 7;
        if (request.text == "timeout") Sleep(6000);
        const std::string output = request.text == "symbols" ? "\xe2\x99\xaa Bonjour \xe2\x99\xab" :
                                   request.text == "only symbols" ? "\xe2\x99\xaa \xe2\x99\xab" : "Bonjour";
        std::cout << "{\"evt\":\"result\",\"id\":" << request.id << ",\"text\":\""
                  << gct::JsonEscape(output) << "\"}\n" << std::flush;
    }
    return 0;
}
