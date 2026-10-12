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

std::string Sha256Hex(const std::string& data) {
    // One-shot BCryptHash takes a ULONG length; larger inputs go through the incremental hash.
    if (data.size() > 0xFFFFFFFFull) {
        Sha256 h;
        h.Update(data.data(), data.size());
        return h.FinishHex();
    }
    BCRYPT_ALG_HANDLE alg = nullptr;
    unsigned char hash[32] = {};
    std::string hex;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0) {
        if (BCryptHash(alg, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())),
                       static_cast<ULONG>(data.size()), hash, sizeof(hash)) == 0)
            hex = Hex(hash);
        BCryptCloseAlgorithmProvider(alg, 0);
    }
    return hex;
}

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

std::string Sha256FileHex(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};
    Sha256 sha;
    std::vector<unsigned char> buf(1u << 20);
    bool ok = sha.Ok();
    while (ok) {
        DWORD read = 0;
        if (!ReadFile(h, buf.data(), static_cast<DWORD>(buf.size()), &read, nullptr)) ok = false;
        else if (read == 0) break;  // end of file
        else sha.Update(buf.data(), read);
    }
    CloseHandle(h);
    return ok ? sha.FinishHex() : std::string();
}

}  // namespace gct
