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
const FontFace kFontFaces[3] = {{L"segoeui", "Segoe UI"}, {L"seguisb", "Segoe UI Semibold"}, {L"tahoma", "Tahoma"}};
std::string g_textFontKey;  // the font asked of Nexus now (game thread): one name per face
void ReleaseTextFont() {
    if (!g_textFontKey.empty() && g_api && g_api->Fonts_Release) g_api->Fonts_Release(g_textFontKey.c_str(), OnTextFont);
    g_textFontKey.clear();
    g_textFont = nullptr;  // until the next one arrives the window scales Nexus' font – never one Nexus may have freed
    g_textFontPx = 0;
}
// "Like the game" (fontPt 0, the first use): GW2's interface size, with a 14 pt minimum so the main window is
// readable at first use. The font slider remains free to choose a smaller or larger size afterwards.
int FollowPt() {
    return 15;
}
float FollowUiPx() {
    if (g_cfg.uiFontPt > 0) return PtToPx(g_cfg.uiFontPt);
    return PtToPx(15);
}
int FollowUiPt() {
    if (g_cfg.uiFontPt > 0) return g_cfg.uiFontPt;
    return 15;
}
void RequestTextFont(int pt, const std::wstring& face) {  // pt 0: like the game
    if (!g_api || !g_api->Fonts_AddFromFile) return;
    const float px = PtToPx(pt > 0 ? pt : FollowPt());
    wchar_t win[MAX_PATH] = {};
    GetWindowsDirectoryW(win, MAX_PATH);
    std::wstring stem = face;
    std::wstring file = std::wstring(win) + L"\\Fonts\\" + stem + L".ttf";
    if (GetFileAttributesW(file.c_str()) == INVALID_FILE_ATTRIBUTES) {  // missing: Segoe UI
        stem = L"segoeui";
        file = std::wstring(win) + L"\\Fonts\\segoeui.ttf";
        if (GetFileAttributesW(file.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    }
    // Segoe UI keeps the name it always had; another face gets its own (a released one is not asked for by that name).
    const std::string key = stem == L"segoeui" ? std::string(kTextFontId) : std::string(kTextFontId) + "_" + ToUtf8(stem);
    if (key == g_textFontKey) {  // the same face: only its size
        if (px != g_textFontPx && g_api->Fonts_Resize) g_api->Fonts_Resize(key.c_str(), px);
        g_textFontPx = px;
        return;
    }
    ReleaseTextFont();
    {
        // Latin (also the extended letters and Vietnamese), Greek, Cyrillic, punctuation, arrows, symbols – what chat
        // and our window show (no Arabic: ImGui 1.80 draws it unjoined, user 2026-10-22).
        static const ImWchar kRanges[] = {0x0020, 0x024F, 0x0370, 0x03FF, 0x0400, 0x04FF, 0x1E00, 0x1EFF, 0x2000, 0x206F,
                                          0x20A0, 0x20CF, 0x2190, 0x21FF, 0x2200, 0x22FF, 0x25A0, 0x25FF, 0};
        static ImFontConfig cfg;
        cfg.GlyphRanges = kRanges;
        g_api->Fonts_AddFromFile(key.c_str(), px, ToUtf8(file).c_str(), OnTextFont, &cfg);
    }
    g_textFontKey = key;
    g_textFontPx = px;
}

// The options window's font: Segoe UI at the size of Nexus' own UI (user, 2026-10-22: the chat's size is the chat
// window's only) – our font so every letter of the UI languages shows (Nexus' font lacks ł, ş, ğ and Cyrillic).
std::atomic<ImFont*> g_uiFont{nullptr};
float g_uiFontPx = 0;
std::atomic<float> s_nexusOptionsFontPx{0};
void OnUiFont(const char*, void* font) { g_uiFont = static_cast<ImFont*>(font); }
void RequestUiFont(float px) {
    px = std::round(px);
    if (!g_api || !g_api->Fonts_AddFromFile || px < 6 || px == g_uiFontPx) return;
    if (g_uiFontPx > 0 && g_api->Fonts_Resize) {  // Nexus' UI size changed: only the size
        g_api->Fonts_Resize(kUiFontId, px);
        g_uiFontPx = px;
        return;
    }
    wchar_t win[MAX_PATH] = {};
    GetWindowsDirectoryW(win, MAX_PATH);
    const std::wstring file = std::wstring(win) + L"\\Fonts\\segoeui.ttf";
    if (GetFileAttributesW(file.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    static const ImWchar kRanges[] = {0x0020, 0x024F, 0x0370, 0x03FF, 0x0400, 0x04FF, 0x1E00, 0x1EFF, 0x2000, 0x206F,
                                      0x20A0, 0x20CF, 0x2190, 0x21FF, 0x2200, 0x22FF, 0x25A0, 0x25FF, 0};
    static ImFontConfig cfg;
    cfg.GlyphRanges = kRanges;
    g_api->Fonts_AddFromFile(kUiFontId, px, ToUtf8(file).c_str(), OnUiFont, &cfg);
    g_uiFontPx = px;
}
void ReleaseUiFont() {
    if (g_uiFontPx > 0 && g_api && g_api->Fonts_Release) g_api->Fonts_Release(kUiFontId, OnUiFont);
    g_uiFont = nullptr;
    g_uiFontPx = 0;
}
// The dashboard tiles' technical lines: Exo 2, embedded in the DLL and written once into the data folder, so Nexus
// loads it the way it loads the UI's Segoe UI (Nexus keeps the file's memory – nothing points into a DLL that goes).
std::atomic<ImFont*> g_techFont{nullptr};
float g_techFontPx = 0;
std::wstring g_techFontFile;  // empty: the resource could not be written – the tiles keep the UI font
void OnTechFont(const char*, void* font) { g_techFont = static_cast<ImFont*>(font); }
void PrepareTechFont() {  // at load
    HRSRC res = FindResourceW(g_self, MAKEINTRESOURCEW(kTechFontResource), RT_RCDATA);
    HGLOBAL h = res ? LoadResource(g_self, res) : nullptr;
    const void* data = h ? LockResource(h) : nullptr;
    const DWORD size = res ? SizeofResource(g_self, res) : 0;
    if (!data || size == 0) return;
    const std::wstring dir = g_cfg.dataDir + L"\\fonts";
    const std::wstring file = dir + L"\\Exo2-Regular.ttf";
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    const bool there = GetFileAttributesExW(file.c_str(), GetFileExInfoStandard, &fa) && fa.nFileSizeHigh == 0 &&
                       fa.nFileSizeLow == size;
    if (!there) {
        CreateDirectoryW(dir.c_str(), nullptr);
        if (!WriteFileAtomic(file, std::string(static_cast<const char*>(data), size))) return;
    }
    g_techFontFile = file;
}
void RequestTechFont(float px) {
    px = std::round(px);
    if (g_techFontFile.empty() || !g_api || !g_api->Fonts_AddFromFile || px < 6 || px == g_techFontPx) return;
    if (g_techFontPx > 0 && g_api->Fonts_Resize) {  // Nexus' UI size changed: only the size
        g_api->Fonts_Resize(kTechFontId, px);
        g_techFontPx = px;
        return;
    }
    // Latin with its extended letters and Vietnamese, Cyrillic, punctuation, currency – what the tiles show
    static const ImWchar kRanges[] = {0x0020, 0x024F, 0x0400, 0x04FF, 0x1E00, 0x1EFF, 0x2000, 0x206F, 0x20A0, 0x20CF, 0};
    static ImFontConfig cfg;
    cfg.GlyphRanges = kRanges;
    g_api->Fonts_AddFromFile(kTechFontId, px, ToUtf8(g_techFontFile).c_str(), OnTechFont, &cfg);
    g_techFontPx = px;
}
void ReleaseTechFont() {
    if (g_techFontPx > 0 && g_api && g_api->Fonts_Release) g_api->Fonts_Release(kTechFontId, OnTechFont);
    g_techFont = nullptr;
    g_techFontPx = 0;
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
        case Icon::Mic:
        case Icon::MicMuted:  // a capsule on a stand
            dl->AddRectFilled(ImVec2(c.x - 2.5f * u, c.y - 6 * u), ImVec2(c.x + 2.5f * u, c.y + 1.5f * u), col, 2.5f * u);
            dl->PathArcTo(ImVec2(c.x, c.y - 1 * u), 4.5f * u, 0.0f, 3.14159f, 10);
            dl->PathStroke(col, false, 1.4f * u);
            dl->AddLine(ImVec2(c.x, c.y + 3.5f * u), ImVec2(c.x, c.y + 6 * u), col, 1.4f * u);
            dl->AddLine(ImVec2(c.x - 3 * u, c.y + 6 * u), ImVec2(c.x + 3 * u, c.y + 6 * u), col, 1.4f * u);
            if (kind == Icon::MicMuted) {
                // Strike-through slash across the mic from bottom-left to top-right (Discord style)
                dl->AddLine(ImVec2(c.x - 6.5f * u, c.y + 6.5f * u), ImVec2(c.x + 6.5f * u, c.y - 6.5f * u), IM_COL32(20, 22, 28, 255), 3.0f * u);
                dl->AddLine(ImVec2(c.x - 6.0f * u, c.y + 6.0f * u), ImVec2(c.x + 6.0f * u, c.y - 6.0f * u), col, 1.6f * u);
            }
            break;
        case Icon::ChevronDown: {  // v: small dropdown arrow
            dl->AddLine(ImVec2(c.x - 3.5f * u, c.y - 1.5f * u), ImVec2(c.x, c.y + 2.0f * u), col, 1.5f * u);
            dl->AddLine(ImVec2(c.x, c.y + 2.0f * u), ImVec2(c.x + 3.5f * u, c.y - 1.5f * u), col, 1.5f * u);
            break;
        }
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
        case Icon::Clear:  // I<I
            dl->AddLine(ImVec2(c.x - 4.5f * u, c.y - 4.5f * u), ImVec2(c.x - 4.5f * u, c.y + 4.5f * u), col, 1.4f * u);
            dl->AddLine(ImVec2(c.x + 2.0f * u, c.y - 3.5f * u), ImVec2(c.x - 1.5f * u, c.y), col, 1.4f * u);
            dl->AddLine(ImVec2(c.x - 1.5f * u, c.y), ImVec2(c.x + 2.0f * u, c.y + 3.5f * u), col, 1.4f * u);
            dl->AddLine(ImVec2(c.x + 4.5f * u, c.y - 4.5f * u), ImVec2(c.x + 4.5f * u, c.y + 4.5f * u), col, 1.4f * u);
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

int MicLayer() {
    if (g_recording) return 2;
    if (g_voiceMuted.load()) return 0;
    return 1;
}

void MicControl(const char* id, float height, std::unique_lock<std::mutex>* callerLock) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = height;
    const float micW = std::floor(h * 1.15f);
    const float arrowW = std::floor(h * 0.75f);
    const float totalW = micW + 1.0f + arrowW;
    const float r = 5.0f;
    const int layer = MicLayer();

    // Background pill
    const ImVec2 pillMax(p.x + totalW, p.y + h);
    dl->AddRectFilled(p, pillMax, IM_COL32(32, 35, 45, 220), r);
    dl->AddRect(p, pillMax, IM_COL32(62, 68, 86, 180), r, ImDrawCornerFlags_All, 1.0f);
    dl->AddLine(ImVec2(p.x + 4.0f, p.y + 1.0f), ImVec2(p.x + totalW - 4.0f, p.y + 1.0f), IM_COL32(255, 255, 255, 25), 1.0f);

    // Left Button: Mic
    const std::string micId = std::string(id) + "_mic";
    ImGui::SetCursorScreenPos(p);
    const bool micClicked = ImGui::InvisibleButton(micId.c_str(), ImVec2(micW, h));
    const bool micHovered = ImGui::IsItemHovered();
    const bool micActive = ImGui::IsItemActive();
    const bool micRight = micHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right);

    if (micHovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        dl->AddRectFilled(p, ImVec2(p.x + micW, p.y + h), IM_COL32(255, 255, 255, micActive ? 35 : 20), r, ImDrawCornerFlags_Left);
    }

    if (micClicked) {
        if (layer == 2) {
            // Layer 2 (Rot): Klick stoppt (Text bleibt im Feld, ändert Stumm-Zustand nicht)
            if (callerLock) callerLock->unlock();
            TalkUp();
            if (callerLock) callerLock->lock();
        } else if (layer == 1) {
            // Layer 1: -> stumm
            if (callerLock) callerLock->unlock();
            SetVoiceMuted(true);
            if (callerLock) callerLock->lock();
        } else if (layer == 0) {
            // Layer 0: -> bereit
            if (callerLock) callerLock->unlock();
            SetVoiceMuted(false);
            if (callerLock) callerLock->lock();
        }
    } else if (micRight) {
        if (layer == 2) {
            // Layer 2 (Rot): Rechtsklick in Rot stoppt ebenso
            if (callerLock) callerLock->unlock();
            TalkUp();
            if (callerLock) callerLock->lock();
        } else if (layer == 1) {
            // Layer 1: -> aktiv: 3 s Zuhören
            if (callerLock) callerLock->unlock();
            TalkLocked();
            if (callerLock) callerLock->lock();
        } else if (layer == 0) {
            // Layer 0: bleibt stumm
        }
    }

    if (micHovered) {
        if (layer == 2) {
            ImGui::SetTooltip("%s", UI(Tr(L"Listening / recording – click to stop")).c_str());
        } else if (layer == 1) {
            ImGui::SetTooltip("%s", UI(Tr(L"Ready – click to mute · right-click: 3 s listening")).c_str());
        } else {
            ImGui::SetTooltip("%s", UI(Tr(L"Muted – click to unmute")).c_str());
        }
    }

    // Draw the Mic Icon
    const ImVec2 micC(p.x + micW * 0.5f, p.y + h * 0.5f);
    const float u = h / 20.0f;
    const ImU32 micCol = (layer == 2) ? IM_COL32(235, 80, 80, 255)
                                      : micHovered ? IM_COL32(255, 255, 255, 255)
                                                   : IM_COL32(200, 205, 220, 255);
    dl->AddRectFilled(ImVec2(micC.x - 2.5f * u, micC.y - 6.0f * u), ImVec2(micC.x + 2.5f * u, micC.y + 1.5f * u), micCol, 2.5f * u);
    dl->PathArcTo(ImVec2(micC.x, micC.y - 1.0f * u), 4.5f * u, 0.0f, 3.14159f, 10);
    dl->PathStroke(micCol, false, 1.4f * u);
    dl->AddLine(ImVec2(micC.x, micC.y + 3.5f * u), ImVec2(micC.x, micC.y + 6.0f * u), micCol, 1.4f * u);
    dl->AddLine(ImVec2(micC.x - 3.0f * u, micC.y + 6.0f * u), ImVec2(micC.x + 3.0f * u, micC.y + 6.0f * u), micCol, 1.4f * u);
    if (layer == 0) {
        // Strike-through slash from bottom-left to top-right (Discord style)
        dl->AddLine(ImVec2(micC.x - 6.5f * u, micC.y + 6.5f * u), ImVec2(micC.x + 6.5f * u, micC.y - 6.5f * u), IM_COL32(20, 22, 28, 255), 3.0f * u);
        dl->AddLine(ImVec2(micC.x - 6.0f * u, micC.y + 6.0f * u), ImVec2(micC.x + 6.0f * u, micC.y - 6.0f * u), micCol, 1.6f * u);
    }

    // Hands free accent indicator (theme accent color dot top-right of mic button)
    if (g_cfg.voiceAutoSend) {
        dl->AddCircleFilled(ImVec2(p.x + micW - 3.5f * u, p.y + 3.5f * u), 2.2f * u, ImGui::GetColorU32(ImGuiCol_CheckMark));
    }

    // Divider line
    const float divX = p.x + micW;
    dl->AddLine(ImVec2(divX, p.y + 4.0f), ImVec2(divX, p.y + h - 4.0f), IM_COL32(255, 255, 255, 30), 1.0f);

    // Right Button: Arrow
    const std::string arrowId = std::string(id) + "_arr";
    ImGui::SetCursorScreenPos(ImVec2(divX + 1.0f, p.y));
    const bool arrowClicked = ImGui::InvisibleButton(arrowId.c_str(), ImVec2(arrowW, h));
    const bool arrowHovered = ImGui::IsItemHovered();
    const bool arrowActive = ImGui::IsItemActive();

    if (arrowHovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        dl->AddRectFilled(ImVec2(divX + 1.0f, p.y), pillMax, IM_COL32(255, 255, 255, arrowActive ? 35 : 20), r, ImDrawCornerFlags_Right);
        ImGui::SetTooltip("%s", UI(Tr(L"Voice Settings")).c_str());
    }
    if (arrowClicked) {
        ToggleVoiceMenu(ImVec2(divX, p.y), pillMax);
    }

    // Draw down-chevron
    const ImVec2 arrC(divX + 1.0f + arrowW * 0.5f, p.y + h * 0.5f);
    const ImU32 arrCol = arrowHovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(170, 175, 195, 255);
    dl->AddLine(ImVec2(arrC.x - 3.5f * u, arrC.y - 1.5f * u), ImVec2(arrC.x, arrC.y + 2.0f * u), arrCol, 1.5f * u);
    dl->AddLine(ImVec2(arrC.x, arrC.y + 2.0f * u), ImVec2(arrC.x + 3.5f * u, arrC.y - 1.5f * u), arrCol, 1.5f * u);

    ImGui::SetCursorScreenPos(ImVec2(pillMax.x, p.y));
}

struct DiscordVoiceMenuState {
    bool open = false;
    ImVec2 anchorMin = ImVec2(0.0f, 0.0f);
    ImVec2 anchorMax = ImVec2(0.0f, 0.0f);
    int openFrame = 0;
    ImVec2 winMin = ImVec2(0.0f, 0.0f);
    ImVec2 winMax = ImVec2(0.0f, 0.0f);
};
static DiscordVoiceMenuState s_voiceMenu;

void ToggleVoiceMenu(const ImVec2& anchorMin, const ImVec2& anchorMax) {
    if (s_voiceMenu.open) {
        s_voiceMenu.open = false;
    } else {
        s_voiceMenu.open = true;
        s_voiceMenu.anchorMin = anchorMin;
        s_voiceMenu.anchorMax = anchorMax;
        s_voiceMenu.openFrame = ImGui::GetFrameCount();
    }
}

void CloseVoiceMenu() {
    s_voiceMenu.open = false;
}

bool IsVoiceMenuOpen() {
    return s_voiceMenu.open;
}

void ConsumeVoiceMenuOutsideClick() {
    if (!s_voiceMenu.open) return;
    if (!ImGui::GetCurrentContext()) return;
    ImGuiIO& io = ImGui::GetIO();
    if (EscLayer() == 1 && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Escape))) {
        s_voiceMenu.open = false;
        return;
    }
    // Only check clicks on frames after the menu was opened
    if (ImGui::GetFrameCount() > s_voiceMenu.openFrame) {
        if (ImGui::IsMouseClicked(0) || ImGui::IsMouseClicked(1) || ImGui::IsMouseClicked(2)) {
            const ImVec2 m = io.MousePos;
            const bool inside = (m.x >= s_voiceMenu.winMin.x && m.x <= s_voiceMenu.winMax.x &&
                                 m.y >= s_voiceMenu.winMin.y && m.y <= s_voiceMenu.winMax.y);
            if (!inside) {
                s_voiceMenu.open = false;
                io.MouseClicked[0] = false;
                io.MouseClicked[1] = false;
                io.MouseClicked[2] = false;
                io.MouseDown[0] = false;
                io.MouseDown[1] = false;
                io.MouseDown[2] = false;
            }
        }
    }
}

void RenderDiscordVoiceMenu() {
    if (!s_voiceMenu.open) return;
    if (!ImGui::GetCurrentContext()) return;
    static int s_renderedFrame = -1;
    if (ImGui::GetFrameCount() == s_renderedFrame) return;
    s_renderedFrame = ImGui::GetFrameCount();

    ImGuiIO& io = ImGui::GetIO();

    // Position window: bottom-left corner right above the chevron button anchor.
    // s_voiceMenu.anchorMin is (divX, p.y).
    const bool openUpwards = (s_voiceMenu.anchorMin.y >= 180.0f);
    const float pivotY = openUpwards ? 1.0f : 0.0f;
    const float posY = openUpwards ? (s_voiceMenu.anchorMin.y - 4.0f) : (s_voiceMenu.anchorMax.y + 4.0f);

    const float approxW = 220.0f;
    float posX = s_voiceMenu.anchorMin.x;
    if (posX + approxW > io.DisplaySize.x - 10.0f) {
        posX = std::max(10.0f, io.DisplaySize.x - approxW - 10.0f);
    }
    ImGui::SetNextWindowPos(ImVec2(posX, posY), ImGuiCond_Always, ImVec2(0.0f, pivotY));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.12f, 0.13f, 0.17f, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.26f, 0.28f, 0.36f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.20f, 0.23f, 0.32f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.28f, 0.32f, 0.45f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.24f, 0.28f, 0.40f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.24f, 0.26f, 0.32f, 0.80f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 6.0f));

    ScopedPopupFont popupFont;
    ImGui::SetNextWindowFocus();
    if (ImGui::Begin("##discord_voice_menu", nullptr, flags)) {
        const ImVec2 wPos = ImGui::GetWindowPos();
        const ImVec2 wSize = ImGui::GetWindowSize();
        s_voiceMenu.winMin = wPos;
        s_voiceMenu.winMax = ImVec2(wPos.x + wSize.x, wPos.y + wSize.y);

        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows)) {
            io.WantCaptureMouse = true;
        }

        bool va = false;
        bool trans = false;
        {
            std::lock_guard<std::mutex> lock(g_mu);
            va = g_cfg.voiceActivation;
            trans = g_cfg.voiceTranslate;
        }

        if (ImGui::MenuItem(UI(Tr(L"Voice activation")).c_str(), nullptr, va)) {
            {
                std::lock_guard<std::mutex> lock(g_mu);
                g_cfg.voiceActivation = true;
                g_cfg.SaveBool(L"Voice", L"Activation", true);
            }
            if (!g_voiceMuted.load() && !g_listenUserOff) StartListening();
            s_voiceMenu.open = false;
        }
        if (ImGui::MenuItem(UI(Tr(L"Push-to-Talk")).c_str(), nullptr, !va)) {
            {
                std::lock_guard<std::mutex> lock(g_mu);
                g_cfg.voiceActivation = false;
                g_cfg.SaveBool(L"Voice", L"Activation", false);
            }
            StopListening();
            s_voiceMenu.open = false;
        }
        if (ImGui::MenuItem(UI(Tr(L"Translate")).c_str(), nullptr, trans)) {
            {
                std::lock_guard<std::mutex> lock(g_mu);
                g_cfg.voiceTranslate = !trans;
                g_cfg.SaveBool(L"Voice", L"Translate", !trans);
            }
            s_voiceMenu.open = false;
        }
        bool handsFree = false;
        {
            std::lock_guard<std::mutex> lock(g_mu);
            handsFree = g_cfg.voiceAutoSend;
        }
        if (ImGui::MenuItem(UI(Tr(L"Hands free (voice only)")).c_str(), nullptr, handsFree, Modules().voiceModule)) {
            {
                std::lock_guard<std::mutex> lock(g_mu);
                g_cfg.voiceAutoSend = !handsFree;
                g_cfg.SaveBool(L"Voice", L"AutoSend", !handsFree);
            }
            s_voiceMenu.open = false;
        }
        ImGui::Separator();
        if (ImGui::MenuItem(UI(Tr(L"Speech input settings...")).c_str())) {
            SwitchOptionsTab(3);
            s_voiceMenu.open = false;
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(6);
}

}  // namespace kb
