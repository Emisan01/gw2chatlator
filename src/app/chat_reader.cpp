// chat_reader.cpp
#include "chat_reader.hpp"

#include <algorithm>
#include <chrono>
#include <climits>
#include <memory>

#include "core/chat_geometry.hpp"
#include "core/i18n.hpp"
#include "core/second_look.hpp"
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
    keepEngineLines_ = o.freeText;
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
    // Dictionaries for the second look: the OCR language plus the languages of the chat you read and write.
    checkers_.clear();
    wordOk_.clear();
    decided_.clear();
    for (const std::wstring& w : o.knownWords) wordOk_[CaseFold(w)] = true;
    if (o.secondLook && haveWin_) {
        std::vector<std::wstring> langs = o.wordLangs;
        langs.insert(langs.begin(), win_.Language());
        langs.push_back(L"EN");
        std::vector<std::wstring> done;
        for (const std::wstring& l : langs) {
            std::wstring primary = CaseFold(Trim(l));
            primary = primary.substr(0, primary.find_first_of(L"-_"));
            if (primary.size() < 2 || std::find(done.begin(), done.end(), primary) != done.end()) continue;
            done.push_back(primary);
            std::wstring region = ToUpperAscii(primary);
            if (primary == L"en") region = L"US";
            auto checker = std::make_unique<SpellChecker>();
            if (checker->Init({primary + L"-" + region, primary})) checkers_.push_back(std::move(checker));
            if (checkers_.size() >= 4) break;
        }
    }
    return haveTess_ || haveWin_;
}

bool ChatOcr::IsWord(const std::wstring& core) {
    const std::wstring key = CaseFold(core);
    if (const auto it = wordOk_.find(key); it != wordOk_.end()) return it->second;
    if (checkers_.empty()) return true;  // no dictionary: nothing is suspicious
    bool ok = false;
    for (const auto& c : checkers_) ok = ok || c->Check(core).empty();
    if (wordOk_.size() > 20000) wordOk_.clear();
    wordOk_[key] = ok;
    return ok;
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
    // Free text (apps, websites): small UI fonts with thin strokes ("w" read as "uv", "ü" as "j") read clearly
    // better at least doubled.
    if (keepEngineLines_ && fixedScale <= 0 && (grid.pitch == 0 || grid.pitch < 40)) scale = std::max(scale, 2);
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
    if (!keepEngineLines_ && grid.Found() && !found.empty() && !found.front().hasColor) {
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
    // Second look: a word the dictionaries do not know is read once more – cut out with some room around it and
    // enlarged more than the first time. Taken only when the new reading is a real word and close to the first
    // ("*ain" -> "main"); otherwise the first stays (people write odd words, names, slang). Each word in its line
    // is decided once (the same line comes in every picture), at most a few new words per picture.
    lastLooks_ = lastFixes_ = 0;
    if (haveWin_ && !checkers_.empty() && !found.empty() && !found.front().hasColor) {
        constexpr int kMaxLooksPerPicture = 6;
        const int altScale = scale >= 3 ? 4 : scale + 2;  // clearly larger than the first reading
        for (Line& line : found) {
            bool changed = false;
            for (Word& w : line.words) {
                size_t at = 0;
                const std::wstring core = WordCore(w.text, &at);
                if (!WorthSecondLook(core)) continue;
                const std::wstring key = core + L'\x1f' + line.text;
                if (const auto it = decided_.find(key); it != decided_.end()) {
                    if (it->second != w.text) {
                        w.text = it->second;
                        changed = true;
                    }
                    continue;
                }
                if (IsWord(core)) continue;
                if (lastLooks_ >= kMaxLooksPerPicture) continue;  // the rest next picture
                ++lastLooks_;
                // The word in the picture (raw pixels) with half a line height above/below and some room aside.
                const int h = std::max(1, w.rect.h / scale);
                RectI r{w.rect.x / scale - h, w.rect.y / scale - h / 2, w.rect.w / scale + 2 * h, h * 2};
                r.x = std::max(0, r.x);
                r.y = std::max(0, r.y);
                r.w = std::min(r.w, raw.width - r.x);
                r.h = std::min(r.h, raw.height - r.y);
                std::wstring decision = w.text;
                std::vector<OcrTextLine> again;
                std::wstring ignored;
                if (r.w > 4 && r.h > 4 && win_.Recognize(UpscaleForOcr(Crop(raw, r), altScale), again, &ignored)) {
                    // The word whose middle is closest to where the first one was.
                    const double want = (w.rect.x / static_cast<double>(scale) + w.rect.w / (2.0 * scale) - r.x) * altScale;
                    const OcrWordBox* best = nullptr;
                    double bestDist = 1e18;
                    for (const OcrTextLine& l : again)
                        for (const OcrWordBox& b : l.words) {
                            const double d = std::abs(b.rect.x + b.rect.w / 2.0 - want);
                            if (d < bestDist) {
                                bestDist = d;
                                best = &b;
                            }
                        }
                    if (best) {
                        const std::wstring second = WordCore(best->text);
                        if (PlausibleRereading(core, second) && IsWord(second)) {
                            // The whole second token: a stray mark read in place of a letter ("*ain") goes too.
                            decision = best->text;
                            ++lastFixes_;
                        }
                    }
                }
                if (decided_.size() > 5000) decided_.clear();
                decided_[key] = decision;
                if (decision != w.text) {
                    w.text = decision;
                    changed = true;
                }
            }
            if (changed) {
                line.text.clear();
                for (const Word& w : line.words) line.text += (line.text.empty() ? L"" : L" ") + w.text;
            }
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
        if (!rects.empty()) {
            int l = INT_MAX, r = 0;
            for (const RectI& x : rects) {
                l = std::min(l, x.x);
                r = std::max(r, x.x + x.w);
            }
            line.left = l;
            line.width = r - l;
        }
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

        capture.SetUseWindowCapture(opt_.windowCapture);
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
        snap->secondLooks = ocr.SecondLooks();
        snap->secondFixes = ocr.SecondFixes();
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
