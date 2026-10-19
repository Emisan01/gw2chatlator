// soft_wrap.hpp — the input's soft breaks. The multi-line box wraps by itself: a break at a space is '\n' (it stands
// for that space), a word longer than the line (a chat code, a link) is broken by "\r\n" (it stands for nothing). '\r'
// never comes from the user (the input filter drops it). Equations (core_tests): Flat(wrapped t) == t, and RawOffset
// points into the wrapped text exactly where the flat text's byte stands. UTF-8 bytes, portable.
#pragma once

#include <cstddef>
#include <string>

namespace gct {

std::string Flat(const char* s);                                    // the input as it is meant: one line
size_t RawOffset(const char* raw, size_t rawLen, size_t flatByte);  // where byte `flatByte` of Flat(raw) stands in raw

}  // namespace gct
