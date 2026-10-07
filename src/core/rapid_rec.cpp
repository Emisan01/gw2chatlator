// rapid_rec.cpp
#include "rapid_rec.hpp"

#include <algorithm>
#include <cmath>

#include "text.hpp"

namespace gct {

RecInput PrepareRecInput(const Image& line, bool invert) {
    RecInput in;
    if (line.Empty()) return in;
    const int h = kRecHeight;
    const int scaledW = std::max(1, static_cast<int>(std::ceil(static_cast<double>(line.width) * h / line.height)));
    in.width = std::max(320, scaledW);
    in.data.assign(static_cast<size_t>(3) * h * in.width, 0.0f);  // 0 = middle grey after normalising (padding)
    const double sx = static_cast<double>(line.width) / scaledW, sy = static_cast<double>(line.height) / h;
    const size_t plane = static_cast<size_t>(h) * in.width;
    for (int y = 0; y < h; ++y) {
        // Bilinear: the pixel centre mapped back into the line picture.
        const double fy = std::clamp((y + 0.5) * sy - 0.5, 0.0, static_cast<double>(line.height - 1));
        const int y0 = static_cast<int>(fy), y1 = std::min(y0 + 1, line.height - 1);
        const double wy = fy - y0;
        for (int x = 0; x < scaledW; ++x) {
            const double fx = std::clamp((x + 0.5) * sx - 0.5, 0.0, static_cast<double>(line.width - 1));
            const int x0 = static_cast<int>(fx), x1 = std::min(x0 + 1, line.width - 1);
            const double wx = fx - x0;
            for (int c = 0; c < 3; ++c) {  // B, G, R as stored in BGRA
                auto px = [&](int xx, int yy) {
                    return static_cast<double>(line.bgra[(static_cast<size_t>(yy) * line.width + xx) * 4 + c]);
                };
                double v = (px(x0, y0) * (1 - wx) + px(x1, y0) * wx) * (1 - wy) +
                           (px(x0, y1) * (1 - wx) + px(x1, y1) * wx) * wy;
                if (invert) v = 255.0 - v;
                in.data[c * plane + static_cast<size_t>(y) * in.width + x] = static_cast<float>((v / 255.0 - 0.5) / 0.5);
            }
        }
    }
    return in;
}

std::vector<std::wstring> ParseRecDictionary(const std::string& utf8) {
    std::vector<std::wstring> dict;
    const std::wstring all = FromUtf8(utf8);
    size_t start = 0;
    while (start < all.size()) {
        size_t end = all.find(L'\n', start);
        if (end == std::wstring::npos) end = all.size();
        std::wstring line = all.substr(start, end - start);
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        dict.push_back(line);
        start = end + 1;
    }
    return dict;
}

RecResult CtcDecode(const float* probs, int steps, int classes, const std::vector<std::wstring>& dict) {
    RecResult r;
    r.steps = steps;
    int last = -1;
    double sum = 0;
    int taken = 0;
    for (int t = 0; t < steps; ++t) {
        const float* row = probs + static_cast<size_t>(t) * classes;
        int best = 0;
        for (int c = 1; c < classes; ++c)
            if (row[c] > row[best]) best = c;
        if (best != 0 && best != last) {
            const size_t idx = static_cast<size_t>(best - 1);
            const std::wstring piece = idx < dict.size() ? dict[idx] : idx == dict.size() ? std::wstring(L" ") : L"";
            r.text += piece;
            for (size_t k = 0; k < piece.size(); ++k) r.charStep.push_back(t);
            sum += row[best];
            ++taken;
        }
        last = best;
    }
    r.confidence = taken ? static_cast<float>(sum / taken) : 0.0f;
    // Trim, keeping the steps in line with the characters.
    size_t a = 0, b = r.text.size();
    while (a < b && r.text[a] == L' ') ++a;
    while (b > a && r.text[b - 1] == L' ') --b;
    r.text = r.text.substr(a, b - a);
    r.charStep = std::vector<int>(r.charStep.begin() + static_cast<std::ptrdiff_t>(a),
                                  r.charStep.begin() + static_cast<std::ptrdiff_t>(b));
    return r;
}

std::vector<RecWord> RecWords(const RecResult& r, int inputWidth) {
    std::vector<RecWord> words;
    if (r.steps <= 0 || r.charStep.size() != r.text.size()) return words;
    const double stepW = static_cast<double>(inputWidth) / r.steps;
    size_t i = 0;
    while (i < r.text.size()) {
        if (r.text[i] == L' ') {
            ++i;
            continue;
        }
        size_t j = i;
        while (j < r.text.size() && r.text[j] != L' ') ++j;
        RecWord w;
        w.text = r.text.substr(i, j - i);
        // A character is seen around its step: half a step before the first to half a step after the last.
        w.x = static_cast<int>(std::max(0.0, (r.charStep[i] - 0.5) * stepW));
        w.w = std::max(1, static_cast<int>((r.charStep[j - 1] + 1.5) * stepW) - w.x);
        words.push_back(std::move(w));
        i = j;
    }
    return words;
}

}  // namespace gct
