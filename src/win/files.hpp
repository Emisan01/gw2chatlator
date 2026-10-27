// files.hpp — where the tool keeps its files, and small file helpers.
#pragma once

#include <string>

namespace gct {

std::wstring ExeDir();


bool EnsureDir(const std::wstring& path);
bool ReadFileBytes(const std::wstring& path, std::string& out);
// Writes to a temp file and swaps it in, so a crash never leaves half a file.
bool WriteFileAtomic(const std::wstring& path, const std::string& data);
bool AppendFileBytes(const std::wstring& path, const std::string& data);

}  // namespace gct
