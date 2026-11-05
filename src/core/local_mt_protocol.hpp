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
    int modelMs = 0, beam = 0, threads = 0, cores = 0, tokensIn = 0, tokensOut = 0;  // "result": what it did (0: not said)
    std::vector<std::string> alts;                                                  // "result": its other versions
};

bool HasCap(const LocalMtMessage& msg, const std::string& cap);

// How a many-to-many model decodes besides the beam: CTranslate2's options, in hundredths so the request stays free of
// float and locale questions. These values are neutral, as if none were given, and are not sent.
struct LocalMtDecode {
    int patience = 100;           // the search goes on until beam * this finished hypotheses (1.00 to 4.00)
    int lengthPenalty = 100;      // score / length ^ this: above 1.00 longer translations win, below shorter ones (0 to 3.00)
    int coveragePenalty = 0;      // above 0: a translation that leaves source words out loses (0 to 1.00)
    int repetitionPenalty = 100;  // above 1.00: pieces already written lose (1.00 to 2.00)
    int noRepeatNgram = 0;        // no run of this many pieces twice, 0 = off (0 to 6)
    bool disableUnk = false;      // never the unknown token
    int hypotheses = 1;           // how many versions come back (1 to 5); the beam is at least this
    bool operator==(const LocalMtDecode& o) const {
        return hypotheses == o.hypotheses && patience == o.patience && lengthPenalty == o.lengthPenalty && coveragePenalty == o.coveragePenalty &&
               repetitionPenalty == o.repetitionPenalty && noRepeatNgram == o.noRepeatNgram && disableUnk == o.disableUnk;
    }
};

// CPU threads when they are left on automatic: the real cores minus 2 for the game, 2 to 4 (measured on 6 cores, M2M100
// beam 2: 1 thread 781 ms, 2 448 ms, 3 374 ms, 4 311 ms, 6 331 ms per line). Above beam 4 two more, one core always stays
// free (beam 8: 3 threads 650 ms, 6 502 ms).
inline int LocalAutoThreads(int beam, int physicalCores) {
    int threads = physicalCores - 2;
    threads = threads < 2 ? 2 : threads > 4 ? 4 : threads;
    if (beam > 4) threads += 2;
    const int most = physicalCores > 3 ? physicalCores - 1 : 2;
    return threads > most ? most : threads;
}

// The values measured with translation_bench for M2M100.
inline LocalMtDecode M2mRecommended() { return {}; }

struct LocalMtRequest {
    int id = 0;
    std::string source, target, text;
    int beam = 0;
    std::string model;
    LocalMtDecode decode;
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
                                const std::string& model = {}, const LocalMtDecode& decode = {});
bool ParseLocalMtRequest(const std::string& line, LocalMtRequest& out);
bool ParseLocalMtMessage(const std::string& line, LocalMtMessage& out);

// Returns the FLORES-200 code (e.g. "deu_Latn") for an ISO/DeepL language code, or "" if unmapped.
std::string NllbLanguageTag(const std::string& langCode);
// Returns M2M100's tag (e.g. "__de__") for an ISO/DeepL language code, or "" for a language the model does not have.
std::string M2mLanguageTag(const std::string& langCode);

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
// like names ("rez" -> "Rez"; NLLB made "rez" into "Gebet" but copies the name, "wtf" stays with its sentence). Only
// keep parts made of plain words and sentence marks – codes, links and tags stay as they are. False when nothing
// changed. RestoreKeepSpelling (translation_check) writes them back.
bool KeepWordsAsNames(std::vector<Segment>& segments);
// The third try ("Lion's Arch" -> "Löwenbogen" in any spelling): every protected part becomes a placeholder X1, X2 …
// that the model copies and builds the sentence around ("Wir treffen uns am Wegpunkt in X1"). The spaces and marks
// around a part stay text. `parts` is empty when there is nothing to replace, more than 9 parts, or the sentence has
// an "X1"-like word itself. Measured (translation_bench): names first keep "wtf was that", "einen qheal"; placeholders
// save the official names.
struct Placeholders {
    std::wstring text;
    std::vector<std::wstring> parts;  // parts[0] stands for X1
};
Placeholders WithPlaceholders(const std::vector<Segment>& pieces);
// The parts back in place of their placeholders (in any case: "x1"); false when one is missing or there twice.
bool FillPlaceholders(const std::wstring& output, const std::vector<std::wstring>& parts, std::wstring& filled);

}  // namespace gct
