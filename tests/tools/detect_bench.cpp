// detect_bench.cpp — which language a text of your own is taken for while you type it, measured on this PC (Windows'
// language detection is the real one). Every prefix of every line of tests/data/detect_corpus.tsv – what the
// translation of the input sees key by key – by the rule before (SureLanguage over Windows' guess, any language) and
// by OwnLanguage (core/own_language.hpp: your languages only):
//   wrong prefixes, results outside your languages, changes of mind while typing, lines wrong when finished.
//   detect_bench <corpus.tsv> [typing language, DE] [your languages, DE,EN,FR,ES] [-v]
// -v lists every prefix only the new rule gets wrong. A measurement, no gate: Windows' detection differs by version.
#include <windows.h>

#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "core/langs.hpp"
#include "core/own_language.hpp"
#include "core/text.hpp"
#include "win/els.hpp"
#include "win/files.hpp"

using namespace gct;

namespace {
struct Tally {
    int prefixes = 0, wrong = 0, outside = 0, flips = 0, lines = 0, finalWrong = 0;
};

void Print(const char* name, const std::map<std::wstring, Tally>& by) {
    Tally all;
    for (const auto& [lang, t] : by) {
        std::printf("  %-7s %ls: %5d of %5d prefixes wrong (%4.1f %%), %3d outside your languages, %3d changes of mind, "
                    "%d of %d lines wrong when finished\n",
                    name, lang.c_str(), t.wrong, t.prefixes, t.prefixes ? 100.0 * t.wrong / t.prefixes : 0.0, t.outside,
                    t.flips, t.finalWrong, t.lines);
        all.prefixes += t.prefixes, all.wrong += t.wrong, all.outside += t.outside, all.flips += t.flips;
        all.lines += t.lines, all.finalWrong += t.finalWrong;
    }
    std::printf("detect_bench %s: %d of %d prefixes wrong (%.1f %%), %d outside your languages, %d changes of mind, "
                "%d of %d lines wrong when finished\n",
                name, all.wrong, all.prefixes, all.prefixes ? 100.0 * all.wrong / all.prefixes : 0.0, all.outside, all.flips,
                all.finalWrong, all.lines);
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        std::printf("detect_bench <corpus.tsv> [typing language] [your languages] [-v]\n");
        return 2;
    }
    const std::wstring typing = argc > 2 ? ToUpperAscii(argv[2]) : L"DE";
    std::vector<std::wstring> own;
    {
        const std::wstring list = argc > 3 ? argv[3] : L"DE,EN,FR,ES";
        std::wstring cur;
        for (wchar_t c : list + L",") {
            if (c == L',') {
                if (!cur.empty()) own.push_back(ToUpperAscii(cur));
                cur.clear();
            } else {
                cur += c;
            }
        }
    }
    const bool verbose = argc > 4 && std::wstring(argv[4]) == L"-v";
    std::string data;
    if (!ReadFileBytes(argv[1], data)) {
        std::printf("cannot read the corpus\n");
        return 2;
    }
    std::printf("typing language %ls, your languages:", typing.c_str());
    for (const std::wstring& o : own) std::printf(" %ls", o.c_str());
    std::printf("\n");

    std::map<std::wstring, Tally> before, after;
    size_t pos = 0;
    while (pos < data.size()) {
        size_t eol = data.find('\n', pos);
        if (eol == std::string::npos) eol = data.size();
        std::wstring line = FromUtf8(data.substr(pos, eol - pos));
        pos = eol + 1;
        while (!line.empty() && (line.back() == L'\r' || line.back() == L' ')) line.pop_back();
        const size_t tab = line.find(L'\t');
        if (line.empty() || line[0] == L'#' || tab == std::wstring::npos) continue;
        const std::wstring want = ToUpperAscii(line.substr(0, tab)), text = line.substr(tab + 1);
        Tally &b = before[want], &a = after[want];
        ++b.lines, ++a.lines;
        std::wstring lastB, lastA;
        for (size_t n = 1; n <= text.size(); ++n) {
            const std::wstring part = text.substr(0, n);
            const std::wstring windows = DetectLanguage(part);
            const std::wstring sureB = PrimaryLang(SureLanguage(part, windows));
            const std::wstring sureA = OwnLanguage(part, own, windows).lang;
            const std::wstring srcB = sureB.empty() ? typing : sureB, srcA = sureA.empty() ? typing : sureA;
            ++b.prefixes, ++a.prefixes;
            if (srcB != want) ++b.wrong;
            if (srcA != want) ++a.wrong;
            if (!OwnLanguageHas(own, srcB)) ++b.outside;
            if (!OwnLanguageHas(own, srcA)) ++a.outside;
            if (n > 1 && srcB != lastB) ++b.flips;
            if (n > 1 && srcA != lastA) ++a.flips;
            if (verbose && srcA != want && srcB == want)
                std::printf("  after only wrong: %ls (Windows %ls) \"%s\"\n", srcA.c_str(), windows.c_str(), ToUtf8(part).c_str());
            lastB = srcB, lastA = srcA;
            if (n == text.size()) {
                if (srcB != want) {
                    ++b.finalWrong;
                    std::printf("  before, wrong when finished: %ls instead of %ls: %s\n", srcB.c_str(), want.c_str(), ToUtf8(text).c_str());
                }
                if (srcA != want) {
                    ++a.finalWrong;
                    std::printf("  after,  wrong when finished: %ls instead of %ls: %s\n", srcA.c_str(), want.c_str(), ToUtf8(text).c_str());
                }
            }
        }
    }
    Print("before", before);
    Print("after", after);
    return 0;
}
