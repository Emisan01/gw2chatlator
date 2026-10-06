// chat_reader.cpp
#include "chat_reader.hpp"

#include <algorithm>
#include <chrono>
#include <climits>
#include <memory>

#include "core/chat_geometry.hpp"
#include "core/i18n.hpp"
#include "core/tesseract_tsv.hpp"
#include "core/text.hpp"
#include "win/files.hpp"
#include "win/folder_cleanup.hpp"
#include "win/screen_capture.hpp"

namespace gct {

namespace {
constexpr int kMaxCaptures = 20;          // diagnostics files rotate (3 files each)
constexpr ULONGLONG kForceRereadMs = 20000;  // re-read an unchanged picture now and then

std::vector<TextRow> ScaledRows(const LineGrid& g, int scale) {
    std::vector<TextRow> rows = g.rows;
    for (TextRow& r : rows) r = {r.top * scale, r.height * scale};
    return rows;
}
}  // namespace

// ---------------------------------------------------------------------------
// Measured on real 4K captures: Windows OCR reads normal chat text better and
// about 20x faster (~90 ms) than Tesseract (~2 s per picture). So "automatic"
// uses Windows OCR and takes Tesseract (when installed) only for very small
// text, where it holds up better.
bool ChatOcr::Init(const ReaderOptions& o, std::wstring* error) {
    choice_ = o.ocrChoice;
    haveTess_ = haveWin_ = useTess_ = false;
    if (o.ocrChoice != 2) {
        TesseractInfo info;
        if (FindTesseract(o.tesseractPath, &info)) {
            const std::string langs = ChooseTesseractLangs(o.tesseractLangs, info.models, o.readChinese);
            haveTess_ = tess_.Init(info, langs, error);
            tess_.SetLightText(true);  // the picture is the chat as captured: light text on dark
        } else if (o.ocrChoice == 1 && error) {
            *error = Tr(L"Tesseract not found – using Windows text recognition");
        }
    }
    if (!(o.ocrChoice == 1 && haveTess_)) haveWin_ = win_.Init(o.ocrLanguage, error);
    useTess_ = haveTess_ && !haveWin_;
    return haveTess_ || haveWin_;
}

std::wstring ChatOcr::EngineName() const { return useTess_ ? L"Tesseract" : L"Windows OCR"; }

std::wstring ChatOcr::Language() const { return useTess_ ? tess_.Language() : win_.Language(); }

bool ChatOcr::Read(const Image& raw, int fixedScale, std::vector<OcrLine>& out, Image* preparedOut, std::wstring* error) {
    out.clear();
    // The text lines and their spacing, measured from the pixels: decide how
    // much to enlarge (about 30 px line spacing reads best; 4K needs ~2x,
    // 1080p ~3x) and later sort the recognized words into these lines.
    const LineGrid grid = FindLineGrid(raw, {0, 0, raw.width, raw.height});
    if (choice_ == 1) useTess_ = haveTess_;
    else if (choice_ == 2) useTess_ = false;
    else useTess_ = haveTess_ && (!haveWin_ || (grid.pitch > 0 && grid.pitch < kSmallTextPitch));
    int scale = fixedScale > 0 ? std::clamp(fixedScale, 1, 4) : OcrScaleFor(grid);
    const int maxDim = useTess_ ? 6000 : win_.MaxImageDimension();
    while (scale > 1 && maxDim > 0 && (raw.width * scale > maxDim || raw.height * scale > maxDim)) --scale;
    const Image prepared = UpscaleForOcr(raw, scale);
    if (preparedOut) *preparedOut = prepared;

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
    if (useTess_) {
        std::vector<TsvLine> tl;
        if (!tess_.Recognize(prepared, tl, error)) return false;
        for (TsvLine& l : tl) {
            Line x;
            x.text = std::move(l.text);
            for (TsvWord& w : l.words) x.words.push_back({std::move(w.text), w.rect});
            found.push_back(std::move(x));
        }
    } else {
        std::vector<OcrTextLine> tl;
        if (!win_.Recognize(prepared, tl, error)) return false;
        for (OcrTextLine& l : tl) {
            Line x;
            x.text = std::move(l.text);
            x.hasColor = l.hasColor;
            x.color = l.color;
            for (OcrWordBox& w : l.words) x.words.push_back({std::move(w.text), w.rect});
            found.push_back(std::move(x));
        }
    }
    // Words into the measured lines: text recognition sometimes merges several
    // chat lines into one or reports them out of order. (Test doubles deliver
    // their own colours per line and are kept as they are.)
    if (grid.Found() && !found.empty() && !found.front().hasColor) {
        std::vector<BoxWord> words;
        for (const Line& l : found)
            for (const Word& w : l.words) words.push_back({w.text, w.rect});
        found.clear();
        const LineGrid scaled{ScaledRows(grid, scale), grid.pitch * scale, grid.textHeight * scale};
        for (const auto& row : GroupWordsByRows(words, scaled)) {
            Line x;
            for (const BoxWord& w : row) {
                x.text += (x.text.empty() ? L"" : L" ") + w.text;
                x.words.push_back({w.text, w.rect});
            }
            found.push_back(std::move(x));
        }
    }
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
        out.push_back(std::move(line));
    }
    std::stable_sort(out.begin(), out.end(), [](const OcrLine& a, const OcrLine& b) { return a.top < b.top; });
    return true;
}

// ---------------------------------------------------------------------------
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

void ChatReader::SetTarget(HWND hwnd) {
    std::lock_guard<std::mutex> lk(mu_);
    if (target_ == hwnd) return;
    target_ = hwnd;
    force_ = true;
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
    ChatOcr ocr;
    std::wstring err;
    if (!ocr.Init(opt_, &err)) {
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
        HWND target;
        bool force, save;
        {
            std::unique_lock<std::mutex> lk(mu_);
            cv_.wait_for(lk, std::chrono::milliseconds(opt_.intervalMs), [this] { return stop_ || force_; });
            if (stop_) return;
            area = area_;
            target = target_;
            force = force_;
            force_ = false;
            save = save_;
        }
        if (IsRectEmpty(&area)) continue;

        capture.SetTarget(target);
        const ULONGLONG t0 = GetTickCount64();
        Image raw;
        if (!capture.Grab(area, raw)) continue;
        const uint64_t fp = ImageFingerprint(raw);
        if (!force && fp == lastFingerprint && t0 - lastOcr < kForceRereadMs) continue;  // nothing new
        lastFingerprint = fp;
        lastOcr = t0;

        auto snap = std::make_unique<ReaderSnapshot>();
        Image prepared;
        if (!ocr.Read(raw, opt_.scale, snap->lines, &prepared, &err)) snap->error = err;
        snap->captureTick = t0;
        snap->method = capture.Method();
        snap->engine = ocr.EngineName();
        snap->language = ocr.Language();
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
