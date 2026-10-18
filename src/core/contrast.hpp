// contrast.hpp — colour contrast as WCAG 2.1 measures it, and the theme code a player can share (the "CBA" theme of
// the window: your own colours, kept readable). Portable: no ImGui, no Windows. Colours are 0xRRGGBB.
// The idea comes from cba4gw2 (Theme::GetContrastTextColor, HybridScanner::WcagContrast); here it is the exact WCAG
// formula with sRGB linearisation, so the numbers shown to the player are the real contrast ratios.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gct {

double RelativeLuminance(uint32_t rgb);         // 0 (black) .. 1 (white)
double ContrastRatio(uint32_t a, uint32_t b);   // 1 .. 21, (L1 + 0.05) / (L2 + 0.05)
double MinContrast(uint32_t fg, const std::vector<uint32_t>& bgs);  // the worst of `fg` against each of `bgs`
uint32_t MixRgb(uint32_t a, uint32_t b, double t);  // t 0: a, 1: b

// `fg` as it is when it already stands `minRatio` against every one of `bgs`; else moved towards white or black (the
// way that gets there with the smaller change, keeping its hue as long as it can) until it does.
uint32_t EnsureContrast(uint32_t fg, const std::vector<uint32_t>& bgs, double minRatio);

// A theme to share: "GW2C1:" and the colours as six hex digits each, separated by '-'. Whatever is pasted is data:
// only exactly that form is accepted (the prefix, the right count, hex digits), anything else is refused.
std::string MakeThemeCode(const std::vector<uint32_t>& colours);
bool ParseThemeCode(const std::string& code, size_t count, std::vector<uint32_t>* colours);

}  // namespace gct
