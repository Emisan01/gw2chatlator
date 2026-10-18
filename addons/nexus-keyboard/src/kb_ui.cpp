// kb_ui.cpp — everything drawn: themes, the main window, the options.

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
ImVec4 Bright(ImVec4 c) {  // the brightness of your choice: below 100 % darker, above it lighter towards white
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
ImVec4 NameColorOf(Channel ch) {  // the name in front of it
    const ChannelLook* l = LookOf(ch);
    if (!g_look.gameColors || !l) return Bright(ImVec4(0.95f, 0.95f, 0.97f, 1.f));
    return Bright(Rgb32(l->strong));
}

// Text as GW2 draws it: a soft dark halo around the letters (when on), then the letters. `wrap`: lines wrap at the
// right edge and stand under each other at the same x (like TextWrapped). It is an item: hover and click work.
void LookText(const char* text, ImVec4 color, bool wrap) {
    ImFont* font = ImGui::GetFont();
    const float fs = ImGui::GetFontSize();
    const float wrapW = wrap ? std::max(1.0f, ImGui::GetContentRegionAvail().x) : 0.0f;
    const ImVec2 size = font->CalcTextSizeA(fs, FLT_MAX, wrapW, text);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(size.x, std::max(size.y, ImGui::GetTextLineHeight())));
    if (!ImGui::IsItemVisible()) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (g_look.glow) {
        const float d = std::max(1.0f, std::round(fs / 15.0f));
        const ImU32 halo = IM_COL32(0, 0, 0, 160);
        const ImVec2 offsets[] = {ImVec2(-d, -d), ImVec2(d, -d), ImVec2(-d, d), ImVec2(d, d)};
        for (const ImVec2& o : offsets) dl->AddText(font, fs, ImVec2(p.x + o.x, p.y + o.y), halo, text, nullptr, wrapW);
    }
    dl->AddText(font, fs, p, ImGui::GetColorU32(color), text, nullptr, wrapW);
}
bool g_collapsed = false;  // the V / > buttons: window minimizes to bottom-left corner
float g_headerH = 0, g_lastW = 520, g_expandedH = 360, g_restoreH = 0;
float g_anchorBottom = -1.0f;
ImVec2 g_savedPos(-1, -1);   // where the window stood when it was minimised
bool g_buttonMoved = false;  // ... and whether you dragged the small button since (then it opens from there)
ImVec2 g_buttonBottomLeft(-1, -1);  // the small button's bottom-left corner (where a moved button opens from)
ImVec2 RestorePos() {  // where the window opens again
    if (!g_buttonMoved && g_savedPos.x >= 0) return g_savedPos;
    if (g_buttonBottomLeft.x >= 0) return ImVec2(g_buttonBottomLeft.x, g_buttonBottomLeft.y - g_expandedH);
    return ImVec2(-1, -1);
}

void MinimizeWindow() {  // g_mu held or main thread
    g_collapsed = true;
    const ImVec2 p = ImGui::GetWindowPos(), s = ImGui::GetWindowSize();
    g_lastW = s.x;
    g_expandedH = s.y;
    g_anchorBottom = p.y + s.y;  // the small button takes its place on the next frame
    g_savedPos = p;
    g_buttonMoved = false;
}

// The footer chips: channel | "Write: German" | "Send: English (UK) · MyMemory" (orange) | auto | counter.
void RenderFooter() {  // g_mu held
    static const Channel kChannels[] = {Channel::Unknown, Channel::Say,   Channel::Map,  Channel::Party,
                                        Channel::Squad,   Channel::Guild, Channel::Team, Channel::Whisper};
    auto chipWidth = [](const std::string& s) { return ImGui::CalcTextSize(s.c_str()).x + ImGui::GetFrameHeight() + 12; };
    const float iconH = ImGui::GetFrameHeight();

    // Native minimize button (bottom-left corner of the window)
    if (Lay().minButton) {
        if (ImGui::Button(">##minchat", ImVec2(iconH, iconH))) MinimizeWindow();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", UI(Tr(L"Minimize chat")).c_str());
        ImGui::SameLine();
    }

    // Footer chips cached: precalculated strings and sizes avoid allocations on idle frames.
    struct FooterCache {
        Channel channel = Channel::Unknown;
        std::wstring whisperTo;
        std::wstring writeIn;
        std::wstring sendLang;
        bool sendOrig = false;
        std::string labelFull, labelNarrow;
        std::string labelBtnFull, labelBtnNarrow;
        std::string writeFull, writeNarrow;
        std::string writeBtnFull, writeBtnNarrow;
        std::string sendFull, sendNarrow;
        std::string sendBtnFull, sendBtnNarrow;
        float labelWFull = 0, labelWNarrow = 0;
        float writeWFull = 0, writeWNarrow = 0;
        float sendWFull = 0, sendWNarrow = 0;
        float totalNeedFull = 0;
    };
    static FooterCache s_fc;
    const bool sendOrig = SendOriginal();
    const std::wstring sendLang = SendLang();
    if (s_fc.channel != g_channel || s_fc.whisperTo != g_whisperTo || s_fc.writeIn != g_cfg.writeIn ||
        s_fc.sendLang != sendLang || s_fc.sendOrig != sendOrig) {
        s_fc.channel = g_channel;
        s_fc.whisperTo = g_whisperTo;
        s_fc.writeIn = g_cfg.writeIn;
        s_fc.sendLang = sendLang;
        s_fc.sendOrig = sendOrig;

        s_fc.labelFull = g_channel == Channel::Unknown ? UI(Tr(L"Active channel")) : UI(ChannelLabel(g_channel));
        if (g_channel == Channel::Whisper && !g_whisperTo.empty()) s_fc.labelFull += " " + U8(g_whisperTo);
        s_fc.labelNarrow = g_channel == Channel::Unknown ? UI(Tr(L"Active")) : s_fc.labelFull;
        s_fc.labelBtnFull = s_fc.labelFull + "##chbtn";
        s_fc.labelBtnNarrow = s_fc.labelNarrow + "##chbtn";

        s_fc.writeFull = UI(TrF(L"Write: {1}", {g_cfg.writeIn.empty() ? Tr(L"like Windows") : NativeName(g_cfg.writeIn)}));
        s_fc.writeNarrow = g_cfg.writeIn.empty() ? std::string("Win") : U8(ToUpperAscii(PrimaryLang(g_cfg.writeIn)));
        s_fc.writeBtnFull = s_fc.writeFull + "##writebtn";
        s_fc.writeBtnNarrow = s_fc.writeNarrow + "##writebtn";

        const std::wstring sendText = sendOrig ? Tr(L"as written") : NativeName(sendLang);
        s_fc.sendFull = UI(TrF(L"Send: {1}", {sendText}));
        s_fc.sendNarrow = "> " + (sendOrig ? UI(Tr(L"as written")) : U8(ToUpperAscii(PrimaryLang(sendLang))));
        s_fc.sendBtnFull = s_fc.sendFull + "##sendbtn";
        s_fc.sendBtnNarrow = s_fc.sendNarrow + "##sendbtn";

        s_fc.labelWFull = chipWidth(s_fc.labelFull);
        s_fc.labelWNarrow = chipWidth(s_fc.labelNarrow);
        s_fc.writeWFull = chipWidth(s_fc.writeFull);
        s_fc.writeWNarrow = chipWidth(s_fc.writeNarrow);
        s_fc.sendWFull = chipWidth(s_fc.sendFull);
        s_fc.sendWNarrow = chipWidth(s_fc.sendNarrow);
        s_fc.totalNeedFull = iconH + s_fc.labelWFull + s_fc.writeWFull + s_fc.sendWFull +
                             ImGui::GetFrameHeight() * 4 + ImGui::CalcTextSize("000/199").x;
    }

    const bool narrow = s_fc.totalNeedFull > ImGui::GetContentRegionAvail().x;
    const std::string& chBtnLabel = narrow ? s_fc.labelBtnNarrow : s_fc.labelBtnFull;
    const float chBtnW = narrow ? s_fc.labelWNarrow : s_fc.labelWFull;

    const ImVec2 chBtnPos = ImGui::GetCursorScreenPos();
    ImGui::PushStyleColor(ImGuiCol_Text, NameColorOf(g_channel == Channel::Unknown ? Channel::Say : g_channel));
    const bool chLeft = ImGui::Button(chBtnLabel.c_str(), ImVec2(chBtnW, iconH));
    ImGui::PopStyleColor();
    const bool chRight = ImGui::IsItemHovered() && ImGui::IsMouseClicked(1);

    if (chLeft || chRight) {
        ImGui::OpenPopup("##channel_popup");
    }

    // Dropdown menu opens UPWARDS directly above the button, overlaying the window upwards!
    ImGui::SetNextWindowPos(ImVec2(chBtnPos.x, chBtnPos.y - 4.0f), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(chBtnW, 0.0f), ImVec2(FLT_MAX, ImGui::GetFontSize() * 16.0f));
    if (ImGui::BeginPopup("##channel_popup")) {
        for (Channel c : kChannels) {
            const std::string name = c == Channel::Unknown ? UI(Tr(L"Active channel")) : UI(ChannelLabel(c));
            ImGui::PushStyleColor(ImGuiCol_Text, NameColorOf(c == Channel::Unknown ? Channel::Say : c));
            if (ImGui::Selectable(name.c_str(), c == g_channel)) {
                if (c != Channel::Whisper && g_activeTab >= 0) LeaveWhisperTab();  // the whisper tab is left
                g_channel = c;
                if (c != Channel::Whisper) g_whisperTo.clear();
                g_focusInput = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopStyleColor();
        }
        ImGui::EndPopup();
    }
    // The language you write in (spelling, learned words and the prediction follow it).
    ImGui::SameLine();
    const ImVec2 writeBtnPos = ImGui::GetCursorScreenPos();
    const float writeBtnW = narrow ? s_fc.writeWNarrow : s_fc.writeWFull;
    const std::string& writeBtnLabel = narrow ? s_fc.writeBtnNarrow : s_fc.writeBtnFull;
    const bool writeLeft = ImGui::Button(writeBtnLabel.c_str(), ImVec2(writeBtnW, iconH));
    const bool writeRight = ImGui::IsItemHovered() && ImGui::IsMouseClicked(1);
    if (writeLeft || writeRight) {
        ImGui::OpenPopup("##write_popup");
    }
    ImGui::SetNextWindowPos(ImVec2(writeBtnPos.x, writeBtnPos.y - 4.0f), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(writeBtnW, 0.0f), ImVec2(FLT_MAX, ImGui::GetFontSize() * 20.0f));
    if (ImGui::BeginPopup("##write_popup")) {
        std::wstring chosen = g_cfg.writeIn;
        if (ImGui::Selectable(UI(Tr(L"like Windows")).c_str(), chosen.empty())) {
            chosen.clear();
            ImGui::CloseCurrentPopup();
        }
        for (const LangInfo& l : Languages()) {
            if (l.latinScript && ImGui::Selectable(U8(l.native).c_str(), g_cfg.writeIn == l.code)) {
                chosen = l.code;
                ImGui::CloseCurrentPopup();
            }
        }
        if (chosen != g_cfg.writeIn) {
            g_cfg.writeIn = chosen;
            g_cfg.SaveValue(L"Spelling", L"WriteIn", chosen);
            g_worker.Post([] {  // spelling and learned words follow the language you write in
                FreeWorkerState();
                InitSpelling();
            });
            ++g_inputGen;
            g_inputChanged = GetTickCount64();
            if (g_nx.translateChat) TranslateRecentLines(false);  // the chat is read in the language you write in
        }
        ImGui::EndPopup();
    }

    // What the chat gets (orange); who translates is told by the status line.
    ImGui::SameLine();
    const ImVec2 sendBtnPos = ImGui::GetCursorScreenPos();
    const float sendBtnW = narrow ? s_fc.sendWNarrow : s_fc.sendWFull;
    const std::string& sendBtnLabel = narrow ? s_fc.sendBtnNarrow : s_fc.sendBtnFull;
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.94f, 0.75f, 0.35f, 1.f));
    const bool sendLeft = ImGui::Button(sendBtnLabel.c_str(), ImVec2(sendBtnW, iconH));
    ImGui::PopStyleColor();
    const bool sendRight = ImGui::IsItemHovered() && ImGui::IsMouseClicked(1);
    if (sendLeft || sendRight) {
        ImGui::OpenPopup("##send_popup");
    }
    ImGui::SetNextWindowPos(ImVec2(sendBtnPos.x, sendBtnPos.y - 4.0f), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(sendBtnW, 0.0f), ImVec2(FLT_MAX, ImGui::GetFontSize() * 20.0f));
    if (ImGui::BeginPopup("##send_popup")) {
        for (size_t i = 0; i <= g_cfg.writeLangs.size(); ++i) {
            const std::string n = i == g_cfg.writeLangs.size() ? UI(Tr(L"as written")) : U8(NativeName(g_cfg.writeLangs[i]));
            const bool current = g_sendOverride.empty() ? static_cast<int>(i) == g_sendIdx
                                 : i == g_cfg.writeLangs.size() ? g_sendOverride == L"="
                                                                : g_sendOverride == g_cfg.writeLangs[i];
            if (ImGui::Selectable(n.c_str(), current)) {
                if (g_activeTab >= 0 && g_activeTab < static_cast<int>(g_tabs.size())) {  // this player's tab only
                    g_tabs[g_activeTab].sendLang = i == g_cfg.writeLangs.size() ? std::wstring(L"=") : g_cfg.writeLangs[i];
                    g_sendOverride = g_tabs[g_activeTab].sendLang;
                } else {
                    g_sendIdx = static_cast<int>(i);
                }
                ++g_inputGen;
                g_preview.clear();
                g_inputChanged = GetTickCount64();
                g_focusInput = true;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::Separator();
        for (const LangInfo& l : Languages()) {  // more languages: the chosen one becomes the first
            if (!l.latinScript || std::find(g_cfg.writeLangs.begin(), g_cfg.writeLangs.end(), l.code) != g_cfg.writeLangs.end())
                continue;
            if (ImGui::Selectable(U8(l.native).c_str(), false)) {
                g_cfg.writeLangs.insert(g_cfg.writeLangs.begin(), l.code);
                g_cfg.SaveAll();
                if (g_activeTab >= 0 && g_activeTab < static_cast<int>(g_tabs.size())) {  // this player's tab only
                    g_tabs[g_activeTab].sendLang = l.code;
                    g_sendOverride = l.code;
                } else {
                    g_sendIdx = 0;
                }
                ++g_inputGen;
                g_preview.clear();
                g_inputChanged = GetTickCount64();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
    if (g_cfg.voiceEnabled) {  // send right after speaking (off by default)
        ImGui::SameLine();
        if (ImGui::Checkbox(UI(Tr(L"auto") + L"##autosend").c_str(), &g_cfg.voiceAutoSend))
            g_cfg.SaveBool(L"Voice", L"AutoSend", g_cfg.voiceAutoSend);
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26);
            ImGui::TextUnformatted(UI(Tr(L"auto: send right after speaking")).c_str());
            ImGui::TextDisabled("%s", UI(Tr(L"When you stop talking (key released, or the pause after speaking), the recognized "
                                            L"text goes into the GW2 chat at once – translated if a send language is "
                                            L"chosen. Off: it waits in the box until you press Enter.")).c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    }
    const size_t n = CodePointCount(g_wFlatInput);
    std::string counter = std::to_string(n) + "/" + std::to_string(g_cfg.maxLength);
    if (n > static_cast<size_t>(g_cfg.maxLength)) {  // sent as several chat lines
        const bool current = g_previewGen == g_inputGen && !g_preview.empty();
        const size_t parts = SplitForChat(Translating() && current ? g_preview : g_wFlatInput, ComposePrefix(),
                                          static_cast<size_t>(g_cfg.maxLength)).size();
        counter += "  (" + std::to_string(parts) + "x)";
    }
    // At the right edge, never over the chips: without room it goes into the next line.
    const float counterX = ImGui::GetWindowContentRegionMax().x - ImGui::CalcTextSize(counter.c_str()).x;
    const float after = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x + ImGui::GetStyle().ItemSpacing.x * 2;
    if (counterX >= after) ImGui::SameLine(counterX);
    else ImGui::SetCursorPosX(std::max(counterX, ImGui::GetCursorPosX()));
    if (!g_layout.infoText && !g_input[0]) return;  // info texts off: the counter only while you write
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", counter.c_str());
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

// The status text (drawn in the input box): a message, else what is going on.
std::wstring StatusText() {  // g_mu held
    if (!g_status.empty()) return g_status;
    if (g_recording) return Tr(L"Hearing you … release the key when you are done");
    if (!g_listenPausedFor.empty()) return TrF(L"Voice activation paused – {1} uses the microphone", {g_listenPausedFor});
    if (g_listening) return Tr(L"Voice activation listens – just speak");
    const bool voiceEnglish = g_cfg.voiceEnabled && g_cfg.voiceTranslate;
    if (!g_nx.translateChat && !Translating()) {  // nothing goes to a translator: no counter of one
        if (!g_cfg.voiceEnabled) return Tr(L"Prediction only");  // the typing help is all that is on
        return voiceEnglish ? Tr(L"Translation off · speech into English (whisper)") : Tr(L"Translation off");
    }
    if (const std::wstring q = MyMemoryQuotaText(); !q.empty()) return q;
    std::wstring route = SendOriginal() ? Tr(L"as written") : ToUpperAscii(PrimaryLang(SendLang()));
    if (Translating() && g_translator) route += L" · " + g_translator->Name();
    return TrF(L"Send: {1}", {route});
}
int g_optionsTab = -1;  // a tab of the options window to bring to the front (General 0, Window 1, Voice 3, Translator 5)

void StatusRow(const std::string& what, int state, const std::string& text) {  // state: 2 ready, 1 busy, 0 missing
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(state == 2 ? ImVec4(0.45f, 0.85f, 0.45f, 1.f) : state == 1 ? ImVec4(0.94f, 0.75f, 0.35f, 1.f)
                                                                                  : ImVec4(0.95f, 0.55f, 0.35f, 1.f),
                       "%s", state == 2 ? "[OK]" : state == 1 ? "[..]" : "[--]");
    ImGui::SameLine();
    ImGui::TextUnformatted(what.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%s", text.c_str());
}

// What is ready (chat, voice, model), with the buttons that load what is missing; `guide`: and where to go next.
void RenderModules(bool guide) {  // g_mu NOT held
    const ModuleState& m = Modules();
    ImGui::PushID("modules");
    // The chat.
    const bool chatLive = g_chatEventsLive || g_chatEventsPresent;
    StatusRow(UI(Tr(L"Chat module")), chatLive ? 2 : g_chatEventsInstalling || g_chatEventsFile ? 1 : 0,
              UI(chatLive ? Tr(L"ready – Events: Chat is loaded")
                 : g_chatEventsInstalling ? Tr(L"loading Events: Chat …")
                 : g_chatEventsFile ? Tr(L"installed – Nexus loads it (else: Nexus → Addons → Events: Chat → Load)")
                                    : Tr(L"Events: Chat is missing")));
    if (!chatLive && !g_chatEventsFile && !g_chatEventsInstalling) {
        ImGui::SameLine();
        if (ImGui::SmallButton(UI(Tr(L"Install Events: Chat")).c_str())) InstallChatEventsNow();
    }
    // The voice module and its model.
    bool enabled, ready;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        enabled = g_cfg.voiceEnabled;
        ready = g_voiceReady;
    }
    if (enabled) {
        const int pct = g_voiceModulePct;
        const bool running = g_voice.Running();
        StatusRow(UI(Tr(L"Voice module")), m.voiceModule ? 2 : g_voiceModuleInstalling ? 1 : 0,
                  UI(g_voiceModuleInstalling ? TrF(L"loading … {1} %", {std::to_wstring(std::max(pct, 0))})
                     : !m.voiceModule        ? Tr(L"missing")
                     : ready                 ? Tr(L"ready – loaded")
                     : running               ? Tr(L"starting")
                                             : Tr(L"ready – loads on the first use of the microphone")));
        ImGui::SameLine();
        if (!m.voiceModule && !g_voiceModuleInstalling) {
            if (ImGui::SmallButton(UI(Tr(L"Load voice module")).c_str())) InstallVoiceModuleNow();
        } else if (m.voiceModule && !running) {
            if (ImGui::SmallButton(UI(Tr(L"Load")).c_str())) LoadVoiceModule();
        } else if (running) {
            if (ImGui::SmallButton(UI(Tr(L"Unload")).c_str())) UnloadVoiceModule();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", UI(Tr(L"Ends the voice module and frees its memory; the microphone loads it again.")).c_str());
        }
        const int dl = g_downloadPct;
        StatusRow(UI(Tr(L"Voice model")), m.model ? 2 : dl >= 0 ? 1 : 0,
                  dl >= 0 ? UI(TrF(L"downloading … {1} %", {std::to_wstring(dl)}))
                          : m.modelName + (m.model ? "" : "  " + UI(Tr(L"missing"))));
        if (!m.model && dl < 0) {
            ImGui::SameLine();
            if (ImGui::SmallButton(UI(Tr(L"Download the model")).c_str())) {
                Config c;
                {
                    std::lock_guard<std::mutex> lock(g_mu);
                    c = g_cfg;
                }
                const VoiceCatalog catalog = LoadVoiceCatalog(c.dataDir);
                const VoiceModelInfo* chosen = catalog.Find(U8(c.voiceModel));
                if (!chosen) chosen = catalog.Recommended();
                if (chosen) StartModelDownload(*chosen);
            }
        }
    }
    if (guide) {
        ImGui::Spacing();
        bool chat, mine, voiceEnglish;
        {
            std::lock_guard<std::mutex> lock(g_mu);
            chat = g_nx.translateChat;
            mine = g_cfg.translateWriting;
            voiceEnglish = g_cfg.voiceTranslate;
        }
        if (!chat || !mine) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", UI(Tr(L"Translate the chat or your text: Options → Translator")).c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton(UI(Tr(L"Open") + L"##tr").c_str())) {
                g_showOptions = true;
                g_optionsTab = 5;
            }
        }
        if (enabled && !voiceEnglish) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", UI(Tr(L"Turn what you say into English: Options → Voice")).c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton(UI(Tr(L"Open") + L"##vo").c_str())) {
                g_showOptions = true;
                g_optionsTab = 3;
            }
        }
    }
    ImGui::PopID();
}

void Hint(const std::string& text) {  // grey and wrapped: long hints are never cut off
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopStyleColor();
}

void RenderOptionsWindow() {
    if (!g_showOptions) return;
    ImGui::SetNextWindowSize(ImVec2(660, 620), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(600, 420), ImVec2(4000, 4000));
    static std::string s_optsTitle;
    static UiLang s_optsLang = static_cast<UiLang>(-1);
    if (s_optsLang != g_cfg.uiLang || s_optsTitle.empty()) {
        s_optsLang = g_cfg.uiLang;
        s_optsTitle = UI(Tr(L"GW2 Keyboard – Options")) + "###kbopts";
    }
    if (ImGui::Begin(s_optsTitle.c_str(), &g_showOptions)) RenderOptionsContent();
    ImGui::End();
}

// Text drawn into the input box (status, the translation in the compact view): on top, clipped to the box.
void BoxText(const ImVec2& at, const ImVec4& clip, ImU32 color, const std::string& text, float scale) {
    ImGui::GetForegroundDrawList()->AddText(ImGui::GetFont(), ImGui::GetFontSize() * scale, at, color, text.c_str(), nullptr,
                                            0, &clip);
}

void RenderWindow() {
    if (!ImGui::GetCurrentContext()) return;  // failsafe: no context, no drawing
    KickSave();
    ImGuiIO& io = ImGui::GetIO();
    if (io.DisplaySize.x <= 0 || io.DisplaySize.y <= 0) return;
    struct PopColoursAtEnd {
        int n = 0;
        ~PopColoursAtEnd() { if (n) ImGui::PopStyleColor(n); }
    } popColours;
    popColours.n = PushTheme();  // for everything drawn below: the options, our window, its popups
    g_keyboardFree = !io.WantCaptureKeyboard;
    g_fps = static_cast<int>(io.Framerate);
    {
        // MumbleLink (every 250 ms): shown only on a map – never at character select or on a loading screen (like the
        // exe waiting for GW2); your character's name; the GW2 chat line opening (focus behaviour).
        static ULONGLONG next = 0;
        static MumbleLink mumble;
        static bool chatWasOpen = false;
        if (GetTickCount64() > next) {
            next = GetTickCount64() + 250;
            const MumbleState ms = mumble.Read();
            std::lock_guard<std::mutex> lock(g_mu);
            g_inMap = ms.live && ms.inMap;
            g_mapOpen = ms.live && ms.MapOpen();
            if (ms.live) {
                g_ownName = ms.identity.name;
                g_uiSize.store(ms.identity.uiSize);
                g_mapId.store(ms.identity.mapId);
            }
            const bool chatOpen = ms.live && ms.TextboxHasFocus();
            g_gameChatOpen = chatOpen;
            const bool chatOpened = chatOpen && !chatWasOpen && g_show && GetTickCount64() > g_suppressChatFocusUntil &&
                                    g_redirect == 0;
            if (chatOpened && g_cfg.focusOnGameChat) g_focusInput = true;   // the focus comes over (opens it as well)
            else if (chatOpened && g_layout.autoOpen && g_collapsed) g_expandRequest = true;  // only opened
            static bool s_redirectChatSeen = false;  // GW2's chat line was open during this redirect
            if (g_redirect == 0) s_redirectChatSeen = false;
            if (g_redirect != 0 && chatOpen) s_redirectChatSeen = true;
            if (g_redirect == 1 && ms.MapOpen()) g_redirect = 2;       // a waypoint: the map went open …
            else if (g_redirect == 3 && chatOpen) g_redirect = 4;       // anything else: GW2's chat line is open …
            const bool done = (g_redirect == 2 && !ms.MapOpen()) || (g_redirect == 4 && !chatOpen && !ms.MapOpen()) ||
                              (g_redirect == 1 && s_redirectChatSeen && !chatOpen && !ms.MapOpen());  // closed, no map
            if (done || (g_redirect != 0 && GetTickCount64() - g_redirectSince > 45000)) {  // … and closed again
                g_redirect = 0;
                g_expandRequest = true;
            }
            chatWasOpen = chatOpen;
        }
    }
    ChatEventsPresent();
    RenderOptionsWindow();
    bool inMap;
    static bool s_uncollapseRestore = false;
    static ImVec2 s_restorePos(-1, -1);
    {
        std::lock_guard<std::mutex> lock(g_mu);
        inMap = (g_inMap || g_test.forceMap) && (!g_mapOpen || GetTickCount64() < g_showOnMapUntil);
        if (g_focusInput && g_collapsed) {  // something wants the input: the window opens again
            g_collapsed = false;
            g_restoreH = g_expandedH;
            s_uncollapseRestore = true;
            s_restorePos = RestorePos();
        }
    }
    if (!g_show || !inMap) return;

    const float iconH = ImGui::GetFrameHeight();
    const float minBtn = iconH + 8.0f;
    if (g_collapsed) {
        g_lastUse = 0;  // opened again: "not in use" counts from then
        ImGui::SetNextWindowSize(ImVec2(minBtn, minBtn), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(std::clamp(g_cfg.opacity, 60, 255) / 255.0f);
        ImGuiWindowFlags minFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_NoScrollWithMouse;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(2, 2));
        const bool beginOk = ImGui::Begin("GW2Chatlator###kbmain", nullptr, minFlags);
        ImGui::PopStyleVar();
        if (beginOk) {
            if (g_anchorBottom > 0) {  // just minimised: the button's bottom edge = the window's bottom edge
                const ImVec2 wp = ImGui::GetWindowPos();
                ImGui::SetWindowPos(ImVec2(wp.x, g_anchorBottom - ImGui::GetWindowSize().y));
                g_anchorBottom = -1.0f;
            }
            const float resScale = std::max(1.0f, io.DisplaySize.y / 1080.0f);
            const float maxBottom = std::max(minBtn, io.DisplaySize.y - Gw2BottomBarOffset(io.DisplaySize.y, g_uiSize.load()));
            const ImVec2 pos = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
            g_buttonBottomLeft = ImVec2(pos.x, pos.y + size.y);
            static bool s_btnDrag = false;
            static ImVec2 s_btnStart;
            if (ImGui::IsMouseClicked(0) && ImGui::IsWindowHovered()) {
                s_btnDrag = true;
                s_btnStart = pos;
            } else if (s_btnDrag && !ImGui::IsMouseDown(0)) {
                s_btnDrag = false;
                if (pos.x != s_btnStart.x || pos.y != s_btnStart.y) {  // you moved it: it opens from here, snapped
                    g_buttonMoved = true;
                    if (g_nx.snap) {
                        const float t = 14.0f * resScale;
                        ImVec2 p = pos;
                        const float uiScale = (g_uiSize.load() == 0 ? 0.89f : g_uiSize.load() == 2 ? 1.11f : g_uiSize.load() == 3 ? 1.25f : 1.0f);
                        const float nativeChatLeft = std::round(10.0f * uiScale * resScale);
                        if (std::abs(p.x - nativeChatLeft) < t) p.x = nativeChatLeft;
                        else if (p.x < t) p.x = 0.0f;
                        else if (p.x + size.x > io.DisplaySize.x - t) p.x = io.DisplaySize.x - size.x;
                        if (p.y < t) p.y = 0.0f;
                        else if (p.y + size.y > maxBottom - t) p.y = maxBottom - size.y;
                        if (p.x != pos.x || p.y != pos.y) ImGui::SetWindowPos(p);
                    }
                }
            }
            // Always prevent being pushed below the bottom bar or off-screen
            {
                ImVec2 p = ImGui::GetWindowPos();
                bool clamped = false;
                if (p.x < -0.5f) { p.x = 0.0f; clamped = true; }
                else if (p.x + size.x > io.DisplaySize.x + 0.5f) { p.x = std::max(0.0f, io.DisplaySize.x - size.x); clamped = true; }
                if (p.y < -0.5f) { p.y = 0.0f; clamped = true; }
                else if (p.y + size.y > maxBottom + 0.5f) { p.y = std::max(0.0f, maxBottom - size.y); clamped = true; }
                if (clamped && !ImGui::IsMouseDown(0)) ImGui::SetWindowPos(p);
            }
            const bool clickedOpen = ImGui::Button("<##expand", ImVec2(iconH, iconH));
            if (clickedOpen || g_expandRequest) {  // the bottom edge stays where it is; the window grows upwards
                std::lock_guard<std::mutex> lock(g_mu);
                g_expandRequest = false;
                g_redirect = 0;
                g_collapsed = false;
                g_restoreH = g_expandedH;
                s_uncollapseRestore = true;
                g_suppressChatFocusUntil = 0;
                const ImVec2 p = ImGui::GetWindowPos(), s = ImGui::GetWindowSize();
                g_buttonBottomLeft = ImVec2(p.x, p.y + s.y);
                s_restorePos = RestorePos();
                ImGui::SetWindowPos(s_restorePos);
                ImGui::SetWindowSize(ImVec2(g_lastW, g_expandedH));
                if (clickedOpen) g_focusInput = true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", UI(Tr(L"Open the window")).c_str());
            if (g_autoSendPending && !g_edit.active && g_input[0]) {
                g_autoSendPending = false;
                StartSend();
            }
            ImGui::End();
            return;
        }
        ImGui::End();
        return;
    }

    // Without the chat the window is exactly its parts (fitted below, down to nothing but the input).
    const float minW = 380, minH = Lay().chat ? 240.0f : 16.0f;
    ImGui::SetNextWindowSize(ImVec2(520, 360), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(minW, minH), ImVec2(FLT_MAX, FLT_MAX));
    if (g_restoreH > 0) {
        ImGui::SetNextWindowSize(ImVec2(g_lastW, g_restoreH), ImGuiCond_Always);
        g_restoreH = 0;
    }
    ImGui::SetNextWindowBgAlpha(std::clamp(g_cfg.opacity, 0, 255) / 255.0f);  // up to fully see-through (the small button keeps 60)
    ImGuiWindowFlags wflags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                              ImGuiWindowFlags_NoScrollWithMouse;
    // Without the chat: a thin edge only, the window is hardly more than its parts (the grey input in the end).
    if (!Lay().chat) ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));
    const bool begun = ImGui::Begin("GW2Chatlator###kbmain", nullptr, wflags);
    if (!Lay().chat) ImGui::PopStyleVar();
    if (!begun) {
        ImGui::End();
        return;
    }
    if (s_uncollapseRestore) {  // opened again: exactly where it stood (or above the moved button)
        s_uncollapseRestore = false;
        if (s_restorePos.x >= 0) {
            ImGui::SetWindowPos(s_restorePos);
            ImGui::SetWindowSize(ImVec2(g_lastW, g_expandedH));
            s_restorePos = ImVec2(-1, -1);
        }
    }
    if (g_minimizeRequest) {  // asked from inside the chat area (a waypoint click): done on the main window
        g_minimizeRequest = false;
        g_expandRequest = false;
        MinimizeWindow();
        ImGui::End();
        return;
    }
    if (g_expandRequest) g_expandRequest = false;  // already open
    struct PopFontAtEnd {
        bool on = false;
        ~PopFontAtEnd() { if (on) ImGui::PopFont(); }
    } popFont;
    {
        const float px = PtToPx(g_cfg.fontPt);
        ImFont* tf = g_textFont.load();
        if (tf && std::abs(tf->FontSize - px) < 0.5f) {  // sharp: the font built at this size
            ImGui::SetWindowFontScale(1.0f);
            ImGui::PushFont(tf);
            popFont.on = true;
        } else {  // not (yet) there: Nexus' font scaled
            ImGui::SetWindowFontScale(px / std::max(1.0f, ImGui::GetFont()->FontSize));
        }
    }
    g_lastW = ImGui::GetWindowWidth();
    g_expandedH = ImGui::GetWindowHeight();
    if (g_expandedH < minH || g_lastW < 380.0f) {
        if (g_expandedH < minH) g_expandedH = Lay().chat ? 360.0f : minH;
        if (g_lastW < 380.0f) g_lastW = 520.0f;
        ImGui::SetWindowSize(ImVec2(g_lastW, g_expandedH));
    }
    std::unique_lock<std::mutex> lock(g_mu);
    {  // Snap: a move that ends near a screen edge puts the window onto it; dragging past screen edge pulls it back
        static bool dragging = false;
        static ImVec2 startPos;
        const ImVec2 pos = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
        const float resScale = std::max(1.0f, io.DisplaySize.y / 1080.0f);
        const float maxBottom = std::max(minH, io.DisplaySize.y - Gw2BottomBarOffset(io.DisplaySize.y, g_uiSize.load()));
        if (ImGui::IsMouseClicked(0) && ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows)) {
            dragging = true;
            startPos = pos;
        } else if (dragging && !ImGui::IsMouseDown(0)) {
            dragging = false;
            const bool moved = (pos.x != startPos.x || pos.y != startPos.y);
            if (g_nx.snap && moved) {
                const float t = 14.0f * resScale;
                ImVec2 p = pos;
                // Left / Right:
                if (p.x < t) p.x = 0.0f;
                else if (p.x + size.x > io.DisplaySize.x - t) p.x = io.DisplaySize.x - size.x;

                // Top / Bottom:
                if (p.y < t) p.y = 0.0f;
                else if (p.y + size.y > maxBottom - t) p.y = maxBottom - size.y;

                if (p.x != pos.x || p.y != pos.y) ImGui::SetWindowPos(p);
            }
        }
        // General hard floor & boundary clamp: always prevent window from being pushed below bottom bar or off-screen
        {
            ImVec2 p = ImGui::GetWindowPos();
            bool clamped = false;
            if (p.x < -0.5f) { p.x = 0.0f; clamped = true; }
            else if (p.x + size.x > io.DisplaySize.x + 0.5f) { p.x = std::max(0.0f, io.DisplaySize.x - size.x); clamped = true; }
            if (p.y < -0.5f) { p.y = 0.0f; clamped = true; }
            else if (p.y + size.y > maxBottom + 0.5f) { p.y = std::max(0.0f, maxBottom - size.y); clamped = true; }
            if (clamped && !ImGui::IsMouseDown(0)) ImGui::SetWindowPos(p);
        }
    }
    if (g_statusUntil && GetTickCount64() > g_statusUntil) {
        g_status.clear();
        g_statusUntil = 0;
    }
    const ImVec4 kAccent(0.94f, 0.75f, 0.35f, 1.f), kPreview(0.59f, 0.80f, 1.f, 1.f);
    const ImGuiStyle& st = ImGui::GetStyle();

    // ---- header: the tabs (the chat, whisper tabs; right-click: a new one) · microphone · V / A · menu · close
    const float icon = ImGui::GetFrameHeight();
    const float gap = std::round(icon * 0.6f);
    const float rightW = (g_cfg.voiceEnabled ? icon + gap : 0) + icon + gap + icon + st.ItemSpacing.x + icon;
    const float tabsW = std::max(60.0f, ImGui::GetContentRegionAvail().x - rightW - gap);
    bool newTab = false, tabMenu = false;
    static bool s_lineRightClick = false;  // a right-click on a chat line (it translates the line, no menu)
    if (Lay().header) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::BeginChild("##hdr", ImVec2(tabsW, icon), false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
        ImGui::PopStyleVar();
        if (ImGui::BeginTabBar("##wtabs", ImGuiTabBarFlags_FittingPolicyResizeDown | ImGuiTabBarFlags_NoTooltip)) {
            static std::string s_chatTabLabel;
            static UiLang s_chatTabLang = static_cast<UiLang>(-1);
            if (s_chatTabLang != g_cfg.uiLang || s_chatTabLabel.empty()) {
                s_chatTabLang = g_cfg.uiLang;
                s_chatTabLabel = UI(Tr(L"Chat")) + "###wt_all";
            }
            if (ImGui::BeginTabItem(s_chatTabLabel.c_str(), nullptr,
                                    g_selectTab == -1 ? ImGuiTabItemFlags_SetSelected : 0)) {
                if (g_selectTab == -1) g_selectTab = -2;
                else if (g_selectTab == -2 && g_activeTab != -1) LeaveWhisperTab();
                ImGui::EndTabItem();
            }
            int close = -1;
            for (int i = 0; i < static_cast<int>(g_tabs.size()); ++i) {
                bool open = true;
                const ImGuiTabItemFlags f = (g_selectTab == i ? ImGuiTabItemFlags_SetSelected : 0) |
                                            (g_tabs[i].unread ? ImGuiTabItemFlags_UnsavedDocument : 0);
                const bool shown = ImGui::BeginTabItem(g_tabs[i].Label(), &open, f);
                if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                    tabMenu = true;  // not the header's menu
                    if (g_tabs[i].custom) g_tabSettings = static_cast<int>(i);
                }
                if (shown) {
                    if (g_selectTab == i) g_selectTab = -2;
                    else if (g_selectTab == -2 && g_activeTab != i) ActivateTab(i);
                    ImGui::EndTabItem();
                }
                if (!open) close = i;
            }
            ImGui::EndTabBar();
            if (close >= 0) {
                const bool active = close == g_activeTab, custom = g_tabs[close].custom;
                g_tabs.erase(g_tabs.begin() + close);
                if (active) LeaveWhisperTab();
                else if (close < g_activeTab) g_selectTab = --g_activeTab;
                if (custom) SaveCustomTabs();
            }
        }
        if (!tabMenu && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            newTab = true;
        ImGui::EndChild();
    }
    // Right-click on the header: a tab of your own (a copy of the chat to set up) or a whisper tab.
    if (newTab) ImGui::OpenPopup("##hdrmenu");
    static bool newTabNext = false;  // asked for in a menu (opened next frame, at this level)
    if (ImGui::BeginPopup("##hdrmenu")) {
        if (ImGui::MenuItem(UI(Tr(L"New tab")).c_str())) {
            const int at = NewCustomTab();
            if (at >= 0) g_tabSettings = at;
            else SetStatus(Tr(L"Six tabs of your own at most – close one first"), 6000);
        }
        if (ImGui::MenuItem(UI(Tr(L"New whisper tab …")).c_str())) newTabNext = true;
        ImGui::EndPopup();
    }
    if (newTabNext) ImGui::OpenPopup("##newtab");
    newTabNext = false;
    // The settings of a tab of your own (right-click on it): name, the channels it shows, where you write from it.
    if (g_tabSettings >= 0) {
        g_tabSettingsOpen = g_tabSettings;
        g_tabSettings = -1;
        ImGui::OpenPopup("##tabcfg");
    }
    if (ImGui::BeginPopup("##tabcfg")) {
        const int i = g_tabSettingsOpen;
        if (i < 0 || i >= static_cast<int>(g_tabs.size()) || !g_tabs[i].custom) {
            ImGui::CloseCurrentPopup();
        } else {
            WhisperTab& t = g_tabs[i];
            static char name[64] = {};
            if (ImGui::IsWindowAppearing()) strncpy_s(name, U8(t.name).c_str(), _TRUNCATE);
            bool changed = false;
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
            if (ImGui::InputText(UI(Tr(L"Name") + L"##tabname").c_str(), name, sizeof(name)) && !Trim(W(name)).empty()) {
                t.SetName(Trim(W(name)));
                changed = true;
            }
            ImGui::TextUnformatted(UI(Tr(L"Shows")).c_str());
            static const Channel kShown[] = {Channel::Say,   Channel::Map,     Channel::Party, Channel::Squad,
                                             Channel::Team,  Channel::Guild,   Channel::Whisper, Channel::System};
            for (int k = 0; k < 8; ++k) {
                bool on = (t.channels & ChannelBit(kShown[k])) != 0;
                if (k % 4) ImGui::SameLine(static_cast<float>(k % 4) * ImGui::GetFontSize() * 6.5f);
                if (ImGui::Checkbox((UI(ChannelLabel(kShown[k])) + "##ch" + std::to_string(k)).c_str(), &on)) {
                    t.channels = on ? (t.channels | ChannelBit(kShown[k])) : (t.channels & ~ChannelBit(kShown[k]));
                    if (kShown[k] == Channel::System)  // lines without a channel go with the system lines
                        t.channels = on ? (t.channels | ChannelBit(Channel::Unknown)) : (t.channels & ~ChannelBit(Channel::Unknown));
                    changed = true;
                }
            }
            static const Channel kWrite[] = {Channel::Unknown, Channel::Say, Channel::Map,  Channel::Party,
                                             Channel::Squad,   Channel::Team, Channel::Guild};
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
            if (ImGui::BeginCombo(UI(Tr(L"Write in") + L"##tabwrite").c_str(),
                                  t.write == Channel::Unknown ? UI(Tr(L"Active channel")).c_str() : UI(ChannelLabel(t.write)).c_str())) {
                for (Channel c : kWrite)
                    if (ImGui::Selectable(c == Channel::Unknown ? UI(Tr(L"Active channel")).c_str() : UI(ChannelLabel(c)).c_str(),
                                          c == t.write)) {
                        t.write = c;
                        changed = true;
                        if (g_activeTab == i) g_channel = c;
                    }
                ImGui::EndCombo();
            }
            if (ImGui::Button(UI(Tr(L"Remove tab")).c_str())) {
                g_tabs.erase(g_tabs.begin() + i);
                if (g_activeTab == i) LeaveWhisperTab();
                else if (i < g_activeTab) g_selectTab = --g_activeTab;
                changed = true;
                ImGui::CloseCurrentPopup();
            }
            if (changed) SaveCustomTabs();
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("##newtab")) {  // a whisper tab of your own: whom you want to write to
        static char name[96] = {};
        if (ImGui::IsWindowAppearing()) {
            name[0] = 0;
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::TextUnformatted(UI(Tr(L"New whisper tab")).c_str());
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14);
        bool go = ImGui::InputTextWithHint("##tabname", UI(Tr(L"Character or account name")).c_str(), name, sizeof(name),
                                           ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        go |= ImGui::Button(UI(Tr(L"Open")).c_str());
        if (go && !Trim(W(name)).empty()) {
            OpenWhisperTab(W(name));
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (Lay().header) {
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - rightW);
        if (g_cfg.voiceEnabled) {
            // Hold with the left button to speak; a right-click: on until a pause (or the next right-click).
            bool right = false;
            const ImU32 tint = g_listening ? IM_COL32(110, 205, 130, 255) : !g_listenPausedFor.empty() ? IM_COL32(240, 190, 90, 255) : 0;
            const bool clicked = IconButton("##mic", Icon::Mic, g_recording, &right, tint);
            static bool held = false;
            if (g_cfg.voiceActivation) {  // voice activation: a click switches listening on / off
                if (clicked) {
                    const bool on = g_listening || g_listenWanted;
                    g_listenUserOff = on;
                    lock.unlock();
                    if (on) StopListening();
                    else StartListening();
                    lock.lock();
                }
            } else if (ImGui::IsItemActivated()) {
                held = true;
                lock.unlock();
                TalkDown();
                lock.lock();
            } else if (held && !ImGui::IsItemActive()) {
                held = false;
                if (!g_micLocked && !g_cfg.voiceToggle) TalkUp();
            }
            if (right) {
                const bool stop = g_recording;
                g_micLocked = !stop;
                lock.unlock();
                if (stop) TalkUp();
                else TalkLocked();
                lock.lock();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", UI(g_cfg.voiceActivation ? Tr(L"Voice activation: click switches it on / off · right-click: speak until a pause")
                                                             : Tr(L"Hold to speak (or the push-to-talk key) · right-click: speak until a pause")).c_str());
            ImGui::SameLine(0, gap);
        }
        if (IconButton("##fold", Icon::Collapse, false, nullptr)) {
            g_collapsed = true;
            const ImVec2 p = ImGui::GetWindowPos(), s = ImGui::GetWindowSize();
            g_lastW = s.x;
            g_expandedH = s.y;
            g_anchorBottom = p.y + s.y;  // the small button takes its place on the next frame
            g_savedPos = p;
            g_buttonMoved = false;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", UI(Tr(L"Minimize chat")).c_str());
        ImGui::SameLine(0, gap);
        if (IconButton("##menu", Icon::Menu, false, nullptr)) ImGui::OpenPopup("kbmenu");
    }
    if (ImGui::BeginPopup("kbmenu")) {
        if (!Lay().header) {  // what the header offers: the tabs, minimise
            if (ImGui::MenuItem(UI(Tr(L"Chat")).c_str(), nullptr, g_activeTab < 0)) LeaveWhisperTab();
            for (int i = 0; i < static_cast<int>(g_tabs.size()); ++i)
                if (ImGui::MenuItem(g_tabs[i].Label(), nullptr, g_activeTab == i)) {
                    ActivateTab(i);
                    g_selectTab = i;  // the tab bar follows when the header comes back
                }
            if (ImGui::MenuItem(UI(Tr(L"Minimize chat")).c_str())) g_minimizeRequest = true;
            ImGui::Separator();
        }
        if (ImGui::MenuItem(UI(Tr(L"Options …")).c_str())) g_showOptions = true;
        if (ImGui::MenuItem(UI(Tr(L"Mini mode")).c_str(), nullptr, &g_mini)) g_cfg.SaveBool(L"NexusWindow", L"Mini", g_mini);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", UI(Tr(L"Nothing but the input with its help. Three quick clicks on it minimise it; right-click in it: Menu … brings the rest back.")).c_str());
        if (ImGui::MenuItem(UI(Tr(L"Compact view")).c_str(), nullptr, &g_compact)) g_cfg.SaveBool(L"NexusWindow", L"Compact", g_compact);
        if (ImGui::MenuItem(UI(Tr(L"Translate the chat once")).c_str()) && TranslateRecentLines(true) == 0)
            SetStatus(Tr(L"No chat lines of other players in view"), 4000);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", UI(Tr(L"What you see of the chat, into the language you write in – once, whatever the chat settings say. Again after you changed the language: in the new one.")).c_str());
        if (ImGui::MenuItem(UI(Tr(L"New whisper tab …")).c_str())) newTabNext = true;
        if (!g_chatEventsPresent && !g_chatEventsFile && ImGui::MenuItem(UI(Tr(L"Install Events: Chat (every channel) …")).c_str()))
            g_showOptions = true;
        ImGui::Separator();
        if (ImGui::MenuItem(UI(Tr(L"Hide (Ctrl+Alt+K shows it again)")).c_str())) g_show = false;
        ImGui::EndPopup();
    }
    if (Lay().header) {
        ImGui::SameLine();
        if (IconButton("##close", Icon::Close, false, nullptr)) g_show = false;
    }
    g_headerH = ImGui::GetCursorPosY() - st.ItemSpacing.y + st.WindowPadding.y;
    if (!g_recording && g_micLocked) g_micLocked = false;  // the recording ended (pause, maximum length)
    // Voice activation listens from the map on (unless you switched it off with the microphone).
    if (g_cfg.voiceEnabled && g_cfg.voiceActivation && !g_listenWanted && !g_listenUserOff) {
        lock.unlock();
        StartListening();
        lock.lock();
    }
    if (g_collapsed) {  // folded up: only the header; what was going on goes on (sending, dictation)
        if (g_autoSendPending && !g_edit.active && g_input[0]) {
            g_autoSendPending = false;
            StartSend();
        }
        lock.unlock();
        ImGui::End();
        return;
    }

    // ---- the chat (the newest at the bottom): name white – a click opens a whisper tab –, text in the channel colour,
    // the scroll bar at the left like GW2's own chat
    const float line = ImGui::GetTextLineHeightWithSpacing();
    const bool showPreview = Lay().preview && !g_compact, showWords = Lay().words;
    const float smallLine = ImGui::GetFontSize() * 0.85f + 2;
    // The input grows with what you write (its soft-wrapped lines) up to 8 lines and falls back once it is sent. A small
    // extra line at its bottom: the translation in compact view (only while something is translated).
    const int textLines = 1 + static_cast<int>(std::count(g_input, g_input + std::strlen(g_input), '\n'));
    g_boxLines = std::clamp(textLines, Lay().inputLines, std::max(8, Lay().inputLines));
    const float boxH = ImGui::GetTextLineHeight() * g_boxLines + st.FramePadding.y * 2 +
                       (g_compact && Translating() ? smallLine : 0);
    const float lowerH = (showPreview ? line * (g_layout.previewLines + 0.2f) + st.ItemSpacing.y : 0) +
                         (showWords ? ImGui::GetFrameHeightWithSpacing() : 0) +
                         (Lay().footer ? ImGui::GetFrameHeightWithSpacing() : 0) + boxH + st.ItemSpacing.y;
    if (!Lay().chat) {  // no chat: the window is exactly as high as its parts – its bottom stays where it is
        // + the thin bottom edge (above); never below ImGui's own minimum – it would raise the window again and the bottom
        // anchor below would push it down a little every frame (it crept 3 px per frame).
        // Whole pixels: ImGui rounds a window's size down – a fraction would move the window without resizing it.
        const float want = std::ceil(std::max(ImGui::GetCursorPosY() + lowerH - st.ItemSpacing.y + 4.0f, st.WindowMinSize.y));
        const ImVec2 p = ImGui::GetWindowPos(), sz = ImGui::GetWindowSize();
        // You drag its top (or bottom) edge: the height becomes whole lines of the input – "Lines of the input" in the
        // options follows at once; while the mouse is down the window keeps your size, then it fits to the lines.
        static float s_setH = -1;
        const bool sizing = s_setH > 0 && std::fabs(sz.y - s_setH) > 1.0f && ImGui::IsMouseDown(ImGuiMouseButton_Left);
        if (sizing) {
            const float lh = ImGui::GetTextLineHeight();
            const int lines = std::clamp(static_cast<int>(std::lround((sz.y - (want - lh * g_boxLines)) / lh)), 1, 8);
            if (lines != g_layout.inputLines) {
                g_layout.inputLines = lines;
                SaveLayout();
            }
        } else if (want != sz.y) {
            ImGui::SetWindowSize(ImVec2(sz.x, want));
            ImGui::SetWindowPos(ImVec2(p.x, p.y + sz.y - want));
        }
        s_setH = sizing ? sz.y : want;
        // Held down for a moment, then dragged: the whole window moves (not only at its edges) – a quick drag still
        // selects text. Not when the press was on an edge (that resizes).
        static ULONGLONG s_downAt = 0;
        static bool s_moving = false;
        const ImVec2 mp = ImGui::GetIO().MousePos;
        const float edge = 6.0f;
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
            mp.y > p.y + edge && mp.y < p.y + sz.y - edge && mp.x > p.x + edge && mp.x < p.x + sz.x - edge)
            s_downAt = GetTickCount64();
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            s_downAt = 0;
            s_moving = false;
        } else if (s_downAt && !s_moving && !sizing && GetTickCount64() - s_downAt > 250) {
            s_moving = true;
        }
        if (s_moving) {
            const ImVec2 d = ImGui::GetIO().MouseDelta;
            if (d.x != 0 || d.y != 0) ImGui::SetWindowPos(ImVec2(std::round(p.x + d.x), std::round(p.y + d.y)));
        }
    } else {  // the input grew past the window: the window grows upwards (its bottom stays) and shrinks back afterwards
        static float s_grown = 0;
        const float need = lowerH + (Lay().chat ? line * 2 : 0) - ImGui::GetContentRegionAvail().y;
        const float delta = need > 0.5f ? need : s_grown > 0 ? -std::min(s_grown, -need) : 0.0f;
        const ImVec2 p = ImGui::GetWindowPos(), sz = ImGui::GetWindowSize();
        const float newH = std::ceil(sz.y + delta);  // whole pixels (ImGui rounds sizes down; 0.8 px crept it upwards)
        if (std::fabs(delta) > 0.01f && newH != sz.y) {
            ImGui::SetWindowSize(ImVec2(sz.x, newH));
            ImGui::SetWindowPos(ImVec2(p.x, p.y + sz.y - newH));  // only what the size really changed
            s_grown = std::max(0.0f, s_grown + newH - sz.y);
        }
    }
    if (Lay().chat) {
        static float scrollY = 0, scrollMax = 0, viewH = 1, dragOffset = 0, lastScrollMax = 0;
        static bool atBottom = true;
        static int lastActiveTab = -999;
        const float h = std::max(line * 2, ImGui::GetContentRegionAvail().y - lowerH);
        const float sb = std::max(8.0f, st.ScrollbarSize * 0.8f);
        const ImVec2 track = ImGui::GetCursorScreenPos();
        float setScroll = -1;
        ImGui::InvisibleButton("##chatsb", ImVec2(sb, h));
        {
            const float grabH = scrollMax > 0 ? std::max(sb * 2, h * viewH / (viewH + scrollMax)) : h;
            const float grabY = track.y + (scrollMax > 0 ? (h - grabH) * scrollY / scrollMax : 0);
            if (ImGui::IsItemActivated()) {
                const float my = io.MousePos.y;
                dragOffset = my >= grabY && my <= grabY + grabH ? my - grabY : grabH * 0.5f;
            }
            if (ImGui::IsItemActive() && scrollMax > 0)
                setScroll = std::clamp((io.MousePos.y - dragOffset - track.y) / std::max(1.0f, h - grabH), 0.0f, 1.0f) * scrollMax;
            if (scrollMax > 0) {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(track, ImVec2(track.x + sb, track.y + h), ImGui::GetColorU32(ImGuiCol_ScrollbarBg), sb * 0.5f);
                const ImGuiCol gc = ImGui::IsItemActive() ? ImGuiCol_ScrollbarGrabActive
                                    : ImGui::IsItemHovered() ? ImGuiCol_ScrollbarGrabHovered : ImGuiCol_ScrollbarGrab;
                dl->AddRectFilled(ImVec2(track.x + 1, grabY), ImVec2(track.x + sb - 1, grabY + grabH), ImGui::GetColorU32(gc), sb * 0.5f);
            }
        }
        ImGui::SameLine(0, 4);
        ImVec4 chatBg = ImGui::GetStyleColorVec4(ImGuiCol_ChildBg);  // fades with the window, like everything but text
        chatBg.w = g_look.chatBg ? chatBg.w * std::clamp(g_cfg.opacity, 0, 255) / 255.0f : 0.0f;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, chatBg);
        ImGui::BeginChild("chat", ImVec2(0, h), false, ImGuiWindowFlags_NoScrollbar);
        ImGui::PopStyleColor();
        if (setScroll >= 0) ImGui::SetScrollY(setScroll);
        const ImVec2 childP0 = ImGui::GetWindowPos();
        const ImVec2 childP1 = ImVec2(childP0.x + ImGui::GetWindowSize().x, childP0.y + ImGui::GetWindowSize().y);
        const bool chatHovered = ImGui::IsMouseHoveringRect(childP0, childP1);
        const float wheel = io.MouseWheel;
        if (chatHovered && wheel > 0.0f) {
            atBottom = false;
        }
        if (g_activeTab != lastActiveTab) {
            lastActiveTab = g_activeTab;
            atBottom = true;
        }
        static const std::wstring s_emptyW;
        const WhisperTab* front = g_activeTab >= 0 && g_activeTab < static_cast<int>(g_tabs.size()) ? &g_tabs[g_activeTab] : nullptr;
        const std::wstring& tabName = front && !front->custom ? front->nameFolded : s_emptyW;
        const uint32_t tabChannels = front && front->custom ? front->channels : 0xFFFFFFFFu;
        if (g_lines.empty() && g_layout.infoText) {  // nothing arrived yet: what is ready, what is missing, where translation is switched on
            lock.unlock();
            RenderModules(true);
            lock.lock();
        }
        g_visibleLines.clear();
        for (const ChatLine& l : g_lines) {
            if (!tabName.empty() && (l.channel != Channel::Whisper || l.speakerFolded != tabName)) continue;
            if (!(tabChannels & ChannelBit(l.channel))) continue;  // a tab of your own: only its channels
            ImGui::PushID(static_cast<int>(l.id));
            if (!l.speaker.empty()) {  // the name in the channel colour – a click: whisper with that player in their tab
                LookText(l.speakerDisplay.c_str(), NameColorOf(l.channel), false);
                const bool canWhisper = !l.own || (l.outgoing && l.channel == Channel::Whisper);
                if (canWhisper && ImGui::IsItemHovered()) {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
                    ImGui::GetWindowDrawList()->AddLine(ImVec2(a.x, b.y - 1), ImVec2(b.x, b.y - 1),
                                                        ImGui::GetColorU32(NameColorOf(l.channel)));
                }
                if (canWhisper && ImGui::IsItemClicked()) OpenWhisperTab(l.speaker);
                ImGui::SameLine(0, ImGui::CalcTextSize(" ").x);
            }
            const std::wstring& shown = l.translated.empty() ? l.text : l.translated;
            const std::string& u8shown = l.translated.empty() ? l.u8text : l.u8translated;
            const auto& segs = l.translated.empty() ? l.textSegs : l.transSegs;
            bool clicked = false;
            if (segs.size() <= 1 && (segs.empty() || segs[0].kind == PieUI::ChatLinks::SegmentKind::Plain)) {
                LookText(u8shown.c_str(), ColorOf(l.channel), true);
            } else {
                clicked = RichLine(shown, segs, ColorOf(l.channel), l.receivedTime);
            }
            if (!l.translated.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", l.u8text.c_str());
            // A click on the text of a line someone else wrote: just this line into your language.
            if (!l.own && ImGui::IsItemVisible()) g_visibleLines.push_back(l.id);  // in view: "translate once" takes it
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) s_lineRightClick = true;
            if (!clicked && !l.own && l.translated.empty() && (ImGui::IsItemClicked() || ImGui::IsItemClicked(ImGuiMouseButton_Right)))
                g_worker.Post([id = l.id, t = l.text] { TranslateLine(id, t, true); });
            ImGui::PopID();
        }
        // Auto-scroll to bottom only when new lines arrive or content expands (e.g. item names resolved),
        // provided the user was at the bottom and is not actively scrolling up with the mouse wheel.
        const bool contentGrew = (scrollMax > lastScrollMax + 1.0f);
        const bool shouldScrollDown = (g_scrollDown || contentGrew);
        if (atBottom && shouldScrollDown && setScroll < 0 && (!chatHovered || wheel <= 0.0f)) {
            ImGui::SetScrollHereY(1.0f);
        }
        g_scrollDown = false;
        scrollY = ImGui::GetScrollY();
        scrollMax = ImGui::GetScrollMaxY();
        lastScrollMax = scrollMax;
        viewH = ImGui::GetWindowHeight();
        atBottom = scrollY >= scrollMax - 4;
        ImGui::EndChild();
    } else {  // no chat: the input stays the lowest line, the space above stays empty (see-through like the window)
        const float fill = ImGui::GetContentRegionAvail().y - lowerH;
        if (fill > 0) ImGui::Dummy(ImVec2(0, fill));
    }
    // A link from the chat opens only after your yes (http / https only).
    if (g_openLinkAsk) {
        g_openLinkAsk = false;
        ImGui::OpenPopup("###openlink");
    }
    if (ImGui::BeginPopupModal((UI(Tr(L"Leaving GW2Chatlator")) + "###openlink").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        // Like Discord: where it goes, big; the whole link small; you decide.
        ImGui::TextUnformatted(UI(Tr(L"This link goes to")).c_str());
        ImGui::TextColored(ImVec4(0.45f, 0.7f, 1.f, 1.f), "%s", U8(LinkHost(g_openLink)).c_str());
        ImGui::TextDisabled("%s", Fit(U8(g_openLink), ImGui::GetFontSize() * 30).c_str());
        ImGui::Spacing();
        ImGui::TextUnformatted(UI(Tr(L"Are you sure you want to open it?")).c_str());
        ImGui::Spacing();
        if (ImGui::Button(UI(Tr(L"Open link")).c_str())) {
            SafeOpenUrl(g_openLink);
            g_openLink.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(UI(Tr(L"Go back")).c_str()) || ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Escape))) {
            g_openLink.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // ---- the preview: what arrives in the GW2 chat, the back-translation small under it
    const bool previewCurrent = g_previewGen == g_inputGen && !g_preview.empty();
    if (showPreview) {
        // The same light field as the input below: the two belong together.
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
        ImGui::BeginChild("preview", ImVec2(0, line * (g_layout.previewLines + 0.2f)), false);
        if (g_recording) {
            ImGui::TextColored(kPreview, "%s", g_voiceLive.empty() ? "..." : U8(g_voiceLive).c_str());
        } else if (!g_input[0]) {
            // empty: no hint text here – the chat above is what you read
        } else if (!Translating()) {
            LookText(U8(ComposePrefix() + SanitizeChatText(g_wFlatInput)).c_str(), Bright(kPreview), true);
        } else if (previewCurrent) {
            LookText(U8(ComposePrefix() + g_preview).c_str(), Bright(kPreview), true);
            if (!g_back.empty()) LookText(U8(g_back).c_str(), Bright(ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)), true);
        } else if (g_layout.infoText) {
            ImGui::TextDisabled("%s", UI(Tr(L"translating …")).c_str());
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    // ---- the word bar: three cells across the line, what Tab takes in orange
    if (showWords) {
        const float w = ImGui::GetContentRegionAvail().x;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = ImGui::GetFrameHeight();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const bool have = g_cfg.suggestions && g_help.text == g_wFlatInput;
        for (int i = 0; i < 3; ++i) {
            const ImVec2 a(p.x + w * i / 3.0f, p.y), b(p.x + w * (i + 1) / 3.0f, p.y + h);
            if (i) dl->AddLine(ImVec2(a.x, a.y + h * 0.25f), ImVec2(a.x, b.y - h * 0.25f), ImGui::GetColorU32(ImGuiCol_Separator));
            if (!have || i >= static_cast<int>(g_help.bar.words.size())) continue;
            const std::string word = U8(g_help.bar.words[i]);
            ImGui::SetCursorScreenPos(a);
            ImGui::PushID(i);
            if (ImGui::InvisibleButton("##word", ImVec2(b.x - a.x, h))) TakeWord(g_help.bar.words[i]);
            const bool hot = ImGui::IsItemHovered();
            ImGui::PopID();
            if (hot) dl->AddRectFilled(a, b, ImGui::GetColorU32(ImGuiCol_FrameBgHovered));
            const ImVec2 size = ImGui::CalcTextSize(word.c_str());
            const bool pick = g_pick >= 0 ? i == g_pick : i == g_help.bar.autoIndex;
            dl->AddText(ImVec2((a.x + b.x - size.x) * 0.5f, a.y + (h - size.y) * 0.5f),
                        pick ? ImGui::GetColorU32(kAccent) : ImGui::GetColorU32(ImGuiCol_Text), word.c_str());
        }
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h + st.ItemSpacing.y));
    }

    // ---- the input line; momentary messages (status) appear inside it
    if (g_focusInput && !g_sending) {
        ImGui::SetKeyboardFocusHere();
        g_focusInput = false;
    }
    if (!Lay().footer && Lay().minButton) {  // no footer: its minimise button stands left of the input, at its bottom
        const float btn = ImGui::GetFrameHeight(), rowY = ImGui::GetCursorPosY();
        ImGui::SetCursorPosY(rowY + std::max(0.0f, boxH - btn));
        if (ImGui::Button(">##mininput", ImVec2(btn, btn))) g_minimizeRequest = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", UI(Tr(L"Minimize chat")).c_str());
        ImGui::SameLine();
        ImGui::SetCursorPosY(rowY);
    }
    const ImVec2 boxSize(-1, boxH);
    g_wrapWidth =ImGui::GetContentRegionAvail().x - st.FramePadding.x * 2 - st.ScrollbarSize - 4;
    const ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CtrlEnterForNewLine |
                                      ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackCharFilter |
                                      ImGuiInputTextFlags_CallbackAlways;
    ImGui::PushStyleColor(ImGuiCol_Text, Bright(ImGui::GetStyleColorVec4(ImGuiCol_Text)));  // your brightness here too
    const bool enter = ImGui::InputTextMultiline("##input", g_input, sizeof(g_input), boxSize, flags, InputCallback);
    ImGui::PopStyleColor();
    const std::string nowFlat = Flat(g_input);
    if (nowFlat != g_flatInput) {
        g_flatInput = nowFlat;
        g_wFlatInput = W(nowFlat);
    }
    const ImVec2 inputMin = ImGui::GetItemRectMin(), inputMax = ImGui::GetItemRectMax();
    const bool inputActive = ImGui::IsItemActive();
    // Minimise when not in use: nobody wrote, pointed at it or talked for a while – down to the small button.
    {
        const ULONGLONG now = GetTickCount64();
        const bool inUse = inputActive || g_sending || g_sendPending || g_focusInput || g_recording || g_showOptions ||
                           g_gameChatOpen || g_redirect != 0 || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) ||
                           ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        if (inUse || g_lastUse == 0) {
            g_lastUse = now;
        } else if (g_layout.autoMinimize && now - g_lastUse > static_cast<ULONGLONG>(g_layout.minimizeAfter) * 1000) {
            g_minimizeRequest = true;  // done on the main window next frame
            g_lastUse = 0;
        }
    }
    if (inputActive && !g_history.empty() && !g_edit.active) {
        const bool up = ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_UpArrow));
        const bool down = ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_DownArrow));
        const std::wstring& cur = g_wFlatInput;
        const bool browsing = cur.empty() || (g_historyPos >= 0 && g_historyPos < static_cast<int>(g_history.size()) &&
                                              cur == g_history[g_historyPos]);
        if ((up || down) && browsing) {
            g_historyPos = up ? std::min<int>(g_historyPos + 1, static_cast<int>(g_history.size()) - 1)
                              : std::max(g_historyPos - 1, -1);
            g_edit.active = true;
            g_edit.startByte = 0;
            g_edit.lenBytes = std::strlen(g_input);
            g_edit.insert = g_historyPos >= 0 ? U8(g_history[g_historyPos]) : std::string();
        }
    }
    // Right-click in the input (your own words only): the learning mode – what you corrected yourself while typing is
    // learned (autocorrection writes it right next time), and abbreviations get their meaning (for the translator).
    static bool teach = false, s_menuNext = false;
    // A right-click on a word you wrote: the learning mode for that word. Anywhere else in the input (no word under the
    // mouse, an empty input): the menu – so it is always there, in the mini mode too.
    if (ImGui::IsMouseHoveringRect(inputMin, inputMax) && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
        ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
        const std::wstring hit = WordAtMouse(ImVec2(inputMin.x + st.FramePadding.x, inputMin.y + st.FramePadding.y));
        if (!hit.empty()) {
            g_teachWord = hit;
            teach = true;
        } else if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) {
            ImGui::OpenPopup("kbmenu");
        }
    }
    else if (!Lay().header && !s_lineRightClick && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
             ImGui::IsMouseReleased(ImGuiMouseButton_Right) && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
        ImGui::OpenPopup("kbmenu");  // without the header the window's own right-click is the menu
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right) && !ImGui::IsMouseReleased(ImGuiMouseButton_Right))
        s_lineRightClick = false;
    // Three quick left clicks on the window (not in the input – there clicks place the caret and pick words –, not on a
    // button) minimise it (Fallen's idea): the way when header, footer and the minimise button are hidden.
    {
        static ULONGLONG s_clicks[3] = {};
        static ImVec2 s_firstAt;
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
            (!Lay().chat || (!ImGui::IsMouseHoveringRect(inputMin, inputMax) && !ImGui::IsAnyItemHovered())) &&
            !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) {
            const ULONGLONG now = GetTickCount64();
            const float radius = ImGui::GetFontSize() * 2;
            if (now - s_clicks[2] > 700 || std::abs(io.MousePos.x - s_firstAt.x) > radius || std::abs(io.MousePos.y - s_firstAt.y) > radius) {
                s_clicks[0] = s_clicks[1] = 0;  // a new series
                s_firstAt = io.MousePos;
            }
            s_clicks[0] = s_clicks[1];
            s_clicks[1] = s_clicks[2];
            s_clicks[2] = now;
            if (s_clicks[0] && now - s_clicks[0] <= 900) {
                s_clicks[0] = s_clicks[1] = s_clicks[2] = 0;
                g_minimizeRequest = true;  // done on the main window next frame
            }
        }
    }
    if (teach) {
        teach = false;
        ImGui::OpenPopup("##teach");
    }
    {  // docked to our window's top right corner, 1 px above it (below it when the window stands at the top of the
       // screen) – in every mode, never where the mouse was (it jumped and was pushed to the screen edge)
        const ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
        const bool below = wp.y < ImGui::GetIO().DisplaySize.y * 0.35f;
        ImGui::SetNextWindowPos(below ? ImVec2(wp.x + ws.x, wp.y + ws.y + 1) : ImVec2(wp.x + ws.x, wp.y - 1), ImGuiCond_Always,
                                below ? ImVec2(1, 0) : ImVec2(1, 1));
    }
    if (ImGui::BeginPopup("##teach")) {
        static char typed[64] = {}, fixed[64] = {}, word[64] = {}, meaning[160] = {};
        if (ImGui::IsWindowAppearing()) {  // the last word you typed, and what is known of it
            const std::wstring& text = g_wFlatInput;
            const std::vector<Span> spans = WordSpans(text);
            const std::wstring last = !g_teachWord.empty() ? g_teachWord  // the word you right-clicked
                                      : spans.empty() ? std::wstring() : text.substr(spans.back().start, spans.back().length);
            typed[0] = 0;
            strncpy_s(fixed, U8(last).c_str(), _TRUNCATE);
            strncpy_s(word, U8(last).c_str(), _TRUNCATE);
            strncpy_s(meaning, U8(g_myWords.MeaningOf(last)).c_str(), _TRUNCATE);
        }
        auto teachFix = [](const std::wstring& t, const std::wstring& f) {
            g_worker.Post([t, f] {
                const bool ok = g_spell && g_spell->TeachFix(t, f);
                std::lock_guard<std::mutex> lock(g_mu);
                SetStatus(ok ? TrF(L"Learned: {1} > {2}", {t, f}) : TrF(L"\"{1}\" is a real word – not learned as a typo", {t}), 6000);
            });
        };
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(UI(Tr(L"Learn your corrections")).c_str());
        ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - ImGui::GetFrameHeight()));
        if (IconButton("##teachclose", Icon::Close, false, nullptr)) ImGui::CloseCurrentPopup();  // the X: top right
        Hint(UI(Tr(L"What you corrected yourself while typing: learned once, autocorrection writes it right next time.")));
        int learned = -1;
        for (size_t i = 0; i < g_selfFixes.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s  >  %s", U8(g_selfFixes[i].typed).c_str(), U8(g_selfFixes[i].fixed).c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton(UI(Tr(L"Learn")).c_str())) learned = static_cast<int>(i);
            ImGui::PopID();
        }
        if (learned >= 0) {
            teachFix(g_selfFixes[learned].typed, g_selfFixes[learned].fixed);
            g_selfFixes.erase(g_selfFixes.begin() + learned);
        }
        if (g_selfFixes.empty()) ImGui::TextDisabled("%s", UI(Tr(L"No corrections of your own yet – or enter one:")).c_str());
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
        ImGui::InputTextWithHint("##typed", UI(Tr(L"I type")).c_str(), typed, sizeof(typed));
        ImGui::SameLine();
        ImGui::TextUnformatted(">");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
        ImGui::InputTextWithHint("##fixed", UI(Tr(L"I mean")).c_str(), fixed, sizeof(fixed));
        ImGui::SameLine();
        if (ImGui::Button(UI(Tr(L"Learn") + L"##fix").c_str()) && !Trim(W(typed)).empty() && !Trim(W(fixed)).empty()) {
            teachFix(Trim(W(typed)), Trim(W(fixed)));
            typed[0] = 0;
        }
        ImGui::Separator();
        ImGui::TextUnformatted(UI(Tr(L"Your word or abbreviation – what it means")).c_str());
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
        ImGui::InputText("##w", word, sizeof(word));
        ImGui::SameLine();
        ImGui::TextUnformatted("=");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14);
        const bool go = ImGui::InputTextWithHint("##m", UI(Tr(L"meaning, e.g. I think")).c_str(), meaning, sizeof(meaning),
                                                 ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if ((ImGui::Button(UI(Tr(L"Learn") + L"##mean").c_str()) || go) && !Trim(W(word)).empty()) {
            const std::wstring w = Trim(W(word)), m = Trim(W(meaning));
            if (m.empty()) g_myWords.Remove(w);
            else g_myWords.Set(w, m);
            SaveMyWords();
            g_worker.Post([w] { if (g_spell) g_spell->AddUserWord(w); });
            ++g_inputGen;  // translated again with it
            g_inputChanged = GetTickCount64();
            SetStatus(m.empty() ? TrF(L"\"{1}\" forgotten", {w}) : TrF(L"Learned: {1} = {2}", {w, m}), 6000);
        }
        Hint(UI(Tr(L"The translator gets the meaning; your text stays as you wrote it.")));
        if (!Lay().header) {  // without the header (maybe nothing but the input left): the way to the menu
            ImGui::Separator();
            if (ImGui::Selectable(UI(Tr(L"Menu …")).c_str())) {
                s_menuNext = true;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
    if (s_menuNext) {  // opened here, at the window's level, where the menu lives
        s_menuNext = false;
        ImGui::OpenPopup("kbmenu");
    }
    const ImVec4 clip(inputMin.x, inputMin.y, inputMax.x, inputMax.y);
    const ImVec2 textAt(inputMin.x + st.FramePadding.x, inputMin.y + st.FramePadding.y);
    if (const std::wstring rest = GhostRest(g_wFlatInput); !rest.empty() && inputActive) {  // what Space / Tab writes
        // In the line where the text ends (the box is its own child window: drawn on top, clipped to the box).
        const size_t inLen = std::strlen(g_input);
        const char* lastLine = std::strrchr(g_input, '\n');
        const int lineNo = static_cast<int>(std::count(g_input, g_input + inLen, '\n'));
        const ImVec2 typed = ImGui::CalcTextSize(lastLine ? lastLine + 1 : g_input);
        if (lineNo < g_boxLines)
            BoxText(ImVec2(textAt.x + typed.x, textAt.y + lineNo * ImGui::GetTextLineHeight()), clip,
                    ImGui::GetColorU32(ImGuiCol_TextDisabled), UI(rest));
    }
    // Status: in the empty box where you would write; while you write, a momentary message small at the bottom right.
    if (!g_input[0]) {
        if (g_recording && !showPreview && !g_voiceLive.empty())
            BoxText(textAt, clip, ImGui::GetColorU32(kPreview), U8(g_voiceLive));
        else {
            static std::wstring s_lastStatusW;
            static std::string s_lastStatusFit;
            static float s_lastStatusWidth = 0.0f;
            const float availW = inputMax.x - textAt.x - st.FramePadding.x;
            const std::wstring stW = StatusText();
            if (stW != s_lastStatusW || std::abs(availW - s_lastStatusWidth) > 1.0f) {
                s_lastStatusW = stW;
                s_lastStatusWidth = availW;
                s_lastStatusFit = Fit(UI(stW), availW);
            }
            if (g_layout.infoText || !g_status.empty())
                BoxText(textAt, clip, g_status.empty() ? ImGui::GetColorU32(ImGuiCol_TextDisabled) : ImGui::GetColorU32(kAccent),
                        s_lastStatusFit);
        }
    } else if (!g_status.empty()) {
        const std::string s = Fit(UI(g_status), (inputMax.x - inputMin.x) * 0.6f);
        const float w = ImGui::CalcTextSize(s.c_str()).x * 0.85f;
        BoxText(ImVec2(inputMax.x - w - st.FramePadding.x - st.ScrollbarSize, inputMax.y - smallLine - 1), clip,
                ImGui::GetColorU32(kAccent), s, 0.85f);
    }
    // Compact view: what the chat will get, small in the box's last line (instead of the preview above).
    if (g_compact && g_input[0] && Translating() && (previewCurrent || g_layout.infoText)) {
        const std::string t = previewCurrent ? "> " + U8(g_preview) : UI(Tr(L"translating …"));
        BoxText(ImVec2(textAt.x, inputMax.y - smallLine - 1), clip, ImGui::GetColorU32(kPreview),
                Fit(t, (inputMax.x - inputMin.x) * (g_status.empty() ? 0.95f : 0.38f)), 0.85f);
    }

    // ---- the footer: channel | write | send · translator | auto | counter
    if (Lay().footer) RenderFooter();
    // Changes of the text: a pause translates it; the typing help follows every key.
    if (g_flatInput != g_lastInput) {
        g_lastInput = g_flatInput;
        NoteSelfFix(g_wFlatInput);
        g_inputChanged = GetTickCount64();
        ++g_inputGen;
        g_back.clear();
        g_pick = -1;
        if (!g_helpRequested) {
            g_helpRequested = true;
            g_worker.Post(ComputeTyping);
        }
    }
    if (g_inputChanged && GetTickCount64() - g_inputChanged > 400 && !g_flatInput.empty() && Translating()) {
        g_inputChanged = 0;
        g_worker.Post([gen = g_inputGen, t = SanitizeChatText(g_wFlatInput)] { TranslateInput(gen, t); });
    }
    if (enter) StartSend();
    if (g_autoSendPending && !g_edit.active && g_input[0]) {  // the dictation is in the line now
        g_autoSendPending = false;
        StartSend();
    }
    lock.unlock();
    ImGui::End();
}

std::wstring FormatSeconds(int ms) {  // 1234 -> "1.2"
    wchar_t b[16];
    swprintf(b, 16, L"%.1f", ms / 1000.0);
    return b;
}

// A language combo over Languages(): `value` "" = `emptyLabel`.
bool LanguageCombo(const char* id, std::wstring& value, const std::wstring& emptyLabel) {
    std::string current = value.empty() ? U8(emptyLabel) : U8(value);
    for (const LangInfo& l : Languages())
        if (PrimaryLang(l.code) == PrimaryLang(value) && !value.empty()) current = U8(std::wstring(l.native) + L" (" + l.code + L")");
    bool changed = false;
    if (ImGui::BeginCombo(id, current.c_str())) {
        if (!emptyLabel.empty() && ImGui::Selectable(U8(emptyLabel).c_str(), value.empty())) {
            value.clear();
            changed = true;
        }
        for (const LangInfo& l : Languages()) {
            const std::string n = U8(std::wstring(l.native) + L" (" + l.code + L")");
            if (ImGui::Selectable(n.c_str(), value == l.code)) {
                value = l.code;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

// The CBA theme's own section (Options → Window, only when it is chosen; g_mu held): a start, the eight roles, the
// contrast each text reaches, automatic contrast, and the theme code to share.
void RenderCbaTheme() {
    if (g_cbaRoles.size() != kRoleCount) g_cbaRoles = CbaPreset(2);
    bool changed = false;
    ImGui::Indent();
    ImGui::TextUnformatted(UI(Tr(L"Start from")).c_str());
    const std::wstring presets[] = {Tr(L"Slate"), Tr(L"Grey"), Tr(L"CBA blue")};
    for (int i = 0; i < 3; ++i) {
        ImGui::SameLine();
        if (ImGui::SmallButton((UI(presets[i]) + "##cbapre" + std::to_string(i)).c_str())) {
            g_cbaRoles = CbaPreset(i);
            changed = true;
        }
    }
    const std::wstring kRoleNames[kRoleCount] = {Tr(L"Window background"), Tr(L"Fields (input, preview)"), Tr(L"Buttons"),
                                                 Tr(L"Buttons – active"), Tr(L"Tab – active"), Tr(L"Text"),
                                                 Tr(L"Text – quiet"), Tr(L"Accent (checks, sliders)")};
    for (int i = 0; i < kRoleCount; ++i) {
        const uint32_t v = g_cbaRoles[i];
        float c[3] = {((v >> 16) & 0xFF) / 255.f, ((v >> 8) & 0xFF) / 255.f, (v & 0xFF) / 255.f};
        if (ImGui::ColorEdit3((UI(kRoleNames[i]) + "##cbarole" + std::to_string(i)).c_str(), c,
                              ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_DisplayHex)) {
            g_cbaRoles[i] = (static_cast<uint32_t>(std::lround(c[0] * 255)) << 16) |
                            (static_cast<uint32_t>(std::lround(c[1] * 255)) << 8) | static_cast<uint32_t>(std::lround(c[2] * 255));
            changed = true;
        }
        // The contrast each text reaches as shown (WCAG): 4.5:1 for text, 3:1 for quiet text and accents.
        const std::vector<uint32_t>& r = g_cbaRoles;
        double ratio = 0, need = 0;
        if (i == kRoleText) {
            ratio = MinContrast(CbaShown(i), {r[kRoleBg], r[kRoleField], r[kRoleButton], r[kRoleButtonActive], r[kRoleTabActive]});
            need = 4.5;
        } else if (i == kRoleTextDim || i == kRoleAccent) {
            ratio = MinContrast(CbaShown(i), {r[kRoleBg], r[kRoleField]});
            need = 3.0;
        }
        if (need > 0) {
            char buf[32];
            snprintf(buf, sizeof buf, "%.1f:1", ratio);
            ImGui::SameLine();
            const bool ok = ratio >= need;
            ImGui::TextColored(ok ? ImVec4(0.24f, 0.81f, 0.43f, 1.f) : ImVec4(0.98f, 0.85f, 0.25f, 1.f), "%s%s", buf,
                               ok ? "" : (" " + UI(Tr(L"low"))).c_str());
        }
    }
    if (ImGui::Checkbox(UI(Tr(L"Automatic contrast (text always readable)")).c_str(), &g_cbaAutoContrast)) changed = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", UI(Tr(L"Text, quiet text and accents are moved just as far as needed to stand out on every surface (WCAG 4.5:1, 3:1).")).c_str());
    // A theme code to share (and to take over one): whatever is pasted is only read as colours.
    static char code[96] = {};
    if (ImGui::SmallButton(UI(Tr(L"Copy theme code")).c_str())) {
        ImGui::SetClipboardText(MakeThemeCode(g_cbaRoles).c_str());
        SetStatus(Tr(L"Theme code copied"), 3000);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14);
    ImGui::InputTextWithHint("##cbacode", "GW2C1:...", code, sizeof code);
    ImGui::SameLine();
    if (ImGui::SmallButton(UI(Tr(L"Apply") + L"##cbaapply").c_str())) {
        std::vector<uint32_t> roles;
        if (ParseThemeCode(code, kRoleCount, &roles)) {
            g_cbaRoles = roles;
            code[0] = 0;
            changed = true;
        } else {
            SetStatus(Tr(L"That is not a theme code"), 5000);
        }
    }
    ImGui::Unindent();
    if (changed) SaveLayout();
}

// Our options window, in tabs. "Save" applies.
void RenderOptionsContent() {
    static Config edit, opened;  // `opened`: g_cfg when the page was filled
    static bool loaded = false;
    static char keyBuf[256] = {}, regionBuf[64] = {}, urlBuf[256] = {}, mailBuf[128] = {};
    static std::vector<AudioDevice> mics;
    static bool forgetAsked = false;
    if (!loaded) {
        std::lock_guard<std::mutex> lock(g_mu);
        edit = opened = g_cfg;
        loaded = true;
        const std::string key = U8(edit.engine == Engine::DeepL ? edit.deeplKey : edit.engine == Engine::Google ? edit.googleKey
                                   : edit.engine == Engine::Microsoft ? edit.msKey : edit.libreKey);
        strncpy_s(keyBuf, key.c_str(), _TRUNCATE);
        strncpy_s(regionBuf, U8(edit.msRegion).c_str(), _TRUNCATE);
        strncpy_s(urlBuf, U8(edit.libreUrl).c_str(), _TRUNCATE);
        strncpy_s(mailBuf, U8(edit.basicEmail).c_str(), _TRUNCATE);
        mics = AudioCapture::Devices();
        forgetAsked = false;
    }
    const float wide = 280;
    auto combo = [&](const char* id, const std::vector<std::string>& items, int* sel) {
        bool changed = false;
        ImGui::SetNextItemWidth(wide);
        if (ImGui::BeginCombo(id, items[std::clamp(*sel, 0, static_cast<int>(items.size()) - 1)].c_str())) {
            for (int i = 0; i < static_cast<int>(items.size()); ++i)
                if (ImGui::Selectable(items[i].c_str(), i == *sel)) {
                    *sel = i;
                    changed = true;
                }
            ImGui::EndCombo();
        }
        return changed;
    };
    auto TabFlag = [](int i) { return g_optionsTab == i ? ImGuiTabItemFlags_SetSelected : 0; };
    if (ImGui::BeginTabBar("##tabs")) {
        // ---- General
        if (ImGui::BeginTabItem(UI(Tr(L"General")).c_str(), nullptr, TabFlag(0))) {
            ImGui::SetNextItemWidth(wide);
            LanguageCombo(UI(Tr(L"Language of this window") + L"##ui").c_str(), edit.uiLangCode, Tr(L"like Windows"));
            ImGui::Checkbox(UI(Tr(L"Show the window")).c_str(), &g_show);
            Hint(UI(Tr(L"Keys (show / hide: Ctrl+Alt+K, push to talk, focus the input): Nexus → Keybinds → GW2 Keyboard.")));
            ImGui::Separator();
            ImGui::TextUnformatted(UI(Tr(L"Modules")).c_str());
            RenderModules(false);
            ImGui::EndTabItem();
        }
        // ---- Window: its building blocks, size, transparency, how the chat text looks
        if (ImGui::BeginTabItem(UI(Tr(L"Window")).c_str(), nullptr, TabFlag(1))) {
            {
                std::lock_guard<std::mutex> lock(g_mu);
                const std::wstring names[] = {Tr(L"Nexus (its own theme)"), Tr(L"Slate"), Tr(L"Grey"), Tr(L"CBA (your own colours)")};
                int ti = static_cast<int>(g_theme);
                ImGui::SetNextItemWidth(wide);
                if (ImGui::BeginCombo(UI(Tr(L"Theme of the window") + L"##theme").c_str(), UI(names[ti]).c_str())) {
                    for (int i = 0; i < 4; ++i)
                        if (ImGui::Selectable(UI(names[i]).c_str(), i == ti)) {
                            g_theme = static_cast<Theme>(i);
                            SaveLayout();
                        }
                    ImGui::EndCombo();
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", UI(Tr(L"Only the window itself: frames, tabs, buttons. The chat keeps its channel colours.")).c_str());
                if (g_theme == Theme::Cba) RenderCbaTheme();
            }
            // Size and transparency take effect at once and are kept (like the chat text below), not only on "Save".
            int pt = std::clamp(edit.fontPt, 8, 28);
            ImGui::SetNextItemWidth(wide);
            if (ImGui::SliderInt(UI(Tr(L"Text size") + L" (pt)##size").c_str(), &pt, 8, 28)) {
                std::lock_guard<std::mutex> lock(g_mu);
                edit.fontPt = g_cfg.fontPt = pt;
                g_cfg.SaveValue(L"Window", L"FontPt", std::to_wstring(pt));
            }
            if (ImGui::IsItemDeactivatedAfterEdit()) RequestTextFont(pt);  // built once you let go (scaled meanwhile)
            int transparency = 100 - (std::clamp(edit.opacity, 0, 255) * 100 + 127) / 255;
            ImGui::SetNextItemWidth(wide);
            if (ImGui::SliderInt(UI(Tr(L"Transparency") + L" (%)##alpha").c_str(), &transparency, 0, 100)) {
                std::lock_guard<std::mutex> lock(g_mu);
                edit.opacity = g_cfg.opacity = (100 - transparency) * 255 / 100;
                g_cfg.SaveValue(L"Window", L"Opacity", std::to_wstring(g_cfg.opacity));
            }
            {
                std::lock_guard<std::mutex> lock(g_mu);
                if (ImGui::Checkbox(UI(Tr(L"Snap to the screen edges")).c_str(), &g_nx.snap)) SaveNexusOptions();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", UI(Tr(L"Moved near an edge of the screen, the window sits right on it.")).c_str());
            ImGui::Separator();
            ImGui::TextUnformatted(UI(Tr(L"Window layout")).c_str());
            {
                std::lock_guard<std::mutex> lock(g_mu);
                // From top to bottom, as they stand in the window; each one can go.
                bool changed = ImGui::Checkbox(UI(Tr(L"Header")).c_str(), &g_layout.header);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", UI(Tr(L"Hidden: a right-click on the window opens the menu (tabs, minimise, options).")).c_str());
                changed |= ImGui::Checkbox(UI(Tr(L"Chat")).c_str(), &g_layout.chat);
                changed |= ImGui::Checkbox(UI(Tr(L"Translation preview")).c_str(), &g_layout.preview);
                changed |= ImGui::Checkbox(UI(Tr(L"Word bar")).c_str(), &g_layout.words);
                changed |= ImGui::Checkbox(UI(Tr(L"Footer")).c_str(), &g_layout.footer);
                changed |= ImGui::Checkbox(UI(Tr(L"Minimize button")).c_str(), &g_layout.minButton);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", UI(Tr(L"The small > in the footer (without the footer: left of the input). Hidden: three quick clicks on the window minimise it, or the header, or the right-click menu.")).c_str());
                changed |= ImGui::Checkbox(UI(Tr(L"Info texts")).c_str(), &g_layout.infoText);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", UI(Tr(L"The grey status lines: what is going on, the modules, the counter. The grey word of the prediction stays.")).c_str());
                ImGui::SetNextItemWidth(wide);
                changed |= ImGui::SliderInt(UI(Tr(L"Lines of the input") + L"##inl").c_str(), &g_layout.inputLines, 1, 8);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", UI(Tr(L"Longer text makes the input grow (up to 8 lines) until it is sent.")).c_str());
                ImGui::SetNextItemWidth(wide);
                changed |= ImGui::SliderInt(UI(Tr(L"Lines of the preview") + L"##prl").c_str(), &g_layout.previewLines, 1, 8);
                ImGui::Separator();
                ImGui::TextUnformatted(UI(Tr(L"When not in use")).c_str());
                changed |= ImGui::Checkbox(UI(Tr(L"Minimize after") + L"##automin").c_str(), &g_layout.autoMinimize);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", UI(Tr(L"Nobody writes, points at the window or talks: it goes down to the small button.")).c_str());
                ImGui::SameLine();
                ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
                changed |= ImGui::SliderInt(UI(Tr(L"seconds") + L"##minafter").c_str(), &g_layout.minimizeAfter, 2, 120);
                changed |= ImGui::Checkbox(UI(Tr(L"Open again when GW2's chat opens (Enter)")).c_str(), &g_layout.autoOpen);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", UI(Tr(L"Only opens it. The focus key (Nexus > Keybinds) always opens it; taking over the focus is its own setting (Writing).")).c_str());
                if (changed) SaveLayout();
            }
            ImGui::Separator();
            ImGui::TextUnformatted(UI(Tr(L"Chat text")).c_str());
            {
                std::lock_guard<std::mutex> lock(g_mu);
                bool changed = ImGui::Checkbox(UI(Tr(L"Dark outline (readable when see-through)")).c_str(), &g_look.glow);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", UI(Tr(L"A dark halo around the letters, like the game draws its chat – readable on any background.")).c_str());
                ImGui::SameLine();
                changed |= ImGui::Checkbox(UI(Tr(L"GW2 colours")).c_str(), &g_look.gameColors);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", UI(Tr(L"The name in the channel colour, the message in a light tint of it – as in the game. Off: the message in the channel colour.")).c_str());
                ImGui::SameLine();
                changed |= ImGui::Checkbox(UI(Tr(L"Background behind the chat")).c_str(), &g_look.chatBg);

                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", UI(Tr(L"Off: only the text stands over the game – with the dark outline it stays readable.")).c_str());
                ImGui::SetNextItemWidth(wide);
                changed |= ImGui::SliderInt(UI(Tr(L"Brightness") + L" (%)##bright").c_str(), &g_look.brightness, 60, 160);
                if (changed) SaveLayout();
                // Item and place names in the game's language (the GW2 API knows these five).
                static const wchar_t* kNames[][2] = {{L"", L"like I write"}, {L"en", L"English"}, {L"de", L"Deutsch"},
                                                     {L"fr", L"Français"}, {L"es", L"Español"}, {L"zh", L"中文"}};
                int ni = 0;
                for (int i = 0; i < 6; ++i)
                    if (g_nx.namesLang == kNames[i][0]) ni = i;
                ImGui::SetNextItemWidth(wide);
                if (ImGui::BeginCombo(UI(Tr(L"Item and place names") + L"##names").c_str(),
                                      ni == 0 ? UI(Tr(L"like I write")).c_str() : U8(kNames[ni][1]).c_str())) {
                    for (int i = 0; i < 6; ++i)
                        if (ImGui::Selectable(i == 0 ? UI(Tr(L"like I write")).c_str() : U8(kNames[i][1]).c_str(), i == ni)) {
                            g_nx.namesLang = kNames[i][0];
                            SaveNexusOptions();
                        }
                    ImGui::EndCombo();
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", UI(Tr(L"Choose the language your game runs in – item links and waypoints then read as in its own chat.")).c_str());
            }
            ImGui::EndTabItem();
        }
        // ---- Writing
        if (ImGui::BeginTabItem(UI(Tr(L"Writing")).c_str(), nullptr, TabFlag(2))) {
            ImGui::SetNextItemWidth(wide);
            LanguageCombo(UI(Tr(L"I write in") + L"##writein").c_str(), edit.writeIn, Tr(L"like Windows"));
            ImGui::Checkbox(UI(Tr(L"Spell checking (Windows, offline)")).c_str(), &edit.spellEnabled);
            int ac = static_cast<int>(edit.autoCorrect);
            if (combo(UI(Tr(L"Autocorrection") + L"##ac").c_str(),
                      {UI(Tr(L"Off")), UI(Tr(L"Safe")), UI(Tr(L"Like a phone keyboard"))}, &ac))
                edit.autoCorrect = static_cast<AutoCorrectMode>(ac);
            ImGui::Checkbox(UI(Tr(L"Word suggestions while typing")).c_str(), &edit.suggestions);
            int learn = !edit.learnWords ? 2 : edit.learnLater ? 1 : 0;
            if (combo("##learn", {UI(Tr(L"Learn from what I send – while playing")), UI(Tr(L"Learn from what I send – later")),
                                  UI(Tr(L"Do not learn"))},
                      &learn)) {
                edit.learnWords = learn != 2;
                edit.learnLater = learn == 1;
            }
            if (!forgetAsked) {
                if (ImGui::Button(UI(Tr(L"Delete everything learned…")).c_str())) forgetAsked = true;
            } else {
                ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.35f, 1.f), "%s", UI(Tr(L"Really delete everything learned?")).c_str());
                ImGui::SameLine();
                if (ImGui::Button(UI(Tr(L"Yes")).c_str())) {
                    g_worker.Post([] { if (g_spell) g_spell->ForgetAll(); });
                    forgetAsked = false;
                }
                ImGui::SameLine();
                if (ImGui::Button(UI(Tr(L"No")).c_str())) forgetAsked = false;
            }
            ImGui::Checkbox(UI(Tr(L"Show the back-translation of my message")).c_str(), &edit.backTranslate);
            int enterDoes = edit.copyOnly ? 1 : 0;
            if (combo(UI(Tr(L"Enter does") + L"##enter").c_str(),
                      {UI(Tr(L"Send into the GW2 chat")), UI(Tr(L"Only copy (not a single key reaches the game)"))}, &enterDoes))
                edit.copyOnly = enterDoes == 1;
            ImGui::TextUnformatted(UI(Tr(L"Focus behaviour")).c_str());
            ImGui::Checkbox(UI(Tr(L"After sending, keep typing here")).c_str(), &edit.returnFocus);
            Hint(UI(Tr(L"The input keeps the keyboard after Enter – the next message goes right after.")));
            ImGui::Checkbox(UI(Tr(L"Take over the whole focus when the GW2 chat opens")).c_str(), &edit.focusOnGameChat);
            Hint(UI(Tr(L"Enter in the game (opening its chat line) brings you into this input instead.")));
            ImGui::EndTabItem();
        }
        // ---- Voice
        if (ImGui::BeginTabItem(UI(Tr(L"Voice")).c_str(), nullptr, TabFlag(3))) {
            ImGui::Checkbox(UI(Tr(L"Voice input")).c_str(), &edit.voiceEnabled);
            RenderModules(false);
            ImGui::Separator();
            const VoiceCatalog catalog = LoadVoiceCatalog(edit.dataDir);
            const VoiceModelInfo* chosen = catalog.Find(U8(edit.voiceModel));
            if (!chosen) chosen = catalog.Recommended();
            ImGui::SetNextItemWidth(wide);
            if (ImGui::BeginCombo(UI(Tr(L"Model") + L"##model").c_str(), chosen ? chosen->name.c_str() : "-")) {
                for (const VoiceModelInfo& m : catalog.models) {
                    const std::string label = m.name + "  (" + std::to_string(m.bytes / (1024 * 1024)) + " MB)" +
                                              (VoiceModelInstalled(edit.VoiceModelDir(), m) ? "  [OK]" : "");
                    if (ImGui::Selectable(label.c_str(), chosen && chosen->id == m.id)) edit.voiceModel = W(m.id);
                }
                ImGui::EndCombo();
            }
            if (chosen && !VoiceModelInstalled(edit.VoiceModelDir(), *chosen)) {
                const int pct = g_downloadPct;
                if (pct >= 0) ImGui::ProgressBar(pct / 100.f, ImVec2(wide, 0));
                else if (ImGui::Button(UI(Tr(L"Download the model")).c_str())) StartModelDownload(*chosen);
            }
            // The installed models: what each needed when it last recognized you; use one, or remove what does not suit.
            static std::string removeAsked;
            for (const VoiceModelInfo& m : catalog.models) {
                if (!VoiceModelInstalled(edit.VoiceModelDir(), m)) continue;
                ImGui::PushID(m.id.c_str());
                const bool inUse = chosen && chosen->id == m.id;
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(inUse ? ImVec4(0.45f, 0.85f, 0.45f, 1.f) : ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s  (%d MB)",
                                   m.name.c_str(), static_cast<int>(m.bytes / (1024 * 1024)));
                ImGui::SameLine();
                VoiceRun run;
                {
                    std::lock_guard<std::mutex> lock(g_mu);
                    if (auto it = g_voiceRuns.find(W(m.id)); it != g_voiceRuns.end()) run = it->second;
                }
                char ms[96];
                if (run.rec > 0)
                    snprintf(ms, sizeof(ms), "%s", UI(TrF(L"peak {1} % CPU (highest {2} %) · {3} s for {4} s speech",
                                                          {std::to_wstring(run.peak), std::to_wstring(run.maxPeak),
                                                           FormatSeconds(run.rms), FormatSeconds(run.rec)})).c_str());
                else
                    snprintf(ms, sizeof(ms), "%s", UI(Tr(L"not measured yet")).c_str());
                ImGui::TextDisabled("%s", ms);
                if (!inUse) {
                    ImGui::SameLine();
                    if (ImGui::SmallButton(UI(Tr(L"Use")).c_str())) edit.voiceModel = W(m.id);
                }
                ImGui::SameLine();
                if (removeAsked != m.id) {
                    if (ImGui::SmallButton(UI(Tr(L"Remove")).c_str())) removeAsked = m.id;
                } else {
                    if (ImGui::SmallButton(UI(Tr(L"Really remove")).c_str())) {
                        const bool ok = DeleteVoiceModel(edit.VoiceModelDir(), m);
                        std::lock_guard<std::mutex> lock(g_mu);
                        SetStatus(ok ? TrF(L"{1} removed", {W(m.name)}) : TrF(L"{1} is in use – try again after Unload", {W(m.name)}), 8000);
                        removeAsked.clear();
                        g_modulesNext = 0;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton(UI(Tr(L"No")).c_str())) removeAsked.clear();
                }
                ImGui::PopID();
            }
            {
                std::lock_guard<std::mutex> lock(g_mu);
                if (ImGui::Checkbox(UI(Tr(L"Too slow twice in a row: use the next smaller model")).c_str(), &g_nx.voiceFallback))
                    SaveNexusOptions();
            }
            Hint(UI(Tr(L"Too slow = recognizing took longer than you spoke (and over 2 s). Peak = the highest share of the "
                       L"whole CPU the voice module used.")));
            // Where it all lies.
            auto folder = [&](const std::wstring& label, const std::wstring& path) {
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("%s", UI(label).c_str());
                ImGui::SameLine();
                ImGui::TextUnformatted(Fit(U8(path), ImGui::GetContentRegionAvail().x - ImGui::GetFontSize() * 9).c_str());
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", U8(path).c_str());
                ImGui::SameLine();
                ImGui::PushID(U8(path).c_str());
                if (ImGui::SmallButton(UI(Tr(L"Open folder")).c_str())) {
                    EnsureDir(path);
                    std::thread([path] {
                        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
                        ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                        CoUninitialize();
                    }).detach();
                }
                ImGui::PopID();
            };
            folder(Tr(L"Models:"), edit.VoiceModelDir());
            folder(Tr(L"Voice module:"), KeyboardDir());
            std::vector<std::string> langs{UI(Tr(L"Like I write")), UI(Tr(L"Detect it"))};
            std::vector<std::wstring> codes{L"", L"auto"};
            for (const LangInfo& l : Languages()) {
                langs.push_back(U8(l.native));
                codes.push_back(l.code);
            }
            int li = 0;
            for (size_t i = 0; i < codes.size(); ++i)
                if (codes[i] == edit.voiceLang) li = static_cast<int>(i);
            if (combo(UI(Tr(L"Language") + L"##vlang").c_str(), langs, &li)) edit.voiceLang = codes[li];
            ImGui::Checkbox(UI(Tr(L"Translate into English (whisper)")).c_str(), &edit.voiceTranslate);
            ImGui::Checkbox(UI(Tr(L"Live text while speaking (more CPU)")).c_str(), &edit.voiceLive);
            ImGui::SetNextItemWidth(wide);
            ImGui::SliderInt(UI(Tr(L"Longest recording") + L" (s)##max").c_str(), &edit.voiceMaxSec, 5, 60);
            std::vector<std::string> threads{UI(Tr(L"Automatic"))};
            for (int i = 1; i <= 8; ++i) threads.push_back(std::to_string(i));
            int ti = std::clamp(edit.voiceThreads, 0, 8);
            if (combo(UI(Tr(L"CPU threads") + L"##thr").c_str(), threads, &ti)) edit.voiceThreads = ti;
            Hint(UI(Tr(L"Automatic: the least busy cores – the ones GW2 does not use.")));
            ImGui::Checkbox(UI(Tr(L"Send right after speaking (releasing the key counts as your Enter)")).c_str(), &edit.voiceAutoSend);
            Hint(UI(Tr(L"Runs on this PC – no sound leaves it, nothing is recorded to disk.")));
            ImGui::EndTabItem();
        }
        // ---- Microphone
        if (ImGui::BeginTabItem(UI(Tr(L"Microphone")).c_str(), nullptr, TabFlag(4))) {
            int mode = edit.voiceActivation ? 2 : edit.voiceToggle ? 1 : 0;
            static std::vector<std::string> s_modeLabels;
            static UiLang s_modeLang = static_cast<UiLang>(-1);
            if (s_modeLang != g_cfg.uiLang || s_modeLabels.empty()) {
                s_modeLang = g_cfg.uiLang;
                s_modeLabels = {UI(Tr(L"Hold the button or key while you speak")),
                                UI(Tr(L"Press once – a pause ends it")),
                                UI(Tr(L"Voice activation – starts when you speak"))};
            }
            if (combo(UI(Tr(L"Starting") + L"##mode").c_str(), s_modeLabels, &mode)) {
                edit.voiceToggle = mode == 1;
                edit.voiceActivation = mode == 2;
            }
            // The push-to-talk key: ours (GW2 does not get it). "Set key ..." takes the next key you press.
            {
                std::lock_guard<std::mutex> lock(g_mu);
                if (!g_capturedKey.empty()) {
                    if (g_capturedKey != L"-") edit.voiceGlobalKey = g_capturedKey;
                    g_capturedKey.clear();
                }
            }
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(UI(Tr(L"Push-to-talk key")).c_str());
            ImGui::SameLine(150);
            ImGui::TextUnformatted(g_captureKey ? UI(Tr(L"press a key … (Esc: cancel)")).c_str()
                                                : UI(edit.voiceGlobalKey.empty() ? Tr(L"none") : edit.voiceGlobalKey).c_str());
            ImGui::SameLine(330);
            if (ImGui::Button(UI(Tr(L"Set key…")).c_str())) g_captureKey = true;
            ImGui::SameLine();
            if (ImGui::Button(UI(Tr(L"None")).c_str())) edit.voiceGlobalKey.clear();
            Hint(UI(Tr(L"Works while GW2 is in front; GW2 does not get this key. The Nexus keybind (Keybinds > GW2 Keyboard) works too.")));
            std::vector<std::string> names{UI(Tr(L"Windows default"))};
            int mi = 0;
            for (size_t i = 0; i < mics.size(); ++i) {
                names.push_back(U8(mics[i].name));
                if (mics[i].id == edit.voiceDevice) mi = static_cast<int>(i) + 1;
            }
            if (combo(UI(Tr(L"Microphone") + L"##dev").c_str(), names, &mi))
                edit.voiceDevice = mi == 0 ? std::wstring() : mics[mi - 1].id;
            int boost = std::clamp(edit.voiceBoost, 0, 3);
            static std::vector<std::string> s_boostLabels;
            static UiLang s_boostLang = static_cast<UiLang>(-1);
            if (s_boostLang != g_cfg.uiLang || s_boostLabels.empty()) {
                s_boostLang = g_cfg.uiLang;
                s_boostLabels = {UI(Tr(L"Off")), "+6 dB", "+12 dB", UI(Tr(L"Automatic"))};
            }
            if (combo(UI(Tr(L"Microphone boost") + L"##boost").c_str(), s_boostLabels, &boost))
                edit.voiceBoost = boost;
            ImGui::SetNextItemWidth(wide);
            ImGui::SliderInt(UI(Tr(L"Pause to end") + L" (ms)##pause").c_str(), &edit.voicePauseMs, 400, 3000);
            Hint(UI(Tr(L"Right-click on the microphone: on until a pause.")));
            ImGui::Separator();
            ImGui::TextUnformatted(UI(Tr(L"Voice activation")).c_str());
            if (!edit.voiceActivation) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
            Hint(UI(Tr(L"Voice activation is not exclusive: everything you say goes into the box, and Discord keeps hearing it.")));
            ImGui::SetNextItemWidth(wide);
            ImGui::SliderInt(UI(Tr(L"Sensitivity") + L"##sens").c_str(), &edit.voiceSensitivity, 0, 100);
            ImGui::SetNextItemWidth(wide);
            ImGui::SliderInt(UI(Tr(L"Pre-roll") + L" (ms)##pre").c_str(), &edit.voicePreRollMs, 0, 1000);
            ImGui::SetNextItemWidth(wide);
            ImGui::SliderInt(UI(Tr(L"Speech to start") + L" (ms)##min").c_str(), &edit.voiceMinMs, 100, 1000);
            ImGui::Checkbox(UI(Tr(L"Pause while a voice chat uses the microphone (Discord, TeamSpeak …)")).c_str(), &edit.voiceChatPause);
            if (!edit.voiceActivation) ImGui::PopStyleVar();
            ImGui::EndTabItem();
        }
        // ---- Translator
        if (ImGui::BeginTabItem(UI(Tr(L"Translator")).c_str(), nullptr, TabFlag(5))) {
            {
                std::lock_guard<std::mutex> lock(g_mu);
                if (ImGui::Checkbox(UI(Tr(L"Translate the chat")).c_str(), &g_nx.translateChat)) {
                    SaveNexusOptions();
                    if (g_nx.translateChat) TranslateRecentLines(false);  // what is already there too
                }
            }
            Hint(UI(Tr(L"Foreign lines of other players into your language. Off: a click on a line translates just that one.")));
            ImGui::Checkbox(UI(Tr(L"Do not translate the language I write in")).c_str(), &edit.skipTypingLang);
            // Languages you understand: never translated by themselves (a click still does) – only the others are.
            ImGui::TextUnformatted(UI(Tr(L"Languages I understand (not translated by themselves):")).c_str());
            {
                static const wchar_t* kCommon[] = {L"EN", L"DE", L"FR", L"ES", L"IT", L"PT", L"NL", L"PL",
                                                   L"RU", L"TR", L"ZH", L"KO", L"JA", L"AR"};
                int column = 0;
                for (const wchar_t* code : kCommon) {
                    bool on = false;
                    for (const std::wstring& u : edit.understoodLangs)
                        if (PrimaryLang(u) == PrimaryLang(code)) on = true;
                    if (column % 7) ImGui::SameLine(static_cast<float>(column % 7) * ImGui::GetFontSize() * 4.2f);
                    ++column;
                    if (ImGui::Checkbox((U8(code) + "##und").c_str(), &on)) {
                        auto& list = edit.understoodLangs;
                        list.erase(std::remove_if(list.begin(), list.end(),
                                                  [&](const std::wstring& u) { return PrimaryLang(u) == PrimaryLang(code); }),
                                   list.end());
                        if (on) list.push_back(code);
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", U8(NativeName(code)).c_str());
                }
            }
            Hint(UI(Tr(L"So the keyboard stays in the background: only a French or Spanish line (whatever you did not tick) "
                       L"is translated. A click on a name opens a whisper tab that answers in that player's language.")));
            ImGui::Checkbox(UI(Tr(L"Translate my text")).c_str(), &edit.translateWriting);
            Hint(UI(Tr(L"What you write goes into the chat in the language chosen at \"Send\" below the input.")));
            static const Engine kEngines[] = {Engine::Auto, Engine::Basic, Engine::DeepL, Engine::Google, Engine::Microsoft, Engine::Libre};
            static const wchar_t* kNames[] = {L"Automatic", L"MyMemory (free)", L"DeepL", L"Google", L"Microsoft", L"LibreTranslate"};
            int ei = 0;
            for (int i = 0; i < 6; ++i)
                if (kEngines[i] == edit.engine) ei = i;
            static std::vector<std::string> s_engines;
            static UiLang s_engineLang = static_cast<UiLang>(-1);
            if (s_engineLang != g_cfg.uiLang || s_engines.empty()) {
                s_engineLang = g_cfg.uiLang;
                s_engines.clear();
                for (const wchar_t* n : kNames) s_engines.push_back(UI(Tr(n)));
            }
            if (combo(UI(Tr(L"Translator") + L"##engine").c_str(), s_engines, &ei)) {
                edit.engine = kEngines[ei];
                const std::wstring k = edit.engine == Engine::DeepL ? edit.deeplKey : edit.engine == Engine::Google ? edit.googleKey
                                       : edit.engine == Engine::Microsoft ? edit.msKey : edit.libreKey;
                strncpy_s(keyBuf, U8(k).c_str(), _TRUNCATE);
            }
            if (edit.engine == Engine::DeepL || edit.engine == Engine::Google || edit.engine == Engine::Microsoft ||
                edit.engine == Engine::Libre) {
                ImGui::SetNextItemWidth(wide);
                ImGui::InputText(UI(Tr(L"API key") + L"##key").c_str(), keyBuf, sizeof(keyBuf), ImGuiInputTextFlags_Password);
            }
            if (edit.engine == Engine::Microsoft) {
                ImGui::SetNextItemWidth(wide);
                ImGui::InputText(UI(Tr(L"Region") + L"##region").c_str(), regionBuf, sizeof(regionBuf));
            }
            if (edit.engine == Engine::Libre) {
                ImGui::SetNextItemWidth(wide);
                ImGui::InputText(UI(Tr(L"Server") + L"##url").c_str(), urlBuf, sizeof(urlBuf));
            }
            if (edit.engine == Engine::Auto || edit.engine == Engine::Basic) {
                ImGui::SetNextItemWidth(wide);
                ImGui::InputText(UI(Tr(L"E-mail for MyMemory (more free text per day)") + L"##mail").c_str(), mailBuf, sizeof(mailBuf));
                Hint(UI(Tr(L"Any made-up address works – MyMemory only counts the free text per address (5,000 characters a "
                           L"day without one, 50,000 with one).")));
            }
            ImGui::EndTabItem();
        }
        // ---- Technical
        if (ImGui::BeginTabItem(UI(Tr(L"Technical")).c_str(), nullptr, TabFlag(6))) {
            ImGui::Checkbox(UI(Tr(L"Automatic timing (FPS-adapted)") + L"##autotiming").c_str(), &edit.send.autoTiming);
            Hint(UI(Tr(L"Calculates key hold and step delays dynamically based on the game's current framerate.")));

            const int oldHold = edit.send.keyHoldMs;
            const int oldStep = edit.send.stepDelayMs;
            ImGui::SetNextItemWidth(120);
            if (ImGui::InputInt(UI(Tr(L"Key hold when sending (ms)") + L"##hold").c_str(), &edit.send.keyHoldMs)) {
                if (edit.send.keyHoldMs != oldHold) edit.send.autoTiming = false;
            }
            ImGui::SetNextItemWidth(120);
            if (ImGui::InputInt(UI(Tr(L"Pause between keys (ms)") + L"##step").c_str(), &edit.send.stepDelayMs)) {
                if (edit.send.stepDelayMs != oldStep) edit.send.autoTiming = false;
            }
            edit.send.keyHoldMs = std::clamp(edit.send.keyHoldMs, 10, 500);
            edit.send.stepDelayMs = std::clamp(edit.send.stepDelayMs, 10, 1000);
            ImGui::Separator();
            std::lock_guard<std::mutex> lock(g_mu);
            const int fps = g_fps;
            const int hold = (fps > 0 && edit.send.autoTiming) ? std::clamp(1000 / std::max(fps, 10) + 10, 15, 1010) : edit.send.keyHoldMs;
            ImGui::Text("GW2Chatlator for Nexus %s", GCT_VERSION_STR);
            ImGui::Text("%s", UI(TrF(L"Game: {1} fps → keys held {2} ms", {std::to_wstring(fps), std::to_wstring(hold)})).c_str());
            ImGui::Text("%s", UI(TrF(L"Translator: {1}", {g_translator ? g_translator->Name() : std::wstring(L"-")})).c_str());
            if (const std::wstring q = MyMemoryQuotaText(); !q.empty() && (g_nx.translateChat || g_cfg.translateWriting))
                ImGui::Text("%s", UI(q).c_str());
            ImGui::Text("Events: Chat: %s", g_chatEventsPresent || g_chatEventsLive ? "[OK]" : "[--]");
            ImGui::Text("%s", UI(TrF(L"Voice: {1}", {!g_cfg.voiceEnabled ? Tr(L"off") : g_voiceReady ? Tr(L"ready – loaded")
                                                       : g_voice.Running() ? Tr(L"starting")
                                                       : g_modules.voiceModule ? Tr(L"ready – loads on the first use of the microphone")
                                                                               : Tr(L"the voice module is missing")})).c_str());
            ImGui::Text("%s", UI(TrF(L"Chat lines: {1}", {std::to_wstring(g_lines.size())})).c_str());
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
        g_optionsTab = -1;
    }

    ImGui::Separator();
    if (ImGui::Button(UI(Tr(L"Save")).c_str())) {
        const std::wstring key = W(keyBuf);
        if (edit.engine == Engine::DeepL) edit.deeplKey = key;
        else if (edit.engine == Engine::Google) edit.googleKey = key;
        else if (edit.engine == Engine::Microsoft) edit.msKey = key;
        else if (edit.engine == Engine::Libre) edit.libreKey = key;
        edit.msRegion = W(regionBuf);
        edit.libreUrl = W(urlBuf);
        edit.basicEmail = W(mailBuf);
        edit.uiLang = EffectiveUiLang(edit.uiLangCode);
        bool typingChanged, voiceChanged;
        {
            std::lock_guard<std::mutex> lock(g_mu);
            typingChanged = edit.writeIn != g_cfg.writeIn || edit.spellEnabled != g_cfg.spellEnabled;
            voiceChanged = edit.voiceEnabled != g_cfg.voiceEnabled;
            edit.myMemoryDay = g_cfg.myMemoryDay;  // counted meanwhile
            edit.myMemoryUsed = g_cfg.myMemoryUsed;
            // Changed outside this page meanwhile (footer: "auto", a language added; a slow model switched) and not
            // here: the newer value stays – "Save" used to turn them back.
            if (edit.voiceAutoSend == opened.voiceAutoSend) edit.voiceAutoSend = g_cfg.voiceAutoSend;
            if (edit.writeLangs == opened.writeLangs) edit.writeLangs = g_cfg.writeLangs;
            if (edit.voiceModel == opened.voiceModel) edit.voiceModel = g_cfg.voiceModel;
            const bool engineChanged = edit.engine != g_cfg.engine;
            g_cfg = opened = edit;
            g_cfg.SaveAll();
            SetUiLang(g_cfg.uiLang);
            g_translator = MakeTranslator(g_cfg);
            g_sendIdx = g_cfg.translateWriting ? 0 : static_cast<int>(g_cfg.writeLangs.size());
            g_sendOverride.clear();
            g_preview.clear();  // what you have written goes through the translator as it is set now
            ++g_inputGen;
            g_inputChanged = GetTickCount64();
            if (g_nx.translateChat && (typingChanged || engineChanged)) TranslateRecentLines(false);
            SetStatus(Tr(L"Saved"), 3000);
        }
        if (typingChanged) g_worker.Post([] {  // spelling and learned words follow the language you write in
            FreeWorkerState();
            InitSpelling();
        });
        else g_worker.Post([learn = edit.learnWords] { if (g_spell) g_spell->SetLearnChoices(learn); });
        ApplyPushToTalkKey(edit.voiceGlobalKey);
        if (!edit.voiceActivation || !edit.voiceEnabled) StopListening();
        else g_listenUserOff = false;  // asked for in the options: it listens (started by the window)
        if (voiceChanged && edit.voiceEnabled && !g_voice.Running() &&
            GetFileAttributesW(HelperExe().c_str()) != INVALID_FILE_ATTRIBUTES)
            g_worker.Post([dir = edit.dataDir] {  // after the ini is written: the helper reads it when it starts
                Config::FlushWrites();
                std::lock_guard<std::mutex> lock(g_mu);
                if (!g_voice.Running()) g_voice.Start(HelperExe(), dir);
            });
        else if (g_voice.Running())
            VoiceReload();
    }
    ImGui::SameLine();
    if (ImGui::Button(UI(Tr(L"Undo changes")).c_str())) {
        loaded = false;
    }
}

// Nexus → Addons → GW2 Keyboard: only the button that opens / closes our own options window.
void RenderNexusOptions() {
    if (!ImGui::GetCurrentContext()) return;
    if (ImGui::Button(U8(g_showOptions ? Tr(L"Close the GW2 Keyboard options") : Tr(L"Open the GW2 Keyboard options")).c_str()))
        g_showOptions = !g_showOptions;
}

}  // namespace kb
