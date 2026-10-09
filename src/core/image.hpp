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

// Lookup table (65536 bytes) for half-float -> sRGB 8-bit:
// value / sdrWhiteFactor, clamp 0..1, sRGB transfer curve.
std::vector<uint8_t> HalfToSrgb8Lut(double sdrWhiteFactor);

// Encodes an FP16 frame (R16G16B16A16_FLOAT) to a .f16 file buffer.
std::string EncodeF16(int width, int height, float sdrWhiteFactor, const uint16_t* rgbaHalf);

// Decodes a .f16 file buffer into BGRA8 Image using HalfToSrgb8Lut.
bool DecodeF16(const std::string& data, Image& out, float* sdrWhiteFactor = nullptr);

// Full chat color palette: calibrated channel colors + white + system yellow + item rarities.
std::vector<Rgb> FullChatPalette(const std::vector<ChannelColor>& userChannels = {});

// Projects a chat row onto the palette, separating letters from the transparent background:
// text ink (dark) on white (or white on dark if !darkOnWhite).
Image ProjectRow(const Image& row, const std::vector<Rgb>& palette, bool darkOnWhite = true);

// FNV-1a hash of the projected and quantised row: background variations behind the chat
// are eliminated so identical text produces identical hashes.
uint64_t HashProjectedRow(const Image& row, const std::vector<Rgb>& palette);

}  // namespace gct
