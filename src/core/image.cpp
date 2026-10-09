// image.cpp
#include "image.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace gct {

namespace {

inline int Brightness(const uint8_t* p) { return std::max({p[0], p[1], p[2]}); }

int Percentile(const std::array<uint32_t, 256>& hist, uint64_t total, double q) {
    const uint64_t want = static_cast<uint64_t>(total * q);
    uint64_t acc = 0;
    for (int v = 0; v < 256; ++v) {
        acc += hist[v];
        if (acc > want) return v;
    }
    return 255;
}

}  // namespace

Image PrepareForOcr(const Image& src, int scale) {
    Image out;
    if (src.Empty()) return out;
    scale = std::clamp(scale, 1, 4);

    std::array<uint32_t, 256> hist{};
    const size_t n = static_cast<size_t>(src.width) * src.height;
    for (size_t i = 0; i < n; ++i) ++hist[Brightness(&src.bgra[i * 4])];
    const int bg = Percentile(hist, n, 0.50);
    const int fg = std::max(bg + 24, Percentile(hist, n, 0.985));
    const int lo = bg + (fg - bg) / 4;
    const int range = std::max(1, fg - lo);

    out.width = src.width * scale;
    out.height = src.height * scale;
    out.bgra.resize(static_cast<size_t>(out.width) * out.height * 4);

    auto bright = [&](int x, int y) {
        x = std::clamp(x, 0, src.width - 1);
        y = std::clamp(y, 0, src.height - 1);
        return Brightness(&src.bgra[(static_cast<size_t>(y) * src.width + x) * 4]);
    };

    for (int y = 0; y < out.height; ++y) {
        // Sample at pixel centres: (y + 0.5) / scale - 0.5, in fixed point (x256).
        const int sy = ((2 * y + 1) * 128) / scale - 128;
        const int y0 = sy >> 8, fy = sy & 255;
        for (int x = 0; x < out.width; ++x) {
            const int sx = ((2 * x + 1) * 128) / scale - 128;
            const int x0 = sx >> 8, fx = sx & 255;
            const int top = bright(x0, y0) * (256 - fx) + bright(x0 + 1, y0) * fx;
            const int bot = bright(x0, y0 + 1) * (256 - fx) + bright(x0 + 1, y0 + 1) * fx;
            const int v = (top * (256 - fy) + bot * fy) >> 16;
            const int stretched = std::clamp((v - lo) * 255 / range, 0, 255);
            const uint8_t g = static_cast<uint8_t>(255 - stretched);  // dark text on white
            uint8_t* p = &out.bgra[(static_cast<size_t>(y) * out.width + x) * 4];
            p[0] = p[1] = p[2] = g;
            p[3] = 255;
        }
    }
    return out;
}

Image AutoContrast(const Image& src, bool greyOnly) {
    Image out = src;
    if (src.Empty()) return out;
    const size_t n = static_cast<size_t>(src.width) * src.height;
    std::vector<uint8_t> grey(n);
    unsigned hist[256] = {};
    for (size_t i = 0; i < n; ++i) {
        const uint8_t* p = &src.bgra[i * 4];
        grey[i] = std::max(p[0], std::max(p[1], p[2]));  // coloured text keeps its strength
        ++hist[grey[i]];
    }
    int lo = 0, hi = 255;
    if (!greyOnly) {
        size_t acc = 0;
        while (lo < 255 && (acc += hist[lo]) < n * 2 / 100) ++lo;
        acc = 0;
        while (hi > 0 && (acc += hist[hi]) < n / 100) --hi;
        if (hi - lo < 16) {  // flat crop: nothing to stretch
            lo = 0;
            hi = 255;
        }
    }
    for (size_t i = 0; i < n; ++i) {
        const int v = std::clamp((grey[i] - lo) * 255 / std::max(1, hi - lo), 0, 255);
        out.bgra[i * 4] = out.bgra[i * 4 + 1] = out.bgra[i * 4 + 2] = static_cast<uint8_t>(v);
    }
    return out;
}

Rgb SampleTextColor(const Image& img, const std::vector<RectI>& rects) {
    if (img.Empty()) return {};
    std::array<uint32_t, 256> hist{};
    uint64_t total = 0;
    auto forEach = [&](auto&& fn) {
        for (const RectI& r : rects) {
            const int x0 = std::max(0, r.x), y0 = std::max(0, r.y);
            const int x1 = std::min(img.width, r.x + r.w), y1 = std::min(img.height, r.y + r.h);
            for (int y = y0; y < y1; ++y)
                for (int x = x0; x < x1; ++x) fn(&img.bgra[(static_cast<size_t>(y) * img.width + x) * 4]);
        }
    };
    forEach([&](const uint8_t* p) {
        ++hist[Brightness(p)];
        ++total;
    });
    if (total == 0) return {};
    // Text pixels: clearly above the background, near the brightest ones.
    const int bg = Percentile(hist, total, 0.20);
    const int peak = Percentile(hist, total, 0.99);
    const int threshold = bg + (peak - bg) * 6 / 10;

    uint64_t r = 0, g = 0, b = 0, count = 0;
    forEach([&](const uint8_t* p) {
        if (Brightness(p) < threshold) return;
        b += p[0];
        g += p[1];
        r += p[2];
        ++count;
    });
    if (count == 0) return {};
    return {static_cast<uint8_t>(r / count), static_cast<uint8_t>(g / count), static_cast<uint8_t>(b / count)};
}

uint64_t ImageFingerprint(const Image& img) {
    uint64_t h = 1469598103934665603ull;  // FNV-1a
    if (img.Empty()) return h;
    for (int y = 0; y < img.height; y += 2) {
        for (int x = 0; x < img.width; x += 3) {
            const uint8_t* p = &img.bgra[(static_cast<size_t>(y) * img.width + x) * 4];
            for (int c = 0; c < 3; ++c) {
                h ^= static_cast<uint64_t>(p[c] >> 3);
                h *= 1099511628211ull;
            }
        }
    }
    h ^= static_cast<uint64_t>(img.width) << 32 | static_cast<uint32_t>(img.height);
    return h;
}

std::string EncodeBmp(const Image& img) {
    if (img.Empty()) return {};
    const uint32_t pixelBytes = static_cast<uint32_t>(img.width) * static_cast<uint32_t>(img.height) * 4;
    const uint32_t offset = 14 + 40;
    std::string out;
    out.reserve(offset + pixelBytes);
    auto u16 = [&](uint32_t v) {
        out.push_back(static_cast<char>(v & 0xff));
        out.push_back(static_cast<char>((v >> 8) & 0xff));
    };
    auto u32 = [&](uint32_t v) {
        u16(v & 0xffff);
        u16(v >> 16);
    };
    out += "BM";
    u32(offset + pixelBytes);
    u32(0);
    u32(offset);
    u32(40);                                               // BITMAPINFOHEADER
    u32(static_cast<uint32_t>(img.width));
    u32(static_cast<uint32_t>(-static_cast<int32_t>(img.height)));  // negative = top-down
    u16(1);                                                // planes
    u16(32);                                               // bits per pixel
    u32(0);                                                // BI_RGB
    u32(pixelBytes);
    u32(2835);                                             // 72 dpi
    u32(2835);
    u32(0);
    u32(0);
    out.append(reinterpret_cast<const char*>(img.bgra.data()), pixelBytes);
    return out;
}

namespace {
float HalfToFloat(uint16_t h) {
    const int sign = h >> 15, exp = (h >> 10) & 31, man = h & 1023;
    const float v = exp == 0    ? std::ldexp(static_cast<float>(man), -24)
                    : exp == 31 ? 65504.0f
                                : std::ldexp(static_cast<float>(man | 1024), exp - 25);
    return sign ? -v : v;
}
}  // namespace

std::vector<uint8_t> HalfToSrgb8Lut(double sdrWhiteFactor) {
    if (sdrWhiteFactor <= 0.0) sdrWhiteFactor = 1.0;
    std::vector<uint8_t> lut(65536);
    for (uint32_t i = 0; i < 65536; ++i) {
        double v = std::clamp(HalfToFloat(static_cast<uint16_t>(i)) / sdrWhiteFactor, 0.0, 1.0);
        v = v <= 0.0031308 ? 12.92 * v : 1.055 * std::pow(v, 1.0 / 2.4) - 0.055;
        lut[i] = static_cast<uint8_t>(std::lround(v * 255.0));
    }
    return lut;
}

std::string EncodeF16(int width, int height, float sdrWhiteFactor, const uint16_t* rgbaHalf) {
    if (width <= 0 || height <= 0 || !rgbaHalf) return {};
    const size_t pixelBytes = static_cast<size_t>(width) * height * 4 * sizeof(uint16_t);
    std::string out;
    out.resize(24 + pixelBytes);
    std::memcpy(&out[0], "GCT_F16\0", 8);
    const uint32_t w = static_cast<uint32_t>(width);
    const uint32_t h = static_cast<uint32_t>(height);
    const uint32_t zero = 0;
    std::memcpy(&out[8], &w, 4);
    std::memcpy(&out[12], &h, 4);
    std::memcpy(&out[16], &sdrWhiteFactor, 4);
    std::memcpy(&out[20], &zero, 4);
    std::memcpy(&out[24], rgbaHalf, pixelBytes);
    return out;
}

bool DecodeF16(const std::string& data, Image& out, float* sdrWhiteFactor) {
    if (data.size() < 24) return false;
    if (std::memcmp(data.data(), "GCT_F16\0", 8) != 0) return false;
    uint32_t w = 0, h = 0;
    float white = 1.0f;
    std::memcpy(&w, &data[8], 4);
    std::memcpy(&h, &data[12], 4);
    std::memcpy(&white, &data[16], 4);
    if (w == 0 || h == 0 || w > 16384 || h > 16384) return false;
    const size_t pixelBytes = static_cast<size_t>(w) * h * 4 * sizeof(uint16_t);
    if (data.size() < 24 + pixelBytes) return false;
    if (sdrWhiteFactor) *sdrWhiteFactor = white;
    const std::vector<uint8_t> lut = HalfToSrgb8Lut(white > 0.0f ? white : 1.0f);
    out.width = static_cast<int>(w);
    out.height = static_cast<int>(h);
    out.bgra.resize(static_cast<size_t>(w) * h * 4);
    const uint16_t* src = reinterpret_cast<const uint16_t*>(&data[24]);
    uint8_t* dst = out.bgra.data();
    const size_t totalPixels = static_cast<size_t>(w) * h;
    for (size_t i = 0; i < totalPixels; ++i, src += 4, dst += 4) {
        dst[0] = lut[src[2]];
        dst[1] = lut[src[1]];
        dst[2] = lut[src[0]];
        dst[3] = 255;
    }
    return true;
}

std::vector<Rgb> FullChatPalette(const std::vector<ChannelColor>& userChannels) {
    std::vector<Rgb> pal;
    const auto& base = userChannels.empty() ? DefaultChannelColors() : userChannels;
    for (const auto& c : base) pal.push_back(c.rgb);
    pal.push_back({255, 255, 255});  // white
    pal.push_back({255, 208, 0});    // system yellow
    // Item rarity colors
    pal.push_back({170, 170, 170});  // junk
    pal.push_back({98, 164, 218});   // fine
    pal.push_back({26, 147, 6});     // masterwork
    pal.push_back({253, 209, 17});   // rare
    pal.push_back({255, 164, 5});    // exotic
    pal.push_back({251, 62, 141});   // ascended
    pal.push_back({160, 56, 224});   // legendary
    return pal;
}

namespace {

std::vector<float> ComputeRowInk(const Image& row, const std::vector<Rgb>& palette) {
    const size_t n = static_cast<size_t>(row.width) * row.height;
    std::vector<float> ink(n, 0.0f);
    if (row.Empty() || row.width < 4 || row.height < 4) return ink;

    std::vector<int> hist(256, 0);
    std::vector<uint8_t> m(n);
    for (size_t i = 0; i < n; ++i) {
        const uint8_t* p = &row.bgra[i * 4];
        m[i] = std::max({p[0], p[1], p[2]});
        ++hist[m[i]];
    }

    auto at = [&](double q) {
        const size_t want = static_cast<size_t>(q * static_cast<double>(n));
        size_t acc = 0;
        for (int v = 0; v < 256; ++v)
            if ((acc += static_cast<size_t>(hist[v])) > want) return v;
        return 255;
    };

    const int lo = at(0.60), hi = at(0.995);
    if (hi - lo < 25) return ink;

    // GW2 font outline: contrast against darkest 5x5 neighbor
    std::vector<float> lum(n);
    for (size_t i = 0; i < n; ++i) lum[i] = m[i] / 255.0f;
    const int r = 2;
    std::vector<float> rowMin(n), darkest(n);
    for (int y = 0; y < row.height; ++y) {
        for (int x = 0; x < row.width; ++x) {
            float d = 1.0f;
            for (int k = std::max(0, x - r); k <= std::min(row.width - 1, x + r); ++k)
                d = std::min(d, lum[static_cast<size_t>(y) * row.width + k]);
            rowMin[static_cast<size_t>(y) * row.width + x] = d;
        }
    }
    for (int y = 0; y < row.height; ++y) {
        for (int x = 0; x < row.width; ++x) {
            float d = 1.0f;
            for (int k = std::max(0, y - r); k <= std::min(row.height - 1, y + r); ++k)
                d = std::min(d, rowMin[static_cast<size_t>(k) * row.width + x]);
            darkest[static_cast<size_t>(y) * row.width + x] = d;
        }
    }

    const auto pal = palette.empty() ? FullChatPalette() : palette;

    for (size_t i = 0; i < n; ++i) {
        const float base = std::clamp((static_cast<float>(m[i]) - lo) / static_cast<float>(hi - lo), 0.0f, 1.0f);
        const float edge = std::clamp((lum[i] - darkest[i]) / 0.35f, 0.0f, 1.0f);
        const uint8_t* p = &row.bgra[i * 4];
        const int pb = p[0], pg = p[1], pr = p[2];
        int minD2 = 255 * 255 * 3;
        for (const Rgb& c : pal) {
            const int dr = pr - c.r, dg = pg - c.g, db = pb - c.b;
            const int d2 = dr * dr + dg * dg + db * db;
            if (d2 < minD2) minD2 = d2;
        }
        const float colorScore = std::clamp(1.0f - std::sqrt(static_cast<float>(minD2)) / 180.0f, 0.0f, 1.0f);
        ink[i] = base * edge * colorScore;
    }
    return ink;
}

}  // namespace

Image ProjectRow(const Image& row, const std::vector<Rgb>& palette, bool darkOnWhite) {
    Image out;
    if (row.Empty()) return out;
    out.width = row.width;
    out.height = row.height;
    out.bgra.resize(static_cast<size_t>(row.width) * row.height * 4);
    const std::vector<float> ink = ComputeRowInk(row, palette);
    const size_t n = ink.size();
    for (size_t i = 0; i < n; ++i) {
        const float v = std::clamp(ink[i], 0.0f, 1.0f);
        const uint8_t val = darkOnWhite ? static_cast<uint8_t>(std::clamp(255.0f * (1.0f - v), 0.0f, 255.0f))
                                        : static_cast<uint8_t>(std::clamp(255.0f * v, 0.0f, 255.0f));
        out.bgra[i * 4 + 0] = val;
        out.bgra[i * 4 + 1] = val;
        out.bgra[i * 4 + 2] = val;
        out.bgra[i * 4 + 3] = 255;
    }
    return out;
}

uint64_t HashProjectedRow(const Image& row, const std::vector<Rgb>& palette) {
    if (row.Empty()) return 0;
    const std::vector<float> ink = ComputeRowInk(row, palette);
    uint64_t h = 1469598103934665603ull;  // FNV-1a
    for (float v : ink) {
        uint8_t q = 0;
        if (v >= 0.70f) q = 3;
        else if (v >= 0.40f) q = 2;
        else if (v >= 0.18f) q = 1;
        h = (h ^ q) * 1099511628211ull;
    }
    h ^= static_cast<uint64_t>(row.width) << 32 | static_cast<uint32_t>(row.height);
    return h;
}

}  // namespace gct
