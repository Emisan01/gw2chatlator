// local_mt_translator.hpp — process-isolated CTranslate2 backend for the shared Translator contract.
#pragma once

#include <memory>
#include <string>

#include "core/translator.hpp"

namespace gct {

std::shared_ptr<Translator> MakeLocalMtTranslator(const std::wstring& helperExe, const std::wstring& dataDir);
void StopLocalMtTranslator(const std::shared_ptr<Translator>& translator);
void SetLocalMtBeamSize(const std::shared_ptr<Translator>& translator, int beamSize);

}  // namespace gct
