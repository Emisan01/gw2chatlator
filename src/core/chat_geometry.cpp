// chat_geometry.cpp
#include "chat_geometry.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace gct {
namespace {

int MaxChannel(const uint8_t* p) { return std::max({p[0], p[1], p[2]}); }

RectI Clip(RectI r, const Image& img) {
    const int x0 = std::clamp(r.x, 0, img.width), y0 = std::clamp(r.y, 0, img.height);
    const int x1 = std::clamp(r.x + r.w, 0, img.width), y1 = std::clamp(r.y + r.h, 0, img.height);
    return {x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
}

int Median(std::vector<int> v) {
    if (v.empty()) return 0;
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(v.size() / 2), v.end());
    return v[v.size() / 2];
}

// Ink = clearly brighter than the panel: the chat is light text on a dark,
// half-transparent background. Threshold between the typical pixel and the
// brightest ones, so coloured names (darker than white text) still count.
struct InkMap {
    RectI area;
    std::vector<uint8_t> ink;  // area.w * area.h, 1 = text pixel
    bool At(int x, int y) const { return ink[static_cast<size_t>(y) * area.w + x] != 0; }
};

InkMap FindInk(const Image& img, RectI area) {
    InkMap m;
    m.area = area;
    m.ink.assign(static_cast<size_t>(area.w) * area.h, 0);
    if (area.w <= 0 || area.h <= 0) return m;
    std::array<uint32_t, 256> hist{};
    for (int y = 0; y < area.h; ++y)
        for (int x = 0; x < area.w; ++x)
            ++hist[MaxChannel(&img.bgra[(static_cast<size_t>(area.y + y) * img.width + area.x + x) * 4])];
    const uint64_t n = static_cast<uint64_t>(area.w) * area.h;
    auto pct = [&](double q) {
        uint64_t acc = 0;
        const uint64_t want = static_cast<uint64_t>(n * q);
        for (int v = 0; v < 256; ++v) {
            acc += hist[v];
            if (acc > want) return v;
        }
        return 255;
    };
    const int bg = pct(0.5), hi = pct(0.99);
    const int threshold = bg + std::max(35, (hi - bg) * 4 / 10);
    if (threshold > 250) return m;  // nothing stands out: no text
    for (int y = 0; y < area.h; ++y)
        for (int x = 0; x < area.w; ++x)
            m.ink[static_cast<size_t>(y) * area.w + x] =
                MaxChannel(&img.bgra[(static_cast<size_t>(area.y + y) * img.width + area.x + x) * 4]) >= threshold;
    return m;
}

LineGrid GridFromInk(const InkMap& m) {
    LineGrid g;
    const RectI& a = m.area;
    if (a.w <= 0 || a.h <= 0) return g;
    const int minInk = std::max(2, a.w / 250);
    // Vertical things (scroll bar, panel border) would glue all lines together,
    // so they do not count: ink 40+ rows in a row, which text never has (the
    // gaps between lines interrupt it).
    std::vector<uint8_t> skip(static_cast<size_t>(a.w), 0);
    for (int x = 0; x < a.w; ++x) {
        int run = 0, longest = 0;
        for (int y = 0; y < a.h; ++y) {
            run = m.At(x, y) ? run + 1 : 0;
            longest = std::max(longest, run);
        }
        skip[static_cast<size_t>(x)] = longest >= 40;
    }
    std::vector<int> profile(static_cast<size_t>(a.h), 0);
    for (int y = 0; y < a.h; ++y)
        for (int x = 0; x < a.w; ++x) profile[static_cast<size_t>(y)] += m.At(x, y) && !skip[static_cast<size_t>(x)];
    std::vector<TextRow> runs;
    for (int y = 0; y < a.h;) {
        if (profile[static_cast<size_t>(y)] < minInk) {
            ++y;
            continue;
        }
        TextRow r{y, 0};
        while (y < a.h && profile[static_cast<size_t>(y)] >= minInk) ++y;
        r.height = y - r.top;
        // Dots and accents one row above the letters belong to the line below them.
        if (!runs.empty() && runs.back().height <= 2 && r.top - (runs.back().top + runs.back().height) <= 1) {
            r = {runs.back().top, y - runs.back().top};
            runs.pop_back();
        }
        runs.push_back(r);
    }
    // Specks (a lone pixel row) are no lines.
    runs.erase(std::remove_if(runs.begin(), runs.end(), [](const TextRow& r) { return r.height <= 2; }), runs.end());
    if (runs.empty()) return g;

    std::vector<int> heights;
    for (const TextRow& r : runs) heights.push_back(r.height);
    const int h = Median(heights);
    std::vector<int> steps;
    for (size_t i = 1; i < runs.size(); ++i)
        if (runs[i].height * 10 <= h * 14 && runs[i - 1].height * 10 <= h * 14) steps.push_back(runs[i].top - runs[i - 1].top);
    g.textHeight = h;
    g.pitch = Median(steps);
    // Descenders touching the next line's ascenders join two lines: split them.
    for (const TextRow& r : runs) {
        if (g.pitch > 0 && r.height * 10 >= h * 16) {
            const int k = std::max(2, static_cast<int>(std::lround(static_cast<double>(r.height + g.pitch - h) / g.pitch)));
            for (int j = 0; j < k; ++j) g.rows.push_back({r.top + j * g.pitch, h});
        } else {
            g.rows.push_back(r);
        }
    }
    return g;
}

double Cubic(double v0, double v1, double v2, double v3, double t) {  // Catmull-Rom
    return v1 + 0.5 * t * (v2 - v0 + t * (2.0 * v0 - 5.0 * v1 + 4.0 * v2 - v3 + t * (3.0 * (v1 - v2) + v3 - v0)));
}

}  // namespace

Image Crop(const Image& img, RectI r) {
    Image out;
    r = Clip(r, img);
    if (r.w <= 0 || r.h <= 0) return out;
    out.width = r.w;
    out.height = r.h;
    out.bgra.resize(static_cast<size_t>(r.w) * r.h * 4);
    for (int y = 0; y < r.h; ++y)
        std::copy_n(&img.bgra[(static_cast<size_t>(r.y + y) * img.width + r.x) * 4], static_cast<size_t>(r.w) * 4,
                    &out.bgra[static_cast<size_t>(y) * r.w * 4]);
    return out;
}

LineGrid FindLineGrid(const Image& img, RectI area) {
    if (img.Empty()) return {};
    return GridFromInk(FindInk(img, Clip(area, img)));
}

int OcrScaleFor(const LineGrid& grid) {
    const double spacing = grid.pitch > 0 ? grid.pitch : grid.textHeight * 1.4;
    if (spacing <= 0) return 2;
    return std::clamp(static_cast<int>(std::lround(30.0 / spacing)), 1, 4);
}

Image UpscaleForOcr(const Image& src, int scale) {
    scale = std::clamp(scale, 1, 4);
    if (scale == 1 || src.Empty()) return src;
    Image out;
    out.width = src.width * scale;
    out.height = src.height * scale;
    out.bgra.resize(static_cast<size_t>(out.width) * out.height * 4);
    auto px = [&](int x, int y, int c) {
        x = std::clamp(x, 0, src.width - 1);
        y = std::clamp(y, 0, src.height - 1);
        return static_cast<double>(src.bgra[(static_cast<size_t>(y) * src.width + x) * 4 + c]);
    };
    for (int oy = 0; oy < out.height; ++oy) {
        const double sy = (oy + 0.5) / scale - 0.5;
        const int y1 = static_cast<int>(std::floor(sy));
        const double ty = sy - y1;
        for (int ox = 0; ox < out.width; ++ox) {
            const double sx = (ox + 0.5) / scale - 0.5;
            const int x1 = static_cast<int>(std::floor(sx));
            const double tx = sx - x1;
            uint8_t* o = &out.bgra[(static_cast<size_t>(oy) * out.width + ox) * 4];
            for (int c = 0; c < 3; ++c) {
                double col[4];
                for (int k = 0; k < 4; ++k)
                    col[k] = Cubic(px(x1 - 1, y1 - 1 + k, c), px(x1, y1 - 1 + k, c), px(x1 + 1, y1 - 1 + k, c),
                                   px(x1 + 2, y1 - 1 + k, c), tx);
                o[c] = static_cast<uint8_t>(std::clamp(std::lround(Cubic(col[0], col[1], col[2], col[3], ty)), 0L, 255L));
            }
            o[3] = 255;
        }
    }
    return out;
}

SnapResult SnapChatArea(const Image& img, RectI rough) {
    SnapResult s;
    rough = Clip(rough, img);
    s.area = rough;
    if (rough.w <= 0 || rough.h <= 0) return s;
    // Look a little above and below: a line the frame cuts in half is
    // taken in if it is mostly inside, else left out.
    const int extra = std::max(24, rough.h / 6);
    const RectI look = Clip({rough.x, rough.y - extra, rough.w, rough.h + 2 * extra}, img);
    const LineGrid wide = GridFromInk(FindInk(img, look));
    if (!wide.Found()) {
        s.grid = FindLineGrid(img, rough);
        s.scale = OcrScaleFor(s.grid);
        return s;
    }
    std::vector<TextRow> keep;
    for (TextRow r : wide.rows) {
        // A line that touches the edge of the picture may be cut: too short means it is.
        const bool atEdge = r.top == 0 || r.top + r.height >= look.h;
        if (atEdge && r.height * 4 < wide.textHeight * 3) continue;
        r.top += look.y;  // image coordinates
        const int inside = std::min(r.top + r.height, rough.y + rough.h) - std::max(r.top, rough.y);
        if (inside * 2 >= r.height) keep.push_back(r);
    }
    // The tab bar above and the input line below sit apart from the regular
    // line spacing (panel borders between them).
    const int p = wide.pitch;
    auto offGrid = [&](int step) { return p > 0 && (step * 100 > p * 135 || step * 100 < p * 65); };
    for (int pass = 0; pass < 2 && keep.size() >= 3; ++pass)
        if (offGrid(keep[1].top - keep[0].top)) keep.erase(keep.begin());
    for (int pass = 0; pass < 2 && keep.size() >= 3; ++pass)
        if (offGrid(keep.back().top - keep[keep.size() - 2].top)) keep.pop_back();
    if (keep.empty()) {
        s.grid = FindLineGrid(img, rough);
        s.scale = OcrScaleFor(s.grid);
        return s;
    }
    const int pad = p > 0 ? std::max(2, (p - wide.textHeight + 1) / 2) : 2;
    const int top = std::max(0, keep.front().top - pad);
    const int bottom = std::min(img.height, keep.back().top + keep.back().height + pad);
    RectI area{rough.x, top, rough.w, bottom - top};

    // A scroll bar at the left: a narrow column of ink set apart from the text.
    const InkMap ink = FindInk(img, area);
    std::vector<int> cols(static_cast<size_t>(area.w), 0);
    for (int y = 0; y < area.h; ++y)
        for (int x = 0; x < area.w; ++x) cols[static_cast<size_t>(x)] += ink.At(x, y);
    const int unit = p > 0 ? p : std::max(8, wide.textHeight);
    int x = 0;
    while (x < area.w && cols[static_cast<size_t>(x)] == 0) ++x;
    const int bandStart = x;
    while (x < area.w && cols[static_cast<size_t>(x)] > 0) ++x;
    const int bandEnd = x;
    while (x < area.w && cols[static_cast<size_t>(x)] == 0) ++x;
    if (bandStart < 3 * unit && bandEnd - bandStart <= std::max(4, unit * 9 / 10) && x - bandEnd >= std::max(3, unit * 3 / 10) &&
        x < area.w) {
        const int cut = bandEnd + (x - bandEnd) / 2;
        area.x += cut;
        area.w -= cut;
    }
    s.area = area;
    s.grid = FindLineGrid(img, area);
    s.scale = OcrScaleFor(s.grid);
    return s;
}

std::vector<std::vector<BoxWord>> GroupWordsByRows(const std::vector<BoxWord>& words, const LineGrid& grid) {
    std::vector<std::vector<BoxWord>> rows(grid.rows.size());
    const int reach = std::max(grid.pitch, grid.textHeight);
    for (const BoxWord& w : words) {
        const int cy = w.rect.y + w.rect.h / 2;
        int best = -1, bestD = 1 << 30;
        for (size_t i = 0; i < grid.rows.size(); ++i) {
            const TextRow& r = grid.rows[i];
            const int d = cy < r.top ? r.top - cy : cy > r.top + r.height ? cy - (r.top + r.height) : 0;
            if (d < bestD) {
                bestD = d;
                best = static_cast<int>(i);
            }
        }
        if (best >= 0 && bestD <= reach) rows[static_cast<size_t>(best)].push_back(w);
    }
    std::vector<std::vector<BoxWord>> out;
    for (auto& r : rows) {
        if (r.empty()) continue;
        std::sort(r.begin(), r.end(), [](const BoxWord& a, const BoxWord& b) { return a.rect.x < b.rect.x; });
        out.push_back(std::move(r));
    }
    return out;
}

AreaQuality RateArea(const SnapResult& s) {
    if (!s.grid.Found() || s.grid.rows.size() < 2) return AreaQuality::None;
    return s.scale >= 3 ? AreaQuality::Small : AreaQuality::Good;
}

}  // namespace gct
