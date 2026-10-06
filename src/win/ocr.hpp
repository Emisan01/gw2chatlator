// ocr.hpp — Windows' built-in text recognition (Windows.Media.Ocr, Windows
// 10+), offline, using the OCR packs of the installed Windows languages.
// Used from one worker thread; Init() must run on that thread.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/chat_line.hpp"
#include "core/image.hpp"

namespace gct {

struct OcrWordBox {
    std::wstring text;
    RectI rect;  // in the recognized image's pixels
};

struct OcrTextLine {
    std::wstring text;
    std::vector<OcrWordBox> words;
    bool hasColor = false;  // only test doubles set this; real OCR colours are sampled
    Rgb color;
};

class OcrEngine {
public:
    OcrEngine();
    ~OcrEngine();
    OcrEngine(const OcrEngine&) = delete;
    OcrEngine& operator=(const OcrEngine&) = delete;

    // `languageTag` like "de-DE"; empty = the Windows user languages.
    bool Init(const std::wstring& languageTag, std::wstring* error);
    bool Ready() const;
    std::wstring Language() const;
    int MaxImageDimension() const;

    bool Recognize(const Image& img, std::vector<OcrTextLine>& out, std::wstring* error);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace gct
