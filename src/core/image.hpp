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

// OCR wants dark text on a light background at a decent size. GW2 chat is
// small light text on a dark, half-transparent panel: scale up (bilinear),
// take the brightest channel (coloured text stays bright), stretch the
// contrast between background and text, invert.
Image PrepareForOcr(const Image& src, int scale);

// Average colour of the text pixels (clearly brighter than the background) inside `rects`.
Rgb SampleTextColor(const Image& img, const std::vector<RectI>& rects);

// Cheap fingerprint to skip OCR when the chat did not change.
uint64_t ImageFingerprint(const Image& img);

// 32-bit BMP file (diagnostic captures; opens in any image viewer).
std::string EncodeBmp(const Image& img);

}  // namespace gct
