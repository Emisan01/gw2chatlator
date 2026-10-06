// image.cpp
#include "image.hpp"

#include <algorithm>
#include <array>

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

}  // namespace gct
