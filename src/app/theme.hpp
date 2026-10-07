// theme.hpp — colours, fonts and DPI scaling shared by all views.
#pragma once

#include <windows.h>

#include <initializer_list>

namespace gct {

struct Theme {
    static constexpr COLORREF kBg = RGB(22, 23, 27);
    static constexpr COLORREF kPanel = RGB(28, 30, 36);
    static constexpr COLORREF kInputBg = RGB(35, 37, 44);
    static constexpr COLORREF kText = RGB(232, 232, 236);
    static constexpr COLORREF kPreviewText = RGB(150, 205, 255);
    static constexpr COLORREF kMuted = RGB(128, 132, 145);
    static constexpr COLORREF kFaint = RGB(70, 73, 84);
    static constexpr COLORREF kAccent = RGB(240, 190, 90);
    static constexpr COLORREF kOk = RGB(120, 205, 140);
    static constexpr COLORREF kWarn = RGB(240, 190, 90);
    static constexpr COLORREF kError = RGB(255, 120, 110);
    static constexpr COLORREF kSquiggle = RGB(255, 95, 90);
    static constexpr COLORREF kSquiggleSoft = RGB(120, 170, 255);  // e.g. doubled word
    static constexpr COLORREF kTabActive = RGB(44, 47, 56);
    static constexpr COLORREF kBadge = RGB(200, 140, 255);           // whisper purple

    int dpi = 96;
    float scale = 1.0f;
    int textPercent = 100;  // user's text size (100 % = 11 pt chat text)
    std::wstring fontFace = L"Segoe UI";  // user's choice among a few well readable Windows fonts
    // Note: not called "small" — rpcndr.h #defines small as char.
    HFONT fontText = nullptr;    // input, preview, log body
    HFONT fontUi = nullptr;      // header/footer
    HFONT fontUiBold = nullptr;
    HFONT fontSmall = nullptr;   // log meta line
    HBRUSH bg = nullptr, panel = nullptr, inputBg = nullptr;
    int textLineHeight = 18;
    int smallLineHeight = 14;

    int S(int v) const { return static_cast<int>(v * scale + 0.5f); }

    void Create(int systemDpi, int percent = 100, const std::wstring& face = L"Segoe UI") {
        dpi = systemDpi;
        scale = dpi / 96.0f;
        textPercent = percent < 70 ? 70 : (percent > 300 ? 300 : percent);
        fontFace = face.empty() ? std::wstring(L"Segoe UI") : face;
        fontText = MakeFont(11, FW_NORMAL);
        fontUi = MakeFont(9, FW_NORMAL);
        fontUiBold = MakeFont(9, FW_SEMIBOLD);
        fontSmall = MakeFont(8, FW_NORMAL);
        bg = CreateSolidBrush(kBg);
        panel = CreateSolidBrush(kPanel);
        inputBg = CreateSolidBrush(kInputBg);

        HDC dc = GetDC(nullptr);
        HGDIOBJ old = SelectObject(dc, fontText);
        TEXTMETRICW tm{};
        GetTextMetricsW(dc, &tm);
        textLineHeight = tm.tmHeight + tm.tmExternalLeading;
        SelectObject(dc, fontSmall);
        GetTextMetricsW(dc, &tm);
        smallLineHeight = tm.tmHeight + tm.tmExternalLeading;
        SelectObject(dc, old);
        ReleaseDC(nullptr, dc);
    }

    void Destroy() {
        for (HGDIOBJ o : {static_cast<HGDIOBJ>(fontText), static_cast<HGDIOBJ>(fontUi),
                          static_cast<HGDIOBJ>(fontUiBold), static_cast<HGDIOBJ>(fontSmall), static_cast<HGDIOBJ>(bg),
                          static_cast<HGDIOBJ>(panel), static_cast<HGDIOBJ>(inputBg)})
            if (o) DeleteObject(o);
        fontText = fontUi = fontUiBold = fontSmall = nullptr;
        bg = panel = inputBg = nullptr;
    }

private:
    HFONT MakeFont(int pt, int weight) const {
        return CreateFontW(-MulDiv(pt * textPercent, dpi, 72 * 100), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, fontFace.c_str());
    }
};

}  // namespace gct
