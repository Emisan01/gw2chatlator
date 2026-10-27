// hash.hpp — SHA-256 (Windows CNG / bcrypt) for checking downloads: one-shot and incremental.
#pragma once

#include <cstddef>
#include <string>

namespace gct {


// Incremental: Update() with every chunk, Finish() once.
class Sha256 {
public:
    Sha256();
    ~Sha256();
    Sha256(const Sha256&) = delete;
    Sha256& operator=(const Sha256&) = delete;
    bool Ok() const { return hash_ != nullptr; }
    void Update(const void* data, size_t bytes);
    std::string FinishHex();  // lower-case hex; empty on failure; the object is spent afterwards

private:
    void* alg_ = nullptr;   // BCRYPT_ALG_HANDLE
    void* hash_ = nullptr;  // BCRYPT_HASH_HANDLE
};


}  // namespace gct
