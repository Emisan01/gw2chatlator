// kb_options.cpp — the options window and the modules list.

#include "kb_shared.hpp"

namespace kb {

int g_optionsTab = -1;  // a tab of the options window to bring to the front (General 0, Window 1, Voice 3, Translator 5)

namespace {
std::atomic<float> s_nexusOptionsFontPx{0};  // sampled inside Nexus' own OptionsRender window
}

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
// A remove button that asks once more: the first click turns it into "Click again to remove" for 4 s.
bool RemoveButton(const char* id) {
    static std::string s_armed;
    static ULONGLONG s_until = 0;
    const bool armed = s_armed == id && GetTickCount64() < s_until;
    ImGui::SameLine();
    const bool clicked = ImGui::SmallButton((UI(armed ? Tr(L"Click again to remove") : Tr(L"Remove")) + "##" + id).c_str());
    if (!clicked) return false;
    if (armed) {
        s_armed.clear();
        return true;
    }
    s_armed = id;
    s_until = GetTickCount64() + 4000;
    return false;
}

// After "remove everything": what happened, and the one step left (the DLL cannot remove itself while it runs).
void RenderRemovedNotice() {
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f, ds.y * 0.4f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::Begin(UI(Tr(L"GW2Chatlator removed") + L"###removed").c_str(), nullptr,
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextUnformatted(UI(g_wipeDone == 1 ? Tr(L"Everything GW2Chatlator had put on your PC is removed – settings, learned words, voice and local translation modules and models.")
                                                  : Tr(L"Most is removed; a file was still in use – delete the folder GW2Keyboard in your addons folder by hand.")).c_str());
        ImGui::TextUnformatted(UI(Tr(L"Last step: Nexus → Addons → GW2Chatlator → Uninstall (the add-on cannot delete itself while it runs).")).c_str());
        if (ImGui::Button(UI(Tr(L"OK")).c_str())) g_disabled = true;  // quiet for the rest of the session
    }
    ImGui::End();
}

// Events: Chat and Decoder Ring – loaded, installed, missing; install / remove (g_mu NOT held). Options > Technical and
// the guide.
void RenderChatModule(bool guide) {
    ImGui::PushID("modules");
    const bool chatLive = g_chatEventsLive || g_chatEventsPresent;
    StatusRow(UI(Tr(L"Chat module")), chatLive ? 2 : g_chatEventsInstalling || g_chatEventsFile ? 1 : 0,
              UI(chatLive ? Tr(L"ready – Events: Chat is loaded")
                 : g_chatEventsInstalling ? Tr(L"loading Events: Chat …")
                 : g_chatEventsFile ? Tr(L"installed – Nexus loads it (else: Nexus → Addons → Events: Chat → Load)")
                                    : Tr(L"Events: Chat is missing")));
    if (!chatLive && !g_chatEventsFile && !g_chatEventsInstalling) {
        ImGui::SameLine();
        if (ImGui::SmallButton(UI(Tr(L"Install Events: Chat")).c_str())) InstallChatEventsNow();
    } else if (!guide && (chatLive || g_chatEventsFile) && !g_chatEventsInstalling) {
        if (RemoveButton("chatevents")) RemoveChatEventsNow();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", UI(Tr(L"Events: Chat is its own add-on – other add-ons may use it too.")).c_str());
    }
    // Decoder Ring (optional, outdated since 2026-10-22 – our own resolver comes, Phase 2a): only in Options >
    // Technical, never in the guide. Without it a chat code shows only its kind; finding the codes is ours anyway.
    if (guide) {
        ImGui::PopID();
        return;
    }
    const bool decoderLive = g_links.HasDecoder();
    StatusRow(UI(Tr(L"Decoder Ring (optional, outdated)")), decoderLive ? 2 : g_decoderInstalling || g_decoderFile ? 1 : 0,
              UI(decoderLive          ? Tr(L"ready – names, maps and details of chat codes")
                 : g_decoderInstalling ? Tr(L"loading Decoder Ring …")
                 : g_decoderFile       ? Tr(L"installed – Nexus loads it (else: Nexus → Addons → Decoder Ring → Load)")
                                       : Tr(L"missing – chat codes show only their kind")));
    if (!decoderLive && !g_decoderFile && !g_decoderInstalling) {
        ImGui::SameLine();
        if (ImGui::SmallButton(UI(Tr(L"Install Decoder Ring")).c_str())) InstallDecoderRingNow();
    } else if ((decoderLive || g_decoderFile) && !g_decoderInstalling) {
        if (RemoveButton("decoderring")) RemoveDecoderRingNow();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", UI(Tr(L"Decoder Ring is its own add-on – other add-ons may use it too.")).c_str());
    }
    ImGui::PopID();
}

// The voice module and its model – load, unload, remove (g_mu NOT held). Options > Voice and the guide.
// Returns whether voice input is on.
bool RenderVoiceModule(bool guide) {
    const ModuleState& m = Modules();
    ImGui::PushID("modules");
    bool enabled, ready;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        enabled = g_cfg.voiceEnabled;
        ready = g_voiceReady;
    }
    if (!enabled) {  // optional: speech to text needs the voice module – switched on here, nothing loads before
        StatusRow(UI(Tr(L"Voice module")), 0, UI(Tr(L"off – speech to text, optional")));
        ImGui::SameLine();
        if (ImGui::SmallButton(UI(Tr(L"Switch on") + L"##voiceon").c_str())) {
            std::lock_guard<std::mutex> lock(g_mu);
            g_cfg.voiceEnabled = true;
            g_cfg.SaveBool(L"Voice", L"Enabled", true);
        }
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
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", UI(Tr(L"Downloads a helper program (about 6 MB) and the speech engine from this version's release on GitHub (checked with SHA-256). It runs as its own process outside the game.")).c_str());
        } else if (m.voiceModule && !running) {
            if (ImGui::SmallButton(UI(Tr(L"Load")).c_str())) LoadVoiceModule();
        } else if (running) {
            if (ImGui::SmallButton(UI(Tr(L"Unload")).c_str())) UnloadVoiceModule();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", UI(Tr(L"Ends the voice module and frees its memory; the microphone loads it again.")).c_str());
        }
        if (!guide && m.voiceModule && !g_voiceModuleInstalling && RemoveButton("voicemodule")) RemoveVoiceModuleNow();
        const int dl = g_downloadPct;
        StatusRow(UI(Tr(L"Voice model")), m.model ? 2 : dl >= 0 ? 1 : 0,
                  dl >= 0 ? UI(TrF(L"downloading … {1} %", {std::to_wstring(dl)}))
                          : m.modelName + (m.model ? "" : "  " + UI(Tr(L"missing"))));
        if (!guide && m.model && dl < 0) {
            if (RemoveButton("voicemodels")) RemoveVoiceModelsNow();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", UI(Tr(L"Removes every downloaded voice model.")).c_str());
        }
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
    ImGui::PopID();
    return enabled;
}

// The empty chat: what is ready, what is missing, where translation is switched on (g_mu NOT held).
void RenderModules(bool guide) {
    RenderChatModule(guide);
    const bool enabled = RenderVoiceModule(guide);
    ImGui::PushID("modules");
    if (guide) {
        ImGui::Spacing();
        bool chat, mine, voiceEnglish;
        {
            std::lock_guard<std::mutex> lock(g_mu);
            chat = g_nx.translateChat;
            mine = g_cfg.translateWriting;
            voiceEnglish = g_cfg.voiceTranslate;  // "Translate what I say"
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
            ImGui::TextDisabled("%s", UI(Tr(L"Translate what you say: Options → Voice")).c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton(UI(Tr(L"Open") + L"##vo").c_str())) {
                g_showOptions = true;
                g_optionsTab = 3;
            }
        }
    }
    ImGui::PopID();
}

// Everything of the tool, reversed – after a yes (g_mu NOT held). Once, at the bottom of Options > Technical.
void RenderRemoveEverything() {
    ImGui::PushID("modules");
    {
        if (ImGui::SmallButton(UI(Tr(L"Remove everything of GW2Chatlator …")).c_str())) ImGui::OpenPopup("###removeall");
        if (ImGui::BeginPopupModal((UI(Tr(L"Remove everything of GW2Chatlator")) + "###removeall").c_str(), nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            static bool chatEventsToo = false, decoderToo = false;
            ImGui::TextUnformatted(UI(Tr(L"This deletes the folder GW2Keyboard in your addons folder: settings, learned words, my words, the voice and local translation modules and their models. It cannot be undone.")).c_str());
            ImGui::Checkbox(UI(Tr(L"Events: Chat too (other add-ons may use it)")).c_str(), &chatEventsToo);
            ImGui::Checkbox(UI(Tr(L"Decoder Ring too (other add-ons may use it)")).c_str(), &decoderToo);
            if (g_localMtBusy) {
                ImGui::TextDisabled("%s", UI(Tr(L"Wait for the local translation operation to finish.")).c_str());
            } else if (ImGui::Button(UI(Tr(L"Remove everything")).c_str())) {
                ImGui::CloseCurrentPopup();
                RemoveEverythingNow(chatEventsToo, decoderToo);
            }
            ImGui::SameLine();
            if (ImGui::Button(UI(Tr(L"Cancel")).c_str()) || ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Escape)))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
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
        s_optsTitle = UI(Tr(L"GW2Chatlator – Options")) + "###kbopts";
    }
    // Nexus' per-window scale is not in FontDefault; sample its actual rendered size from RenderNexusOptions below.
    // Divide out ImGui's global scale before building our font, which will receive that global scale itself.
    ImGuiIO& io = ImGui::GetIO();
    ImFont* nexusFont = io.FontDefault ? io.FontDefault : io.Fonts->Fonts[0];
    const float globalScale = std::max(0.1f, io.FontGlobalScale);
    const float sampledPx = s_nexusOptionsFontPx.load();
    const float nexusPx = std::max(PtToPx(14), std::round(sampledPx > 0.0f ? sampledPx / globalScale
                                                                            : nexusFont->FontSize * nexusFont->Scale));
    RequestUiFont(nexusPx);
    ImFont* uf = g_uiFont.load();
    const bool ours = uf && std::abs(uf->FontSize - nexusPx) < 0.5f;
    const bool visible = ImGui::Begin(s_optsTitle.c_str(), &g_showOptions);
    if (ours) {
        ImGui::SetWindowFontScale(1.0f);  // the requested font already has the minimum size
        ImGui::PushFont(uf);
    } else {
        // Keep the minimum readable while Nexus builds our font or when its font API is unavailable.
        // This fallback may be softer, but it must not silently fall back to Nexus' tiny default size.
        const float currentPx = std::max(1.0f, ImGui::GetFontSize());
        ImGui::SetWindowFontScale(nexusPx * globalScale / currentPx);
    }
    if (visible) RenderOptionsContent();
    ImGui::End();
    if (ours) ImGui::PopFont();
}

std::wstring FormatSeconds(int ms) {  // 1234 -> "1.2"
    wchar_t b[16];
    swprintf(b, 16, L"%.1f", ms / 1000.0);
    return b;
}

// A language combo over the languages our window draws (DrawnInOurWindow): `value` "" = `emptyLabel`.
bool LanguageCombo(const char* id, std::wstring& value, const std::wstring& emptyLabel) {
    std::string current = value.empty() ? U8(emptyLabel) : U8(value);
    for (const LangInfo& l : Languages())
        if (PrimaryLang(l.code) == PrimaryLang(value) && !value.empty()) current = U8(NativeName(l.code) + L" (" + l.code + L")");
    bool changed = false;
    if (ImGui::BeginCombo(id, current.c_str())) {
        if (!emptyLabel.empty() && ImGui::Selectable(U8(emptyLabel).c_str(), value.empty())) {
            value.clear();
            changed = true;
        }
        for (const LangInfo& l : Languages()) {
            if (!DrawnInOurWindow(l)) continue;
            const std::string n = U8(NativeName(l.code) + L" (" + l.code + L")");
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

// The dashboard (Options > General): one switch – a chip, green on, grey off, amber changed and not saved yet ("Save"
// applies it, as everywhere in this window) – its name, and below it in grey what it does right now.
bool DashSwitch(const char* id, const std::string& name, const std::string& detail, bool* on, bool pending, bool usable) {
    const ImVec4 col = !usable  ? ImVec4(0.5f, 0.5f, 0.5f, 1.f)
                       : pending ? ImVec4(0.94f, 0.75f, 0.35f, 1.f)
                       : *on     ? ImVec4(0.45f, 0.85f, 0.45f, 1.f)
                                 : ImVec4(0.62f, 0.62f, 0.62f, 1.f);
    const std::string onText = UI(Tr(L"on")), offText = UI(Tr(L"off"));
    const float chipW = std::ceil(std::max(ImGui::CalcTextSize(onText.c_str()).x, ImGui::CalcTextSize(offText.c_str()).x) +
                                  ImGui::GetStyle().FramePadding.x * 2 + 6);
    ImGui::PushID(id);
    ImGui::PushStyleColor(ImGuiCol_Text, col);
    ImGui::PushStyleColor(ImGuiCol_Border, col);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    const bool clicked = ImGui::Button((*on ? onText : offText).c_str(), ImVec2(chipW, 0)) && usable;
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    if (clicked) *on = !*on;
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    if (usable) ImGui::TextUnformatted(name.c_str());
    else ImGui::TextDisabled("%s", name.c_str());
    Hint(detail);
    ImGui::PopID();
    return clicked;
}

// A state of the dashboard that is switched elsewhere: the chip ([OK] / [--]) opens that tab.
void DashState(const char* id, const std::string& name, const std::string& detail, bool fine, int tab) {
    const ImVec4 col = fine ? ImVec4(0.45f, 0.85f, 0.45f, 1.f) : ImVec4(0.95f, 0.55f, 0.35f, 1.f);
    const std::string onText = UI(Tr(L"on")), offText = UI(Tr(L"off"));
    const float chipW = std::ceil(std::max(ImGui::CalcTextSize(onText.c_str()).x, ImGui::CalcTextSize(offText.c_str()).x) +
                                  ImGui::GetStyle().FramePadding.x * 2 + 6);
    ImGui::PushID(id);
    ImGui::PushStyleColor(ImGuiCol_Text, col);
    ImGui::PushStyleColor(ImGuiCol_Border, col);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    if (ImGui::Button(fine ? "OK" : "--", ImVec2(chipW, 0))) g_optionsTab = tab;
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", UI(Tr(L"Open")).c_str());
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(name.c_str());
    Hint(detail);
    ImGui::PopID();
}

// What is on, at a glance (g_mu NOT held): the chat's translation applies at once (as in the Translator tab), the rest
// with "Save".
// "Translated is everything except: ..." – the one list of what is never translated by itself (core/translate_policy):
// your writing language (it follows when you change it; on by default, removable – [Translate] SkipTypingLang) and the
// languages you understand ([Translate] Understood). g_mu NOT held.
std::wstring WritingLangNow() {
    std::lock_guard<std::mutex> lock(g_mu);
    return PrimaryLang(TypingLang());
}
std::string ExceptText(const Config& c) {
    std::wstring out = c.skipTypingLang ? TrF(L"My writing language ({1})", {WritingLangNow()}) : std::wstring();
    for (const std::wstring& u : c.understoodLangs) out += (out.empty() ? L"" : L", ") + NativeName(u);
    return UI(out.empty() ? Tr(L"nothing") : out);
}
void RenderExceptList(Config& edit) {
    static const wchar_t* kCommon[] = {L"EN", L"DE", L"FR", L"ES", L"IT", L"PT", L"NL", L"PL",
                                       L"RU", L"TR"};  // what our window draws (no CJK, no Arabic)
    bool first = true;
    auto chip = [&first](const std::string& label) {  // one entry; the row wraps where it no longer fits
        const float w = ImGui::CalcTextSize(label.c_str(), nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2;
        if (!first) {
            ImGui::SameLine();
            if (ImGui::GetContentRegionAvail().x < w) ImGui::NewLine();
        }
        first = false;
        const bool clicked = ImGui::SmallButton(label.c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", UI(Tr(L"Click: translated again")).c_str());
        return clicked;
    };
    ImGui::PushID("except");
    if (edit.skipTypingLang && chip(UI(TrF(L"My writing language ({1})", {WritingLangNow()})) + " x##mine"))
        edit.skipTypingLang = false;
    auto& list = edit.understoodLangs;
    for (size_t i = 0; i < list.size(); ++i)
        if (chip(U8(NativeName(list[i])) + " x##" + U8(list[i]))) {
            list.erase(list.begin() + static_cast<std::ptrdiff_t>(i));
            break;
        }
    if (!first) ImGui::SameLine();
    if (ImGui::SmallButton("+")) ImGui::OpenPopup("add");
    if (ImGui::BeginPopup("add")) {
        if (!edit.skipTypingLang && ImGui::Selectable(UI(TrF(L"My writing language ({1})", {WritingLangNow()})).c_str()))
            edit.skipTypingLang = true;
        for (const wchar_t* code : kCommon) {
            const bool in = std::any_of(list.begin(), list.end(), [code](const std::wstring& u) {
                return PrimaryLang(u) == PrimaryLang(code);
            });
            if (!in && ImGui::Selectable((U8(NativeName(code)) + " (" + U8(code) + ")").c_str())) list.push_back(code);
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
}

void RenderDashboard(Config& edit, const Config& saved) {
    bool chatOn, voiceReady;
    std::wstring writeIn, engine;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        chatOn = g_nx.translateChat;
        voiceReady = g_voiceReady;
        writeIn = g_cfg.writeIn;
        engine = g_translator ? g_translator->Name() : std::wstring(L"-");
    }
    const ModuleState& m = Modules();
    std::wstring readingText;
    const bool chatThere = g_chatEventsLive || g_chatEventsPresent;
    const bool reading = chatThere && ChatSourceStatus(&readingText) == 2;
    ImGui::TextUnformatted(UI(Tr(L"At a glance")).c_str());
    ImGui::TextDisabled("%s", UI(Tr(L"Active features")).c_str());
    if (ImGui::BeginTable("##dash_features", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        if (DashSwitch("chat", UI(Tr(L"Translate the chat")),
                       UI(chatOn ? TrF(L"into {1}", {NativeName(writeIn)}) : Tr(L"a click on a line translates just that one")),
                       &chatOn, false, true)) {
            std::lock_guard<std::mutex> lock(g_mu);
            g_nx.translateChat = chatOn;
            SaveNexusOptions();
            if (chatOn) TranslateRecentLines(false);  // what is already there too
        }
        ImGui::TableNextColumn();
        DashSwitch("mine", UI(Tr(L"Translate my text")),
                   UI(edit.translateWriting ? Tr(L"into the language chosen at \"Send\" below the input") : Tr(L"sent as you write it")),
                   &edit.translateWriting, edit.translateWriting != saved.translateWriting, true);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        DashSwitch("voice", UI(Tr(L"Voice input")),
                   UI(!edit.voiceEnabled ? Tr(L"off – speech to text, optional")
                      : !m.voiceModule   ? Tr(L"the voice module is missing")
                      : voiceReady       ? Tr(L"ready – loaded")
                                         : Tr(L"ready – loads on the first use of the microphone")),
                   &edit.voiceEnabled, edit.voiceEnabled != saved.voiceEnabled, true);
        ImGui::TableNextColumn();
        std::wstring voiceTo;
        {
            std::lock_guard<std::mutex> lock(g_mu);
            voiceTo = VoiceLang();
        }
        const bool voiceEnglish = PrimaryLang(voiceTo) == L"EN";
        DashSwitch("voiceen", UI(Tr(L"Translate what I say")),
                   UI(!edit.voiceTranslate ? Tr(L"as you say it")
                      : voiceEnglish       ? TrF(L"into {1} – on this PC (whisper)", {NativeName(voiceTo)})
                                           : TrF(L"into {1} – with your translator", {NativeName(voiceTo)})),
                   &edit.voiceTranslate, edit.voiceTranslate != saved.voiceTranslate, edit.voiceEnabled);
        ImGui::EndTable();
    }
    if (edit.translateWriting != saved.translateWriting || edit.voiceEnabled != saved.voiceEnabled ||
        edit.voiceTranslate != saved.voiceTranslate)
        Hint(UI(Tr(L"Amber: changed – \"Save\" applies it.")));

    ImGui::Spacing();
    ImGui::TextDisabled("%s", UI(Tr(L"Modules & status")).c_str());
    if (ImGui::BeginTable("##dash_modules", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        DashState("translator", UI(Tr(L"Translator")), UI(engine), true, 5);
        ImGui::TableNextColumn();
        DashState("reading", UI(Tr(L"Reading the chat")), UI(!chatThere ? Tr(L"Events: Chat is missing") : readingText), reading, 6);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        DashState("except", UI(Tr(L"Translated is everything except:")), ExceptText(edit), true, 5);
        ImGui::EndTable();
    }
    Hint(UI(Tr(L"A click on a line always translates it.")));
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
            {  // only the languages this window has texts in (core/i18n: UiLanguages)
                std::string current = UI(Tr(L"like Windows"));
                for (const UiLangInfo& l : UiLanguages())
                    if (ToLowerAscii(PrimaryLang(edit.uiLangCode)) == l.code) current = U8(l.native);  // "DE" of old too
                if (ImGui::BeginCombo(UI(Tr(L"Language of this window") + L"##ui").c_str(), current.c_str())) {
                    if (ImGui::Selectable(UI(Tr(L"like Windows")).c_str(), edit.uiLangCode.empty())) edit.uiLangCode.clear();
                    for (const UiLangInfo& l : UiLanguages())
                        if (ImGui::Selectable(U8(l.native).c_str(), ToLowerAscii(PrimaryLang(edit.uiLangCode)) == l.code))
                            edit.uiLangCode = l.code;
                    ImGui::EndCombo();
                }
            }
            ImGui::Checkbox(UI(Tr(L"Show the window")).c_str(), &g_show);
            {
                auto drawBadge = [](const char* key, const char* label) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.78f, 0.35f, 1.0f));
                    ImGui::SmallButton(key);
                    ImGui::PopStyleColor(2);
                    ImGui::SameLine(0, ImGui::GetFontSize() * 0.35f);
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextDisabled("%s", label);
                };
                drawBadge("Ctrl+Alt+K", UI(Tr(L"Window")).c_str());
                ImGui::SameLine(0, ImGui::GetFontSize() * 0.9f);
                drawBadge("Focus", UI(Tr(L"Input")).c_str());
                if (edit.voiceEnabled) {
                    ImGui::SameLine(0, ImGui::GetFontSize() * 0.9f);
                    drawBadge("PTT", UI(Tr(L"Voice")).c_str());
                }
            }
            Hint(UI(Tr(L"Keybinds: Nexus → Keybinds → GW2Chatlator.")));
            ImGui::Separator();
            RenderDashboard(edit, opened);  // what is on, switched right here
            ImGui::Separator();
            ImGui::TextUnformatted(UI(Tr(L"Chat focus & behavior")).c_str());
            {
                std::lock_guard<std::mutex> lock(g_mu);
                if (ImGui::Checkbox(UI(Tr(L"Open again when GW2's chat opens (Enter)")).c_str(), &g_layout.autoOpen)) {
                    SaveLayout();
                }
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", UI(Tr(L"Only opens it. The focus key (Nexus > Keybinds) always opens it; taking over the focus is its own setting (General).")).c_str());
            ImGui::Checkbox(UI(Tr(L"Enter in the game opens GW2Chatlator's input")).c_str(), &edit.focusOnGameChat);
            Hint(UI(Tr(L"Instead of GW2's own chat line: you type here, with the typing help and the translation.")));
            ImGui::Checkbox(UI(Tr(L"After sending, keep typing in GW2Chatlator")).c_str(), &edit.returnFocus);
            Hint(UI(Tr(L"The input keeps the keyboard after Enter – the next message goes right after.")));
            ImGui::Separator();
            ImGui::TextUnformatted(UI(Tr(L"What is ready")).c_str());
            RenderModules(true);  // the same box as in the empty chat
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
            // starts at the game's interface size while you have none of your own (fontPt 0)
            int pt = edit.fontPt > 0 ? std::clamp(edit.fontPt, 8, 28) : FollowPt();
            ImGui::SetNextItemWidth(wide);
            if (ImGui::SliderInt(UI(Tr(L"Text size") + L" (pt)##size").c_str(), &pt, 8, 28)) {
                std::lock_guard<std::mutex> lock(g_mu);
                edit.fontPt = g_cfg.fontPt = pt;
                g_cfg.SaveValue(L"Window", L"FontPt", std::to_wstring(pt));
            }
            if (ImGui::IsItemDeactivatedAfterEdit()) RequestTextFont(pt, edit.fontFace);  // built once you let go (scaled meanwhile)
            {  // the font beside its size – taken at once and kept, like the size
                int at = 0;
                for (int i = 0; i < 3; ++i)
                    if (edit.fontFace == kFontFaces[i].file) at = i;
                int chosen = at;
                ImGui::SetNextItemWidth(wide);
                if (ImGui::BeginCombo(UI(Tr(L"Font") + L"##face").c_str(), kFontFaces[at].name)) {
                    for (int i = 0; i < 3; ++i)
                        if (ImGui::Selectable(kFontFaces[i].name, i == at)) chosen = i;
                    ImGui::EndCombo();
                }
                if (chosen != at) {
                    {
                        std::lock_guard<std::mutex> lock(g_mu);
                        edit.fontFace = g_cfg.fontFace = kFontFaces[chosen].file;
                        g_cfg.SaveValue(L"Window", L"FontFace", edit.fontFace);
                    }
                    RequestTextFont(edit.fontPt, edit.fontFace);
                }
            }
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
            ImGui::EndTabItem();
        }
        // ---- Voice
        if (ImGui::BeginTabItem(UI(Tr(L"Voice")).c_str(), nullptr, TabFlag(3))) {
            ImGui::Checkbox(UI(Tr(L"Voice input")).c_str(), &edit.voiceEnabled);
            RenderVoiceModule(false);
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
                        Shielded([&path] {
                            CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
                            ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                            CoUninitialize();
                        });
                    }).detach();
                }
                ImGui::PopID();
            };
            folder(Tr(L"Models:"), edit.VoiceModelDir());
            folder(Tr(L"Voice module:"), KeyboardDir());
            std::vector<std::string> langs{UI(Tr(L"Like I write")), UI(Tr(L"Detect it"))};
            std::vector<std::wstring> codes{L"", L"auto"};
            for (const LangInfo& l : Languages()) {
                if (!DrawnInOurWindow(l)) continue;
                langs.push_back(U8(NativeName(l.code)));
                codes.push_back(l.code);
            }
            int li = 0;
            for (size_t i = 0; i < codes.size(); ++i)
                if (codes[i] == edit.voiceLang) li = static_cast<int>(i);
            if (combo(UI(Tr(L"Language") + L"##vlang").c_str(), langs, &li)) edit.voiceLang = codes[li];
            ImGui::Checkbox(UI(Tr(L"Translate what I say")).c_str(), &edit.voiceTranslate);
            Hint(UI(Tr(L"What you said goes into the language at \"Send\" below the input – before \"Translate my text\". English: whisper does it on this PC; other languages: your translator.")));
            ImGui::Checkbox(UI(Tr(L"Live text while speaking (more CPU)")).c_str(), &edit.voiceLive);
            ImGui::SetNextItemWidth(wide);
            ImGui::SliderInt(UI(Tr(L"Longest recording") + L" (s)##max").c_str(), &edit.voiceMaxSec, 5, 60);
            std::vector<std::string> threads{UI(Tr(L"Automatic"))};
            for (int i = 1; i <= 8; ++i) threads.push_back(std::to_string(i));
            int ti = std::clamp(edit.voiceThreads, 0, 8);
            if (combo(UI(Tr(L"CPU threads") + L"##thr").c_str(), threads, &ti)) edit.voiceThreads = ti;
            Hint(UI(Tr(L"Automatic: the least busy cores – the ones GW2 does not use.")));
            ImGui::Checkbox(UI(Tr(L"Hands-free: with voice activation, the pause after speaking sends it (push-to-talk waits for your Enter)")).c_str(), &edit.voiceAutoSend);
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
            ImGui::TextUnformatted(UI(Tr(L"Translated is everything except:")).c_str());
            RenderExceptList(edit);
            Hint(UI(Tr(L"A click on a line always translates it. A click on a name opens a whisper tab that answers in "
                       L"that player's language.")));
            ImGui::Checkbox(UI(Tr(L"Translate my text")).c_str(), &edit.translateWriting);
            Hint(UI(Tr(L"What you write goes into the chat in the language chosen at \"Send\" below the input.")));
            static const Engine kEngines[] = {Engine::Auto, Engine::Basic, Engine::DeepL, Engine::Google, Engine::Microsoft, Engine::Libre, Engine::Local};
            static const wchar_t* kNames[] = {L"Automatic", L"MyMemory (free)", L"DeepL", L"Google", L"Microsoft", L"LibreTranslate", L"OPUS-MT (local)"};
            int ei = 0;
            for (int i = 0; i < 7; ++i)
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
            ImGui::Separator();
            RenderLocalMtOptions(edit);
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
            RenderChatModule(false);  // Events: Chat: loaded, install / remove – reading it follows in the details
            {
                std::lock_guard<std::mutex> lock(g_mu);
                static Steady s_fps;
                const int fps = static_cast<int>(std::lround(s_fps(g_fps)));
                const int hold = (fps > 0 && edit.send.autoTiming) ? std::clamp(1000 / std::max(fps, 10) + 10, 15, 1010) : edit.send.keyHoldMs;
                ImGui::Text("GW2Chatlator for Nexus %s", GCT_VERSION_STR);
                ImGui::Text("%s", UI(TrF(L"Game: {1} fps → keys held {2} ms", {std::to_wstring(fps), std::to_wstring(hold)})).c_str());
                ImGui::Text("%s", UI(TrF(L"Translator: {1}", {g_translator ? g_translator->Name() : std::wstring(L"-")})).c_str());
                if (const std::wstring q = MyMemoryQuotaText(); !q.empty() && (g_nx.translateChat || g_cfg.translateWriting))
                    ImGui::Text("%s", UI(q).c_str());
                RenderChatSourceDetails();  // the version of Events: Chat, reading, the counts, the last lines (no text)
                ImGui::Text("%s", UI(TrF(L"Voice: {1}", {!g_cfg.voiceEnabled ? Tr(L"off") : g_voiceReady ? Tr(L"ready – loaded")
                                                           : g_voice.Running() ? Tr(L"starting")
                                                           : g_modules.voiceModule ? Tr(L"ready – loads on the first use of the microphone")
                                                                                   : Tr(L"the voice module is missing")})).c_str());
                const size_t nLines = g_lines.size();
                wchar_t ramBuf[32];
                swprintf_s(ramBuf, L"%.1f", static_cast<double>(nLines * 750) / (1024.0 * 1024.0));
                ImGui::Text("%s", UI(TrF(L"Chat history buffer: {1} / {2} lines (~{3} MB RAM)",
                                         {std::to_wstring(nLines), std::to_wstring(kMaxLines), ramBuf})).c_str());
            }
            ImGui::Separator();
            RenderRemoveEverything();  // once, at the very bottom
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
    if (ImGui::GetFont() && ImGui::GetFontSize() > 0.0f)
        s_nexusOptionsFontPx.store(ImGui::GetFontSize());  // includes Nexus' per-window scale and the active Windows/game DPI scale
    if (ImGui::Button(U8(g_showOptions ? Tr(L"Close the GW2Chatlator options") : Tr(L"Open the GW2Chatlator options")).c_str()))
        g_showOptions = !g_showOptions;
}

}  // namespace kb
