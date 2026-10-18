// kb_options.cpp — the options window and the modules list.

#include "kb_shared.hpp"

namespace kb {

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
                // The two profiles first (presets of the parts below; changing a part switches them off).
                bool compact = ProfileOn(0), mini = ProfileOn(1);
                if (ImGui::Checkbox(UI(Tr(L"Compact view")).c_str(), &compact)) SetProfile(0, compact);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", UI(Tr(L"Header, chat, input and footer – no word bar, no preview. Change a part below and it is your own layout again.")).c_str());
                if (ImGui::Checkbox(UI(Tr(L"Mini mode")).c_str(), &mini)) SetProfile(1, mini);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", UI(Tr(L"Nothing but the input with its help. Three quick clicks on it minimise it; right-click in it: Menu … brings the rest back.")).c_str());
                ImGui::Spacing();
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
