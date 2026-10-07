// chat_reader.cpp
#include "chat_reader.hpp"

#include <algorithm>
#include <chrono>
#include <climits>
#include <memory>

#include "core/chat_geometry.hpp"
#include "core/i18n.hpp"
#include "core/rapid_models.hpp"
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
    SaveGlyphs();
    glyphDir_ = o.glyphDir;
    glyphs_.clear();
    glyphDirty_.clear();
    glyphLearned_.clear();
    glyphCache_.clear();
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
    // RapidOCR (open source, local): the installed model groups for your languages, Latin first.
    rapid_.clear();
    rapidCache_.clear();
    useRapid_ = false;
    if ((o.ocrChoice == 0 || o.ocrChoice == 3) && RapidRecognizer::RuntimeAvailable(nullptr)) {
        for (const std::wstring& id : o.rapidGroups) {
            const RapidModelGroup* g = FindRapidGroup(id);
            const std::wstring dir = g ? RapidGroupDir(*g, o.rapidDir) : L"";
            if (dir.empty()) continue;
            auto r = std::make_unique<RapidRecognizer>();
            if (r->Load(dir + L"\\" + RapidModelFile(*g), dir + L"\\" + RapidDictFile(*g), nullptr))
                rapid_.push_back(std::move(r));
        }
    }
    if (o.ocrChoice == 3 && rapid_.empty() && error)
        *error = Tr(L"RapidOCR is not installed – using Windows text recognition");
    // Dictionaries for the second look: the OCR language plus the languages of the chat you read and write.
    checkers_.clear();
    wordOk_.clear();
    decided_.clear();
    for (const std::wstring& w : o.knownWords) wordOk_[CaseFold(w)] = true;
    fixes_.clear();
    for (const auto& [wrong, right] : o.ocrFixes) fixes_[CaseFold(wrong)] = right;
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
    return haveTess_ || haveWin_ || !rapid_.empty();
}

// One line with RapidOCR: from the cache when the pixels are the same as before; otherwise the first model
// group, and the other groups only when it was unsure (another script).
bool ChatOcr::ReadRapidLine(const Image& crop, RapidLine* out) {
    uint64_t h = 1469598103934665603ull;  // FNV-1a over the pixels
    for (uint8_t b : crop.bgra) h = (h ^ b) * 1099511628211ull;
    h ^= static_cast<uint64_t>(crop.width) << 32 | static_cast<uint32_t>(crop.height);
    if (const auto it = rapidCache_.find(h); it != rapidCache_.end()) {
        *out = it->second;
        return true;
    }
    RecResult best;
    for (const auto& r : rapid_) {
        RecResult res = r->Recognize(crop, false, nullptr);
        if (res.confidence > best.confidence) best = std::move(res);
        if (best.confidence >= 0.8f) break;
    }
    RapidLine line;
    // Below RapidOCR's own default score (0.5) a line is mostly noise: a half-hidden line under the tab bar,
    // an icon, a frame edge.
    if (best.confidence >= 0.5f) line.text = best.text;
    const double toRaw = static_cast<double>(crop.height) / kRecHeight;
    for (RecWord w : RecWords(best, best.inputWidth)) {
        w.x = static_cast<int>(w.x * toRaw);
        w.w = std::max(1, static_cast<int>(w.w * toRaw));
        line.words.push_back(std::move(w));
    }
    if (rapidCache_.size() > 2000) rapidCache_.clear();
    rapidCache_[h] = line;
    *out = std::move(line);
    return true;
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

std::wstring ChatOcr::PickConfusion(const std::wstring& core, bool garbled) {
    if (!garbled && core.size() < 5) return {};  // short words and names: too many real words one letter apart
    std::vector<std::wstring> valid;
    for (const std::wstring& c : ConfusionCandidates(core)) {
        if (c.size() < 2 || !IsWord(c)) continue;
        valid.push_back(c);
        if (valid.size() >= 4) break;
    }
    if (valid.empty()) return {};
    if (valid.size() == 1) return valid.front();
    // Several real words: the dictionary's own ranking decides ("putput": "output" before "putout").
    for (const auto& checker : checkers_)
        for (const std::wstring& s : checker->Suggest(garbled ? MarksToLetters(core) : core, 8))
            for (const std::wstring& v : valid)
                if (CaseFold(s) == CaseFold(v)) return v;
    return garbled ? valid.front() : std::wstring();
}

std::wstring ChatOcr::SuggestFor(const std::wstring& core) {
    const std::wstring base = MarksToLetters(core);
    if (base.size() < 3) return {};
    for (const auto& checker : checkers_)
        for (const std::wstring& s : checker->Suggest(base, 5))
            if (s.find(L' ') == std::wstring::npos && PlausibleRereading(base, s)) return s;
    return {};
}

GlyphReader& ChatOcr::GlyphsFor(int textHeight) {
    auto it = glyphs_.find(textHeight);
    if (it != glyphs_.end()) return it->second;
    GlyphReader& gr = glyphs_[textHeight];
    std::string data;
    if (ReadFileBytes(glyphDir_ + L"\\glyphs_" + std::to_wstring(textHeight) + L".txt", data)) gr.Parse(data);
    return gr;
}

void ChatOcr::SaveGlyphs() {
    if (glyphDir_.empty()) return;
    for (auto& [height, dirty] : glyphDirty_) {
        if (dirty == 0) continue;
        EnsureDir(glyphDir_);
        if (WriteFileAtomic(glyphDir_ + L"\\glyphs_" + std::to_wstring(height) + L".txt", glyphs_[height].Serialize()))
            dirty = 0;
    }
}

size_t ChatOcr::GlyphLetters() const {
    size_t n = 0;
    for (const auto& [h, g] : glyphs_) n = std::max(n, g.Letters());
    return n;
}

std::wstring ChatOcr::EngineName() const {
    return useRapid_ ? L"RapidOCR" : useTess_ ? L"Tesseract" : L"Windows OCR";
}

std::wstring ChatOcr::Language() const {
    return useRapid_ ? std::to_wstring(rapid_.size()) + L" model(s)" : useTess_ ? tess_.Language() : win_.Language();
}

bool ChatOcr::Read(const Image& raw, int fixedScale, std::vector<OcrLine>& out, Image* preparedOut, std::wstring* error) {
    out.clear();
    // The text lines and their spacing, measured from the pixels: decide how
    // much to enlarge (about 30 px line spacing reads best; 4K needs ~2x,
    // 1080p ~3x) and later sort the recognized words into these lines.
    const LineGrid grid = FindLineGrid(raw, {0, 0, raw.width, raw.height});
    // Measured (ocr_bench): Windows OCR is best at normal/large chat text, RapidOCR clearly best at small text
    // (1080p), Tesseract in between and slowest. Automatic: RapidOCR (if installed) or Tesseract for small text.
    const bool smallText = grid.pitch > 0 && grid.pitch < kSmallTextPitch;
    useRapid_ = !rapid_.empty() && (choice_ == 3 || (choice_ == 0 && (smallText || !haveWin_)));
    if (useRapid_ && !keepEngineLines_ && !grid.Found()) useRapid_ = false;  // no lines measured: nothing to cut out
    if (useRapid_ && keepEngineLines_ && !haveWin_) useRapid_ = false;       // free text needs the line boxes
    if (useRapid_) useTess_ = false;
    else if (choice_ == 1) useTess_ = haveTess_;
    else if (choice_ == 2 || choice_ == 3) useTess_ = false;
    else useTess_ = haveTess_ && (!haveWin_ || smallText);
    int scale = fixedScale > 0 ? std::clamp(fixedScale, 1, 4) : OcrScaleFor(grid);
    // Free text (apps, websites): small UI fonts with thin strokes ("w" read as "uv", "ü" as "j") read clearly
    // better at least doubled.
    if (keepEngineLines_ && fixedScale <= 0 && (grid.pitch == 0 || grid.pitch < 40)) scale = std::max(scale, 2);
    const int maxDim = useTess_ ? 6000 : win_.MaxImageDimension();
    while (scale > 1 && maxDim > 0 && (raw.width * scale > maxDim || raw.height * scale > maxDim)) --scale;
    // RapidOCR on the measured chat rows cuts its lines from the raw picture: no enlarged copy needed.
    Image prepared;
    if (!(useRapid_ && !keepEngineLines_) || preparedOut) prepared = UpscaleForOcr(raw, scale);
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
    if (useRapid_) {
        // RapidOCR reads single lines: the measured rows of the chat, or for free text the line boxes
        // Windows OCR finds (columns stay apart). Rectangles in raw pixels with a little room above/below.
        std::vector<RectI> rows;
        auto addRow = [&](int x, int y, int w, int h) {
            const int pad = std::max(2, h / 4);
            const int top = std::max(0, y - pad), bottom = std::min(raw.height, y + h + pad);
            const int left = std::max(0, x), right = std::min(raw.width, x + w);
            if (right - left > 4 && bottom - top > 4) rows.push_back({left, top, right - left, bottom - top});
        };
        if (keepEngineLines_) {
            std::vector<OcrTextLine> tl;
            if (!win_.Recognize(prepared, tl, error)) return false;
            for (const OcrTextLine& l : tl) {
                int x0 = INT_MAX, y0 = INT_MAX, x1 = 0, y1 = 0;
                for (const OcrWordBox& w : l.words) {
                    x0 = std::min(x0, w.rect.x);
                    y0 = std::min(y0, w.rect.y);
                    x1 = std::max(x1, w.rect.x + w.rect.w);
                    y1 = std::max(y1, w.rect.y + w.rect.h);
                }
                if (x1 > x0 && y1 > y0) addRow(x0 / scale - 4, y0 / scale, (x1 - x0) / scale + 8, (y1 - y0) / scale);
            }
        } else {
            for (const TextRow& row : grid.rows) addRow(0, row.top, raw.width, row.height);
        }
        for (const RectI& rr : rows) {
            RapidLine rl;
            ReadRapidLine(Crop(raw, rr), &rl);
            if (rl.text.empty()) continue;
            Line x;
            x.text = rl.text;
            for (const RecWord& w : rl.words)
                x.words.push_back({w.text, RectI{(rr.x + w.x) * scale, rr.y * scale, w.w * scale, rr.h * scale}});
            found.push_back(std::move(x));
        }
    } else if (useTess_) {
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
    if (!useRapid_ && !keepEngineLines_ && grid.Found() && !found.empty() && !found.front().hasColor) {
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
    // Second look: a word the dictionaries do not know is checked against the characters text recognition confuses
    // ("syn!ax" -> "syntax", "putput" -> "output"), else read once more – cut out with some room around it and
    // enlarged more than the first time. Taken only when the result is a real word and close to the first;
    // otherwise an ordinary unknown word stays (people write odd words, names, slang). A garbled token nobody types
    // ("pvg!pyt", "9QEine") that cannot be repaired is dropped, and so is an unknown word cut off at the edge of a
    // free screen area. Each word in its line is decided once (the same line comes in every picture), at most a few
    // new readings per picture.
    lastLooks_ = lastFixes_ = 0;
    newFixes_.clear();
    if (!found.empty() && !found.front().hasColor) {
        constexpr int kMaxLooksPerPicture = 6;
        const bool dict = !checkers_.empty();
        const int altScale = scale >= 3 ? 4 : scale + 2;  // clearly larger than the first reading
        // A frame drawn tightly around a text column touches the first word of most lines: that is not a cut. Only
        // when few lines start at the left edge is a word there a cut-off piece.
        size_t atLeftEdge = 0;
        for (const Line& l : found)
            if (!l.words.empty() && l.words.front().rect.x / scale <= 3) ++atLeftEdge;
        const bool tightLeft = atLeftEdge * 2 >= found.size() && atLeftEdge >= 2;
        for (Line& line : found) {
            bool changed = false;
            for (Word& w : line.words) {
                const bool garbled = LooksGarbled(w.text);
                size_t at = 0;
                const std::wstring core = garbled ? GarbledCore(w.text, &at) : WordCore(w.text, &at);
                if (!garbled && (!dict || !WorthSecondLook(core))) continue;
                // The speaker ("Marco:") is a name: never made into a dictionary word.
                if (!garbled && !keepEngineLines_ && &w == &line.words.front() && !w.text.empty() &&
                    w.text.back() == L':')
                    continue;
                const std::wstring key = core + L'\x1f' + line.text;
                if (const auto it = decided_.find(key); it != decided_.end()) {
                    if (it->second != w.text) {
                        w.text = it->second;
                        changed = true;
                    }
                    continue;
                }
                // A recognition error seen before: fixed at once, no second reading needed.
                if (const auto fx = fixes_.find(CaseFold(core)); fx != fixes_.end()) {
                    const std::wstring decision = w.text.substr(0, at) + fx->second + w.text.substr(at + core.size());
                    decided_[key] = decision;
                    w.text = decision;
                    changed = true;
                    continue;
                }
                if (!garbled && IsWord(core)) continue;
                std::wstring fixed = dict ? PickConfusion(core, garbled) : L"";
                std::wstring decision = w.text;
                if (fixed.empty() && haveWin_) {
                    if (lastLooks_ >= kMaxLooksPerPicture) continue;  // the rest next picture
                    ++lastLooks_;
                    // The word in the picture (raw pixels) with half a line height above/below and some room aside.
                    const int h = std::max(1, w.rect.h / scale);
                    RectI r{w.rect.x / scale - h, w.rect.y / scale - h / 2, w.rect.w / scale + 2 * h, h * 2};
                    r.x = std::max(0, r.x);
                    r.y = std::max(0, r.y);
                    r.w = std::min(r.w, raw.width - r.x);
                    r.h = std::min(r.h, raw.height - r.y);
                    std::vector<OcrTextLine> again;
                    std::wstring ignored;
                    if (r.w > 4 && r.h > 4 && win_.Recognize(UpscaleForOcr(Crop(raw, r), altScale), again, &ignored)) {
                        // The word whose middle is closest to where the first one was.
                        const double want = (w.rect.x / static_cast<double>(scale) + w.rect.w / (2.0 * scale) - r.x) * altScale;
                        const OcrWordBox* best = nullptr;
                        double bestDist = 1e18;
                        for (const OcrTextLine& l : again)
                            for (const OcrWordBox& bx : l.words) {
                                const double d = std::abs(bx.rect.x + bx.rect.w / 2.0 - want);
                                if (d < bestDist) {
                                    bestDist = d;
                                    best = &bx;
                                }
                            }
                        if (best) {
                            const std::wstring second = WordCore(best->text);
                            const std::wstring base = garbled ? MarksToLetters(core) : core;
                            const bool close = PlausibleRereading(base, second) || (garbled && base == second) ||
                                               (garbled && PlausibleRereading(core, second));
                            if (!second.empty() && !LooksGarbled(best->text) && close && (!dict || IsWord(second)))
                                fixed = second;
                        }
                    }
                }
                if (fixed.empty() && dict && garbled) fixed = SuggestFor(core);
                if (!fixed.empty()) {
                    decision = w.text.substr(0, at) + fixed + w.text.substr(at + core.size());
                    ++lastFixes_;
                    fixes_[CaseFold(core)] = fixed;  // learned: next time without reading again
                    newFixes_.push_back({core, fixed});
                } else if (garbled) {
                    decision.clear();  // no person wrote this: better a gap than nonsense
                } else if (keepEngineLines_) {
                    // An unknown word at the left or right edge of the free area: cut off by the frame ("berc").
                    const int x0 = w.rect.x / scale, x1 = (w.rect.x + w.rect.w) / scale;
                    if ((x0 <= 3 && !tightLeft) || x1 >= raw.width - 3) decision.clear();
                }
                if (decided_.size() > 5000) decided_.clear();
                decided_[key] = decision;
                if (decision != w.text) {
                    w.text = decision;
                    changed = true;
                }
            }
            if (changed) {
                line.words.erase(std::remove_if(line.words.begin(), line.words.end(),
                                                [](const Word& x) { return Trim(x.text).empty(); }),
                                 line.words.end());
                line.text.clear();
                for (const Word& w : line.words) line.text += (line.text.empty() ? L"" : L" ") + w.text;
            }
        }
        found.erase(std::remove_if(found.begin(), found.end(), [](const Line& l) { return l.words.empty(); }),
                    found.end());
    }
    // Glyph reader (chat only: the GW2 chat font). Every measured row: learned from when the text recognition read
    // it and every word is a real word (the letters are only taken when they also fit the pictures already known),
    // and read by it when it is sure of every letter – then its reading replaces the recognition's for that row.
    glyphRows_ = 0;
    if (!keepEngineLines_ && !glyphDir_.empty() && grid.Found() && grid.textHeight >= 6) {
        GlyphReader& gr = GlyphsFor(grid.textHeight);
        int learnBudget = 24;  // words per picture: learning is the slow part
        for (const TextRow& row : grid.rows) {
            const int pad = std::max(2, row.height / 4);
            const int y0 = std::max(0, row.top - pad), y1 = std::min(raw.height, row.top + row.height + pad);
            if (y1 - y0 < 4) continue;
            const Image crop = Crop(raw, {0, y0, raw.width, y1 - y0});
            uint64_t h = 1469598103934665603ull;  // FNV-1a over the pixels
            for (uint8_t b : crop.bgra) h = (h ^ b) * 1099511628211ull;
            // The recognition's line in this row: the one whose words sit in it.
            Line* mine = nullptr;
            for (Line& l : found) {
                int inside = 0;
                for (const Word& wd : l.words) {
                    const int cy = (wd.rect.y + wd.rect.h / 2) / scale;
                    inside += cy >= y0 && cy < y1;
                }
                if (inside * 2 > static_cast<int>(l.words.size())) {
                    mine = &l;
                    break;
                }
            }
            // Word by word: every word of the recognition's line that is a real word teaches its letters, right where
            // the recognition found it (names, slang and misread words in the same line do not matter).
            if (mine && !checkers_.empty() && !glyphLearned_.count(h)) {
                glyphLearned_.insert(h);
                for (const Word& wd : mine->words) {
                    if (learnBudget <= 0) break;
                    const std::wstring core = WordCore(wd.text);
                    if (core.size() < 2 || core != wd.text) continue;  // with punctuation around: not exact enough
                    bool letters = true;
                    for (wchar_t c : core) letters = letters && IsWordChar(c) && !(c >= L'0' && c <= L'9');
                    if (!letters || !IsWord(core)) continue;
                    --learnBudget;
                    if (gr.LearnWord(crop, core, wd.rect.x / scale, (wd.rect.x + wd.rect.w) / scale) > 0)
                        ++glyphDirty_[grid.textHeight];
                }
            }
            if (!gr.Ready()) continue;
            auto it = glyphCache_.find(h);
            if (it == glyphCache_.end()) {
                if (glyphCache_.size() > 2000) glyphCache_.clear();
                it = glyphCache_.emplace(h, gr.Read(crop)).first;
            }
            const GlyphReader::Result& r = it->second;
            if (!r.sure) continue;
            Line x;
            x.text = r.text;
            size_t from = 0;
            for (const auto& [wx0, wx1] : r.words) {
                const size_t to = std::min(r.text.find(L' ', from), r.text.size());
                x.words.push_back({r.text.substr(from, to - from),
                                   RectI{wx0 * scale, row.top * scale, (wx1 - wx0) * scale, row.height * scale}});
                from = to + 1;
            }
            if (mine) *mine = std::move(x);
            else found.push_back(std::move(x));
            ++glyphRows_;
        }
        if (glyphDirty_[grid.textHeight] >= 10) SaveGlyphs();
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
        snap->glyphRows = ocr.GlyphRows();
        snap->glyphLetters = static_cast<int>(ocr.GlyphLetters());
        snap->secondFixes = ocr.SecondFixes();
        snap->newFixes = ocr.NewFixes();
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
