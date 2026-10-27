// local_mt_translator.hpp — process-isolated CTranslate2 backend for the shared Translator contract.
#pragma once

#include <memory>
#include <string>

#include "core/translator.hpp"

namespace gct {

enum class HelperStatus {
    Unknown,
    Missing,
    Ok,
    Outdated,
    Failed
};

struct HelperInfo {
    HelperStatus status = HelperStatus::Unknown;
    std::string helper;
    int proto = 0;
    std::vector<std::string> caps;
    std::string build;
    std::wstring outdatedReason;
};

std::shared_ptr<Translator> MakeLocalMtTranslator(const std::wstring& helperExe, const std::wstring& dataDir,
                                                  bool forceNllb = false);
void StopLocalMtTranslator(const std::shared_ptr<Translator>& translator);
void SetLocalMtBeamSize(const std::shared_ptr<Translator>& translator, int beamSize);
bool IsLocalMtTranslator(const std::shared_ptr<Translator>& translator);
HelperInfo GetLocalMtHelperInfo(const std::shared_ptr<Translator>& translator);
HelperInfo ProbeLocalMtHelper(const std::wstring& helperExe, const std::wstring& dataDir,
                             bool forceNllb = false);

}  // namespace gct
