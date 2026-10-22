// kb_theme.cpp — the window's themes (CBA too), the text font, chat colours, icons.

#include "kb_shared.hpp"

namespace kb {

std::vector<uint32_t> g_cbaRoles;  // 0xRRGGBB per role ([NexusTheme] Colours, a theme code)
bool g_cbaAutoContrast = true;
const std::vector<uint32_t>& CbaPreset(int i) {  // 0 Slate, 1 Grey, 2 CBA blue (the palette approved in cba4gw2)
    static const std::vector<uint32_t> kPresets[3] = {
        {0x121214, 0x333A47, 0x36404F, 0x596B85, 0x404D61, 0xEBEBF0, 0x8C919B, 0xDBE3ED},
        {0x111111, 0x2C2C2C, 0x2C2C2C, 0x4C4C4C, 0x444444, 0xDDDDDD, 0x8A8A8A, 0xDDDDDD},
        {0x111111, 0x1E1E1E, 0x0D2B45, 0x4A73A3, 0x163D5E, 0xDDDDDD, 0x8A8A8A, 0xB8D8F8},
    };
    return kPresets[std::clamp(i, 0, 2)];
}
// The text colours as shown: with automatic contrast lifted (or darkened) until they stand out on every fill.
uint32_t CbaShown(int role) {
    const std::vector<uint32_t>& r = g_cbaRoles;
    if (!g_cbaAutoContrast) return r[role];
    if (role == kRoleText)
        return EnsureContrast(r[kRoleText], {r[kRoleBg], r[kRoleField], r[kRoleButton], r[kRoleButtonActive], r[kRoleTabActive]}, 4.5);
    if (role == kRoleTextDim) return EnsureContrast(r[kRoleTextDim], {r[kRoleBg], r[kRoleField]}, 3.0);
    if (role == kRoleAccent) return EnsureContrast(r[kRoleAccent], {r[kRoleBg], r[kRoleField]}, 3.0);
    return r[role];
}

const ThemeColours* ColoursOf(Theme t) {
    // Slate: dark greys with a cool slate accent (the look of 1.9.5).
    static const ThemeColours kSlate = {
        Rgb(0xEBEBF0), Rgb(0x8C919B), Rgb(0x121214, 240), Rgb(0x08080A, 115), Rgb(0x141417, 248), Rgb(0x52545E, 128),
        Rgb(0x575963, 128), Rgb(0x333A47, 230), Rgb(0x404A59, 242), Rgb(0x475466), Rgb(0x36404F, 230),
        Rgb(0x4A596E), Rgb(0x596B85), Rgb(0x262B36, 230), Rgb(0x4F5E75), Rgb(0x404D61), Rgb(0xDBE3ED),
        Rgb(0x8C99AD), Rgb(0xA8B8CC), Rgb(0x5C759E, 128)};
    // Grey: black, white and grey only – quiet, nothing coloured.
    static const ThemeColours kGrey = {
        Rgb(0xDDDDDD), Rgb(0x8A8A8A), Rgb(0x111111, 240), Rgb(0x1E1E1E, 140), Rgb(0x161616, 248), Rgb(0x2C2C2C),
        Rgb(0x444444, 160), Rgb(0x2C2C2C, 235), Rgb(0x383838), Rgb(0x444444), Rgb(0x2C2C2C, 235),
        Rgb(0x3C3C3C), Rgb(0x4C4C4C), Rgb(0x1E1E1E, 235), Rgb(0x3C3C3C), Rgb(0x444444), Rgb(0xDDDDDD),
        Rgb(0x6B6B6B), Rgb(0xAAAAAA), Rgb(0xFFFFFF, 46)};
    if (t == Theme::Cba && g_cbaRoles.size() == kRoleCount) {
        static ThemeColours s_cba;
        static std::vector<uint32_t> s_built;
        static bool s_builtAuto = false;
        if (s_built != g_cbaRoles || s_builtAuto != g_cbaAutoContrast) {  // derived once per change, not per frame
            const std::vector<uint32_t>& r = g_cbaRoles;
            const uint32_t bg = r[kRoleBg], field = r[kRoleField], text = CbaShown(kRoleText), accent = CbaShown(kRoleAccent);
            s_cba = {Rgb(text), Rgb(CbaShown(kRoleTextDim)), Rgb(bg, 240), Rgb(MixRgb(bg, 0x000000, 0.35), 115),
                     Rgb(MixRgb(bg, 0xFFFFFF, 0.04), 248), Rgb(MixRgb(bg, text, 0.25), 128), Rgb(MixRgb(bg, text, 0.25), 160),
                     Rgb(field, 235), Rgb(MixRgb(field, text, 0.08)), Rgb(MixRgb(field, text, 0.14)),
                     Rgb(r[kRoleButton], 235), Rgb(MixRgb(r[kRoleButton], r[kRoleButtonActive], 0.5)), Rgb(r[kRoleButtonActive]),
                     Rgb(MixRgb(bg, r[kRoleButton], 0.5), 235), Rgb(r[kRoleButtonActive]), Rgb(r[kRoleTabActive]),
                     Rgb(accent), Rgb(MixRgb(accent, bg, 0.35)), Rgb(accent), Rgb(accent, 90)};
            s_built = g_cbaRoles;
            s_builtAuto = g_cbaAutoContrast;
        }
        return &s_cba;
    }
    switch (t) {
        case Theme::Slate: return &kSlate;
        case Theme::Grey: return &kGrey;
        default: return nullptr;
    }
}
Theme g_theme = Theme::Slate;
int PushTheme() {
    const ThemeColours* c = ColoursOf(g_theme);
    if (!c) return 0;  // Nexus' own
    const std::pair<ImGuiCol, uint32_t> colours[] = {
        {ImGuiCol_Text, c->text}, {ImGuiCol_TextDisabled, c->textDim}, {ImGuiCol_WindowBg, c->bg},
        {ImGuiCol_ChildBg, c->child}, {ImGuiCol_PopupBg, c->popup}, {ImGuiCol_Border, c->border},
        {ImGuiCol_FrameBg, c->frame}, {ImGuiCol_FrameBgHovered, c->frameHovered}, {ImGuiCol_FrameBgActive, c->frameActive},
        {ImGuiCol_TitleBg, c->bg}, {ImGuiCol_TitleBgActive, c->tab}, {ImGuiCol_TitleBgCollapsed, c->bg},
        {ImGuiCol_ScrollbarBg, Rgb(0x000000, 38)}, {ImGuiCol_ScrollbarGrab, c->grab},
        {ImGuiCol_ScrollbarGrabHovered, c->grabActive}, {ImGuiCol_ScrollbarGrabActive, c->check},
        {ImGuiCol_CheckMark, c->check}, {ImGuiCol_SliderGrab, c->grab}, {ImGuiCol_SliderGrabActive, c->grabActive},
        {ImGuiCol_Button, c->button}, {ImGuiCol_ButtonHovered, c->buttonHovered}, {ImGuiCol_ButtonActive, c->buttonActive},
        {ImGuiCol_Header, c->button}, {ImGuiCol_HeaderHovered, c->buttonHovered}, {ImGuiCol_HeaderActive, c->buttonActive},
        {ImGuiCol_Separator, c->separator}, {ImGuiCol_SeparatorHovered, c->grab}, {ImGuiCol_SeparatorActive, c->grabActive},
        {ImGuiCol_ResizeGrip, c->button}, {ImGuiCol_ResizeGripHovered, c->grab}, {ImGuiCol_ResizeGripActive, c->grabActive},
        {ImGuiCol_Tab, c->tab}, {ImGuiCol_TabHovered, c->tabHovered}, {ImGuiCol_TabActive, c->tabActive},
        {ImGuiCol_TabUnfocused, c->tab}, {ImGuiCol_TabUnfocusedActive, c->tabActive},
        {ImGuiCol_TextSelectedBg, c->selection}, {ImGuiCol_NavHighlight, c->grabActive},
    };
    for (const auto& col : colours) ImGui::PushStyleColor(col.first, col.second);
    return static_cast<int>(std::size(colours));
}
const wchar_t* ThemeKey(Theme t) {
    return t == Theme::Nexus ? L"nexus" : t == Theme::Grey ? L"grey" : t == Theme::Cba ? L"cba" : L"slate";
}
std::atomic<ImFont*> g_textFont{nullptr};
float g_textFontPx = 0;  // the size asked of Nexus (game thread)
void OnTextFont(const char*, void* font) { g_textFont = static_cast<ImFont*>(font); }
float PtToPx(int pt) { return std::round(std::clamp(pt, 8, 28) * 96.0f / 72.0f); }
void RequestTextFont(int pt) {
    if (!g_api || !g_api->Fonts_AddFromFile) return;
    const float px = PtToPx(pt);
    if (px == g_textFontPx) return;
    if (g_textFontPx > 0 && g_api->Fonts_Resize) {
        g_api->Fonts_Resize(kTextFontId, px);
    } else {
        wchar_t win[MAX_PATH] = {};
        GetWindowsDirectoryW(win, MAX_PATH);
        const std::wstring file = std::wstring(win) + L"\\Fonts\\segoeui.ttf";
        if (GetFileAttributesW(file.c_str()) == INVALID_FILE_ATTRIBUTES) return;
        // Latin (also the extended letters), Greek, Cyrillic, Arabic (+ its presentation forms), punctuation, arrows,
        // symbols – what chat and our window show.
        static const ImWchar kRanges[] = {0x0020, 0x024F, 0x0370, 0x03FF, 0x0400, 0x04FF, 0x0600, 0x06FF, 0x2000, 0x206F,
                                          0x20A0, 0x20CF, 0x2190, 0x21FF, 0x2200, 0x22FF, 0x25A0, 0x25FF, 0xFB50, 0xFDFF,
                                          0xFE70, 0xFEFF, 0};
        static ImFontConfig cfg;
        cfg.GlyphRanges = kRanges;
        g_api->Fonts_AddFromFile(kTextFontId, px, ToUtf8(file).c_str(), OnTextFont, &cfg);
    }
    g_textFontPx = px;
}
ImVec4 Rgb32(uint32_t rgb) { return ImVec4(((rgb >> 16) & 0xFF) / 255.f, ((rgb >> 8) & 0xFF) / 255.f, (rgb & 0xFF) / 255.f, 1.f); }
ImVec4 Bright(ImVec4 c) {  // the message's brightness of your choice (never a name's): below 100 % darker, above it lighter towards white
    const float b = g_look.brightness / 100.0f;
    if (b <= 1.0f) return ImVec4(c.x * b, c.y * b, c.z * b, c.w);
    const float t = std::min(1.0f, (b - 1.0f) / 0.6f) * 0.75f;  // 160 %: three quarters of the way (the light
    return ImVec4(c.x + (1 - c.x) * t, c.y + (1 - c.y) * t, c.z + (1 - c.z) * t, c.w);  // tints were cut off at 1)
}
const ChannelLook* LookOf(Channel ch) {
    for (const ChannelLook& l : kChannelLook)
        if (l.channel == ch) return &l;
    return nullptr;
}
ImVec4 ColorOf(Channel ch) {  // the message
    const ChannelLook* l = LookOf(ch);
    if (!l) return Bright(ImVec4(0.9f, 0.9f, 0.92f, 1.f));
    return Bright(Rgb32(g_look.gameColors ? l->light : l->strong));
}
ImVec4 NameColorOf(Channel ch) {  // the name in front of it: GW2's palette as it is – the brightness never touches it
    const ChannelLook* l = LookOf(ch);  // (user, 2026-10-22: the salmon map name turned pinkish white)
    if (!g_look.gameColors || !l) return ImVec4(0.95f, 0.95f, 0.97f, 1.f);
    return Rgb32(l->strong);
}

// Text as GW2 draws it: a soft dark halo around the letters (when on), then the letters. `wrap`: lines wrap at the
// right edge and stand under each other at the same x (like TextWrapped). It is an item: hover and click work.
// Text for the font it is drawn in: when the text holds General Punctuation (dashes, curly quotes, "…" – players
// write them, DeepL's German quotes) and the font lacks it, its plain forms; else as it is. Only the drawing – the
// line itself (and what the translator gets) stays as it came.
const char* FontSafe(const char* text, std::string& buf) {
    if (!std::strstr(text, "\xE2\x80")) return text;  // U+2000..U+203F start with these two bytes in UTF-8
    ImFont* font = ImGui::GetFont();
    if (font && font->FindGlyphNoFallback(0x2013) && font->FindGlyphNoFallback(0x201C)) return text;
    buf = ToUtf8(PlainPunctuation(FromUtf8(text)));
    return buf.c_str();
}

void LookText(const char* text, ImVec4 color, bool wrap) {
    std::string plain;
    text = FontSafe(text, plain);
    ImFont* font = ImGui::GetFont();
    const float fs = ImGui::GetFontSize();
    const float wrapW = wrap ? std::max(1.0f, ImGui::GetContentRegionAvail().x) : 0.0f;
    const ImVec2 size = font->CalcTextSizeA(fs, FLT_MAX, wrapW, text);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(size.x, std::max(size.y, ImGui::GetTextLineHeight())));
    if (!ImGui::IsItemVisible()) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // The halo: 8 copies round the letters (the 4 diagonals alone left the edges above, below and beside them open).
    // A renderer that cannot split a draw list past 65536 vertices (16-bit indices) gets no halo near that limit.
    const bool room = (ImGui::GetIO().BackendFlags & ImGuiBackendFlags_RendererHasVtxOffset) ||
                      dl->_VtxCurrentIdx + std::strlen(text) * 4 * 9 < 60000;
    if (g_look.glow && room) {
        const float d = std::max(1.0f, std::round(fs / 15.0f));
        const ImU32 halo = IM_COL32(0, 0, 0, 220);
        const ImVec2 offsets[] = {ImVec2(-d, -d), ImVec2(d, -d), ImVec2(-d, d), ImVec2(d, d),
                                  ImVec2(-d, 0),  ImVec2(d, 0),  ImVec2(0, -d), ImVec2(0, d)};
        for (const ImVec2& o : offsets) dl->AddText(font, fs, ImVec2(p.x + o.x, p.y + o.y), halo, text, nullptr, wrapW);
    }
    dl->AddText(font, fs, p, ImGui::GetColorU32(color), text, nullptr, wrapW);
}
bool IconButton(const char* id, Icon kind, bool active, bool* rightClick, ImU32 tint) {
    const float s = ImGui::GetFrameHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(id, ImVec2(s, s));
    const bool hovered = ImGui::IsItemHovered();
    if (rightClick) *rightClick = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 col = active ? IM_COL32(235, 85, 85, 255)
                      : tint     ? tint
                                 : ImGui::GetColorU32(hovered ? ImGuiCol_Text : ImGuiCol_TextDisabled);
    const ImVec2 c(p.x + s * 0.5f, p.y + s * 0.5f);
    const float u = s / 18.0f;
    switch (kind) {
        case Icon::Mic:  // a capsule on a stand
            dl->AddRectFilled(ImVec2(c.x - 2.5f * u, c.y - 6 * u), ImVec2(c.x + 2.5f * u, c.y + 1.5f * u), col, 2.5f * u);
            dl->PathArcTo(ImVec2(c.x, c.y - 1 * u), 4.5f * u, 0.0f, 3.14159f, 10);
            dl->PathStroke(col, false, 1.4f * u);
            dl->AddLine(ImVec2(c.x, c.y + 3.5f * u), ImVec2(c.x, c.y + 6 * u), col, 1.4f * u);
            dl->AddLine(ImVec2(c.x - 3 * u, c.y + 6 * u), ImVec2(c.x + 3 * u, c.y + 6 * u), col, 1.4f * u);
            break;
        case Icon::Menu:  // ≡
            for (int i = -1; i <= 1; ++i)
                dl->AddLine(ImVec2(c.x - 5.5f * u, c.y + i * 4 * u), ImVec2(c.x + 5.5f * u, c.y + i * 4 * u), col, 1.6f * u);
            break;
        case Icon::Collapse:  // V: fold the window up
        case Icon::Expand: {  // A: open it again
            const float d = kind == Icon::Collapse ? 1.0f : -1.0f;
            dl->AddLine(ImVec2(c.x - 5 * u, c.y - 2.5f * u * d), ImVec2(c.x, c.y + 2.5f * u * d), col, 1.6f * u);
            dl->AddLine(ImVec2(c.x, c.y + 2.5f * u * d), ImVec2(c.x + 5 * u, c.y - 2.5f * u * d), col, 1.6f * u);
            break;
        }
        case Icon::Close:  // ×
            dl->AddLine(ImVec2(c.x - 4.5f * u, c.y - 4.5f * u), ImVec2(c.x + 4.5f * u, c.y + 4.5f * u), col, 1.6f * u);
            dl->AddLine(ImVec2(c.x - 4.5f * u, c.y + 4.5f * u), ImVec2(c.x + 4.5f * u, c.y - 4.5f * u), col, 1.6f * u);
            break;
    }
    return pressed;
}

// Cut to the width with "…" at the end.
std::string Fit(const std::string& s, float width) {
    if (ImGui::CalcTextSize(s.c_str()).x <= width) return s;
    const std::wstring w = W(s);
    const float ellipsisW = ImGui::CalcTextSize("...").x;
    const float targetW = width - ellipsisW;
    if (targetW <= 0.0f) return "...";
    size_t low = 0, high = w.size();
    while (low < high) {
        const size_t mid = low + (high - low + 1) / 2;
        const std::string sub = U8(w.substr(0, mid));
        if (ImGui::CalcTextSize(sub.c_str()).x <= targetW) low = mid;
        else high = mid - 1;
    }
    return U8(w.substr(0, low)) + "...";
}

// Text drawn into the input box (status, the translation in the compact view): on top, clipped to the box.
void BoxText(const ImVec2& at, const ImVec4& clip, ImU32 color, const std::string& text, float scale) {
    std::string plain;
    ImGui::GetForegroundDrawList()->AddText(ImGui::GetFont(), ImGui::GetFontSize() * scale, at, color,
                                            FontSafe(text.c_str(), plain), nullptr, 0, &clip);
}

}  // namespace kb
