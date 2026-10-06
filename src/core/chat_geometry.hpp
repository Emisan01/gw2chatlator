// chat_geometry.hpp — where the text lines of the chat panel are, how big
// the letters are, and how much to enlarge the picture for text recognition.
// Portable (no windows.h), works on the BGRA pictures of image.hpp.
#pragma once

#include <string>
#include <vector>

#include "image.hpp"

namespace gct {

struct TextRow {
    int top = 0;     // first row with ink
    int height = 0;  // rows with ink, incl. ascenders/descenders
};

struct LineGrid {
    std::vector<TextRow> rows;  // text lines, top to bottom
    int pitch = 0;              // distance of two lines (0 = fewer than two lines)
    int textHeight = 0;         // typical height of a line's ink
    bool Found() const { return !rows.empty() && textHeight > 0; }
};

// Text lines inside `area` of `img`: light text on the dark chat panel.
LineGrid FindLineGrid(const Image& img, RectI area);

// Enlargement that brings the line spacing to about 30 px, which is where
// Tesseract and Windows OCR read best (measured on real GW2 captures: 4K
// chat lines are ~20 px apart and read best as they are or 2x; 1080p lines
// are ~10 px apart and need 3x). 1..4.
int OcrScaleFor(const LineGrid& grid);

// Enlarges for text recognition with a smooth (bicubic) filter. No contrast
// tricks: the old stretch-and-invert made Tesseract read 2-3x worse.
Image UpscaleForOcr(const Image& src, int scale);

// The user's rough frame around the chat, snapped to the text lines:
// half-cut lines at the top or bottom are included when mostly inside and
// dropped otherwise, the tab bar and the input line (set apart from the
// lines by a wider gap) are left out, and a narrow scroll bar at the left
// edge is cut off. `rough` in `img` pixels; returns `rough` if no lines.
struct SnapResult {
    RectI area;
    LineGrid grid;  // in `area` coordinates
    int scale = 1;  // OcrScaleFor(grid)
};
SnapResult SnapChatArea(const Image& img, RectI rough);

// A recognized word and where it is (in the captured picture's pixels).
struct BoxWord {
    std::wstring text;
    RectI rect;
};

// Words sorted into the text lines of `grid` by where they are, left to
// right in each line. Text recognition sometimes reports words of several
// chat lines as one line (Tesseract) or lines out of order (Windows OCR);
// the line grid is measured from the pixels and does not make that mistake.
// Lines without words are left out.
std::vector<std::vector<BoxWord>> GroupWordsByRows(const std::vector<BoxWord>& words, const LineGrid& grid);

// Traffic light for the setup: how readable the snapped area is.
enum class AreaQuality { None, Small, Good };
AreaQuality RateArea(const SnapResult& s);

Image Crop(const Image& img, RectI r);

}  // namespace gct
