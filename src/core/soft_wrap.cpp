// soft_wrap.cpp — see soft_wrap.hpp.

#include "core/soft_wrap.hpp"

namespace gct {

std::string Flat(const char* s) {
    std::string f;
    for (const char* p = s; *p; ++p) {
        if (*p == '\r') {
            if (p[1] == '\n') ++p;  // a break inside a word: nothing
            continue;
        }
        f += *p == '\n' ? ' ' : *p;
    }
    return f;
}

size_t RawOffset(const char* raw, size_t rawLen, size_t flatByte) {
    size_t flat = 0, i = 0;
    while (i < rawLen && flat < flatByte) {
        if (raw[i] == '\r') {
            i += (i + 1 < rawLen && raw[i + 1] == '\n') ? 2 : 1;
            continue;
        }
        ++flat;
        ++i;
    }
    while (i < rawLen && raw[i] == '\r') i += (i + 1 < rawLen && raw[i + 1] == '\n') ? 2 : 1;  // after a word break
    return i;
}

}  // namespace gct
