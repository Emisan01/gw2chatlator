// translation_bench — a translator against a fixed set of made-up chat lines (tests/data/translation_corpus.tsv):
// every line translated, then core/translation_check (protected parts kept, sentences kept, nothing cut short) and the
// meaning check of the corpus (words the translation must hold, words it must never hold). One mode per run, so the
// same lines compare our logic, the bare model and the online services:
//   translation_bench <gw2kb_translate.exe> <data folder with translate\> <corpus.tsv> [mode]
//   mode: nllb (default, our logic) | nllb-raw (the bare model: codes and links only protected) | opus |
//         mymemory (GCT_MYMEMORY_EMAIL optional) | deepl (GCT_DEEPL_KEY) | google (GCT_GOOGLE_KEY)
// Corpus line: language <TAB> text [<TAB> must <TAB> never] – must: groups split by ";", each group's alternatives by
// "|", one of each group in the translation; never: alternatives by "|". A word matches whole, a phrase as written.
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "core/protect.hpp"
#include "core/slang.hpp"
#include "core/text.hpp"
#include "core/translation_check.hpp"
#include "win/deepl_translator.hpp"
#include "win/local_mt_translator.hpp"
#include "win/online_translators.hpp"

namespace {
const char* IssueName(gct::TranslationIssue i) {
    switch (i) {
        case gct::TranslationIssue::LostProtected: return "LOST-PROTECTED";
        case gct::TranslationIssue::LostSentence: return "LOST-SENTENCE";
        case gct::TranslationIssue::TooShort: return "TOO-SHORT";
        default: return "ok";
    }
}

std::vector<std::wstring> Split(const std::wstring& s, wchar_t sep) {
    std::vector<std::wstring> out;
    size_t start = 0;
    for (size_t at; (at = s.find(sep, start)) != std::wstring::npos; start = at + 1) out.push_back(gct::Trim(s.substr(start, at - start)));
    out.push_back(gct::Trim(s.substr(start)));
    out.erase(std::remove(out.begin(), out.end(), std::wstring()), out.end());
    return out;
}

// A word as a whole word, a phrase or a code ("Lion's Arch", "[&AgGvWwEA]") as written – in any case.
bool Holds(const std::wstring& text, const std::wstring& alt) {
    const std::wstring t = gct::CaseFold(text), a = gct::CaseFold(alt);
    if (std::any_of(a.begin(), a.end(), [](wchar_t c) { return !gct::IsWordChar(c); })) return t.find(a) != std::wstring::npos;
    for (const gct::Span& w : gct::WordSpans(t))
        if (t.compare(w.start, w.length, a) == 0) return true;
    return false;
}

std::wstring Env(const wchar_t* name) {
    wchar_t buf[512] = {};
    const DWORD n = GetEnvironmentVariableW(name, buf, 512);
    return n && n < 512 ? std::wstring(buf, n) : std::wstring();
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "Usage: translation_bench <gw2kb_translate.exe> <data folder> <corpus.tsv> "
                             "[nllb|nllb-raw|opus|mymemory|deepl|google]\n");
        return 2;
    }
    SetConsoleOutputCP(CP_UTF8);
    const std::wstring mode = argc < 5 ? L"nllb" : argv[4];
    const bool raw = mode == L"nllb-raw";
    std::shared_ptr<gct::Translator> translator;
    if (mode == L"mymemory") translator = gct::MakeMyMemoryTranslator(Env(L"GCT_MYMEMORY_EMAIL"));
    else if (mode == L"deepl") translator = gct::MakeDeepLTranslator(Env(L"GCT_DEEPL_KEY"));
    else if (mode == L"google") translator = gct::MakeGoogleTranslator(Env(L"GCT_GOOGLE_KEY"));
    else translator = gct::MakeLocalMtTranslator(argv[1], argv[2], mode != L"opus");
    if (raw) gct::SetLocalMtRaw(translator, true);
    std::ifstream file(argv[3], std::ios::binary);
    if (!file) {
        std::fprintf(stderr, "corpus not readable\n");
        return 2;
    }
    std::stringstream all;
    all << file.rdbuf();
    const gct::WordSet keep = gct::BuiltinKeepWords();
    int lines = 0, ok = 0, lostProtected = 0, lostSentence = 0, tooShort = 0, failed = 0, unsure = 0;
    int judged = 0, meaningOk = 0;
    size_t sentencesIn = 0, sentencesOut = 0;
    double totalMs = 0;
    std::string row;
    while (std::getline(all, row)) {
        if (!row.empty() && row.back() == '\r') row.pop_back();
        if (row.empty() || row[0] == '#') continue;
        const std::vector<std::wstring> cols = Split(gct::FromUtf8(row), L'\t');
        if (cols.size() < 2) continue;
        const std::wstring& source = cols[0];
        const std::wstring& text = cols[1];
        const std::vector<std::wstring> must = cols.size() > 2 ? Split(cols[2], L';') : std::vector<std::wstring>();
        const std::vector<std::wstring> never = cols.size() > 3 ? Split(cols[3], L'|') : std::vector<std::wstring>();
        const std::wstring target = source == L"de" ? L"EN" : L"DE";
        const gct::ProtectedText p = raw ? gct::ProtectCodesOnly(text)
                                         : gct::ProtectForTranslation(text, gct::BuiltinGlossary(target), &keep);
        const auto t0 = std::chrono::steady_clock::now();
        const gct::TranslateResult r = translator->Translate(p.segments, source, target);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        ++lines;
        totalMs += ms;
        if (!r.ok) {
            ++failed;
            std::printf("[FAILED] %s: %s\n", gct::ToUtf8(source).c_str(), gct::ToUtf8(r.error).c_str());
            continue;
        }
        const gct::TranslationCheck c = gct::CheckTranslation(p.segments, r.text, target);
        const size_t in = gct::CountSentences(text), out = gct::CountSentences(r.text);
        sentencesIn += in;
        sentencesOut += std::min(in, out);
        switch (c.issue) {
            case gct::TranslationIssue::None: ++ok; break;
            case gct::TranslationIssue::LostProtected: ++lostProtected; break;
            case gct::TranslationIssue::LostSentence: ++lostSentence; break;
            case gct::TranslationIssue::TooShort: ++tooShort; break;
        }
        if (r.scored && r.score < gct::kUnsureScore) ++unsure;
        std::wstring meaning;  // empty: no meaning words for this line
        if (!must.empty() || !never.empty()) {
            ++judged;
            std::wstring missing, found;
            for (const std::wstring& group : must) {
                const std::vector<std::wstring> alts = Split(group, L'|');
                if (std::none_of(alts.begin(), alts.end(), [&](const std::wstring& a) { return Holds(r.text, a); }))
                    missing += (missing.empty() ? L"" : L", ") + group;
            }
            for (const std::wstring& w : never)
                if (Holds(r.text, w)) found += (found.empty() ? L"" : L", ") + w;
            if (missing.empty() && found.empty()) {
                ++meaningOk;
                meaning = L"meaning ok";
            } else {
                meaning = L"MEANING" + (missing.empty() ? std::wstring() : L" missing: " + missing) +
                          (found.empty() ? std::wstring() : L" wrong: " + found);
            }
        }
        char score[16] = "-";
        if (r.scored) std::snprintf(score, sizeof(score), "%.2f", r.score);
        std::printf("[%s%s%s] %s>%s %zu/%zu sentences, score %s, %.0f ms%s%s\n  in:  %s\n  out: %s\n", IssueName(c.issue),
                    c.detail.empty() ? "" : " ", gct::ToUtf8(c.detail).c_str(), gct::ToUtf8(source).c_str(),
                    gct::ToUtf8(target).c_str(), out, in, score, ms, meaning.empty() ? "" : " - ", gct::ToUtf8(meaning).c_str(),
                    gct::ToUtf8(text).c_str(), gct::ToUtf8(r.text).c_str());
    }
    gct::StopLocalMtTranslator(translator);
    std::printf("\ntranslation_bench (%s): %d lines, %d ok, %d lost protected, %d lost a sentence, %d cut short, %d failed; "
                "sentences kept %zu/%zu; meaning ok %d/%d; %d unsure (score < %.1f); %.0f ms per line\n",
                gct::ToUtf8(mode).c_str(), lines, ok, lostProtected, lostSentence, tooShort, failed, sentencesOut, sentencesIn,
                meaningOk, judged, unsure, gct::kUnsureScore, lines ? totalMs / lines : 0.0);
    return 0;
}
