// glyph_reader.hpp — reads GW2 chat rows by recognising the letters of its one
// fixed font, the way a person who knows the font would: every letter is a known
// picture, compared pixel by pixel – no guessing from a general model.
//
// Reading: the best sequence of known letter pictures and empty columns that
// explains all the ink of the row (dynamic programming over the columns, like
// classic readers for a known font) – touching letters need no gap, a letter in
// two faint parts is still one picture. A row counts as read only when every
// letter is clearly that letter and no ink is left unexplained; otherwise the
// general text recognition reads it.
//
// Learning: rows whose text is known (a measured truth, or a line every word of
// which is a real word). First from rows whose ink falls apart into exactly one
// piece per letter, then by placing the known text where its letters fit best
// (forced alignment) – round by round more letters become known.
// Portable (no windows.h).
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "image.hpp"

namespace gct {

// A text row as ink: 0 (background) .. 1 (letter), from brightness relative to the row itself, so the channel colour
// and the game behind the half-transparent panel do not matter.
struct InkRow {
    int width = 0, height = 0;
    std::vector<float> v;  // row-major
    float At(int x, int y) const { return v[static_cast<size_t>(y) * width + x]; }
};
InkRow MakeInkRow(const Image& row);

class GlyphReader {
public:
    // Learns from one row (a cut-out text row) and what it says. Returns the number of letters taken.
    int Learn(const Image& row, const std::wstring& text);
    // Learns one word of a row: `word` stands in columns [x0, x1) (e.g. where the general text recognition found it).
    // The rest of the row may hold names or misread words – only this word teaches its letters.
    int LearnWord(const Image& row, const std::wstring& word, int x0, int x1);
    // Learns from many rows in rounds: what is learned from one row helps to place the letters of the others.
    int Train(const std::vector<std::pair<const Image*, std::wstring>>& rows, int rounds = 4);

    struct Result {
        std::wstring text;
        bool sure = false;  // every letter clearly that letter, no ink left over
        int letters = 0;
        int unsure = 0;     // letters that were not clearly that letter
        double worst = 0;   // the largest difference of a letter to its picture (per pixel, 0 = identical)
        double margin = 1;  // the smallest lead of a letter over the nearest other letter (per pixel)
        bool leftover = false;  // ink in a column read as empty
        std::vector<std::pair<int, int>> words;  // columns [x0, x1) of each word, left to right
    };
    Result Read(const Image& row) const;

    size_t Letters() const;  // distinct characters known
    // Measurement knobs (tests/tools/ocr_bench): the cost of one more letter and the space gap (0 = learned).
    void Tune(double penalty, double space) {
        penalty_ = penalty;
        space_ = space;
    }
    double LearnedSpace() const { return SpaceGap(); }
    bool Ready() const { return Letters() >= 20 && asc_ > 0; }

    // "glyphs 2" text format, ASCII.
    std::string Serialize() const;
    bool Parse(const std::string& data);

    // Thresholds per pixel (0..1), measured with tests/tools/ocr_bench.
    static constexpr double kSure = 0.04;     // a letter this close to its picture is that letter
    static constexpr double kMargin = 0.01;   // ... and every other letter of that width is clearly further away
    static constexpr double kAlign = 0.06;    // learning: the letters placed must fit at least this well
    static constexpr double kLeftover = 0.2;  // ink left in an empty column (per pixel): something was not read

private:
    struct Glyph {
        wchar_t ch = 0;
        int width = 0;
        int count = 0;
        std::vector<float> sum;  // width x height, the mean is sum / count
    };
    struct Piece {
        int x0 = 0, x1 = 0;
    };
    // The letter window of a row: normalised ink rows [base - asc + 1, base + desc], every column.
    struct Window {
        int width = 0, height = 0;
        std::vector<float> n;  // normalised ink, row-major
        std::vector<float> blank;  // per column: the cost of calling it empty (its squared ink)
        std::vector<Piece> pieces;  // ink between empty columns (for the first, one-to-one learning)
        float At(int x, int y) const { return n[static_cast<size_t>(y) * width + x]; }
    };
    bool MakeWindow(const Image& row, Window* w) const;
    static Window Columns(const Window& w, int x0, int x1);  // columns [x0, x1) of a window
    bool EnsureSize(const Image& row);  // the letter window's size from the first row learned
    double Cost(const Window& w, int x, const Glyph& g) const;  // squared difference, summed
    void Add(const Window& w, int x, int width, wchar_t ch);
    int LearnOneToOne(const Window& w, const std::vector<std::wstring>& words);
    int LearnAligned(const Window& w, const std::vector<std::wstring>& words);
    bool Knows(wchar_t ch) const;
    double SpaceGap() const;
    void LearnGaps(const std::vector<std::pair<int, int>>& spans, const std::vector<size_t>& wordOf);

    std::vector<Glyph> glyphs_;
    double penalty_ = 0.06;  // cost of one more letter, per row of the letter window (fewer, wider letters win)
    double space_ = 0.2;     // the space: a gap of this part of the letter window height (measured; 0 = learned)
    int asc_ = 0, desc_ = 0;  // rows above / below the baseline in a letter window
    double gapIn_ = 0, gapOut_ = 0;  // per row: widest gap inside a word / narrowest between words (averages)
    int gapInN_ = 0, gapOutN_ = 0;
};

}  // namespace gct
