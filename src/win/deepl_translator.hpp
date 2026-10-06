// deepl_translator.hpp — DeepL backend for the Translator interface.
#pragma once

#include <memory>
#include <string>

#include "core/translator.hpp"

namespace gct {

std::shared_ptr<Translator> MakeDeepLTranslator(const std::wstring& apiKey);

}  // namespace gct
