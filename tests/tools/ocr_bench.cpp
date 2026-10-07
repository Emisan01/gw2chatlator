// ocr_bench.cpp — measures text recognition on real chat pictures, with the
// app's own code paths: picture preparation (old fixed 2x stretch-and-invert
// vs. the dynamic enlargement), Tesseract and Windows OCR, and the chat
// parser. Decides which reader and preparation to use by numbers.
//
//   ocr_bench.exe [folder]        (default: local\bench)
//
// The folder holds name.png (a chat crop) + name.txt (what really stands
// there, one chat line per line). Real captures show other players' names
// and messages: keep them in local\ (git-ignored), never in the repo.
#include <windows.h>
#include <wincodec.h>

#include "app/chat_reader.hpp"
#include "win/rapid_ocr.hpp"

#include <algorithm>
#include <chrono>
#include <climits>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "core/chat_geometry.hpp"
#include "core/chat_line.hpp"
#include "core/image.hpp"
#include "core/tesseract_tsv.hpp"
#include "core/text.hpp"
#include "win/ocr.hpp"
#include "win/tesseract_ocr.hpp"

using namespace gct;

namespace {

bool LoadPicture(const std::wstring& path, Image& out) {
    IWICImagingFactory* f = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f)))) return false;
    IWICBitmapDecoder* dec = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICBitmapSource* bgra = nullptr;
    bool ok = SUCCEEDED(f->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &dec)) &&
              SUCCEEDED(dec->GetFrame(0, &frame)) &&
              SUCCEEDED(WICConvertBitmapSource(GUID_WICPixelFormat32bppBGRA, frame, &bgra));
    UINT w = 0, h = 0;
    if (ok) ok = SUCCEEDED(bgra->GetSize(&w, &h));
    if (ok) {
        out.width = static_cast<int>(w);
        out.height = static_cast<int>(h);
        out.bgra.resize(static_cast<size_t>(w) * h * 4);
        ok = SUCCEEDED(bgra->CopyPixels(nullptr, w * 4, static_cast<UINT>(out.bgra.size()), out.bgra.data()));
    }
    if (bgra) bgra->Release();
    if (frame) frame->Release();
    if (dec) dec->Release();
    f->Release();
    return ok;
}

std::wstring ReadUtf8(const std::wstring& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    std::string s = ss.str();
    if (s.size() >= 3 && s.compare(0, 3, "\xEF\xBB\xBF") == 0) s.erase(0, 3);
    return FromUtf8(s);
}

std::vector<std::wstring> Lines(const std::wstring& s) {
    std::vector<std::wstring> out;
    std::wstringstream ss(s);
    std::wstring l;
    while (std::getline(ss, l)) {
        if (!l.empty() && l.back() == L'\r') l.pop_back();
        if (!Trim(l).empty()) out.push_back(l);
    }
    return out;
}

std::wstring Squash(const std::wstring& s) {  // whitespace runs -> one space
    std::wstring out;
    bool space = false;
    for (wchar_t c : s) {
        if (c == L' ' || c == L'\t' || c == L'\n' || c == L'\r') {
            space = !out.empty();
            continue;
        }
        if (space) out += L' ';
        space = false;
        out += c;
    }
    return out;
}

size_t Levenshtein(const std::wstring& a, const std::wstring& b) {
    std::vector<size_t> prev(b.size() + 1), cur(b.size() + 1);
    for (size_t j = 0; j <= b.size(); ++j) prev[j] = j;
    for (size_t i = 1; i <= a.size(); ++i) {
        cur[0] = i;
        for (size_t j = 1; j <= b.size(); ++j)
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)});
        prev.swap(cur);
    }
    return prev[b.size()];
}

double Cer(const std::wstring& truth, const std::wstring& got) {
    const std::wstring t = Squash(truth), g = Squash(got);
    return t.empty() ? 0.0 : 100.0 * static_cast<double>(Levenshtein(t, g)) / static_cast<double>(t.size());
}

// What the user finally sees: the parser's speaker + text per message.
std::wstring Parsed(const std::vector<OcrLine>& lines) {
    std::wstring s;
    for (const ChatMessage& m : BuildMessages(lines, DefaultChannelColors()))
        s += (m.speaker.empty() ? L"" : m.speaker + L": ") + m.text + L"\n";
    return s;
}

struct Word {
    std::wstring text;
    RectI rect;
};
struct Line {
    std::wstring text;
    std::vector<Word> words;
};

// The same conversion as ChatReader: boxes back to raw pixels, colours sampled
// there; with a line grid the words are first sorted into the measured lines.
std::vector<OcrLine> ToOcrLines(std::vector<Line> found, const Image& raw, int scale, const LineGrid* grid) {
    if (grid && grid->Found()) {
        std::vector<BoxWord> words;
        for (const Line& l : found)
            for (const Word& w : l.words)
                words.push_back({w.text, {w.rect.x / scale, w.rect.y / scale, std::max(1, w.rect.w / scale),
                                          std::max(1, w.rect.h / scale)}});
        found.clear();
        for (const auto& row : GroupWordsByRows(words, *grid)) {
            Line l;
            for (const BoxWord& w : row) {
                l.text += (l.text.empty() ? L"" : L" ") + w.text;
                l.words.push_back({w.text, {w.rect.x * scale, w.rect.y * scale, w.rect.w * scale, w.rect.h * scale}});
            }
            found.push_back(std::move(l));
        }
    }
    std::vector<OcrLine> out;
    for (const Line& tl : found) {
        OcrLine line;
        line.text = tl.text;
        std::vector<RectI> rects;
        int top = INT_MAX, bottom = 0;
        for (const Word& w : tl.words) {
            const RectI r{w.rect.x / scale, w.rect.y / scale, std::max(1, w.rect.w / scale), std::max(1, w.rect.h / scale)};
            rects.push_back(r);
            top = std::min(top, r.y);
            bottom = std::max(bottom, r.y + r.h);
            OcrWord ow;
            ow.text = w.text;
            ow.color = SampleTextColor(raw, {r});
            ow.hasColor = true;
            line.words.push_back(ow);
        }
        if (rects.empty()) top = bottom = 0;
        line.top = top;
        line.height = bottom - top;
        line.color = SampleTextColor(raw, rects);
        out.push_back(std::move(line));
    }
    std::stable_sort(out.begin(), out.end(), [](const OcrLine& a, const OcrLine& b) { return a.top < b.top; });
    return out;
}

struct Score {
    double cer = 0, parsedCer = 0, ms = 0;
    int n = 0;
};

}  // namespace

int wmain(int argc, wchar_t** argv) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const std::wstring dir = argc > 1 ? argv[1] : L"local\\bench";

    TesseractOcr tess;
    TesseractInfo info;
    std::wstring err;
    const bool haveTess = FindTesseract(L"", &info) && tess.Init(info, ChooseTesseractLangs("", info.models, false), &err);
    OcrEngine win;
    const bool haveWin = win.Init(L"", &err);
    std::printf("Tesseract: %s   Windows OCR: %s\n\n", haveTess ? "yes" : "no", haveWin ? "yes" : "no");

    std::map<std::string, Score> total;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*.png").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) {
        std::printf("No pictures in %ls\n", dir.c_str());
        return 1;
    }
    std::printf("%-28s %-6s %-12s %6s %8s %7s\n", "picture", "engine", "preparation", "CER %", "parsed %", "ms");
    do {
        const std::wstring name = fd.cFileName;
        const std::wstring base = dir + L"\\" + name.substr(0, name.size() - 4);
        const std::wstring truth = ReadUtf8(base + L".txt");
        Image frame;
        if (truth.empty() || !LoadPicture(base + L".png", frame)) continue;
        // The old path read the frame as drawn; the new one snaps it to the
        // text lines first (no half lines, no scroll bar), as the app does now.
        const SnapResult snap = SnapChatArea(frame, {0, 0, frame.width, frame.height});
        std::printf("%-28ls frame %dx%d -> snapped x=%d y=%d %dx%d, %zu lines, pitch %d, text %d px, scale %d\n",
                    name.c_str(), frame.width, frame.height, snap.area.x, snap.area.y, snap.area.w, snap.area.h,
                    snap.grid.rows.size(), snap.grid.pitch, snap.grid.textHeight, snap.scale);
        const Image snapped = Crop(frame, snap.area);
        const Image& raw = snapped;
        std::vector<OcrLine> truthLines;
        int top = 0;
        for (const std::wstring& l : Lines(truth)) {
            OcrLine o;
            o.text = l;
            o.top = top;
            o.height = 15;
            o.color = {230, 230, 230};
            top += 20;
            truthLines.push_back(o);
        }
        const std::wstring truthText = Squash(truth), truthParsed = Parsed(truthLines);

        const LineGrid grid = FindLineGrid(raw, {0, 0, raw.width, raw.height});
        const int dyn = OcrScaleFor(grid);
        struct Prep {
            std::string name;
            Image img;
            int scale;
            bool light;  // light text on dark (Tesseract gets it inverted, no second try)
            const Image* src;  // where the colours are sampled
        };
        const Image up = UpscaleForOcr(raw, dyn);
        std::vector<Prep> preps = {{"old 2x", PrepareForOcr(frame, 2), 2, false, &frame},
                                   {"new " + std::to_string(dyn) + "x", up, dyn, false, &raw},
                                   {"new " + std::to_string(dyn) + "x inv", up, dyn, true, &raw},
                                   {"raw 1x inv", raw, 1, true, &raw},
                                   {"new contrast", AutoContrast(up), dyn, false, &raw},
                                   {"new grey", AutoContrast(up, true), dyn, false, &raw}};
        for (const Prep& p : preps) {
            for (int e = 0; e < 2; ++e) {
                if ((e == 0 && !haveTess) || (e == 1 && !haveWin)) continue;
                if (e == 1 && p.light && p.scale > 1) continue;  // Windows OCR gets the same picture as "new"
                std::vector<Line> found;
                const auto t0 = std::chrono::steady_clock::now();
                if (e == 0) {
                    tess.SetLightText(p.light);
                    std::vector<TsvLine> tl;
                    if (!tess.Recognize(p.img, tl, &err)) continue;
                    for (TsvLine& l : tl) {
                        Line x{l.text, {}};
                        for (TsvWord& w : l.words) x.words.push_back({w.text, w.rect});
                        found.push_back(x);
                    }
                } else {
                    std::vector<OcrTextLine> tl;
                    if (!win.Recognize(p.img, tl, &err)) continue;
                    for (OcrTextLine& l : tl) {
                        Line x{l.text, {}};
                        for (OcrWordBox& w : l.words) x.words.push_back({w.text, w.rect});
                        found.push_back(x);
                    }
                }
                const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                const std::vector<OcrLine> lines = ToOcrLines(found, *p.src, p.scale, p.src == &raw ? &snap.grid : nullptr);
                std::wstring all;
                for (const OcrLine& l : lines) all += l.text + L"\n";
                const double cer = Cer(truthText, all), parsed = Cer(truthParsed, Parsed(lines));
                const std::string engine = e == 0 ? "tess" : "win";
                std::printf("%-28ls %-6s %-12s %6.1f %8.1f %7.0f\n", name.c_str(), engine.c_str(), p.name.c_str(), cer, parsed, ms);
                if (GetEnvironmentVariableW(L"BENCH_DUMP", nullptr, 0) > 0)
                    std::printf("--- recognized:\n%s--- parsed:\n%s--- truth:\n%s\n", ToUtf8(all).c_str(),
                                ToUtf8(Parsed(lines)).c_str(), ToUtf8(truthParsed).c_str());
                std::string kind = p.name.substr(0, p.name.find(' '));
                if (p.light) kind += " inv";
                Score& s = total[engine + " " + kind];
                s.cer += cer;
                s.parsedCer += parsed;
                s.ms += ms;
                ++s.n;
            }
        }
        // The app's own pipeline (ChatOcr, Windows OCR) without and with the second look at unknown words.
        for (int look = 0; look < 3 && haveWin; ++look) {
            ReaderOptions o;
            o.ocrChoice = look == 2 ? 3 : 2;  // 3 = RapidOCR
            o.secondLook = look == 1;
            o.rapidDir = L"local\\rapid";
            o.rapidGroups = {L"latin"};
            o.wordLangs = {L"DE", L"EN-GB"};
            ChatOcr co;
            std::wstring e2;
            if (!co.Init(o, &e2)) break;
            std::vector<OcrLine> lines;
            const auto t0 = std::chrono::steady_clock::now();
            if (!co.Read(raw, 0, lines, nullptr, &e2)) break;
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            std::wstring all;
            for (const OcrLine& l : lines) all += l.text + L"\n";
            const double cer = Cer(truthText, all), parsed = Cer(truthParsed, Parsed(lines));
            if (look == 2) {  // the same picture again: lines that did not change come from the cache
                std::vector<OcrLine> again;
                const auto t1 = std::chrono::steady_clock::now();
                co.Read(raw, 0, again, nullptr, &e2);
                std::printf("    rapid, same picture again: %.0f ms\n",
                            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count());
            }
            const std::string kind = look == 2 ? "rapid" : look ? "2nd look" : "plain";
            std::printf("%-28ls %-6s %-12s %6.1f %8.1f %7.0f  (second look: %d read again, %d taken)\n", name.c_str(),
                        "app", kind.c_str(), cer, parsed, ms, co.SecondLooks(), co.SecondFixes());
            if (GetEnvironmentVariableW(L"BENCH_DUMP", nullptr, 0) > 0)
                std::printf("--- app %s:\n%s\n", kind.c_str(), ToUtf8(Parsed(lines)).c_str());
            Score& s = total["app " + kind];
            s.cer += cer;
            s.parsedCer += parsed;
            s.ms += ms;
            ++s.n;
        }
        // RapidOCR (PaddleOCR line recognition on ONNX Runtime): each measured line cut out and recognized.
        static RapidRecognizer rapid;
        static bool rapidTried = false;
        if (!rapidTried) {
            rapidTried = true;
            std::wstring e3;
            const std::wstring dirR = L"local\\rapid\\";
            if (!rapid.Load(dirR + L"latin_v5_rec.onnx", dirR + L"latin_v5_dict.txt", &e3))
                std::printf("RapidOCR not available: %ls\n", e3.c_str());
        }
        for (int inv = 0; inv < 2 && rapid.Ready(); ++inv) {
            const auto t0 = std::chrono::steady_clock::now();
            std::vector<OcrLine> lines;
            for (const TextRow& row : snap.grid.rows) {
                // Experiment knobs: BENCH_RAPID_PAD = margin in percent of the line height (default 25),
                // BENCH_RAPID_PRE = enlarge the crop first with the bicubic upscaler (default 1 = off).
                wchar_t knob[16] = {};
                const int padPct = GetEnvironmentVariableW(L"BENCH_RAPID_PAD", knob, 16) ? _wtoi(knob) : 25;
                const int pre = GetEnvironmentVariableW(L"BENCH_RAPID_PRE", knob, 16) ? std::max(1, _wtoi(knob)) : 1;
                const int pad = std::max(2, row.height * padPct / 100);
                const RectI rr{0, std::max(0, row.top - pad), raw.width,
                               std::min(raw.height, row.top + row.height + pad) - std::max(0, row.top - pad)};
                std::wstring e4;
                const int contrast = GetEnvironmentVariableW(L"BENCH_CONTRAST", knob, 16) ? _wtoi(knob) : 0;
                Image crop = UpscaleForOcr(Crop(raw, rr), pre);
                if (contrast > 0) crop = AutoContrast(crop, contrast == 1);
                const RecResult rec = rapid.Recognize(crop, inv == 1, &e4);
                OcrLine l;
                l.text = rec.text;
                l.top = rr.y;
                l.height = rr.h;
                l.color = SampleTextColor(raw, {rr});
                lines.push_back(l);
            }
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            std::wstring all;
            for (const OcrLine& l : lines) all += l.text + L"\n";
            const double cer = Cer(truthText, all), parsed = Cer(truthParsed, Parsed(lines));
            const std::string kind = inv ? "lines inv" : "lines";
            std::printf("%-28ls %-6s %-12s %6.1f %8.1f %7.0f\n", name.c_str(), "rapid", kind.c_str(), cer, parsed, ms);
            if (GetEnvironmentVariableW(L"BENCH_DUMP", nullptr, 0) > 0)
                std::printf("--- rapid %s:\n%s\n", kind.c_str(), ToUtf8(all).c_str());
            Score& sc = total["rapid " + kind];
            sc.cer += cer;
            sc.parsedCer += parsed;
            sc.ms += ms;
            ++sc.n;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::printf("\naverage            CER %%  parsed %%      ms\n");
    for (const auto& [k, s] : total)
        std::printf("%-16s %7.1f %9.1f %7.0f\n", k.c_str(), s.cer / s.n, s.parsedCer / s.n, s.ms / s.n);
    return 0;
}
