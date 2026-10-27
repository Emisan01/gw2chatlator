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
    double score = 0;       // "result": the model's mean log probability per token (0 best); with `scored`
    bool scored = false;
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

// Remove decorative symbols the model hallucinated (e.g. subtitle music notes), preserving symbols present in input;
// a full stop the source did not end with goes too (a piece came back as a sentence: "Gracias por el" -> "Danke für
// ihn."), and the spacing follows `targetLang` (TidyPunctuation).
std::wstring CleanLocalMtOutput(const std::wstring& source, const std::wstring& output,
                                const std::string& targetLang = {});
// The target language's spacing: no space between a word and , . ! ? ; : (French keeps it before ! ? ; :). Only
// before a mark that ends there – a space, the end or another mark follow –, so ":)" and ";-)" stay as they are.
std::wstring TidyPunctuation(const std::wstring& text, const std::string& targetLang);
// Reject empty/punctuation-only output and decorative symbol blocks hallucinated by short-chat models.
bool IsPlausibleLocalMtOutput(const std::wstring& source, const std::wstring& output);
// Decorative symbols (⊱♛⊰, ✿, emoji, arrows, box drawing …) made OPUS-MT return nothing or hallucinate (user,
// 2026-10-23: French lines with crowns failed as "implausible"). Runs of them, with the spaces around them, become
// `keep` segments; only the words between them go to the model. A piece without a word stays as it is too.
std::vector<Segment> SplitForLocalMt(const std::vector<Segment>& segments);

// The pieces of one sentence (SplitForLocalMt) that go to the model as one text, [from, to): from its first to its
// last piece to translate, with a protected word right before or after them ("Gracias por el rez!" – without "rez"
// the model got "Gracias por el" and made "Danke für ihn."). Spaces, emoticons and symbols around stay outside.
// Nothing to translate: from == to == pieces.size().
struct PieceRange {
    size_t from = 0, to = 0;
};
PieceRange WholeSentenceRange(const std::vector<Segment>& pieces);
// A second try for a whole sentence whose protected word the model translated: plain lowercase keep-words written
// like names ("rez" -> "Rez"; NLLB made "rez" into "Gebet" but copies the name). Only keep parts made of plain words
// and sentence marks – codes, links and tags stay as they are. False when nothing changed.
bool KeepWordsAsNames(std::vector<Segment>& segments);
// After a whole sentence: the protected words the way they were written – the model capitalised some ("wtf" ->
// "WTF", "brb" -> "Brb") and the second try wrote them as names ("Danke für den Rez!" -> "… rez!").
std::wstring RestoreKeepSpelling(std::wstring output, const std::vector<Segment>& original);

}  // namespace gct
