// kb_options_window.cpp — the Window page and the CBA theme's section.

#include "kb_shared.hpp"
#include "kb_options_parts.hpp"

namespace kb {

// The CBA theme's own section (Options → Window, only when it is chosen; g_mu held): a start, the eight roles, the
// contrast each text reaches, automatic contrast, and the theme code to share.
void RenderCbaTheme() {
    if (g_cbaRoles.size() != kRoleCount) g_cbaRoles = CbaPreset(2);
    bool changed = false;
    ImGui::Indent();
    ImGui::TextUnformatted(UI(Tr(L"Start from")).c_str());
    const std::wstring presets[] = {Tr(L"Slate"), Tr(L"Grey"), Tr(L"CBA blue"), Tr(L"CBA cyan")};
    for (int i = 0; i < 4; ++i) {
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
            ImGui::TextColored(ok ? ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled) : ImVec4(0.98f, 0.85f, 0.25f, 1.f),
                               "%s %s%s", UI(Tr(L"contrast")).c_str(), buf, ok ? "" : (" – " + UI(Tr(L"low"))).c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", UI(Tr(L"How well it stands out against the window's surfaces (WCAG): readable from 4.5:1 for text, 3:1 for quiet text and accents.")).c_str());
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

void RenderWindowPage(Config& edit) {
    const float fs = ImGui::GetFontSize();
    BeginBox(UI(Tr(L"Window")));
    RenderUiLanguageRow(edit);
    {
        std::lock_guard<std::mutex> lock(g_mu);
        RowLabel(UI(Tr(L"Theme of the window")));
        const std::wstring names[] = {Tr(L"Nexus"), Tr(L"Slate"), Tr(L"Grey"), Tr(L"CBA (your own colours)")};
        int ti = static_cast<int>(g_theme);
        if (RowCombo("##theme", {UI(names[0]), UI(names[1]), UI(names[2]), UI(names[3])}, &ti)) {
            g_theme = static_cast<Theme>(ti);
            SaveLayout();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", UI(Tr(L"Only the window itself: frames, tabs, buttons. The chat keeps its channel colours.")).c_str());
        if (g_theme == Theme::Cba) RenderCbaTheme();
        if (Switch("snap", UI(Tr(L"Snap to the screen edges")), &g_nx.snap, UI(Tr(L"Moved near an edge of the screen, the window sits right on it.")).c_str()))
            SaveNexusOptions();
    }
    EndBox();
    // Sizes, the font and transparency take effect at once and are kept.
    BeginBox(UI(Tr(L"Size & font")));
    RowLabel(UI(Tr(L"Chat text size")));
    int pt = edit.fontPt > 0 ? std::clamp(edit.fontPt, 8, 28) : FollowPt();  // the game's size while you have none of your own
    if (RowSlider("##size", &pt, 8, 28, "%d pt")) {
        std::lock_guard<std::mutex> lock(g_mu);
        edit.fontPt = g_cfg.fontPt = pt;
        g_cfg.SaveValue(L"Window", L"FontPt", std::to_wstring(pt));
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", UI(Tr(L"Only chat messages, preview, input and typing help.")).c_str());
    if (ImGui::IsItemDeactivatedAfterEdit()) RequestTextFont(pt, edit.fontFace);  // built once you let go (scaled meanwhile)
    RowLabel(UI(Tr(L"Line spacing")));
    int spacing = std::clamp(edit.chatSpacing, 0, 100);  // from nearly touching (0) to the usual (100)
    if (RowSlider("##lspace", &spacing, 0, 100, "%d %%")) {
        std::lock_guard<std::mutex> lock(g_mu);
        edit.chatSpacing = g_cfg.chatSpacing = spacing;
        g_cfg.SaveValue(L"Window", L"LineSpacing", std::to_wstring(spacing));
    }
    RowLabel(UI(Tr(L"UI & window scale")));
    int uiPt = edit.uiFontPt > 0 ? std::clamp(edit.uiFontPt, 11, 24) : FollowUiPt();
    if (RowSlider("##uisize", &uiPt, 11, 24, "%d pt")) {
        std::lock_guard<std::mutex> lock(g_mu);
        edit.uiFontPt = g_cfg.uiFontPt = uiPt;
        g_cfg.SaveValue(L"Window", L"UiFontPt", std::to_wstring(uiPt));
    }
    g_uiScaleDragging = ImGui::IsItemActive();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", UI(Tr(L"Header tabs, footer buttons, menus and this options window.")).c_str());
    if (ImGui::IsItemDeactivatedAfterEdit()) RequestUiFont(FollowUiPx());
    RowLabel(UI(Tr(L"Font")));
    {
        int at = 0;
        for (int i = 0; i < 3; ++i)
            if (edit.fontFace == kFontFaces[i].file) at = i;
        int chosen = at;
        if (RowCombo("##face", {kFontFaces[0].name, kFontFaces[1].name, kFontFaces[2].name}, &chosen) && chosen != at) {
            {
                std::lock_guard<std::mutex> lock(g_mu);
                edit.fontFace = g_cfg.fontFace = kFontFaces[chosen].file;
                g_cfg.SaveValue(L"Window", L"FontFace", edit.fontFace);
            }
            RequestTextFont(edit.fontPt, edit.fontFace);
        }
    }
    RowLabel(UI(Tr(L"Transparency")));
    int transparency = 100 - (std::clamp(edit.opacity, 0, 255) * 100 + 127) / 255;
    if (RowSlider("##alpha", &transparency, 0, 100, "%d %%")) {
        std::lock_guard<std::mutex> lock(g_mu);
        edit.opacity = g_cfg.opacity = (100 - transparency) * 255 / 100;
        g_cfg.SaveValue(L"Window", L"Opacity", std::to_wstring(g_cfg.opacity));
    }
    EndBox();
    BeginBox(UI(Tr(L"Window layout")));
    {
        std::lock_guard<std::mutex> lock(g_mu);
        // The two profiles first (presets of the parts below; changing a part switches them off).
        bool compact = ProfileOn(0), mini = ProfileOn(1);
        if (Switch("compact", UI(Tr(L"Compact view")), &compact,
                   UI(Tr(L"Header, chat, input and footer – no word bar, no preview. Change a part below and it is your own layout again.")).c_str()))
            SetProfile(0, compact);
        ImGui::SameLine(fs * 14.0f);
        if (Switch("mini", UI(Tr(L"Mini mode")), &mini,
                   UI(Tr(L"Nothing but the input with its help. Three quick clicks on it minimise it; right-click in it: Menu … brings the rest back.")).c_str()))
            SetProfile(1, mini);
        bool subs = ProfileOn(2);
        if (Switch("subtitles", UI(Tr(L"Subtitle mode")), &subs, UI(Tr(L"Only the chat's lines, see-through over the game. The input shows while you write and fades after sending.")).c_str())) SetProfile(2, subs);
        ImGui::Spacing();
        // From top to bottom, as they stand in the window; each one can go – in two columns.
        bool changed = false;
        int col = 0;
        auto part = [&](const char* id, const std::wstring& label, bool* v, const std::wstring& tip) {
            if (col++ % 2) ImGui::SameLine(fs * 14.0f);
            const std::string t = UI(tip);
            if (Switch(id, UI(label), v, tip.empty() ? nullptr : t.c_str())) {
                changed = true;
                g_layout.subtitles = false;  // a part by hand: your own layout, no longer the subtitle mode
            }
        };
        part("header", Tr(L"Header"), &g_layout.header, Tr(L"Hidden: a right-click on the window opens the menu (tabs, minimise, options)."));
        part("chat", Tr(L"Chat"), &g_layout.chat, {});
        part("preview", Tr(L"Translation preview"), &g_layout.preview, {});
        part("words", Tr(L"Word bar"), &g_layout.words, {});
        part("footer", Tr(L"Footer"), &g_layout.footer, {});
        part("minbtn", Tr(L"Minimize button"), &g_layout.minButton,
             Tr(L"The small > in the footer (without the footer: left of the input). Hidden: three quick clicks on the window minimise it, or the header, or the right-click menu."));
        part("backline", Tr(L"Back-translation line"), &g_layout.backLine, Tr(L"Without the preview (mini mode, compact view): what your text says when translated back, in one small line above the input."));
        part("info", Tr(L"Info texts"), &g_layout.infoText,
             Tr(L"The grey status lines: what is going on, the modules, the counter. The grey word of the prediction stays."));
        RowLabel(UI(Tr(L"Lines of the input")));
        changed |= RowSlider("##inl", &g_layout.inputLines, 1, 8, "%d");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", UI(Tr(L"Longer text makes the input grow (up to 8 lines) until it is sent.")).c_str());
        RowLabel(UI(Tr(L"Lines of the preview")));
        changed |= RowSlider("##prl", &g_layout.previewLines, 1, 8, "%d");
        if (changed) SaveLayout();
    }
    EndBox();
    BeginBox(UI(Tr(L"When not in use")));
    {
        std::lock_guard<std::mutex> lock(g_mu);
        bool changed = Switch("automin", UI(Tr(L"Minimize after")), &g_layout.autoMinimize,
                              UI(Tr(L"Nobody writes, points at the window or talks: it goes down to the small button.")).c_str());
        ImGui::SameLine();
        ImGui::SetNextItemWidth(fs * 8);
        changed |= ImGui::SliderInt(UI(Tr(L"seconds") + L"##minafter").c_str(), &g_layout.minimizeAfter, 2, 120);
        if (changed) SaveLayout();
    }
    EndBox();
    // The chat's own colours stay a choice: GW2's channel colours, or the message in the channel colour.
    BeginBox(UI(Tr(L"Chat text")));
    {
        std::lock_guard<std::mutex> lock(g_mu);
        bool changed = Switch("glow", UI(Tr(L"Dark outline (readable when see-through)")), &g_look.glow,
                              UI(Tr(L"A dark halo around the letters, like the game draws its chat – readable on any background.")).c_str());
        changed |= Switch("gamecol", UI(Tr(L"GW2 colours")), &g_look.gameColors,
                          UI(Tr(L"The name in the channel colour, the message in a light tint of it – as in the game. Off: the message in the channel colour.")).c_str());
        RowLabel(UI(Tr(L"Brightness")));
        changed |= RowSlider("##bright", &g_look.brightness, 60, 160, "%d %%");
        if (changed) SaveLayout();
    }
    EndBox();
}

}  // namespace kb
