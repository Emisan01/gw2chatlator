// secret.cpp
#include "secret.hpp"

#include <windows.h>
#include <wincrypt.h>

#include <vector>

#include "core/text.hpp"

namespace gct {

namespace {

constexpr wchar_t kPrefix[] = L"dpapi:";
constexpr size_t kPrefixLen = 6;
constexpr wchar_t kEntropy[] = L"GW2ChatTranslator";

DATA_BLOB EntropyBlob() {
    DATA_BLOB e;
    e.pbData = reinterpret_cast<BYTE*>(const_cast<wchar_t*>(kEntropy));
    e.cbData = static_cast<DWORD>(sizeof(kEntropy));
    return e;
}

}  // namespace

bool IsProtectedSecret(const std::wstring& stored) { return stored.compare(0, kPrefixLen, kPrefix) == 0; }

std::wstring ProtectSecret(const std::wstring& plain) {
    if (plain.empty()) return L"";
    const std::string utf8 = ToUtf8(plain);
    DATA_BLOB in{static_cast<DWORD>(utf8.size()), reinterpret_cast<BYTE*>(const_cast<char*>(utf8.data()))};
    DATA_BLOB entropy = EntropyBlob(), out{};
    if (!CryptProtectData(&in, L"GW2 Chat Translator key", &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
        return AsciiEscape(plain);  // no DPAPI (Wine): stays readable as before
    DWORD len = 0;
    std::wstring b64;
    if (CryptBinaryToStringW(out.pbData, out.cbData, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &len) && len) {
        b64.resize(len);
        CryptBinaryToStringW(out.pbData, out.cbData, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, b64.data(), &len);
        b64.resize(len);
    }
    LocalFree(out.pbData);
    return b64.empty() ? AsciiEscape(plain) : kPrefix + b64;
}

std::wstring UnprotectSecret(const std::wstring& stored) {
    if (!IsProtectedSecret(stored)) return AsciiUnescape(stored);
    const std::wstring b64 = stored.substr(kPrefixLen);
    DWORD len = 0;
    if (!CryptStringToBinaryW(b64.c_str(), static_cast<DWORD>(b64.size()), CRYPT_STRING_BASE64, nullptr, &len, nullptr,
                              nullptr) ||
        !len)
        return L"";
    std::vector<BYTE> bytes(len);
    if (!CryptStringToBinaryW(b64.c_str(), static_cast<DWORD>(b64.size()), CRYPT_STRING_BASE64, bytes.data(), &len,
                              nullptr, nullptr))
        return L"";
    DATA_BLOB in{len, bytes.data()}, entropy = EntropyBlob(), out{};
    if (!CryptUnprotectData(&in, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return L"";
    const std::string utf8(reinterpret_cast<const char*>(out.pbData), out.cbData);
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return FromUtf8(utf8);
}

}  // namespace gct
