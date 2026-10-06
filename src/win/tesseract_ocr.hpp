#pragma once
// Optional OCR engine: Tesseract (https://github.com/tesseract-ocr/tesseract),
// run as a separate process per picture. Much more accurate on the small GW2
// chat font than Windows' own text recognition. Nothing is installed by this
// tool: it uses a Tesseract the user installed (or put next to the exe in
// "tesseract\").
//
//   tesseract.exe stdin stdout --psm 6 -l eng+deu -c tessedit_create_tsv=1
//
// The picture goes in as PGM through stdin, the result comes back as TSV
// through stdout. No temp files.

#include <string>
#include <vector>

#include "core/image.hpp"
#include "core/tesseract_tsv.hpp"

namespace gct {

struct TesseractInfo {
    std::wstring exe;                  // full path of tesseract.exe
    std::wstring tessdata;             // folder with the *.traineddata files
    std::vector<std::string> models;   // installed models ("eng", "deu", ...)
};

// `configured`: a path from the settings (exe or its folder), may be empty.
// Search order: configured, "<our exe>\tesseract\", Program Files
// ("Tesseract-OCR"), %LOCALAPPDATA%\Programs\Tesseract-OCR, PATH.
bool FindTesseract(const std::wstring& configured, TesseractInfo* info);

class TesseractOcr {
public:
    bool Init(const TesseractInfo& info, const std::string& langs, std::wstring* error);
    bool Ready() const { return !info_.exe.empty(); }
    std::wstring Language() const;  // "eng+deu"
    bool Recognize(const Image& img, std::vector<TsvLine>& out, std::wstring* error);

private:
    TesseractInfo info_;
    std::string langs_;
};

}  // namespace gct
