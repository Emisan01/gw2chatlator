// chat_reader.hpp — reads the GW2 chat panel from the screen.
//
// Worker thread: grab the area (DXGI / GDI) -> skip if unchanged -> enlarge
// and invert -> OCR (Tesseract when installed, else Windows' own) -> lines
// with their text colours -> post a snapshot to the window. Nothing here touches the game process; it only
// looks at pixels that are on the screen anyway.
#pragma once

#include <windows.h>

#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/chat_line.hpp"
#include "core/image.hpp"

namespace gct {

struct ReaderOptions {
    int intervalMs = 900;
    int ocrChoice = 0;            // 0 auto, 1 tesseract, 2 windows (see OcrChoice)
    std::wstring tesseractPath;   // empty = search
    std::string tesseractLangs;   // empty = automatic
    bool readChinese = false;
    std::wstring ocrLanguage;     // Windows OCR: empty = Windows languages
    int scale = 2;
    std::wstring captureDir;      // diagnostics target
};

// Posted as LPARAM of the notify message; the receiver deletes it.
struct ReaderSnapshot {
    std::vector<OcrLine> lines;  // top to bottom
    std::wstring error;          // OCR unavailable etc. (lines empty)
    std::wstring method;         // "DXGI" / "GDI"
    std::wstring engine;         // "Tesseract" / "Windows OCR"
    std::wstring language;       // OCR language tag(s)
    int milliseconds = 0;        // capture + OCR time
    ULONGLONG captureTick = 0;   // GetTickCount64() when the picture was taken
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
    void SetSaveCaptures(bool on);
    // Read again at once, even if the picture did not change.
    void Rescan();

private:
    void Loop();
    void SaveDiagnostics(const Image& raw, const Image& prepared, const std::vector<OcrLine>& lines);

    std::thread thread_;
    std::mutex mu_;
    std::condition_variable cv_;
    bool stop_ = false;
    bool force_ = false;
    bool save_ = false;
    RECT area_{};
    HWND notify_ = nullptr;
    UINT message_ = 0;
    ReaderOptions opt_;
    int captureIndex_ = 0;
};

}  // namespace gct
