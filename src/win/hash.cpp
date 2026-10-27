// hash.cpp — SHA-256 with Windows CNG (bcrypt), no own crypto code.
#include "win/hash.hpp"

#include <windows.h>

#include <bcrypt.h>

#include <algorithm>
#include <vector>

#ifdef _MSC_VER
#pragma comment(lib, "bcrypt.lib")
#endif

namespace gct {
namespace {

std::string Hex(const unsigned char (&hash)[32]) {
    static const char* digits = "0123456789abcdef";
    std::string hex;
    hex.reserve(64);
    for (unsigned char b : hash) {
        hex += digits[b >> 4];
        hex += digits[b & 15];
    }
    return hex;
}

}  // namespace

Sha256::Sha256() {
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return;
    BCRYPT_HASH_HANDLE hash = nullptr;
    // Without a hash-object buffer CNG allocates it itself (Windows 7+).
    if (BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) != 0) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return;
    }
    alg_ = alg;
    hash_ = hash;
}

Sha256::~Sha256() {
    if (hash_) BCryptDestroyHash(static_cast<BCRYPT_HASH_HANDLE>(hash_));
    if (alg_) BCryptCloseAlgorithmProvider(static_cast<BCRYPT_ALG_HANDLE>(alg_), 0);
}

void Sha256::Update(const void* data, size_t bytes) {
    auto* p = static_cast<const unsigned char*>(data);
    while (hash_ && bytes > 0) {
        const ULONG n = static_cast<ULONG>(std::min<size_t>(bytes, 0x40000000));  // ULONG per call
        if (BCryptHashData(static_cast<BCRYPT_HASH_HANDLE>(hash_), const_cast<PUCHAR>(p), n, 0) != 0) {
            // A failed update would give a wrong hash later: spend the object so FinishHex() is empty.
            BCryptDestroyHash(static_cast<BCRYPT_HASH_HANDLE>(hash_));
            hash_ = nullptr;
            return;
        }
        p += n;
        bytes -= n;
    }
}

std::string Sha256::FinishHex() {
    if (!hash_) return {};
    unsigned char hash[32] = {};
    const bool ok = BCryptFinishHash(static_cast<BCRYPT_HASH_HANDLE>(hash_), hash, sizeof(hash), 0) == 0;
    BCryptDestroyHash(static_cast<BCRYPT_HASH_HANDLE>(hash_));
    hash_ = nullptr;
    return ok ? Hex(hash) : std::string();
}

}  // namespace gct
