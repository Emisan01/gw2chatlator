// chat_reader.hpp — reads the GW2 chat panel from the screen.
//
// Worker thread: grab the area (WGC / DXGI / GDI) -> skip if unchanged ->
// measure the line grid and enlarge to ~30 px line spacing -> OCR (Tesseract
// when installed, else Windows' own) -> words sorted into the measured lines,
// with their text colours -> post a snapshot to the window. Nothing here
// touches the game process; it only looks at pixels that are on the screen anyway.
#pragma once

#include <windows.h>

#include <condition_variable>
#include <deque>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/chat_line.hpp"
#include "core/image.hpp"
#include "win/ocr.hpp"
#include "win/rapid_ocr.hpp"
#include "win/screen_capture.hpp"
#include "win/spellcheck.hpp"
#include "win/tesseract_ocr.hpp"
#include "core/glyph_reader.hpp"

#include <map>
#include <unordered_set>

namespace gct {

struct ReaderOptions {
    int intervalMs = 400;
    int ocrChoice = 0;            // 0 auto, 1 tesseract, 2 windows, 3 RapidOCR (see OcrChoice)
    std::wstring rapidDir;        // RapidOCR models folder (<data>\rapid)
    std::vector<std::wstring> rapidGroups;  // model groups for the user's languages, Latin first
    std::wstring tesseractPath;   // empty = search
    std::string tesseractLangs;   // empty = automatic
    bool readChinese = false;
    std::wstring ocrLanguage;     // Windows OCR: empty = Windows languages
    int scale = 0;                // 0 = automatic from the line grid
    bool windowCapture = true;    // WGC allowed (else screen capture: no yellow frame on Windows 10)
    bool freeText = false;        // free screen area: keep the lines of the text recognition (columns stay apart)
    bool secondLook = true;       // words the dictionary does not know are read once more, enlarged more
    std::vector<std::wstring> wordLangs;  // dictionaries for that ("DE", "EN-GB"); the OCR language is added
    std::vector<std::wstring> knownWords; // your own words: always correct for the second look
    // Learned recognition fixes ("rnain" -> "main"): applied at once, without reading the word again.
    std::vector<std::pair<std::wstring, std::wstring>> ocrFixes;
    std::wstring glyphDir;        // the glyph reader's letters (<data>\glyphs); empty = no glyph reader
    std::wstring captureDir;      // diagnostics target
    std::vector<ChannelColor> palette;    // calibrated GW2 channel colours
    std::vector<std::wstring> recentSent; // own messages to learn glyphs from
};

// Posted as LPARAM of the notify message; the receiver deletes it.
struct ReaderSnapshot {
    std::vector<OcrLine> lines;  // top to bottom
    std::wstring error;          // OCR unavailable etc. (lines empty)
    std::wstring method;         // "DXGI" / "GDI"
    std::wstring engine;         // "Tesseract" / "Windows OCR"
    std::wstring language;       // OCR language tag(s)
    int milliseconds = 0;        // capture + OCR time
    int secondLooks = 0;         // words read a second time in this picture
    int secondFixes = 0;         // ... of which the second reading was taken
    int glyphRows = 0;           // rows the glyph reader read itself (sure of every letter)
    int glyphLetters = 0;        // letters of the chat font it knows
    std::vector<std::pair<std::wstring, std::wstring>> newFixes;  // learned in this picture (to be remembered)
    ULONGLONG captureTick = 0;   // GetTickCount64() when the picture was taken
    CaptureStatus captureStatus; // HDR, SDR white level and fallback reason
};

// Text recognition of one chat picture, as the reader does it (also used by
// the setup's preview). Init and Read on the same thread.
class ChatOcr {
public:
    // Tesseract when wanted and installed, else Windows OCR. False if neither works.
    bool Init(const ReaderOptions& o, std::wstring* error);
    // Picture of the chat lines -> lines with colours, top to bottom.
    // `fixedScale` 0 = automatic. `prepared` (optional) gets what was recognized.
    bool Read(const Image& raw, int fixedScale, std::vector<OcrLine>& out, Image* prepared, std::wstring* error);
    std::wstring EngineName() const;
    std::wstring Language() const;
    // Second look in the last picture: words read again / taken from the second reading.
    int SecondLooks() const { return lastLooks_; }
    int SecondFixes() const { return lastFixes_; }
    // Glyph reader in the last picture: rows it read itself (sure of every letter), letters it knows.
    int GlyphRows() const { return glyphRows_; }
    size_t GlyphLetters() const;
    ~ChatOcr() { SaveGlyphs(); }
    // Fixes the second look found in the last picture (wrong reading -> word), to be stored.
    const std::vector<std::pair<std::wstring, std::wstring>>& NewFixes() const { return newFixes_; }
    void SetRecentSent(const std::vector<std::wstring>& sent) { recentSent_ = sent; }

    // Line spacing (px) below which "automatic" prefers Tesseract (if installed).
    static constexpr int kSmallTextPitch = 14;

private:
    OcrEngine win_;
    TesseractOcr tess_;
    int choice_ = 0;  // 0 auto, 1 Tesseract, 2 Windows, 3 RapidOCR
    bool haveTess_ = false, haveWin_ = false;
    bool useTess_ = false;  // the engine of the last picture
    bool keepEngineLines_ = false;  // free text: no regrouping by rows (it would merge side-by-side columns)
    std::vector<Rgb> palette_;
    std::vector<std::wstring> recentSent_;

    // RapidOCR: one recognizer per installed model group; a line that looks exactly as before is not
    // recognized again (the chat mostly only scrolls), its text comes from the cache.
    struct RapidLine {
        std::wstring text;
        std::vector<RecWord> words;  // x/w in raw pixels relative to the line's left edge
    };
    bool ReadRapidLine(const Image& crop, RapidLine* out);
    std::vector<std::unique_ptr<RapidRecognizer>> rapid_;
    std::unordered_map<uint64_t, RapidLine> rapidCache_;
    bool useRapid_ = false;

    // Second look: dictionaries (Windows spell checker), answers cached per word, and per word in its line
    // what the second look decided (the same line comes again in every picture: it is looked at once).
    bool IsWord(const std::wstring& core);
    // A confusion-repaired reading the dictionaries know ("syn!ax" -> "syntax", "putput" -> "output"), or "".
    std::wstring PickConfusion(const std::wstring& core, bool garbled);
    // For a garbled word: a dictionary suggestion close to it with marks read as letters, or "".
    std::wstring SuggestFor(const std::wstring& core);
    std::vector<std::unique_ptr<SpellChecker>> checkers_;
    std::unordered_map<std::wstring, bool> wordOk_;
    std::unordered_map<std::wstring, std::wstring> decided_;
    std::unordered_map<std::wstring, std::wstring> fixes_;  // folded wrong reading -> word (learned, persistent)
    std::vector<std::pair<std::wstring, std::wstring>> newFixes_;
    int lastLooks_ = 0, lastFixes_ = 0;

    // Glyph reader (core/glyph_reader): one per letter size, learning from rows whose every word is a real word,
    // reading the rows it is sure of. Rows are known by a hash of their pixels: learned once, read once.
    GlyphReader& GlyphsFor(int textHeight);
    void SaveGlyphs();
    std::wstring glyphDir_;
    std::map<int, GlyphReader> glyphs_;
    std::map<int, int> glyphDirty_;  // rows learned since the last save, per letter size
    std::unordered_set<uint64_t> glyphLearned_;
    std::unordered_map<uint64_t, GlyphReader::Result> glyphCache_;
    int glyphRows_ = 0;
};

class ChatReader {
public:
    ChatReader() = default;
    ~ChatReader() { Stop(); }
    ChatReader(const ChatReader&) = delete;
    ChatReader& operator=(const ChatReader&) = delete;

    void Start(HWND notify, UINT message, ReaderOptions options);
    void Stop();
    bool Running() const { return thread_.joinable(); }

    // Screen area to read; an empty rect pauses reading.
    void SetArea(const RECT& area);
    // Sets the target window for window-targeted capture (e.g. WGC).
    void SetTarget(HWND hwnd);
    void SetSaveCaptures(bool on);
    // Saves the next capture of the chat area as an .f16 file.
    void SaveNextF16(const std::wstring& path);
    // Read again at once, even if the picture did not change.
    void Rescan();
    // Tells the reader of own messages sent, so the glyph reader can learn from them.
    void AddRecentSent(const std::wstring& text);

private:
    void Loop();
    void SaveDiagnostics(const Image& raw, const Image& prepared, const std::vector<OcrLine>& lines);

    std::thread thread_;
    std::mutex mu_;
    std::condition_variable cv_;
    bool stop_ = false;
    bool force_ = false;
    bool save_ = false;
    std::wstring saveF16Path_;
    RECT area_{};
    HWND target_ = nullptr;
    HWND notify_ = nullptr;
    UINT message_ = 0;
    ReaderOptions opt_;
    std::deque<std::wstring> recentSent_;
    int captureIndex_ = 0;
};

}  // namespace gct
