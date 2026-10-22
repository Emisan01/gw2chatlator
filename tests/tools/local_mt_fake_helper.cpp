// Fake local translator child for local_mt_translator_selftest. Never uses a model or network.
#include <windows.h>

#include <iostream>
#include <string>

#include "core/json.hpp"
#include "core/local_mt_protocol.hpp"

int wmain() {
    std::cout << "{\"evt\":\"ready\"}\n" << std::flush;
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
