// local_mt_translator.hpp — process-isolated CTranslate2 backend for the shared Translator contract.
#pragma once

#include <memory>
#include <string>

#include "core/local_mt_protocol.hpp"
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
// M2M100 from translate\m2m100 (a folder put there by hand, no download offered).
std::shared_ptr<Translator> MakeM2mTranslator(const std::wstring& helperExe, const std::wstring& dataDir);
void StopLocalMtTranslator(const std::shared_ptr<Translator>& translator);
void SetLocalMtBeamSize(const std::shared_ptr<Translator>& translator, int beamSize);
// How the model decodes, for the requests to come; never waits for a running translation.
void SetLocalMtDecode(const std::shared_ptr<Translator>& translator, const LocalMtDecode& decode);
// Other versions to come back with each translation (0 or 1: none). One-version: for now only the best (a back-translation).
void SetLocalMtVersions(const std::shared_ptr<Translator>& translator, int versions);
void SetLocalMtOneVersion(const std::shared_ptr<Translator>& translator, bool one);
int LocalMtVersions(const std::shared_ptr<Translator>& translator);
// Every call and answer of the helper, for a logbook: numbers and settings, never a text.
void SetLocalMtLog(void (*log)(const std::wstring& line));
// The helper's CPU time so far, in 100 ns; 0 while none runs. Never waits for a running translation.
uint64_t LocalMtHelperCpu(const std::shared_ptr<Translator>& translator);
// What differs from neutral, for the tag a translation carries (" · rep 1.10 · ngram 3"); empty when nothing does.
std::wstring LocalMtDecodeNote(const std::shared_ptr<Translator>& translator);
bool IsLocalMtTranslator(const std::shared_ptr<Translator>& translator);
// Raw translation (Technical tab, for comparing): the parts between protected ones go to the model as written, no
// sentence split, no extra tries, no clean-up of its output.
void SetLocalMtRaw(const std::shared_ptr<Translator>& translator, bool raw);
HelperInfo GetLocalMtHelperInfo(const std::shared_ptr<Translator>& translator);
HelperInfo ProbeLocalMtHelper(const std::wstring& helperExe, const std::wstring& dataDir,
                             bool forceNllb = false);

}  // namespace gct
