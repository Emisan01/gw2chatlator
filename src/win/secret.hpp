// secret.hpp — API keys in the settings file, encrypted with Windows DPAPI
// (bound to this Windows account: another user or PC cannot read them).
#pragma once

#include <string>

namespace gct {

// "dpapi:<base64>" (ASCII, safe for the ini). Empty stays empty.
std::wstring ProtectSecret(const std::wstring& plain);
// Reads what ProtectSecret wrote; an older plain-text value is taken as it is
// (AsciiUnescape'd). Empty when it cannot be decrypted (other account / PC).
std::wstring UnprotectSecret(const std::wstring& stored);
// True for a value written by ProtectSecret.
bool IsProtectedSecret(const std::wstring& stored);

}  // namespace gct
