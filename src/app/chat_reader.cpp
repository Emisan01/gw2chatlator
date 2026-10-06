// chat_reader.cpp
#include "chat_reader.hpp"

#include <algorithm>
#include <chrono>
#include <climits>
#include <memory>

#include "core/i18n.hpp"
#include "core/tesseract_tsv.hpp"
#include "core/text.hpp"
#include "win/files.hpp"
#include "win/folder_cleanup.hpp"
#include "win/ocr.hpp"
#include "win/screen_capture.hpp"
#include "win/tesseract_ocr.hpp"

namespace gct {

namespace {
constexpr int kMaxCaptures = 20;          // diagnostics files rotate (3 files each)
constexpr ULONGLONG kForceRereadMs = 20000;  // re-read an unchanged picture now and then
}  // namespace

void ChatReader::Start(HWND notify, UINT message, ReaderOptions options) {
    Stop();
    notify_ = notify;
    message_ = message;
    opt_ = std::move(options);
    {
        std::lock_guard<std::mutex> lk(mu_);
        stop_ = false;
        force_ = true;
    }
    thread_ = std::thread([this] { Loop(); });
}

void ChatReader::Stop() {
    if (!thread_.joinable()) return;
    {
        std::lock_guard<std::mutex> lk(mu_);
        stop_ = true;
    }
    cv_.notify_all();
    thread_.join();
}

void ChatReader::SetArea(const RECT& area) {
    std::lock_guard<std::mutex> lk(mu_);
    if (EqualRect(&area_, &area)) return;
    area_ = area;
    force_ = !IsRectEmpty(&area);
    cv_.notify_all();
}

void ChatReader::SetSaveCaptures(bool on) {
    std::lock_guard<std::mutex> lk(mu_);
    save_ = on;
}

void ChatReader::Rescan() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        force_ = true;
    }
    cv_.notify_all();
}

void ChatReader::Loop() {
    auto post = [this](std::unique_ptr<ReaderSnapshot> s) {
        if (PostMessageW(notify_, message_, 0, reinterpret_cast<LPARAM>(s.get()))) s.release();
    };

    // Tesseract if wanted and installed, else Windows' own text recognition.
    OcrEngine winOcr;
    TesseractOcr tess;
    std::wstring err;
    bool useTess = false;
    if (opt_.ocrChoice != 2) {
        TesseractInfo info;
        if (FindTesseract(opt_.tesseractPath, &info)) {
            const std::string langs = ChooseTesseractLangs(opt_.tesseractLangs, info.models, opt_.readChinese);
            useTess = tess.Init(info, langs, &err);
        } else if (opt_.ocrChoice == 1) {
            err = Tr(L"Tesseract not found \u2013 using Windows text recognition");
        }
    }
    if (!useTess && !winOcr.Init(opt_.ocrLanguage, &err)) {
        auto s = std::make_unique<ReaderSnapshot>();
        s->error = err;
        post(std::move(s));
        return;
    }
    ScreenCapture capture;
    uint64_t lastFingerprint = 0;
    ULONGLONG lastOcr = 0;

    for (;;) {
        RECT area;
        bool force, save;
        {
            std::unique_lock<std::mutex> lk(mu_);
            cv_.wait_for(lk, std::chrono::milliseconds(opt_.intervalMs), [this] { return stop_ || force_; });
            if (stop_) return;
            area = area_;
            force = force_;
            force_ = false;
            save = save_;
        }
        if (IsRectEmpty(&area)) continue;

        const ULONGLONG t0 = GetTickCount64();
        Image raw;
        if (!capture.Grab(area, raw)) continue;
        const uint64_t fp = ImageFingerprint(raw);
        if (!force && fp == lastFingerprint && t0 - lastOcr < kForceRereadMs) continue;  // nothing new
        lastFingerprint = fp;
        lastOcr = t0;

        int scale = std::clamp(opt_.scale, 1, 4);
        // Tesseract likes ~25 px high letters; Windows OCR has a size limit.
        if (useTess) scale = std::max(scale, 2);
        const int maxDim = useTess ? 6000 : winOcr.MaxImageDimension();
        while (scale > 1 && maxDim > 0 && (raw.width * scale > maxDim || raw.height * scale > maxDim)) --scale;
        const Image prepared = PrepareForOcr(raw, scale);

        auto snap = std::make_unique<ReaderSnapshot>();
        // One line of words with their boxes (in `prepared` pixels).
        struct Word {
            std::wstring text;
            RectI rect;
        };
        struct Line {
            std::wstring text;
            std::vector<Word> words;
            bool hasColor = false;
            Rgb color;
        };
        std::vector<Line> found;
        bool ok = false;
        if (useTess) {
            std::vector<TsvLine> tl;
            ok = tess.Recognize(prepared, tl, &err);
            for (TsvLine& l : tl) {
                Line x;
                x.text = std::move(l.text);
                for (TsvWord& w : l.words) x.words.push_back({std::move(w.text), w.rect});
                found.push_back(std::move(x));
            }
        } else {
            std::vector<OcrTextLine> tl;
            ok = winOcr.Recognize(prepared, tl, &err);
            for (OcrTextLine& l : tl) {
                Line x;
                x.text = std::move(l.text);
                x.hasColor = l.hasColor;
                x.color = l.color;
                for (OcrWordBox& w : l.words) x.words.push_back({std::move(w.text), w.rect});
                found.push_back(std::move(x));
            }
        }
        if (!ok) {
            snap->error = err;
        } else {
            for (const Line& tl : found) {
                OcrLine line;
                line.text = tl.text;
                std::vector<RectI> rects;
                int top = INT_MAX, bottom = 0;
                for (const Word& w : tl.words) {
                    const RectI r{w.rect.x / scale, w.rect.y / scale, std::max(1, w.rect.w / scale),
                                  std::max(1, w.rect.h / scale)};
                    rects.push_back(r);
                    top = std::min(top, r.y);
                    bottom = std::max(bottom, r.y + r.h);
                    OcrWord ow;
                    ow.text = w.text;
                    if (!tl.hasColor) {
                        ow.color = SampleTextColor(raw, {r});
                        ow.hasColor = true;
                    }
                    line.words.push_back(std::move(ow));
                }
                if (rects.empty()) top = bottom = 0;
                line.top = top;
                line.height = bottom - top;
                line.color = tl.hasColor ? tl.color : SampleTextColor(raw, rects);
                snap->lines.push_back(std::move(line));
            }
            std::stable_sort(snap->lines.begin(), snap->lines.end(),
                             [](const OcrLine& a, const OcrLine& b) { return a.top < b.top; });
        }
        snap->captureTick = t0;
        snap->method = capture.Method();
        snap->engine = useTess ? L"Tesseract" : L"Windows OCR";
        snap->language = useTess ? tess.Language() : winOcr.Language();
        snap->milliseconds = static_cast<int>(GetTickCount64() - t0);
        if (save) SaveDiagnostics(raw, prepared, snap->lines);
        post(std::move(snap));
    }
}

void ChatReader::SaveDiagnostics(const Image& raw, const Image& prepared, const std::vector<OcrLine>& lines) {
    if (opt_.captureDir.empty() || !EnsureDir(opt_.captureDir)) return;
    // Never more than the rotation (also cleans up files of older versions).
    if (captureIndex_ == 0) CleanFolder(opt_.captureDir, {kMaxCaptures * 3, 150ull << 20, 3 * 86400}, IsCaptureFile);
    wchar_t name[32];
    swprintf(name, 32, L"\\capture_%02d", captureIndex_);
    captureIndex_ = (captureIndex_ + 1) % kMaxCaptures;
    const std::wstring base = opt_.captureDir + name;
    WriteFileAtomic(base + L"_raw.bmp", EncodeBmp(raw));
    WriteFileAtomic(base + L"_ocr.bmp", EncodeBmp(prepared));
    std::string txt = "# colour | top | height | recognized text\r\n";
    for (const OcrLine& l : lines)
        txt += ToUtf8(RgbToHex(l.color)) + " | " + std::to_string(l.top) + " | " + std::to_string(l.height) + " | " +
               ToUtf8(l.text) + "\r\n";
    WriteFileAtomic(base + L".txt", txt);
}

}  // namespace gct
