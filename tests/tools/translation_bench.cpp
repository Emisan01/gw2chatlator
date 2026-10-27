// translation_bench — the local translator against a fixed set of made-up chat lines (tests/data/translation_corpus.tsv):
// every line through the real helper and model, then core/translation_check: protected parts kept, sentences kept,
// nothing cut short. A measurement, not a gate – it needs an installed model:
//   translation_bench <gw2kb_translate.exe> <data folder with translate\> <corpus.tsv> [nllb|opus]
#include <windows.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "core/protect.hpp"
#include "core/slang.hpp"
#include "core/text.hpp"
#include "core/translation_check.hpp"
#include "win/local_mt_translator.hpp"

namespace {
const char* IssueName(gct::TranslationIssue i) {
    switch (i) {
        case gct::TranslationIssue::LostProtected: return "LOST-PROTECTED";
        case gct::TranslationIssue::LostSentence: return "LOST-SENTENCE";
        case gct::TranslationIssue::TooShort: return "TOO-SHORT";
        default: return "ok";
    }
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "Usage: translation_bench <gw2kb_translate.exe> <data folder> <corpus.tsv> [nllb|opus]\n");
        return 2;
    }
    SetConsoleOutputCP(CP_UTF8);
    const bool nllb = argc < 5 || std::wstring(argv[4]) != L"opus";
    auto translator = gct::MakeLocalMtTranslator(argv[1], argv[2], nllb);
    std::ifstream file(argv[3], std::ios::binary);
    if (!file) {
        std::fprintf(stderr, "corpus not readable\n");
        return 2;
    }
    std::stringstream all;
    all << file.rdbuf();
    const gct::WordSet keep = gct::BuiltinKeepWords();
    int lines = 0, ok = 0, lostProtected = 0, lostSentence = 0, tooShort = 0, failed = 0, unsure = 0;
    size_t sentencesIn = 0, sentencesOut = 0;
    double totalMs = 0;
    std::string row;
    while (std::getline(all, row)) {
        if (!row.empty() && row.back() == '\r') row.pop_back();
        if (row.empty() || row[0] == '#') continue;
        const size_t tab = row.find('\t');
        if (tab == std::string::npos) continue;
        const std::wstring source = gct::FromUtf8(row.substr(0, tab)), text = gct::FromUtf8(row.substr(tab + 1));
        const std::wstring target = source == L"de" ? L"EN" : L"DE";
        const gct::ProtectedText p = gct::ProtectForTranslation(text, nullptr, &keep);
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
        char score[16] = "-";
        if (r.scored) std::snprintf(score, sizeof(score), "%.2f", r.score);
        std::printf("[%s%s%s] %s>%s %zu/%zu sentences, score %s, %.0f ms\n  in:  %s\n  out: %s\n", IssueName(c.issue),
                    c.detail.empty() ? "" : " ", gct::ToUtf8(c.detail).c_str(), gct::ToUtf8(source).c_str(),
                    gct::ToUtf8(target).c_str(), out, in, score, ms, gct::ToUtf8(text).c_str(), gct::ToUtf8(r.text).c_str());
    }
    gct::StopLocalMtTranslator(translator);
    std::printf("\ntranslation_bench (%s): %d lines, %d ok, %d lost protected, %d lost a sentence, %d cut short, %d failed; "
                "sentences kept %zu/%zu; %d unsure (score < %.1f); %.0f ms per line\n",
                nllb ? "nllb" : "opus", lines, ok, lostProtected, lostSentence, tooShort, failed, sentencesOut, sentencesIn,
                unsure, gct::kUnsureScore, lines ? totalMs / lines : 0.0);
    return 0;
}
