// local_mt_protocol.hpp — newline-delimited JSON contract and routing for the local MT helper.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "protect.hpp"

namespace gct {

struct LocalMtMessage {
    enum class Kind { Invalid, Ready, Loaded, Result, Error } kind = Kind::Invalid;
    int id = 0;
    std::string pair;
    std::string text;
    std::string error;
    int64_t milliseconds = 0;
    std::string helper;
    int proto = 0;
    std::vector<std::string> caps;
    std::string build;
};

bool HasCap(const LocalMtMessage& msg, const std::string& cap);

struct LocalMtRequest {
    int id = 0;
    std::string source, target, text;
    int beam = 0;
    std::string model;
};

struct LocalMtModelInfo {
    const char* pair;          // package/directory ID, e.g. "de-en"
    const char* model;         // upstream Helsinki-NLP repository
    const char* sourcePrefix;  // required only by multilingual source models
    const char* license;       // license declared by this exact upstream model card
};

// OPUS-MT packages available to the add-on. Every language can route through English when both directions are installed.
const std::vector<LocalMtModelInfo>& LocalMtModelCatalog();

std::string BuildLocalMtRequest(int id, const std::string& source, const std::string& target,
                                const std::string& utf8Text, int beam = 0,
                                const std::string& model = {});
bool ParseLocalMtRequest(const std::string& line, LocalMtRequest& out);
bool ParseLocalMtMessage(const std::string& line, LocalMtMessage& out);

// Returns the FLORES-200 code (e.g. "deu_Latn") for an ISO/DeepL language code, or "" if unmapped.
std::string NllbLanguageTag(const std::string& langCode);

// Returns one direct pair or a two-step route through English. Pair IDs are lowercase, e.g. "de-en".
std::vector<std::string> LocalMtRoute(const std::wstring& source, const std::wstring& target,
                                      const std::vector<std::string>& installedPairs);

// Remove decorative symbols the model hallucinated (e.g. subtitle music notes), preserving symbols present in input.
std::wstring CleanLocalMtOutput(const std::wstring& source, const std::wstring& output);
// Reject empty/punctuation-only output and decorative symbol blocks hallucinated by short-chat models.
bool IsPlausibleLocalMtOutput(const std::wstring& source, const std::wstring& output);
// Decorative symbols (⊱♛⊰, ✿, emoji, arrows, box drawing …) made OPUS-MT return nothing or hallucinate (user,
// 2026-10-23: French lines with crowns failed as "implausible"). Runs of them, with the spaces around them, become
// `keep` segments; only the words between them go to the model. A piece without a word stays as it is too.
std::vector<Segment> SplitForLocalMt(const std::vector<Segment>& segments);

}  // namespace gct
