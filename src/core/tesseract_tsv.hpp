#pragma once
// Tesseract OCR glue that needs no Windows: language model names, the
// language set to load, the PGM image we pipe in and the TSV we get back.
// The process itself is started by win/tesseract_ocr.

#include <string>
#include <vector>

#include "core/image.hpp"

namespace gct {

struct TsvWord {
    std::wstring text;
    RectI rect;          // in the recognized image's pixels
    float confidence = 0;  // 0..100
};

struct TsvLine {
    std::wstring text;   // words joined by single spaces
    std::vector<TsvWord> words;
};

// Tesseract's TSV output (`-c tessedit_create_tsv=1`): word rows (level 5)
// grouped into lines (block, paragraph, line). Words below `minConfidence`
// are kept when they are part of a line with good words (chat text is short,
// a single bad word must not drop the line); lines whose words are all below
// it are dropped. Lines come out top to bottom.
std::vector<TsvLine> ParseTesseractTsv(const std::string& tsvUtf8, float minConfidence = 30.0f);

// "DE" -> "deu", "EN-GB" -> "eng", "ZH-HANS" -> "chi_sim" ... empty if unknown.
std::string TesseractModel(const std::wstring& langCode);

// The "-l" argument. `configured` ("eng+deu", or empty for automatic) is
// filtered to the installed models (file names without .traineddata).
// Automatic: English, German, French, Spanish (what the EU/NA chat is
// written in) where installed, plus Simplified Chinese when `chinese`.
std::string ChooseTesseractLangs(const std::string& configured, const std::vector<std::string>& installed,
                                 bool chinese);

// Binary PGM (P5) of the image's brightness: the format Tesseract reads from stdin.
std::string EncodePgm(const Image& img);

}  // namespace gct
