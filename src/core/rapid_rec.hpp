// rapid_rec.hpp — the parts of PaddleOCR / RapidOCR text-line recognition that
// need no runtime: preparing one line picture for the model and turning the
// model's output back into text (CTC). The model itself runs in win/rapid_ocr.
//
// Model input: 1 x 3 x 48 x W floats, channels B, G, R (as PaddleOCR reads
// pictures), (value / 255 - 0.5) / 0.5, the line scaled to height 48 and
// padded to at least 320 wide. Output: 1 x T x C probabilities; class 0 is the
// CTC blank, 1..N the dictionary lines, N+1 a space.
#pragma once

#include <string>
#include <vector>

#include "image.hpp"

namespace gct {

constexpr int kRecHeight = 48;

struct RecInput {
    std::vector<float> data;  // 3 * 48 * width
    int width = 0;
};

// `line` is one text line (a crop). `invert`: light text on a dark background
// becomes dark on light, as the models mostly saw in training.
RecInput PrepareRecInput(const Image& line, bool invert);

// Dictionary file: one character per line (UTF-8).
std::vector<std::wstring> ParseRecDictionary(const std::string& utf8);

struct RecResult {
    std::wstring text;
    float confidence = 0;  // mean probability of the characters taken (0..1)
    // Where each character of `text` was seen: the model step (0..steps-1); a step is inputWidth / steps
    // model pixels wide. Gives word boxes for colours.
    std::vector<int> charStep;
    int steps = 0;
    int inputWidth = 0;  // width of the model input (model pixels; the line is 48 high there)
};
// Words of a result with their horizontal extent in model pixels (input width `inputWidth`).
struct RecWord {
    std::wstring text;
    int x = 0, w = 0;
};
std::vector<RecWord> RecWords(const RecResult& r, int inputWidth);
// Greedy CTC: best class per step, repeats merged, blanks dropped.
RecResult CtcDecode(const float* probs, int steps, int classes, const std::vector<std::wstring>& dict);

}  // namespace gct
