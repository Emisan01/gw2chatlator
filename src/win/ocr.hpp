// ocr.hpp — Windows' own text recognition (Windows.Media.Ocr, Windows 10+): offline, with the OCR packs of the
// installed Windows languages, nothing downloaded. Ported from the archived chat reader (branch desktop-archive),
// slimmed to what the waypoint click needs: an image in, lines with word boxes out. One thread per engine; Init on it.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace gct {

struct OcrImage {  // top-down BGRA, 4 bytes per pixel, no padding
    int width = 0, height = 0;
    std::vector<uint8_t> bgra;
    bool Empty() const { return width <= 0 || height <= 0 || bgra.size() < static_cast<size_t>(width) * height * 4; }
};

struct OcrWordBox {
    std::wstring text;
    int x = 0, y = 0, w = 0, h = 0;  // in the image's pixels
};

struct OcrTextLine {
    std::wstring text;
    std::vector<OcrWordBox> words;
};

class OcrEngine {
public:
    OcrEngine();
    ~OcrEngine();
    OcrEngine(const OcrEngine&) = delete;
    OcrEngine& operator=(const OcrEngine&) = delete;

    bool Init(std::string* error);  // the Windows user languages
    int MaxImageDimension() const;  // larger images go in tiles
    bool Recognize(const OcrImage& img, std::vector<OcrTextLine>& out, std::string* error);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace gct
