// extension.hpp — GW2 Keyboard's "Load extension": the chat translation by OCR is GW2 Chat Translator itself. On the
// user's click it is downloaded from the project's latest GitHub release and unpacked into a temporary folder; that
// exe is then started like a fresh download, so its own setup installs it as always. Nothing runs in the game.
#pragma once

#include <string>

namespace gct {

// The release asset that holds both programs (the same ZIP as on the download page).
constexpr wchar_t kExtensionUrl[] =
    L"https://github.com/Emisan01/gw2chatlator/releases/latest/download/GW2ChatTranslator-win64.zip";

// GW2ChatTranslator.exe next to this program or in the install folder; empty if neither.
std::wstring FindChatTranslatorExe();

// Blocking – call it on a worker thread. Downloads the ZIP and unpacks it (Windows' own tar.exe) into
// %TEMP%\GW2ChatTranslator-download. Your settings and words from `dataDir` go along (the downloaded exe keeps its
// data next to itself until its setup installs it), with the chat reading on and the setup to run.
// Returns the unpacked GW2ChatTranslator.exe; empty and `error` set on failure.
std::wstring LoadChatExtension(const std::wstring& dataDir, std::wstring* error);

}  // namespace gct
