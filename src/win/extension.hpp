// extension.hpp — GW2 Keyboard's "Load extension": the chat translation by OCR is GW2 Chat Translator itself. On the
// user's click it is downloaded from the project's latest GitHub release and put into the install folder
// (%LOCALAPPDATA%\Programs\GW2ChatTranslator, invariant 8). Nothing else is written; nothing runs in the game.
#pragma once

#include <string>

namespace gct {

// The release asset that holds both programs (the same ZIP as on the download page).
constexpr wchar_t kExtensionUrl[] =
    L"https://github.com/Emisan01/gw2chatlator/releases/latest/download/GW2ChatTranslator-win64.zip";

// GW2ChatTranslator.exe next to this program or in the install folder; empty if neither.
std::wstring FindChatTranslatorExe();

// Blocking – call it on a worker thread. Downloads the ZIP, unpacks it (Windows' own tar.exe) into a temporary
// folder and copies GW2ChatTranslator.exe, onnxruntime.dll, the RapidOCR models and the notices into the install
// folder. Returns the installed exe; empty and `error` set on failure.
std::wstring LoadChatExtension(std::wstring* error);

}  // namespace gct
