// files.hpp — where the tool keeps its files, and small file helpers.
#pragma once

#include <string>

namespace gct {

std::wstring ExeDir();

// Folder for the INI, the word list and the name cache. Next to the exe if
// that folder is writable (portable use), otherwise
// %LOCALAPPDATA%\GW2ChatTranslator. Created if missing.
std::wstring DataDir();

bool EnsureDir(const std::wstring& path);
bool ReadFileBytes(const std::wstring& path, std::string& out);
// Writes to a temp file and swaps it in, so a crash never leaves half a file.
bool WriteFileAtomic(const std::wstring& path, const std::string& data);
bool AppendFileBytes(const std::wstring& path, const std::string& data);
// Age of the file in days, or a negative value if it does not exist.
double FileAgeDays(const std::wstring& path);

}  // namespace gct
