// image.hpp — pixel work for the chat reader (portable).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "chat_line.hpp"

namespace gct {

// Top-down BGRA, 4 bytes per pixel, no padding.
struct Image {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> bgra;
    bool Empty() const { return width <= 0 || height <= 0 || bgra.size() < static_cast<size_t>(width) * height * 4; }
};

struct RectI {
    int x = 0, y = 0, w = 0, h = 0;
};

// The old preparation (scale up bilinear, brightest channel, contrast
// stretch, invert). No longer used by the reader: measured on real 4K
// captures it made Tesseract read 2-3x worse than the dynamic enlargement
// (chat_geometry.hpp). Kept as the baseline of tests/tools/ocr_bench.
Image PrepareForOcr(const Image& src, int scale);

// Average colour of the text pixels (clearly brighter than the background) inside `rects`.
Rgb SampleTextColor(const Image& img, const std::vector<RectI>& rects);

// Automatic contrast of one crop: grey levels from the brightest channel, the darkest 2 % mapped to black and the
// brightest 1 % to white (coloured text on a half-transparent panel spreads over the whole range). `greyOnly`:
// without stretching (measurement baseline).
Image AutoContrast(const Image& src, bool greyOnly = false);

// Cheap fingerprint to skip OCR when the chat did not change.
uint64_t ImageFingerprint(const Image& img);

// 32-bit BMP file (diagnostic captures; opens in any image viewer).
std::string EncodeBmp(const Image& img);

}  // namespace gct
