// glyph_reader.cpp
#include "glyph_reader.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <sstream>

#include "text.hpp"

namespace gct {

namespace {

constexpr float kInk = 0.5f;     // ink from half of a piece's brightest pixel (baseline, letter size, pieces)
constexpr float kFaint = 0.15f;  // where text is at all (grey timestamps included)
constexpr float kFloor = 0.12f;  // normalised ink below this is background
constexpr float kEdge = 0.35f;   // a letter pixel is this much brighter than its darkest neighbour (the outline)

double Percentile(std::vector<int> v, double p) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[std::min(v.size() - 1, static_cast<size_t>(p * (v.size() - 1) + 0.5))];
}

std::wstring Squash(const std::wstring& s) {
    std::wstring out;
    bool space = false;
    for (wchar_t c : s) {
        if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n') {
            space = !out.empty();
            continue;
        }
        if (space) out += L' ';
        space = false;
        out += c;
    }
    return out;
}

std::vector<std::wstring> Words(const std::wstring& text) {
    std::vector<std::wstring> words;
    std::wstring cur;
    for (wchar_t c : Squash(text)) {
        if (c == L' ') {
            if (!cur.empty()) words.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) words.push_back(cur);
    return words;
}

struct InkSpan {
    int x0 = 0, x1 = 0;
};

// Pieces of ink between empty columns of rows [y0, y1): faint ink finds where text is at all, then each stretch is
// cut again at half of its own brightest pixel (grey timestamps like bright text).
std::vector<InkSpan> InkPieces(const InkRow& ink, int y0, int y1) {
    y0 = std::max(0, y0);
    y1 = std::min(ink.height, y1);
    auto colMax = [&](int x) {
        float m = 0;
        for (int y = y0; y < y1; ++y) m = std::max(m, ink.At(x, y));
        return m;
    };
    std::vector<InkSpan> coarse, out;
    int start = -1;
    for (int x = 0; x <= ink.width; ++x) {
        const bool on = x < ink.width && colMax(x) >= kFaint;
        if (on && start < 0) start = x;
        if (!on && start >= 0) {
            coarse.push_back({start, x});
            start = -1;
        }
    }
    for (const InkSpan& c : coarse) {
        float peak = 0;
        for (int x = c.x0; x < c.x1; ++x) peak = std::max(peak, colMax(x));
        start = -1;
        for (int x = c.x0; x <= c.x1; ++x) {
            const bool on = x < c.x1 && colMax(x) >= peak * kInk;
            if (on && start < 0) start = x;
            if (!on && start >= 0) {
                out.push_back({start, x});
                start = -1;
            }
        }
    }
    return out;
}

// Baseline (the bottom of most letters) and, for each piece, its top and bottom row of ink.
struct Metrics {
    int base = -1;
    std::vector<int> up, down;  // per piece: rows above the baseline (incl.), rows below it
};

Metrics Measure(const InkRow& ink) {
    Metrics m;
    std::vector<int> bottoms, tops;
    for (const InkSpan& p : InkPieces(ink, 0, ink.height)) {
        float peak = 0.01f;
        for (int y = 0; y < ink.height; ++y)
            for (int x = p.x0; x < p.x1; ++x) peak = std::max(peak, ink.At(x, y));
        int top = -1, bottom = -1;
        for (int y = 0; y < ink.height; ++y)
            for (int x = p.x0; x < p.x1; ++x)
                if (ink.At(x, y) >= peak * kInk) {
                    if (top < 0) top = y;
                    bottom = y;
                }
        if (bottom < 0 || p.x1 - p.x0 < 2) continue;
        tops.push_back(top);
        bottoms.push_back(bottom);
    }
    if (bottoms.size() < 3) return m;
    m.base = static_cast<int>(Percentile(bottoms, 0.5));
    for (size_t i = 0; i < bottoms.size(); ++i) {
        m.up.push_back(m.base - tops[i] + 1);
        m.down.push_back(std::max(0, bottoms[i] - m.base));
    }
    return m;
}

}  // namespace

InkRow MakeInkRow(const Image& row) {
    InkRow ink;
    if (row.Empty()) return ink;
    ink.width = row.width;
    ink.height = row.height;
    ink.v.assign(static_cast<size_t>(row.width) * row.height, 0.0f);
    // The brightest channel: coloured text (blue, green, yellow) is bright in one of them.
    std::vector<int> hist(256, 0);
    std::vector<uint8_t> m(static_cast<size_t>(row.width) * row.height);
    for (size_t i = 0; i < m.size(); ++i) {
        const uint8_t* p = &row.bgra[i * 4];
        m[i] = std::max({p[0], p[1], p[2]});
        ++hist[m[i]];
    }
    auto at = [&](double q) {
        const size_t want = static_cast<size_t>(q * static_cast<double>(m.size()));
        size_t acc = 0;
        for (int v = 0; v < 256; ++v)
            if ((acc += static_cast<size_t>(hist[v])) > want) return v;
        return 255;
    };
    // Text covers a small part of a row: the darker part is panel and game, the brightest pixels are letters.
    const int lo = at(0.6), hi = at(0.995);
    if (hi - lo < 30) return ink;
    for (size_t i = 0; i < m.size(); ++i)
        ink.v[i] = std::clamp((static_cast<float>(m[i]) - lo) / static_cast<float>(hi - lo), 0.0f, 1.0f);
    // GW2 draws its chat letters with a black outline: a letter pixel always has a much darker pixel next to it.
    // The game shining through the half-transparent panel (leaves, grass, sky) has soft edges: it fades out.
    std::vector<float> lum(m.size());
    for (size_t i = 0; i < m.size(); ++i) lum[i] = m[i] / 255.0f;
    const int r = 2;
    std::vector<float> rowMin(m.size()), darkest(m.size());
    for (int y = 0; y < row.height; ++y)
        for (int x = 0; x < row.width; ++x) {
            float d = 1;
            for (int k = std::max(0, x - r); k <= std::min(row.width - 1, x + r); ++k)
                d = std::min(d, lum[static_cast<size_t>(y) * row.width + k]);
            rowMin[static_cast<size_t>(y) * row.width + x] = d;
        }
    for (int y = 0; y < row.height; ++y)
        for (int x = 0; x < row.width; ++x) {
            float d = 1;
            for (int k = std::max(0, y - r); k <= std::min(row.height - 1, y + r); ++k)
                d = std::min(d, rowMin[static_cast<size_t>(k) * row.width + x]);
            darkest[static_cast<size_t>(y) * row.width + x] = d;
        }
    for (size_t i = 0; i < m.size(); ++i) {
        const float edge = lum[i] - darkest[i];  // how far above its darkest neighbour
        ink.v[i] *= std::clamp(edge / kEdge, 0.0f, 1.0f);
    }
    return ink;
}

bool GlyphReader::MakeWindow(const Image& row, Window* w) const {
    const InkRow ink = MakeInkRow(row);
    if (ink.width == 0 || asc_ == 0) return false;
    const Metrics m = Measure(ink);
    if (m.base < 0) return false;
    const int top = m.base - asc_ + 1, H = asc_ + desc_;
    w->width = ink.width;
    w->height = H;
    // Normalised ink: each column against the brightest ink around it, so grey timestamps and bright text alike.
    std::vector<float> colPeak(static_cast<size_t>(ink.width), 0.0f);
    for (int x = 0; x < ink.width; ++x)
        for (int y = 0; y < H; ++y) {
            const int yy = top + y;
            if (yy >= 0 && yy < ink.height) colPeak[static_cast<size_t>(x)] = std::max(colPeak[static_cast<size_t>(x)], ink.At(x, yy));
        }
    const int reach = std::max(4, H / 3);
    w->n.assign(static_cast<size_t>(ink.width) * H, 0.0f);
    w->blank.assign(static_cast<size_t>(ink.width), 0.0f);
    for (int x = 0; x < ink.width; ++x) {
        float local = 0.25f;
        for (int k = std::max(0, x - reach); k <= std::min(ink.width - 1, x + reach); ++k)
            local = std::max(local, colPeak[static_cast<size_t>(k)]);
        for (int y = 0; y < H; ++y) {
            const int yy = top + y;
            float v = yy >= 0 && yy < ink.height ? ink.At(x, yy) / local : 0.0f;
            v = v < kFloor ? 0.0f : std::min(v, 1.0f);
            w->n[static_cast<size_t>(y) * ink.width + x] = v;
            w->blank[static_cast<size_t>(x)] += v * v;
        }
    }
    // Pieces for the first, one-to-one learning (letters that do not touch).
    w->pieces.clear();
    for (const InkSpan& s : InkPieces(ink, top, top + H)) {
        double sum = 0;
        for (int x = s.x0; x < s.x1; ++x) sum += w->blank[static_cast<size_t>(x)];
        if (sum >= 1.5) w->pieces.push_back({s.x0, s.x1});
    }
    return true;
}

double GlyphReader::Cost(const Window& w, int x, const Glyph& g) const {
    double sum = 0;
    const float inv = 1.0f / static_cast<float>(g.count);
    for (int c = 0; c < g.width; ++c) {
        const int xx = x + c;
        for (int y = 0; y < w.height; ++y) {
            const float a = xx < w.width ? w.At(xx, y) : 0.0f;
            const float b = g.sum[static_cast<size_t>(y) * g.width + c] * inv;
            sum += (a - b) * (a - b);
        }
    }
    return sum;
}

void GlyphReader::Add(const Window& w, int x, int width, wchar_t ch) {
    if (width <= 0 || width > w.height * 2) return;
    Glyph* g = nullptr;
    for (Glyph& k : glyphs_)
        if (k.ch == ch && k.width == width) g = &k;
    if (!g) {
        glyphs_.push_back({ch, width, 0, std::vector<float>(static_cast<size_t>(width) * w.height, 0.0f)});
        g = &glyphs_.back();
    }
    for (int y = 0; y < w.height; ++y)
        for (int c = 0; c < width; ++c)
            g->sum[static_cast<size_t>(y) * width + c] += x + c < w.width ? w.At(x + c, y) : 0.0f;
    ++g->count;
}

bool GlyphReader::Knows(wchar_t ch) const {
    for (const Glyph& g : glyphs_)
        if (g.ch == ch) return true;
    return false;
}

void GlyphReader::LearnGaps(const std::vector<std::pair<int, int>>& spans, const std::vector<size_t>& wordOf) {
    int widestIn = -1, narrowestOut = 1 << 30;
    for (size_t i = 1; i < spans.size(); ++i) {
        const int gap = spans[i].first - spans[i - 1].second;
        if (wordOf[i] == wordOf[i - 1]) widestIn = std::max(widestIn, gap);
        else narrowestOut = std::min(narrowestOut, gap);
    }
    if (widestIn >= 0) {
        gapIn_ = (gapIn_ * gapInN_ + widestIn) / (gapInN_ + 1);
        ++gapInN_;
    }
    if (narrowestOut < (1 << 30)) {
        gapOut_ = (gapOut_ * gapOutN_ + narrowestOut) / (gapOutN_ + 1);
        ++gapOutN_;
    }
}

// Every letter its own piece and the word gaps in the right places (else the count matched by chance: one letter in
// two pieces, two letters in one): each piece teaches its letter.
int GlyphReader::LearnOneToOne(const Window& w, const std::vector<std::wstring>& words) {
    std::vector<wchar_t> chars;
    std::vector<size_t> wordOf;
    for (size_t k = 0; k < words.size(); ++k)
        for (wchar_t c : words[k]) {
            chars.push_back(c);
            wordOf.push_back(k);
        }
    if (w.pieces.size() != chars.size()) {
        // Word by word: words are where the gaps are wide (a space is wider than a fifth of the letter height, a gap
        // inside a word at most a few pixels); a word whose pieces match its letters one to one teaches them.
        const int spaceGap = std::max(3, w.height / 5);
        std::vector<std::vector<Piece>> groups(1);
        for (size_t i = 0; i < w.pieces.size(); ++i) {
            if (i > 0 && w.pieces[i].x0 - w.pieces[i - 1].x1 >= spaceGap) groups.emplace_back();
            groups.back().push_back(w.pieces[i]);
        }
        if (groups.size() != words.size()) return 0;
        {
            std::vector<std::pair<int, int>> spans;
            std::vector<size_t> groupOf;
            for (size_t k = 0; k < groups.size(); ++k)
                for (const Piece& p : groups[k]) {
                    spans.push_back({p.x0, p.x1});
                    groupOf.push_back(k);
                }
            LearnGaps(spans, groupOf);
        }
        int taken = 0;
        for (size_t k = 0; k < words.size(); ++k) {
            if (groups[k].size() != words[k].size()) continue;
            for (size_t j = 0; j < words[k].size(); ++j) {
                Add(w, groups[k][j].x0, groups[k][j].x1 - groups[k][j].x0, words[k][j]);
                ++taken;
            }
        }
        return taken;
    }
    int widestIn = -1, narrowestOut = 1 << 30;
    for (size_t i = 1; i < w.pieces.size(); ++i) {
        const int gap = w.pieces[i].x0 - w.pieces[i - 1].x1;
        if (wordOf[i] == wordOf[i - 1]) widestIn = std::max(widestIn, gap);
        else narrowestOut = std::min(narrowestOut, gap);
    }
    if (words.size() >= 2 && narrowestOut <= widestIn) return 0;
    std::vector<std::pair<int, int>> spans;
    for (size_t i = 0; i < chars.size(); ++i) {
        Add(w, w.pieces[i].x0, w.pieces[i].x1 - w.pieces[i].x0, chars[i]);
        spans.push_back({w.pieces[i].x0, w.pieces[i].x1});
    }
    LearnGaps(spans, wordOf);
    return static_cast<int>(chars.size());
}

// Forced alignment: the known letters placed in order where their pictures fit best, empty columns between them.
// Taken only when every letter fits well and no ink is left over.
int GlyphReader::LearnAligned(const Window& w, const std::vector<std::wstring>& words) {
    std::vector<wchar_t> chars;
    std::vector<size_t> wordOf;
    int unknown = 0;
    for (size_t k = 0; k < words.size(); ++k)
        for (wchar_t c : words[k]) {
            unknown += !Knows(c);
            chars.push_back(c);
            wordOf.push_back(k);
        }
    // A few unknown letters are placed between the known ones (their width follows from where the neighbours fit):
    // that is how letters that never stand alone ("m" falls into two pieces) are learned.
    if (unknown > 2) return 0;
    const int W = w.width, n = static_cast<int>(chars.size());
    const int minW = std::max(2, w.height / 8), maxW = w.height * 4 / 3;
    if (n == 0) return 0;
    const double inf = std::numeric_limits<double>::infinity();
    const double penalty = penalty_ * w.height;
    std::vector<double> F(static_cast<size_t>(n + 1) * (W + 1), inf);
    std::vector<int> fromX(F.size(), -1), fromG(F.size(), -1);
    auto at = [&](int i, int x) { return static_cast<size_t>(i) * (W + 1) + x; };
    F[at(0, 0)] = 0;
    for (int i = 0; i <= n; ++i)
        for (int x = 0; x <= W; ++x) {
            const double f = F[at(i, x)];
            if (f == inf) continue;
            if (x < W && f + w.blank[static_cast<size_t>(x)] < F[at(i, x + 1)]) {
                F[at(i, x + 1)] = f + w.blank[static_cast<size_t>(x)];
                fromX[at(i, x + 1)] = x;
                fromG[at(i, x + 1)] = -1;
            }
            if (i == n || x == W) continue;
            if (!Knows(chars[static_cast<size_t>(i)])) {
                // Unknown: any width with ink at both edges and no empty column inside, at a typical letter's cost.
                if (w.blank[static_cast<size_t>(x)] < 0.3) continue;
                for (int wd = minW; wd <= maxW && x + wd <= W; ++wd) {
                    const float edge = w.blank[static_cast<size_t>(x + wd - 1)];
                    if (w.blank[static_cast<size_t>(x + wd - 1)] < 0.05) break;  // an empty column: the letter ended
                    if (edge < 0.3) continue;
                    const double c = f + kAlign * 0.6 * wd * w.height + penalty;
                    if (c < F[at(i + 1, x + wd)]) {
                        F[at(i + 1, x + wd)] = c;
                        fromX[at(i + 1, x + wd)] = x;
                        fromG[at(i + 1, x + wd)] = -2;
                    }
                }
                continue;
            }
            for (size_t gi = 0; gi < glyphs_.size(); ++gi) {
                const Glyph& g = glyphs_[gi];
                if (g.ch != chars[static_cast<size_t>(i)] || x + g.width > W) continue;
                const double c = f + Cost(w, x, g) + penalty;
                if (c < F[at(i + 1, x + g.width)]) {
                    F[at(i + 1, x + g.width)] = c;
                    fromX[at(i + 1, x + g.width)] = x;
                    fromG[at(i + 1, x + g.width)] = static_cast<int>(gi);
                }
            }
        }
    if (F[at(n, W)] == inf) return 0;
    // Back from the end: the placed letters and the empty columns.
    std::vector<std::pair<int, int>> spans(static_cast<size_t>(n));
    int i = n, x = W;
    while (x > 0) {
        const int px = fromX[at(i, x)], g = fromG[at(i, x)];
        if (g == -2) {  // an unknown letter
            spans[static_cast<size_t>(i - 1)] = {px, x};
            --i;
        } else if (g >= 0) {
            const Glyph& gl = glyphs_[static_cast<size_t>(g)];
            if (Cost(w, px, gl) / (gl.width * w.height) > kAlign) return 0;  // a letter that does not fit
            spans[static_cast<size_t>(i - 1)] = {px, x};
            --i;
        } else if (w.blank[static_cast<size_t>(px)] / w.height > kLeftover) {
            return 0;  // ink nobody explains
        }
        x = px;
    }
    for (int k = 0; k < n; ++k)
        Add(w, spans[static_cast<size_t>(k)].first, spans[static_cast<size_t>(k)].second - spans[static_cast<size_t>(k)].first,
            chars[static_cast<size_t>(k)]);
    LearnGaps(spans, wordOf);
    return n;
}

bool GlyphReader::EnsureSize(const Image& row) {
    if (asc_ > 0) return true;
    // The letter window's size from the first row: tallest letters, longest descenders (90 % / 95 % of the pieces, so
    // a speck of a neighbour line does not count).
    const Metrics m = Measure(MakeInkRow(row));
    if (m.up.size() < 4) return false;
    asc_ = static_cast<int>(Percentile(m.up, 0.9)) + 1;
    desc_ = static_cast<int>(Percentile(m.down, 0.95)) + 1;
    return true;
}

GlyphReader::Window GlyphReader::Columns(const Window& w, int x0, int x1) {
    Window s;
    x0 = std::clamp(x0, 0, w.width);
    x1 = std::clamp(x1, x0, w.width);
    s.width = x1 - x0;
    s.height = w.height;
    s.n.assign(static_cast<size_t>(s.width) * s.height, 0.0f);
    for (int y = 0; y < s.height; ++y)
        for (int x = 0; x < s.width; ++x) s.n[static_cast<size_t>(y) * s.width + x] = w.At(x0 + x, y);
    s.blank.assign(w.blank.begin() + x0, w.blank.begin() + x1);
    for (const Piece& p : w.pieces)
        if (p.x0 >= x0 && p.x1 <= x1) s.pieces.push_back({p.x0 - x0, p.x1 - x0});
    return s;
}

int GlyphReader::LearnWord(const Image& row, const std::wstring& word, int x0, int x1) {
    const std::vector<std::wstring> words = Words(word);
    if (words.size() != 1 || !EnsureSize(row)) return 0;
    Window full;
    if (!MakeWindow(row, &full)) return 0;
    // A little room on both sides: the word's box from a text recognition is not exact.
    const Window w = Columns(full, x0 - 2, x1 + 2);
    const int aligned = LearnAligned(w, words);
    return aligned > 0 ? aligned : LearnOneToOne(w, words);
}

int GlyphReader::Learn(const Image& row, const std::wstring& text) {
    const std::vector<std::wstring> words = Words(text);
    if (words.empty() || !EnsureSize(row)) return 0;
    Window w;
    if (!MakeWindow(row, &w)) return 0;
    const int aligned = LearnAligned(w, words);
    return aligned > 0 ? aligned : LearnOneToOne(w, words);
}

int GlyphReader::Train(const std::vector<std::pair<const Image*, std::wstring>>& rows, int rounds) {
    std::vector<bool> used(rows.size(), false);
    int total = 0;
    for (int r = 0; r < rounds; ++r) {
        int got = 0;
        for (size_t i = 0; i < rows.size(); ++i) {
            if (used[i]) continue;
            const int n = Learn(*rows[i].first, rows[i].second);
            if (n > 0) {
                used[i] = true;
                got += n;
            }
        }
        total += got;
        if (got == 0) break;
    }
    return total;
}

double GlyphReader::SpaceGap() const {
    if (space_ > 0) return space_ * (asc_ + desc_);
    if (gapInN_ > 0 && gapOutN_ > 0) return (gapIn_ + gapOut_) / 2;
    return (asc_ + desc_) * 0.2;
}

GlyphReader::Result GlyphReader::Read(const Image& row) const {
    Result r;
    Window w;
    if (glyphs_.empty() || !MakeWindow(row, &w)) return r;
    const int W = w.width;
    const double inf = std::numeric_limits<double>::infinity();
    const double penalty = penalty_ * w.height;
    std::vector<double> R(static_cast<size_t>(W + 1), inf);
    std::vector<int> fromX(R.size(), -1), fromG(R.size(), -1);
    R[0] = 0;
    for (int x = 0; x < W; ++x) {
        const double f = R[static_cast<size_t>(x)];
        if (f == inf) continue;
        if (f + w.blank[static_cast<size_t>(x)] < R[static_cast<size_t>(x + 1)]) {
            R[static_cast<size_t>(x + 1)] = f + w.blank[static_cast<size_t>(x)];
            fromX[static_cast<size_t>(x + 1)] = x;
            fromG[static_cast<size_t>(x + 1)] = -1;
        }
        if (w.blank[static_cast<size_t>(x)] < 0.05) continue;  // a letter starts with ink
        for (size_t gi = 0; gi < glyphs_.size(); ++gi) {
            const Glyph& g = glyphs_[gi];
            if (x + g.width > W) continue;
            const double c = f + Cost(w, x, g) + penalty;
            if (c < R[static_cast<size_t>(x + g.width)]) {
                R[static_cast<size_t>(x + g.width)] = c;
                fromX[static_cast<size_t>(x + g.width)] = x;
                fromG[static_cast<size_t>(x + g.width)] = static_cast<int>(gi);
            }
        }
    }
    if (R[static_cast<size_t>(W)] == inf) return r;
    struct Placed {
        int x0, x1, g;
    };
    std::vector<Placed> placed;
    bool leftover = false;
    for (int x = W; x > 0;) {
        const int px = fromX[static_cast<size_t>(x)], g = fromG[static_cast<size_t>(x)];
        if (g >= 0) placed.push_back({px, x, g});
        else if (w.blank[static_cast<size_t>(px)] / w.height > kLeftover) leftover = true;
        x = px;
    }
    std::reverse(placed.begin(), placed.end());
    const double space = SpaceGap();
    int prevEnd = -1;
    for (const Placed& p : placed) {
        const Glyph& g = glyphs_[static_cast<size_t>(p.g)];
        if (prevEnd < 0 || p.x0 - prevEnd >= space) {
            if (prevEnd >= 0) r.text += L' ';
            r.words.push_back({p.x0, p.x1});
        }
        r.words.back().second = p.x1;
        prevEnd = p.x1;
        const double px = g.width * w.height;
        const double e = Cost(w, p.x0, g) / px;
        // The nearest other letter of the same width at the same place.
        double other = inf;
        for (const Glyph& o : glyphs_)
            if (o.ch != g.ch && o.width == g.width) other = std::min(other, Cost(w, p.x0, o) / px);
        const bool clear = e <= kSure && other - e >= kMargin;
        r.margin = std::min(r.margin, other - e);
        r.text += g.ch;
        ++r.letters;
        r.worst = std::max(r.worst, e);
        if (!clear) ++r.unsure;
    }
    r.leftover = leftover;
    r.sure = r.letters > 0 && r.unsure == 0 && !leftover;
    return r;
}

size_t GlyphReader::Letters() const {
    std::vector<wchar_t> seen;
    for (const Glyph& g : glyphs_)
        if (std::find(seen.begin(), seen.end(), g.ch) == seen.end()) seen.push_back(g.ch);
    return seen.size();
}

std::string GlyphReader::Serialize() const {
    std::ostringstream o;
    o << "glyphs 2\n" << asc_ << ' ' << desc_ << ' ' << gapIn_ << ' ' << gapInN_ << ' ' << gapOut_ << ' ' << gapOutN_
      << '\n';
    const int H = asc_ + desc_;
    for (const Glyph& g : glyphs_) {
        o << static_cast<unsigned>(g.ch) << ' ' << g.width << ' ' << g.count;
        for (int i = 0; i < g.width * H; ++i)
            o << ' ' << static_cast<int>(std::lround(255.0 * g.sum[static_cast<size_t>(i)] / g.count));
        o << '\n';
    }
    return o.str();
}

bool GlyphReader::Parse(const std::string& data) {
    std::istringstream in(data);
    std::string magic;
    int version = 0;
    if (!(in >> magic >> version) || magic != "glyphs" || version != 2) return false;
    GlyphReader r;
    if (!(in >> r.asc_ >> r.desc_ >> r.gapIn_ >> r.gapInN_ >> r.gapOut_ >> r.gapOutN_)) return false;
    const int H = r.asc_ + r.desc_;
    if (r.asc_ <= 0 || r.desc_ < 0 || H > 400) return false;
    unsigned ch = 0;
    Glyph g;
    while (in >> ch >> g.width >> g.count) {
        if (g.width <= 0 || g.width > H * 2 || g.count <= 0) return false;
        g.ch = static_cast<wchar_t>(ch);
        g.sum.assign(static_cast<size_t>(g.width) * H, 0.0f);
        for (float& v : g.sum) {
            int x = 0;
            if (!(in >> x)) return false;
            v = static_cast<float>(x) / 255.0f * static_cast<float>(g.count);
        }
        r.glyphs_.push_back(g);
    }
    *this = std::move(r);
    return true;
}

}  // namespace gct
