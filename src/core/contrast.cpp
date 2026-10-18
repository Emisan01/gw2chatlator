#include "core/contrast.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace gct {

namespace {
double Channel(uint32_t v) {  // sRGB 0..255 -> linear light
    const double c = v / 255.0;
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}
uint32_t Comp(uint32_t rgb, int shift) { return (rgb >> shift) & 0xFF; }
}  // namespace

double RelativeLuminance(uint32_t rgb) {
    return 0.2126 * Channel(Comp(rgb, 16)) + 0.7152 * Channel(Comp(rgb, 8)) + 0.0722 * Channel(Comp(rgb, 0));
}

double ContrastRatio(uint32_t a, uint32_t b) {
    const double la = RelativeLuminance(a), lb = RelativeLuminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

double MinContrast(uint32_t fg, const std::vector<uint32_t>& bgs) {
    double worst = 21.0;
    for (uint32_t bg : bgs) worst = std::min(worst, ContrastRatio(fg, bg));
    return worst;
}

uint32_t MixRgb(uint32_t a, uint32_t b, double t) {
    t = std::clamp(t, 0.0, 1.0);
    uint32_t out = 0;
    for (int shift : {16, 8, 0}) {
        const double v = Comp(a, shift) + (static_cast<double>(Comp(b, shift)) - Comp(a, shift)) * t;
        out |= static_cast<uint32_t>(std::lround(std::clamp(v, 0.0, 255.0))) << shift;
    }
    return out;
}

uint32_t EnsureContrast(uint32_t fg, const std::vector<uint32_t>& bgs, double minRatio) {
    if (bgs.empty() || MinContrast(fg, bgs) >= minRatio) return fg;
    // Try both ways in small steps; take the first step count that reaches it (the smaller change wins).
    uint32_t bestLight = 0, bestDark = 0;
    int stepsLight = -1, stepsDark = -1;
    for (int i = 1; i <= 40; ++i) {
        const double t = i / 40.0;
        if (stepsLight < 0) {
            const uint32_t c = MixRgb(fg, 0xFFFFFF, t);
            if (MinContrast(c, bgs) >= minRatio) { bestLight = c; stepsLight = i; }
        }
        if (stepsDark < 0) {
            const uint32_t c = MixRgb(fg, 0x000000, t);
            if (MinContrast(c, bgs) >= minRatio) { bestDark = c; stepsDark = i; }
        }
        if (stepsLight >= 0 && stepsDark >= 0) break;
    }
    if (stepsLight >= 0 && (stepsDark < 0 || stepsLight <= stepsDark)) return bestLight;
    if (stepsDark >= 0) return bestDark;
    // Neither reaches it (backgrounds far apart): whichever of white / black stands better.
    return MinContrast(0xFFFFFF, bgs) >= MinContrast(0x000000, bgs) ? 0xFFFFFF : 0x000000;
}

std::string MakeThemeCode(const std::vector<uint32_t>& colours) {
    static const char* kHex = "0123456789ABCDEF";
    std::string out = "GW2C1:";
    for (size_t i = 0; i < colours.size(); ++i) {
        if (i) out += '-';
        for (int shift = 20; shift >= 0; shift -= 4) out += kHex[(colours[i] >> shift) & 0xF];
    }
    return out;
}

bool ParseThemeCode(const std::string& code, size_t count, std::vector<uint32_t>* colours) {
    std::string s;
    for (char c : code)
        if (!std::isspace(static_cast<unsigned char>(c))) s += c;  // pasted with a line break or spaces: fine
    if (s.size() != 6 + count * 7 - 1 || s.compare(0, 6, "GW2C1:") != 0) return false;
    std::vector<uint32_t> out;
    for (size_t i = 0; i < count; ++i) {
        const size_t at = 6 + i * 7;
        if (i && s[at - 1] != '-') return false;
        uint32_t v = 0;
        for (size_t k = 0; k < 6; ++k) {
            const char c = s[at + k];
            const int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (d < 0) return false;
            v = (v << 4) | static_cast<uint32_t>(d);
        }
        out.push_back(v);
    }
    if (colours) *colours = std::move(out);
    return true;
}

}  // namespace gct
