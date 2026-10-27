// kb_options.cpp — the options window and the modules list.

#include "kb_shared.hpp"
#include "imgui/imgui_internal.h"
#include "win/local_mt_packages.hpp"
#include "voice_module.hpp"
#include <iomanip>
#include <sstream>

namespace kb {

int g_optionsTab = -1;  // a tab of the options window to bring to the front (General 0, Window 1, Voice 3, Translator 5)
static int s_delayedTab = -1;  // cross-frame safe tab switch (set during frame, applied on next BeginTabBar)



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

// Events: Chat – loaded, installed, missing; install / remove (g_mu NOT held). Options > Technical, the dashboard and
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
            static bool chatEventsToo = false;
            ImGui::TextUnformatted(UI(Tr(L"This deletes the folder GW2Keyboard in your addons folder: settings, learned words, my words, the voice and local translation modules and their models. It cannot be undone.")).c_str());
            ImGui::Checkbox(UI(Tr(L"Events: Chat too (other add-ons may use it)")).c_str(), &chatEventsToo);
            if (g_localMtBusy) {
                ImGui::TextDisabled("%s", UI(Tr(L"Wait for the local translation operation to finish.")).c_str());
            } else if (ImGui::Button(UI(Tr(L"Remove everything")).c_str())) {
                ImGui::CloseCurrentPopup();
                RemoveEverythingNow(chatEventsToo);
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
    static std::string s_optsTitle;
    static UiLang s_optsLang = static_cast<UiLang>(-1);
    if (s_optsLang != g_cfg.uiLang || s_optsTitle.empty()) {
        s_optsLang = g_cfg.uiLang;
        s_optsTitle = UI(Tr(L"GW2Chatlator – Options")) + "###kbopts";
    }
    const float nexusPx = FollowUiPx();
    RequestUiFont(nexusPx);
    ImFont* uf = g_uiFont.load();
    const float optsW = std::max(820.0f, 820.0f * (nexusPx / 16.0f));
    const float optsH = std::max(620.0f, 620.0f * (nexusPx / 16.0f));
    ImGui::SetNextWindowSize(ImVec2(optsW, optsH), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(660, 480), ImVec2(4000, 4000));
    const bool visible = ImGui::Begin(s_optsTitle.c_str(), &g_showOptions);
    if (visible && ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows)) {
        ImGui::GetIO().WantCaptureMouse = true;
    }
    bool pushedUf = false;
    if (uf && uf->FontSize > 0.0f) {
        ImGui::SetWindowFontScale(nexusPx / uf->FontSize);
        ImGui::PushFont(uf);
        pushedUf = true;
    } else {
        const float currentPx = std::max(1.0f, ImGui::GetFontSize());
        ImGui::SetWindowFontScale(nexusPx / currentPx);
    }
    if (visible) RenderOptionsContent();
    if (pushedUf) ImGui::PopFont();
    ImGui::End();
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

// Small pill toggle button for focus/settings ([on] in green / [off] in subtle grey); not usable: dimmed, clicks do
// nothing, the tooltip says why.
bool FocusSwitch(const char* id, const std::string& label, bool* val, const char* tooltip = nullptr, bool usable = true) {
    const bool on = *val;
    const ImVec4 col = !usable ? ImVec4(0.40f, 0.40f, 0.42f, 1.f)
                       : on    ? ImVec4(0.45f, 0.85f, 0.45f, 1.f)
                               : ImVec4(0.62f, 0.62f, 0.62f, 1.f);
    const std::string onText = UI(Tr(L"on")), offText = UI(Tr(L"off"));
    const float chipW = std::ceil(std::max(ImGui::CalcTextSize(onText.c_str()).x, ImGui::CalcTextSize(offText.c_str()).x) +
                                  ImGui::GetStyle().FramePadding.x * 2 + 6);
    ImGui::PushID(id);
    ImGui::PushStyleColor(ImGuiCol_Text, col);
    ImGui::PushStyleColor(ImGuiCol_Border, col);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    bool clicked = ImGui::Button((on ? onText : offText).c_str(), ImVec2(chipW, 0)) && usable;
    const bool btnHovered = ImGui::IsItemHovered();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    if (clicked) *val = !*val;
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    if (usable) ImGui::TextUnformatted(label.c_str());
    else ImGui::TextDisabled("%s", label.c_str());
    const bool textHovered = ImGui::IsItemHovered();
    if (usable && ImGui::IsItemClicked()) {
        *val = !*val;
        clicked = true;
    }
    if (tooltip && (btnHovered || textHovered)) {
        ImGui::SetTooltip("%s", tooltip);
    }
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
    if (ImGui::Button(fine ? "OK" : "--", ImVec2(chipW, 0))) s_delayedTab = tab;
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

bool IsOptionsDirty(const Config& edit, const Config& opened,
                    const char* keyBuf, const char* regionBuf, const char* urlBuf, const char* mailBuf) {
    if (edit.engine != opened.engine) return true;
    if (edit.writeIn != opened.writeIn) return true;
    if (edit.translateWriting != opened.translateWriting) return true;
    if (edit.voiceEnabled != opened.voiceEnabled) return true;
    if (edit.voiceTranslate != opened.voiceTranslate) return true;
    if (edit.spellEnabled != opened.spellEnabled) return true;
    if (edit.learnWords != opened.learnWords) return true;
    if (edit.focusOnGameChat != opened.focusOnGameChat) return true;
    if (edit.returnFocus != opened.returnFocus) return true;
    if (edit.fontPt != opened.fontPt) return true;
    if (edit.uiFontPt != opened.uiFontPt) return true;
    if (edit.fontFace != opened.fontFace) return true;
    if (edit.opacity != opened.opacity) return true;
    if (edit.uiLangCode != opened.uiLangCode) return true;
    if (edit.localQuality != opened.localQuality) return true;
    if (edit.understoodLangs != opened.understoodLangs) return true;
    if (edit.skipTypingLang != opened.skipTypingLang) return true;
    const std::string origKey = U8(opened.engine == Engine::DeepL ? opened.deeplKey : opened.engine == Engine::Google ? opened.googleKey
                                   : opened.engine == Engine::Microsoft ? opened.msKey : opened.libreKey);
    if (origKey != keyBuf) return true;
    if (U8(opened.msRegion) != regionBuf) return true;
    if (U8(opened.libreUrl) != urlBuf) return true;
    if (U8(opened.basicEmail) != mailBuf) return true;
    return false;
}

void DoSaveOptions(Config& edit, Config& opened,
                   const char* keyBuf, const char* regionBuf, const char* urlBuf, const char* mailBuf) {
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
        const bool writingSwitched = edit.translateWriting != g_cfg.translateWriting;
        const std::wstring sendWas = g_sendIdx >= 0 && g_sendIdx < static_cast<int>(g_cfg.writeLangs.size())
                                         ? g_cfg.writeLangs[static_cast<size_t>(g_sendIdx)]
                                         : std::wstring();
        edit.myMemoryDay = g_cfg.myMemoryDay;
        edit.myMemoryUsed = g_cfg.myMemoryUsed;
        // what the microphone menu switched meanwhile stays, unless it was changed here too
        if (edit.voiceAutoSend == opened.voiceAutoSend) edit.voiceAutoSend = g_cfg.voiceAutoSend;
        if (edit.voiceActivation == opened.voiceActivation) edit.voiceActivation = g_cfg.voiceActivation;
        if (edit.voiceTranslate == opened.voiceTranslate) edit.voiceTranslate = g_cfg.voiceTranslate;
        if (edit.writeLangs == opened.writeLangs) edit.writeLangs = g_cfg.writeLangs;
        if (edit.voiceModel == opened.voiceModel) edit.voiceModel = g_cfg.voiceModel;
        const bool engineChanged = edit.engine != g_cfg.engine;
        g_cfg = opened = edit;
        g_cfg.SaveAll();
        SetUiLang(g_cfg.uiLang);
        PrepareFaultAlert();  // the alert after a fault in the language chosen now
        g_translator = MakeTranslator(g_cfg);
        const int asWritten = static_cast<int>(g_cfg.writeLangs.size());
        const auto kept = std::find(g_cfg.writeLangs.begin(), g_cfg.writeLangs.end(), sendWas);
        if (writingSwitched) g_sendIdx = g_cfg.translateWriting ? 0 : asWritten;
        else if (sendWas.empty()) g_sendIdx = asWritten;
        else if (kept != g_cfg.writeLangs.end()) g_sendIdx = static_cast<int>(kept - g_cfg.writeLangs.begin());
        else g_sendIdx = g_cfg.translateWriting ? 0 : asWritten;
        g_preview.clear();
        ++g_inputGen;
        g_inputChanged = GetTickCount64();
        if (g_nx.translateChat && (typingChanged || engineChanged)) TranslateRecentLines(false);
        SetStatus(Tr(L"Saved"), 3000);
    }
    if (typingChanged) g_worker.Post([] {
        FreeWorkerState();
        InitSpelling();
    });
    else g_worker.Post([learn = edit.learnWords] { if (g_spell) g_spell->SetLearnChoices(learn); });
    ApplyPushToTalkKey(edit.voiceGlobalKey);
    if (!edit.voiceActivation || !edit.voiceEnabled) StopListening();
    else g_listenUserOff = false;
    if (voiceChanged && edit.voiceEnabled && !g_voice.Running() &&
        GetFileAttributesW(HelperExe().c_str()) != INVALID_FILE_ATTRIBUTES)
        g_worker.Post([dir = edit.dataDir] {
            Config::FlushWrites();
            std::lock_guard<std::mutex> lock(g_mu);
            if (!g_voice.Running()) g_voice.Start(HelperExe(), dir);
        });
    else if (g_voice.Running())
        VoiceReload();
}

enum class PillKind { Missing, Installed, Ready, Loading, Off };

void DrawPill(const char* label, PillKind kind) {
    ImVec4 borderCol, fillCol, textCol;
    switch (kind) {
        case PillKind::Installed:
        case PillKind::Ready:
            borderCol = ImVec4(0.24f, 0.86f, 0.55f, 0.8f);
            fillCol = ImVec4(0.24f, 0.86f, 0.55f, 0.14f);
            textCol = ImVec4(0.62f, 0.97f, 0.76f, 1.0f);
            break;
        case PillKind::Missing:
            borderCol = ImVec4(1.0f, 0.47f, 0.47f, 0.7f);
            fillCol = ImVec4(1.0f, 0.47f, 0.47f, 0.12f);
            textCol = ImVec4(1.0f, 0.70f, 0.70f, 1.0f);
            break;
        case PillKind::Loading:
            borderCol = ImVec4(0.35f, 0.75f, 1.0f, 0.8f);
            fillCol = ImVec4(0.35f, 0.75f, 1.0f, 0.14f);
            textCol = ImVec4(0.65f, 0.90f, 1.0f, 1.0f);
            break;
        case PillKind::Off:
        default:
            borderCol = ImVec4(0.32f, 0.35f, 0.42f, 0.8f);
            fillCol = ImVec4(0.20f, 0.22f, 0.27f, 0.4f);
            textCol = ImVec4(0.65f, 0.67f, 0.73f, 1.0f);
            break;
    }
    ImFont* font = ImGui::GetFont();
    const float fs = ImGui::GetFontSize() * 0.80f;
    const ImVec2 txtSize = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, label);
    const float padX = 8.0f;
    const float padY = 3.0f;
    const ImVec2 pillSize(txtSize.x + padX * 2.0f, txtSize.y + padY * 2.0f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = pillSize.y * 0.5f;
    dl->AddRectFilled(p, ImVec2(p.x + pillSize.x, p.y + pillSize.y), ImGui::GetColorU32(fillCol), r);
    dl->AddRect(p, ImVec2(p.x + pillSize.x, p.y + pillSize.y), ImGui::GetColorU32(borderCol), r, ImDrawCornerFlags_All, 1.0f);
    dl->AddText(font, fs, ImVec2(p.x + padX, p.y + padY), ImGui::GetColorU32(textCol), label);
    ImGui::Dummy(pillSize);
}


void SwitchOptionsTab(int tab) {
    s_delayedTab = tab;
    g_showOptions = true;
}

void DrawCardPill(ImDrawList* dl, ImVec2 topRight, const std::string& label, ImU32 fillCol, ImU32 borderCol, ImU32 textCol) {
    ImFont* font = ImGui::GetFont();
    const float fs = ImGui::GetFontSize() * 0.78f;
    const ImVec2 txtSize = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, label.c_str());
    const float padX = 7.0f;
    const float padY = 2.5f;
    const ImVec2 pillSize(txtSize.x + padX * 2.0f, txtSize.y + padY * 2.0f);
    const ImVec2 p(topRight.x - pillSize.x, topRight.y);
    const float r = pillSize.y * 0.5f;
    dl->AddRectFilled(p, ImVec2(p.x + pillSize.x, p.y + pillSize.y), fillCol, r);
    dl->AddRect(p, ImVec2(p.x + pillSize.x, p.y + pillSize.y), borderCol, r, ImDrawCornerFlags_All, 1.0f);
    dl->AddText(font, fs, ImVec2(p.x + padX, p.y + padY), textCol, label.c_str());
}

void DrawVectorChatIcon(ImDrawList* dl, ImVec2 c, float u, ImU32 col, ImU32 dotCol) {
    dl->AddRectFilled(ImVec2(c.x - 7 * u, c.y - 6 * u), ImVec2(c.x + 7 * u, c.y + 3 * u), col, 3 * u);
    dl->AddTriangleFilled(ImVec2(c.x - 4 * u, c.y + 3 * u), ImVec2(c.x - 0.5f * u, c.y + 3 * u), ImVec2(c.x - 5.5f * u, c.y + 6.5f * u), col);
    dl->AddCircleFilled(ImVec2(c.x - 3.5f * u, c.y - 1.5f * u), 1.0f * u, dotCol);
    dl->AddCircleFilled(ImVec2(c.x, c.y - 1.5f * u), 1.0f * u, dotCol);
    dl->AddCircleFilled(ImVec2(c.x + 3.5f * u, c.y - 1.5f * u), 1.0f * u, dotCol);
}

void DrawVectorMicIcon(ImDrawList* dl, ImVec2 c, float u, ImU32 col, bool slash = false) {
    dl->AddRectFilled(ImVec2(c.x - 2.5f * u, c.y - 6 * u), ImVec2(c.x + 2.5f * u, c.y + 1.5f * u), col, 2.5f * u);
    dl->PathArcTo(ImVec2(c.x, c.y - 1 * u), 4.5f * u, 0.0f, 3.14159f, 10);
    dl->PathStroke(col, false, 1.4f * u);
    dl->AddLine(ImVec2(c.x, c.y + 3.5f * u), ImVec2(c.x, c.y + 6 * u), col, 1.4f * u);
    dl->AddLine(ImVec2(c.x - 3 * u, c.y + 6 * u), ImVec2(c.x + 3 * u, c.y + 6 * u), col, 1.4f * u);
    if (slash) {
        // Strike-through slash from bottom-left to top-right (Discord style)
        dl->AddLine(ImVec2(c.x - 6.5f * u, c.y + 6.5f * u), ImVec2(c.x + 6.5f * u, c.y - 6.5f * u), IM_COL32(20, 22, 28, 255), 3.0f * u);
        dl->AddLine(ImVec2(c.x - 6.0f * u, c.y + 6.0f * u), ImVec2(c.x + 6.0f * u, c.y - 6.0f * u), col, 1.8f * u);
    }
}

void DrawVectorGlobeIcon(ImDrawList* dl, ImVec2 c, float u, ImU32 col) {
    dl->AddCircle(c, 6.5f * u, col, 16, 1.4f * u);
    dl->AddLine(ImVec2(c.x - 6.5f * u, c.y), ImVec2(c.x + 6.5f * u, c.y), col, 1.2f * u);
    dl->AddLine(ImVec2(c.x, c.y - 6.5f * u), ImVec2(c.x, c.y + 6.5f * u), col, 1.2f * u);
    dl->PathArcTo(ImVec2(c.x - 2.5f * u, c.y), 4.0f * u, -1.57f, 1.57f, 8);
    dl->PathStroke(col, false, 1.0f * u);
    dl->PathArcTo(ImVec2(c.x + 2.5f * u, c.y), 4.0f * u, 1.57f, 4.71f, 8);
    dl->PathStroke(col, false, 1.0f * u);
}

// The translators in the order the dashboard lists them; their names as in the Translator tab.
const wchar_t* EngineLabel(Engine e) {
    switch (e) {
        case Engine::Auto: return L"Automatic";
        case Engine::Basic: return L"MyMemory (free)";
        case Engine::DeepL: return L"DeepL";
        case Engine::Google: return L"Google";
        case Engine::Microsoft: return L"Microsoft";
        case Engine::Libre: return L"LibreTranslate";
        case Engine::Local: return L"OPUS-MT (local)";
        case Engine::Nllb: return L"NLLB-200 (local)";
        default: return L"MyMemory (free)";
    }
}

// The dashboard's choices apply at once, like the microphone menu: the setting, the INI, the running translator.
void ApplyEngineNow(Config& edit, Config& opened, Engine e) {
    edit.engine = opened.engine = e;
    std::lock_guard<std::mutex> lock(g_mu);
    g_cfg.engine = e;
    g_cfg.SaveValue(L"Translate", L"Engine", EngineKey(e));
    g_translator = MakeTranslator(g_cfg);
    g_preview.clear();
    ++g_inputGen;
    g_inputChanged = GetTickCount64();
    if (g_nx.translateChat) TranslateRecentLines(false);
    SetStatus(TrF(L"Translator: {1}", {g_translator ? g_translator->Name() : std::wstring(L"-")}), 3000);
}

void ApplyQualityNow(Config& edit, Config& opened, int quality) {
    edit.localQuality = opened.localQuality = quality;
    std::lock_guard<std::mutex> lock(g_mu);
    g_cfg.localQuality = quality;
    g_cfg.SaveValue(L"Translate", L"LocalQuality", std::to_wstring(quality));
}

// The voice model chosen on the dashboard: the setting, the INI, the helper reloads it (as the automatic fallback does).
void ApplyVoiceModelNow(Config& edit, Config& opened, const std::string& id) {
    edit.voiceModel = opened.voiceModel = W(id);
    {
        std::lock_guard<std::mutex> lock(g_mu);
        g_cfg.voiceModel = W(id);
        g_cfg.SaveValue(L"Voice", L"Model", g_cfg.voiceModel);
        g_modulesNext = 0;
    }
    VoiceReload();
}

// A button with a down chevron at its right end (the dashboard's choosers).
bool ChooserButton(const char* id, const std::string& label, float width, float height) {
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
    const bool clicked = ImGui::Button((label + id).c_str(), ImVec2(width, height));
    ImGui::PopStyleVar();
    const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
    const float u = height / 20.0f;
    const ImVec2 c(mx.x - 10.0f * u, (mn.y + mx.y) * 0.5f);
    const ImU32 col = ImGui::IsItemHovered() ? IM_COL32(255, 255, 255, 255) : IM_COL32(170, 175, 195, 255);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddLine(ImVec2(c.x - 3.5f * u, c.y - 1.5f * u), ImVec2(c.x, c.y + 2.0f * u), col, 1.5f * u);
    dl->AddLine(ImVec2(c.x, c.y + 2.0f * u), ImVec2(c.x + 3.5f * u, c.y - 1.5f * u), col, 1.5f * u);
    return clicked;
}

// Returns true when the translator was changed here without being applied (it needs a key first): the caller fills
// the Translator tab's key field for it.
bool RenderDashboard(Config& edit, Config& opened) {
    bool engineNeedsSetup = false;
    const ModuleState& m = Modules();
    const bool voiceInstalled = gct::VoiceModulePresent(KeyboardDir()) || m.voiceModule;
    const bool voiceActive = voiceInstalled && (edit.voiceEnabled || g_cfg.voiceEnabled);

    const LocalMtSummary local = LocalMtNow(edit.dataDir);
    const bool nllbReady = local.module && local.nllb;
    const bool mtInstalled = local.module && (local.nllb || local.opus);
    const bool isLocal = edit.engine == Engine::Nllb || edit.engine == Engine::Local;
    const bool localReady = local.module && (edit.engine == Engine::Nllb ? local.nllb : local.opus);
    bool chatOn = false, textOn = false, handsFree = false, voiceTrans = false, voiceAct = false;
    std::wstring running, quota;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        chatOn = g_nx.translateChat;
        textOn = g_cfg.translateWriting;
        running = g_translator ? g_translator->Name() : std::wstring();
        quota = MyMemoryQuotaText();
        handsFree = g_cfg.voiceAutoSend;
        voiceTrans = g_cfg.voiceTranslate;
        voiceAct = g_cfg.voiceActivation;
    }
    // "Install NLLB-200" from this tile: once the files are there it becomes the translator by itself.
    static bool s_nllbAfterInstall = false;
    if (s_nllbAfterInstall && !g_localMtBusy) {
        s_nllbAfterInstall = false;
        if (nllbReady) ApplyEngineNow(edit, opened, Engine::Nllb);
    }
    const std::wstring onOff[2] = {Tr(L"off"), Tr(L"on")};

    // The voice models as the voice tile shows them: the catalog and what is installed, read again every 2 s.
    struct VoiceModels {
        VoiceCatalog catalog;
        std::vector<bool> installed;
        ULONGLONG next = 0;
    };
    static VoiceModels s_vm;
    if (GetTickCount64() >= s_vm.next) {
        s_vm.next = GetTickCount64() + 2000;
        s_vm.catalog = LoadVoiceCatalog(edit.dataDir);
        s_vm.installed.clear();
        for (const VoiceModelInfo& vm : s_vm.catalog.models) s_vm.installed.push_back(VoiceModelInstalled(edit.VoiceModelDir(), vm));
    }
    const VoiceModelInfo* voiceModel = s_vm.catalog.Find(U8(edit.voiceModel));
    if (!voiceModel) voiceModel = s_vm.catalog.Recommended();
    bool voiceModelThere = false;
    for (size_t k = 0; k < s_vm.catalog.models.size(); ++k)
        if (voiceModel && s_vm.catalog.models[k].id == voiceModel->id) voiceModelThere = s_vm.installed[k];
    const int voiceModelMb = voiceModel ? static_cast<int>(voiceModel->bytes / (1024 * 1024)) : 0;
    std::wstring recLang = edit.voiceLang;  // the language it recognizes, as the Voice tab says it
    if (recLang.empty() || recLang == L"write" || recLang == L"auto") recLang = edit.writeIn.empty() ? L"en" : edit.writeIn;
    const std::string whisperLang = WhisperLang(recLang);
    const gct::VoiceRecommendation voiceRec = gct::RecommendVoiceModel(s_vm.catalog, voiceModel, whisperLang);
    VoiceRun voiceRun;
    if (voiceModel) {
        std::lock_guard<std::mutex> lock(g_mu);
        if (auto it = g_voiceRuns.find(W(voiceModel->id)); it != g_voiceRuns.end()) voiceRun = it->second;
    }

    // 2 Feature Tiles: Translation & Voice Module
    static float s_cardGlow[2] = {0.0f, 0.0f};
    static bool s_cardConfirm[2] = {false, false};

    struct CardDef {
        int id;
        std::string title;
        std::string desc;
        std::string badgeText;
        ImU32 badgeBg;
        ImU32 badgeBorder;
        ImU32 badgeTextCol;
        bool isOk;
        bool isBusy;
        int activePct;
        int actionTab;
        int iconType;  // 0 = globe, 1 = mic
    };

    CardDef cards[2];

    // Card 0: Translation – what runs, how, and what it translates
    cards[0].id = 0;
    cards[0].title = UI(Tr(L"Translation"));
    {
        std::wstring how;
        if (isLocal && !local.module) how = Tr(L"The local translation module is missing – click the tile to install it.");
        else if (isLocal && !localReady) how = Tr(L"The translation model is missing – Options → Translator.");
        else if (isLocal && local.helper.status == gct::HelperStatus::Outdated)
            how = TrF(L"Helper is outdated ({1})", {local.helper.outdatedReason});
        else if (isLocal) how = TrF(L"Offline on this PC · Beam {1}", {std::to_wstring(edit.localQuality)});
        else if (edit.engine == Engine::Basic || edit.engine == Engine::Auto) how = quota.empty() ? Tr(L"Free and online – works without setup") : quota;
        else how = Tr(L"Online with your API key");
        cards[0].desc = UI(running.empty() ? Tr(EngineLabel(edit.engine)) : running) + "\n" + UI(how) + "\n" +
                        UI(TrF(L"Chat: {1} · My text: {2}", {onOff[chatOn], onOff[textOn]}));
    }
    cards[0].isOk = mtInstalled;
    cards[0].isBusy = g_localMtBusy;
    cards[0].activePct = g_localMtPct.load();
    cards[0].actionTab = 5;
    cards[0].iconType = 0;
    if (chatOn || textOn) {
        cards[0].badgeText = UI(Tr(L"Active"));
        cards[0].badgeBg = IM_COL32(35, 75, 50, 255);
        cards[0].badgeBorder = IM_COL32(61, 220, 140, 200);
        cards[0].badgeTextCol = IM_COL32(61, 220, 140, 255);
    } else {
        cards[0].badgeText = UI(Tr(L"Off"));
        cards[0].badgeBg = IM_COL32(45, 48, 55, 255);
        cards[0].badgeBorder = IM_COL32(75, 80, 95, 200);
        cards[0].badgeTextCol = IM_COL32(170, 175, 185, 255);
    }

    // Card 1: Voice Module – the model, how well it knows your language, how fast it was the last time
    cards[1].id = 1;
    cards[1].title = UI(Tr(L"Voice Module"));
    cards[1].isOk = voiceInstalled;
    cards[1].isBusy = g_voiceModuleInstalling || (g_downloadPct >= 0);
    const int vPct = g_downloadPct >= 0 ? g_downloadPct.load() : g_voiceModulePct.load();
    cards[1].activePct = vPct;
    cards[1].actionTab = 3;
    cards[1].iconType = 1;
    {
        const std::wstring quality = voiceRec.supportedByWhisper
                                         ? TrF(L"Recognition of {1}: {2}", {NativeName(recLang), gct::VoiceQualityWord(voiceRec.quality)})
                                         : Tr(L"Language not supported by speech recognition");
        std::wstring third;
        if (!voiceInstalled) third = Tr(L"Recognizes on this PC – no sound leaves it.");
        else if (!voiceActive) third = Tr(L"Installed – switched off.");
        else if (!voiceModelThere) third = TrF(L"The model is not downloaded yet ({1} MB).", {std::to_wstring(voiceModelMb)});
        else if (voiceRun.rec > 0)
            third = TrF(L"peak {1} % CPU (highest {2} %) · {3} s for {4} s speech",
                        {std::to_wstring(voiceRun.peak), std::to_wstring(voiceRun.maxPeak), FormatSeconds(voiceRun.rms),
                         FormatSeconds(voiceRun.rec)});
        else third = TrF(L"Speed: {1}", {Tr(L"not measured yet")});
        // switched on: the microphone mode right under the model (drawn in the accent colour)
        const std::wstring mode = !voiceActive ? std::wstring()
                                  : (voiceAct ? Tr(L"Voice activation") : Tr(L"Push-to-Talk")) +
                                        (handsFree ? L" · " + TrF(L"Hands free: {1}", {onOff[1]}) : std::wstring());
        cards[1].desc = (voiceModel ? voiceModel->name + "  ·  " + std::to_string(voiceModelMb) + " MB" : std::string("-")) + "\n" +
                        (mode.empty() ? std::string() : UI(mode) + "\n") + UI(quality) + "\n" + UI(third);
    }
    if (voiceActive) {
        cards[1].badgeText = UI(Tr(L"Active"));
        cards[1].badgeBg = IM_COL32(35, 75, 50, 255);
        cards[1].badgeBorder = IM_COL32(61, 220, 140, 200);
        cards[1].badgeTextCol = IM_COL32(61, 220, 140, 255);
    } else if (voiceInstalled) {
        cards[1].badgeText = UI(Tr(L"Disabled"));
        cards[1].badgeBg = IM_COL32(55, 45, 20, 255);
        cards[1].badgeBorder = IM_COL32(200, 155, 60, 200);
        cards[1].badgeTextCol = IM_COL32(240, 195, 80, 255);
    } else {
        cards[1].badgeText = UI(Tr(L"Optional"));
        cards[1].badgeBg = IM_COL32(45, 48, 55, 255);
        cards[1].badgeBorder = IM_COL32(75, 80, 95, 200);
        cards[1].badgeTextCol = IM_COL32(170, 175, 185, 255);
    }

    if (ImGui::BeginTable("##dashboard_tiles", 2, ImGuiTableFlags_SizingStretchSame)) {
        for (int i = 0; i < 2; ++i) {
            ImGui::TableNextColumn();
            ImGui::PushID(i);

            // Disarm the confirmation once its target is there or installing (the translation tile installs NLLB-200)
            const bool confirmDone = i == 0 ? nllbReady : cards[i].isOk;
            if (confirmDone || cards[i].isBusy) {
                s_cardConfirm[i] = false;
            }

            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const float colW = ImGui::GetContentRegionAvail().x;
            // The tiles grow with their text – Nexus' large font, longer languages: both as high as the one that needs
            // most (measured while drawing, used from the next frame on).
            static float s_cardNeed[2] = {0.0f, 0.0f};
            const float cardH = std::max({180.0f, s_cardNeed[0], s_cardNeed[1]});
            const ImVec2 size(colW, cardH);
            const float pad = 12.0f;

            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImFont* font = ImGui::GetFont();
            const float fs = ImGui::GetFontSize();

            if (cards[i].isBusy) {
                // ---- STATE 1: Professional Integration & Progress View
                const ImVec2 cPos = pos;

                // Ambient Drop Shadow
                dl->AddRectFilled(
                    ImVec2(pos.x + 3.0f, pos.y + 4.0f),
                    ImVec2(pos.x + size.x - 3.0f, pos.y + size.y + 5.0f),
                    IM_COL32(0, 0, 0, 75), 12.0f
                );

                // Elevated Card Rect
                dl->AddRectFilled(cPos, ImVec2(cPos.x + size.x, cPos.y + size.y), IM_COL32(26, 29, 38, 250), 10.0f);
                dl->AddRect(cPos, ImVec2(cPos.x + size.x, cPos.y + size.y), IM_COL32(88, 101, 242, 220), 10.0f, ImDrawCornerFlags_All, 1.5f);
                dl->AddLine(ImVec2(cPos.x + 10.0f, cPos.y + 1.0f), ImVec2(cPos.x + size.x - 10.0f, cPos.y + 1.0f),
                            IM_COL32(255, 255, 255, 35), 1.0f);

                // Animated Pulsing Icon
                const float pulse = 0.5f + 0.5f * sinf(static_cast<float>(GetTickCount64()) * 0.005f);
                const ImVec2 iconCenter(cPos.x + pad + 14.0f, cPos.y + pad + 14.0f);
                dl->AddCircle(iconCenter, 14.0f + pulse * 4.0f, IM_COL32(88, 101, 242, static_cast<int>(pulse * 140)), 20, 1.5f);
                if (cards[i].iconType == 0) DrawVectorGlobeIcon(dl, iconCenter, 1.25f, IM_COL32(99, 130, 255, 255));
                else DrawVectorMicIcon(dl, iconCenter, 1.25f, IM_COL32(99, 130, 255, 255));

                // Pulsing pill badge top-right
                const ImU32 pillFill = IM_COL32(32, 48, 80 + static_cast<int>(pulse * 30), 255);
                DrawCardPill(dl, ImVec2(cPos.x + size.x - pad, cPos.y + pad + 4.0f),
                             UI(Tr(L"Installing…")), pillFill, IM_COL32(88, 101, 242, 230), IM_COL32(145, 190, 255, 255));

                // Title
                const std::string progTitle = (i == 0) ? UI(Tr(L"Integrating Translation Module…")) : UI(Tr(L"Integrating Voice Module…"));
                dl->AddText(font, fs * 1.05f, ImVec2(cPos.x + pad, cPos.y + pad + 36.0f), IM_COL32(245, 246, 250, 255), progTitle.c_str());

                // Subtitle (current stage)
                std::string stageText;
                if (i == 0) {
                    stageText = UI(Tr(L"Downloading neural translation model…"));
                } else {
                    if (g_voiceModuleInstalling) {
                        stageText = UI(Tr(L"Downloading whisper speech engine…"));
                    } else if (g_downloadPct >= 0) {
                        stageText = UI(Tr(L"Downloading speech recognition model…"));
                    } else {
                        stageText = UI(Tr(L"Integrating Voice Module…"));
                    }
                }
                dl->AddText(font, fs * 0.84f, ImVec2(cPos.x + pad, cPos.y + pad + 60.0f), IM_COL32(165, 178, 208, 255), stageText.c_str());

                // Professional Progress Bar
                const ImVec2 barPos(cPos.x + pad, cPos.y + 100.0f);
                const ImVec2 barSize(size.x - pad * 2.0f, 24.0f);
                dl->AddRectFilled(barPos, ImVec2(barPos.x + barSize.x, barPos.y + barSize.y), IM_COL32(16, 18, 25, 255), 12.0f);
                dl->AddRect(barPos, ImVec2(barPos.x + barSize.x, barPos.y + barSize.y), IM_COL32(48, 54, 72, 220), 12.0f, ImDrawCornerFlags_All, 1.0f);

                const int pctVal = cards[i].activePct;
                const float fillFrac = std::clamp(pctVal >= 0 ? pctVal / 100.0f : 0.0f, 0.0f, 1.0f);
                const float fillW = (pctVal >= 0) ? std::max(barSize.y, barSize.x * fillFrac) : 0.0f;

                if (pctVal >= 0 && fillW > 0.0f) {
                    dl->AddRectFilled(barPos, ImVec2(barPos.x + fillW, barPos.y + barSize.y), IM_COL32(78, 120, 240, 255), 12.0f);

                    // Specular shimmer sweep
                    const float cycle = fmodf(static_cast<float>(GetTickCount64()) * 0.0006f, 1.0f);
                    const float shimmerX = barPos.x + (fillW + 40.0f) * cycle - 20.0f;
                    if (shimmerX >= barPos.x && shimmerX + 18.0f <= barPos.x + fillW) {
                        dl->AddRectFilled(ImVec2(shimmerX, barPos.y + 2.0f), ImVec2(shimmerX + 18.0f, barPos.y + barSize.y - 2.0f),
                                          IM_COL32(255, 255, 255, 60), 6.0f);
                    }
                    // Glass reflex line
                    dl->AddLine(ImVec2(barPos.x + 8.0f, barPos.y + 2.0f), ImVec2(barPos.x + fillW - 8.0f, barPos.y + 2.0f),
                                IM_COL32(255, 255, 255, 65), 1.0f);
                } else if (pctVal < 0) {
                    const float cycle = 0.5f + 0.5f * sinf(static_cast<float>(GetTickCount64()) * 0.004f);
                    const float bW = barSize.x * 0.35f;
                    const float bX = barPos.x + (barSize.x - bW) * cycle;
                    dl->AddRectFilled(ImVec2(bX, barPos.y + 2.0f), ImVec2(bX + bW, barPos.y + barSize.y - 2.0f),
                                      IM_COL32(78, 120, 240, 220), 10.0f);
                }

                // Centered percentage text with shadow
                const std::string pctText = (pctVal >= 0) ? (std::to_string(pctVal) + " %") : UI(Tr(L"Installing…"));
                const ImVec2 ts = font->CalcTextSizeA(fs * 0.88f, 1000.0f, 0.0f, pctText.c_str());
                const ImVec2 textPos(barPos.x + (barSize.x - ts.x) * 0.5f, barPos.y + (barSize.y - ts.y) * 0.5f);
                dl->AddText(font, fs * 0.88f, ImVec2(textPos.x + 1.0f, textPos.y + 1.0f), IM_COL32(0, 0, 0, 220), pctText.c_str());
                dl->AddText(font, fs * 0.88f, textPos, IM_COL32(255, 255, 255, 255), pctText.c_str());

                // Telemetry / hint text below bar
                dl->AddText(font, fs * 0.80f, ImVec2(cPos.x + pad, cPos.y + 138.0f), IM_COL32(130, 138, 155, 255),
                            UI(Tr(L"Downloading in background – game remains playable")).c_str());

                ImGui::SetCursorScreenPos(pos);
                ImGui::Dummy(size);
            } else if (s_cardConfirm[i] && !confirmDone) {
                // ---- STATE 2: In-Tile Confirmation View
                const ImVec2 cPos = pos;

                // Ambient Drop Shadow
                dl->AddRectFilled(
                    ImVec2(pos.x + 3.0f, pos.y + 4.0f),
                    ImVec2(pos.x + size.x - 3.0f, pos.y + size.y + 5.0f),
                    IM_COL32(0, 0, 0, 75), 12.0f
                );

                // Elevated Card Rect with Accent Border
                dl->AddRectFilled(cPos, ImVec2(cPos.x + size.x, cPos.y + size.y), IM_COL32(26, 28, 36, 250), 10.0f);
                dl->AddRect(cPos, ImVec2(cPos.x + size.x, cPos.y + size.y), IM_COL32(88, 101, 242, 230), 10.0f, ImDrawCornerFlags_All, 1.5f);
                dl->AddLine(ImVec2(cPos.x + 10.0f, cPos.y + 1.0f), ImVec2(cPos.x + size.x - 10.0f, cPos.y + 1.0f),
                            IM_COL32(255, 255, 255, 30), 1.0f);

                // Vector Icon
                const ImVec2 iconCenter(cPos.x + pad + 14.0f, cPos.y + pad + 14.0f);
                if (cards[i].iconType == 0) DrawVectorGlobeIcon(dl, iconCenter, 1.25f, IM_COL32(99, 130, 255, 255));
                else DrawVectorMicIcon(dl, iconCenter, 1.25f, IM_COL32(99, 130, 255, 255));

                // Cancel badge top-right
                DrawCardPill(dl, ImVec2(cPos.x + size.x - pad, cPos.y + pad + 4.0f),
                             UI(Tr(L"Cancel")), IM_COL32(40, 44, 55, 255), IM_COL32(75, 80, 95, 200), IM_COL32(170, 175, 185, 255));

                // Confirmation Title
                const std::string confTitle = (i == 0) ? UI(Tr(L"Install NLLB-200 (local)")) : UI(Tr(L"Download Voice Module"));
                dl->AddText(font, fs * 1.05f, ImVec2(cPos.x + pad, cPos.y + pad + 34.0f), IM_COL32(245, 246, 250, 255), confTitle.c_str());

                // Description
                const std::string confDesc = (i == 0)
                    ? UI(Tr(L"Local Translation installs the local neural machine translation engine and NLLB-200 multilingual model."))
                    : UI(TrF(L"The voice module installs the local whisper speech recognition and the model {1}.",
                             {voiceModel ? W(voiceModel->name) : std::wstring(L"-")}));
                const float descW = size.x - pad * 2.0f;
                dl->AddText(font, fs * 0.82f, ImVec2(cPos.x + pad, cPos.y + pad + 56.0f), IM_COL32(160, 166, 180, 255),
                            confDesc.c_str(), nullptr, descW);

                // Specs / Size info
                const std::string confSize = (i == 0)
                    ? UI(Tr(L"Download size: about 600 MB · translates offline on this PC"))
                    : UI(TrF(L"Download size: about {1} MB · recognizes on this PC", {std::to_wstring(voiceModelMb + 7)}));
                dl->AddText(font, fs * 0.80f, ImVec2(cPos.x + pad, cPos.y + pad + 96.0f), IM_COL32(130, 195, 160, 255), confSize.c_str());

                // Bottom Action Buttons: Big "OK" button & "Cancel" button
                const float btnH = 32.0f;
                const float btnY = cPos.y + size.y - pad - btnH;
                const float spacing = 8.0f;
                const float totalW = size.x - pad * 2.0f;
                const float okW = std::floor(totalW * 0.68f);
                const float cancelW = totalW - okW - spacing;

                ImGui::SetCursorScreenPos(ImVec2(cPos.x + pad, btnY));
                ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(61, 220, 140, 240));        // emerald green
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(80, 235, 155, 255)); // brighter green
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(45, 185, 115, 255));  // deep green
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(15, 30, 20, 255));            // high contrast dark text
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);

                if (ImGui::Button((UI(Tr(L"OK")) + "##confirm_ok").c_str(), ImVec2(okW, btnH))) {
                    s_cardConfirm[i] = false;
                    if (i == 0) {
                        InstallLocalMtQuickSetup(edit.dataDir);
                        s_nllbAfterInstall = true;  // becomes the translator once the files are there
                    } else if (i == 1) {
                        InstallVoiceQuickSetup();
                        edit.voiceEnabled = true;
                    }
                }
                if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                ImGui::PopStyleVar();
                ImGui::PopStyleColor(4);

                ImGui::SameLine(0.0f, spacing);
                ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(40, 44, 55, 200));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(56, 62, 78, 240));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(30, 33, 42, 255));
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(200, 205, 220, 255));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);

                if (ImGui::Button((UI(Tr(L"Cancel")) + "##confirm_cancel").c_str(), ImVec2(cancelW, btnH))) {
                    s_cardConfirm[i] = false;
                }
                if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                ImGui::PopStyleVar();
                ImGui::PopStyleColor(4);

                ImGui::SetCursorScreenPos(pos);
                ImGui::Dummy(size);
            } else {
                // ---- STATE 3: Normal Card View (Active, Disabled, or Uninstalled)
                const float btnH = std::max(28.0f, std::ceil(fs + ImGui::GetStyle().FramePadding.y * 2.0f + 4.0f));
                const float topH = size.y - pad * 1.5f - btnH;

                // Stateless interaction bounds for the top/body area
                ImGui::SetCursorScreenPos(pos);
                ImGui::InvisibleButton("##tile_top_btn", ImVec2(size.x, topH));
                ImGui::SetItemAllowOverlap();
                const bool topHovered = ImGui::IsItemHovered();
                const bool topActive = ImGui::IsItemActive();
                const bool topClicked = ImGui::IsItemClicked();

                if (topHovered) {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                }

                // Smooth stateless glow transition (frame-rate independent lerp)
                const float dt = ImGui::GetIO().DeltaTime;
                const float targetGlow = topHovered ? 1.0f : 0.0f;
                s_cardGlow[i] += (targetGlow - s_cardGlow[i]) * std::clamp(dt * 12.0f, 0.0f, 1.0f);

                // Elevation: hover lift (-2px) or click depression (+1px)
                const float liftY = topActive ? 1.0f : (topHovered ? -2.0f : 0.0f);
                const ImVec2 cPos(pos.x, pos.y + liftY);

                // Ambient Drop Shadow
                dl->AddRectFilled(
                    ImVec2(pos.x + 3.0f, pos.y + (topHovered ? 6.0f : 3.0f)),
                    ImVec2(pos.x + size.x - 3.0f, pos.y + size.y + (topHovered ? 7.0f : 4.0f)),
                    IM_COL32(0, 0, 0, topHovered ? 95 : 55),
                    12.0f
                );

                const float g = std::clamp(s_cardGlow[i], 0.0f, 1.0f);
                const ImU32 baseBg = IM_COL32(
                    static_cast<int>(24 + g * 12),
                    static_cast<int>(26 + g * 14),
                    static_cast<int>(34 + g * 18),
                    250
                );
                const ImU32 borderCol = IM_COL32(
                    static_cast<int>(45 + g * 80),
                    static_cast<int>(48 + g * 85),
                    static_cast<int>(60 + g * 140),
                    static_cast<int>(200 + g * 55)
                );

                // Subtle border glow if hovered
                if (g > 0.01f) {
                    const ImU32 glowCol = IM_COL32(88, 101, 242, static_cast<int>(g * 180));
                    dl->AddRect(ImVec2(cPos.x - 1.5f, cPos.y - 1.5f), ImVec2(cPos.x + size.x + 1.5f, cPos.y + size.y + 1.5f),
                                glowCol, 10.0f, ImDrawCornerFlags_All, 2.0f);
                }

                // Tile card rect (elevated)
                dl->AddRectFilled(cPos, ImVec2(cPos.x + size.x, cPos.y + size.y), baseBg, 10.0f);
                dl->AddRect(cPos, ImVec2(cPos.x + size.x, cPos.y + size.y), borderCol, 10.0f, ImDrawCornerFlags_All, 1.0f);

                // Top highlight reflex
                dl->AddLine(
                    ImVec2(cPos.x + 10.0f, cPos.y + 1.0f),
                    ImVec2(cPos.x + size.x - 10.0f, cPos.y + 1.0f),
                    IM_COL32(255, 255, 255, topHovered ? 45 : 20),
                    1.0f
                );

                // Inner layout
                const ImVec2 iconCenter(cPos.x + pad + 14.0f, cPos.y + pad + 14.0f);
                ImU32 iconCol = IM_COL32(160, 170, 200, 255);
                bool micSlash = false;
                bool micControlHit = false;
                if (i == 0) {
                    iconCol = (chatOn || textOn) ? IM_COL32(61, 220, 140, 255) : IM_COL32(160, 170, 200, 255);
                    DrawVectorGlobeIcon(dl, iconCenter, 1.25f, iconCol);
                } else if (i == 1) {
                    if (voiceActive) {
                        const float micH = 24.0f;
                        const float micW = std::floor(micH * 1.15f);
                        const float arrowW = std::floor(micH * 0.75f);
                        const float totalW = micW + 1.0f + arrowW;
                        const ImVec2 micMin(cPos.x + pad, cPos.y + pad + 2.0f);
                        const ImVec2 micMax(micMin.x + totalW, micMin.y + micH);
                        micControlHit = ImGui::IsMouseHoveringRect(micMin, micMax);
                        ImGui::SetCursorScreenPos(micMin);
                        MicControl("##dash_top_mic", micH, nullptr);
                    } else {
                        if (voiceInstalled) {
                            iconCol = IM_COL32(240, 195, 80, 255);
                            micSlash = true;
                        } else {
                            iconCol = IM_COL32(160, 170, 200, 255);
                        }
                        DrawVectorMicIcon(dl, iconCenter, 1.25f, iconCol, micSlash);
                    }
                }

                // Pill badge top-right
                DrawCardPill(dl, ImVec2(cPos.x + size.x - pad, cPos.y + pad + 4.0f),
                             cards[i].badgeText, cards[i].badgeBg, cards[i].badgeBorder, cards[i].badgeTextCol);

                // Card Title: in the top row, right of the symbol (the microphone control when voice is on)
                const float iconW = (i == 1 && voiceActive) ? std::floor(24.0f * 1.15f) + 1.0f + std::floor(24.0f * 0.75f) : 28.0f;
                dl->AddText(font, fs * 1.05f, ImVec2(cPos.x + pad + iconW + 10.0f, cPos.y + pad + 14.0f - fs * 0.55f),
                            IM_COL32(245, 246, 250, 255), cards[i].title.c_str());

                // Card Description: the first line (what runs) bright, the voice mode in the accent colour, the rest
                // (how) grey; each line placed below the one before, wrapped ones included
                const float descW = size.x - pad * 2.0f;
                float lineY = cPos.y + std::max(pad + 40.0f, pad + 14.0f + fs * 0.5f + 10.0f);  // below the title row
                const std::string& desc = cards[i].desc;
                for (size_t start = 0, ln = 0; start < desc.size(); ++ln) {
                    size_t end = desc.find('\n', start);
                    if (end == std::string::npos) end = desc.size();
                    const std::string line = desc.substr(start, end - start);
                    const float sz = ln == 0 ? fs * 0.92f : fs * 0.82f;
                    const ImU32 col = ln == 0                              ? IM_COL32(215, 220, 232, 255)
                                      : (i == 1 && voiceActive && ln == 1) ? ImGui::GetColorU32(ImGuiCol_CheckMark)
                                                                           : IM_COL32(150, 155, 170, 255);
                    dl->AddText(font, sz, ImVec2(cPos.x + pad, lineY), col, line.c_str(), nullptr, descW);
                    lineY += font->CalcTextSizeA(sz, FLT_MAX, descW, line.c_str()).y + (ln == 0 ? 3.0f : 1.0f);
                    start = end + 1;
                }

                s_cardNeed[i] = std::ceil(lineY - cPos.y + 8.0f + btnH + pad);

                // Bottom Buttons
                const float availW = size.x - pad * 2.0f;
                const float actualBtnY = cPos.y + size.y - pad - btnH;

                if (i == 0) {
                    // Card 0: Translation – left the translator (chooser, opens upwards), right the quality of a local
                    // one or the way to the Translator tab
                    const float gap = 8.0f;
                    const bool showQuality = isLocal && localReady;
                    const float leftW = std::floor((availW - gap) * 0.6f);
                    const float rightW = availW - gap - leftW;
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.13f, 0.15f, 0.20f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.24f, 0.35f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.25f, 0.30f, 0.42f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.30f, 0.35f, 0.48f, 0.8f));
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
                    ImGui::SetCursorScreenPos(ImVec2(cPos.x + pad, actualBtnY));
                    static ImVec2 s_engineAnchor;
                    if (ChooserButton("##dash_engine", UI(Tr(EngineLabel(edit.engine))), leftW, btnH)) {
                        s_engineAnchor = ImGui::GetItemRectMin();
                        ImGui::OpenPopup("##dash_engine_pop");
                    }
                    ImGui::SetCursorScreenPos(ImVec2(cPos.x + pad + leftW + gap, actualBtnY));
                    static const int kQuality[3] = {1, 2, 4};
                    static const wchar_t* kStep[3] = {L"Fast", L"Balanced", L"Best"};
                    static const wchar_t* kStepTip[3] = {L"Beam 1 – about 350 ms per line, the lowest CPU load",
                                                         L"Beam 2 – about 500 ms per line, recommended",
                                                         L"Beam 4 – about 900 ms per line, the finest nuance"};
                    const int qi = edit.localQuality == 1 ? 0 : edit.localQuality == 4 ? 2 : 1;
                    static ImVec2 s_qualityAnchor;
                    if (showQuality) {  // the three steps, as a chooser: their names fit in every language
                        if (ChooserButton("##dash_quality", UI(Tr(kStep[qi])), rightW, btnH)) {
                            s_qualityAnchor = ImGui::GetItemRectMin();
                            ImGui::OpenPopup("##dash_quality_pop");
                        }
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", UI(Tr(kStepTip[qi])).c_str());
                    } else {
                        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));
                        if (ImGui::Button((UI(Tr(L"Translator")) + " ->##dash_c0_btn").c_str(), ImVec2(rightW, btnH)))
                            SwitchOptionsTab(5);
                        ImGui::PopStyleVar();
                    }
                    ImGui::PopStyleVar(2);
                    ImGui::PopStyleColor(4);

                    ImGui::SetNextWindowPos(ImVec2(s_qualityAnchor.x, s_qualityAnchor.y - 4.0f), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
                    if (ImGui::BeginPopup("##dash_quality_pop")) {
                        ImGui::TextDisabled("%s", UI(Tr(L"Quality Mode")).c_str());
                        for (int q = 0; q < 3; ++q) {
                            if (ImGui::Selectable((UI(Tr(kStep[q])) + "   " + UI(Tr(kStepTip[q]))).c_str(), q == qi) && q != qi)
                                ApplyQualityNow(edit, opened, kQuality[q]);
                        }
                        ImGui::EndPopup();
                    }
                    ImGui::SetNextWindowPos(ImVec2(s_engineAnchor.x, s_engineAnchor.y - 4.0f), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
                    if (ImGui::BeginPopup("##dash_engine_pop")) {
                        Engine chosen = edit.engine;
                        bool picked = false;
                        auto item = [&](Engine e, const std::wstring& note) {
                            std::string text = UI(Tr(EngineLabel(e)));
                            if (!note.empty()) text += "   " + UI(note);
                            if (ImGui::Selectable(text.c_str(), edit.engine == e)) {
                                chosen = e;
                                picked = true;
                            }
                        };
                        ImGui::TextDisabled("%s", UI(Tr(L"Ready right away")).c_str());
                        item(Engine::Basic, {});
                        item(Engine::Auto, {});
                        ImGui::Separator();
                        ImGui::TextDisabled("%s", UI(Tr(L"On this PC (offline)")).c_str());
                        item(Engine::Nllb, nllbReady ? std::wstring() : Tr(L"install, about 600 MB"));
                        item(Engine::Local, local.module && local.opus ? std::wstring() : Tr(L"choose a language pair first"));
                        ImGui::Separator();
                        ImGui::TextDisabled("%s", UI(Tr(L"With your own API key")).c_str());
                        const std::wstring keyNeeded = Tr(L"key needed");
                        item(Engine::DeepL, edit.deeplKey.empty() ? keyNeeded : std::wstring());
                        item(Engine::Google, edit.googleKey.empty() ? keyNeeded : std::wstring());
                        item(Engine::Microsoft, edit.msKey.empty() ? keyNeeded : std::wstring());
                        item(Engine::Libre, edit.libreUrl.empty() ? Tr(L"server needed") : std::wstring());
                        ImGui::EndPopup();
                        if (picked) {
                            const bool needsKey = (chosen == Engine::DeepL && edit.deeplKey.empty()) ||
                                                  (chosen == Engine::Google && edit.googleKey.empty()) ||
                                                  (chosen == Engine::Microsoft && edit.msKey.empty()) ||
                                                  (chosen == Engine::Libre && edit.libreUrl.empty());
                            if (chosen == Engine::Nllb && !nllbReady) {
                                s_cardConfirm[0] = true;  // the size first, then OK installs and switches over
                            } else if (chosen == Engine::Local && !(local.module && local.opus)) {
                                SwitchOptionsTab(5);      // a language pair is chosen there
                            } else if (needsKey) {
                                edit.engine = chosen;     // the key field waits in the Translator tab; Save applies it
                                engineNeedsSetup = true;
                                SwitchOptionsTab(5);
                            } else if (chosen != edit.engine || chosen != opened.engine) {
                                ApplyEngineNow(edit, opened, chosen);
                            }
                        }
                    }
                } else if (i == 1) {
                    // Card 1: Voice – the model chooser (opens upwards; the mode is in the microphone control above),
                    // beside it a button only when there is something to do: install, switch on, download the model
                    const float gap = 8.0f;
                    enum class VoiceAction { None, Install, Enable, Download } action = VoiceAction::None;
                    if (!voiceInstalled) action = VoiceAction::Install;
                    else if (!voiceActive) action = VoiceAction::Enable;
                    else if (!voiceModelThere) action = VoiceAction::Download;
                    const float leftW = action == VoiceAction::None ? availW : std::floor((availW - gap) * 0.55f);
                    ImGui::SetCursorScreenPos(ImVec2(cPos.x + pad, actualBtnY));
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.16f, 0.22f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.26f, 0.38f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.18f, 0.22f, 0.32f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.32f, 0.38f, 0.52f, 0.7f));
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
                    static ImVec2 s_modelAnchor;
                    if (ChooserButton("##dash_voice_model", voiceModel ? voiceModel->name : std::string("-"), leftW, btnH)) {
                        s_modelAnchor = ImGui::GetItemRectMin();
                        ImGui::OpenPopup("##dash_model_pop");
                    }
                    ImGui::PopStyleVar(2);
                    ImGui::PopStyleColor(4);
                    if (action != VoiceAction::None) {
                        const bool blue = action != VoiceAction::Enable;
                        ImGui::SetCursorScreenPos(ImVec2(cPos.x + pad + leftW + gap, actualBtnY));
                        ImGui::PushStyleColor(ImGuiCol_Button, blue ? ImVec4(0.35f, 0.40f, 0.95f, 1.0f) : ImVec4(0.15f, 0.45f, 0.25f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, blue ? ImVec4(0.42f, 0.48f, 1.0f, 1.0f) : ImVec4(0.20f, 0.58f, 0.32f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive, blue ? ImVec4(0.30f, 0.35f, 0.85f, 1.0f) : ImVec4(0.12f, 0.38f, 0.20f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_Border, blue ? ImVec4(0.48f, 0.55f, 1.0f, 0.8f) : ImVec4(0.25f, 0.85f, 0.45f, 0.9f));
                        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
                        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
                        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));
                        const std::string label = action == VoiceAction::Install ? UI(Tr(L"Install"))
                                                  : action == VoiceAction::Enable ? UI(Tr(L"Enable"))
                                                  : UI(TrF(L"Load model ({1} MB)", {std::to_wstring(voiceModelMb)}));
                        if (ImGui::Button((label + "##dash_voice_act").c_str(), ImVec2(availW - gap - leftW, btnH))) {
                            if (action == VoiceAction::Install) {
                                s_cardConfirm[1] = true;  // the size first, then OK installs the module with this model
                            } else if (action == VoiceAction::Enable) {
                                edit.voiceEnabled = opened.voiceEnabled = true;
                                std::lock_guard<std::mutex> lock(g_mu);
                                g_cfg.voiceEnabled = true;
                                g_cfg.SaveBool(L"Voice", L"Enabled", true);
                            } else if (voiceModel) {
                                StartModelDownload(*voiceModel);
                            }
                        }
                        ImGui::PopStyleVar(3);
                        ImGui::PopStyleColor(4);
                    }

                    ImGui::SetNextWindowPos(ImVec2(s_modelAnchor.x, s_modelAnchor.y - 4.0f), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
                    if (ImGui::BeginPopup("##dash_model_pop")) {
                        ImGui::TextDisabled("%s", UI(TrF(L"Voice model · recognition of {1}", {NativeName(recLang)})).c_str());
                        for (size_t k = 0; k < s_vm.catalog.models.size(); ++k) {
                            const VoiceModelInfo& vm = s_vm.catalog.models[k];
                            std::string text = vm.name + "   " + std::to_string(vm.bytes / (1024 * 1024)) + " MB   " +
                                               UI(gct::VoiceQualityWord(vm.QualityFor(whisperLang)));
                            if (s_vm.installed[k]) text += "   " + UI(Tr(L"installed"));
                            if (voiceRec.recommended && voiceRec.recommended->id == vm.id) text += "   " + UI(Tr(L"recommended"));
                            if (ImGui::Selectable(text.c_str(), voiceModel && voiceModel->id == vm.id) &&
                                !(voiceModel && voiceModel->id == vm.id))
                                ApplyVoiceModelNow(edit, opened, vm.id);
                        }
                        ImGui::EndPopup();
                    }
                }

                // Click interaction on card top / body (only if inner control was not clicked)
                if (topClicked && !micControlHit) {
                    if (cards[i].isOk) {
                        s_delayedTab = cards[i].actionTab;
                    } else {
                        s_cardConfirm[i] = true;
                    }
                }

                ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + cardH));
            }

            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    return engineNeedsSetup;
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
    // Style the tabs: rounded, sleek, modern colors
    ImGui::PushStyleVar(ImGuiStyleVar_TabRounding, 5.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 4.0f));
    ImGui::PushStyleColor(ImGuiCol_Tab, IM_COL32(28, 32, 42, 200));
    ImGui::PushStyleColor(ImGuiCol_TabHovered, IM_COL32(50, 60, 85, 240));
    ImGui::PushStyleColor(ImGuiCol_TabActive, IM_COL32(42, 50, 72, 255));
    ImGui::PushStyleColor(ImGuiCol_TabUnfocused, IM_COL32(22, 25, 34, 180));
    ImGui::PushStyleColor(ImGuiCol_TabUnfocusedActive, IM_COL32(34, 40, 56, 220));

    if (s_delayedTab >= 0) {
        g_optionsTab = s_delayedTab;
        s_delayedTab = -1;
    }

    auto TabFlag = [](int i) { return g_optionsTab == i ? ImGuiTabItemFlags_SetSelected : 0; };
    if (ImGui::BeginTabBar("##tabs")) {
        // ---- Dashboard (formerly General)
        if (ImGui::BeginTabItem(UI(Tr(L"Dashboard") + L"###tab_general").c_str(), nullptr, TabFlag(0))) {
            if (ImGui::BeginChild("##tab_scroll_dash", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysUseWindowPadding)) {
            ImGui::SetNextItemWidth(wide);
            {  // only the languages this window has texts in (core/i18n: UiLanguages)
                std::string current = UI(Tr(L"System"));
                for (const UiLangInfo& l : UiLanguages())
                    if (ToLowerAscii(PrimaryLang(edit.uiLangCode)) == l.code) current = U8(l.native);  // "DE" of old too
                if (ImGui::BeginCombo(UI(Tr(L"Language of this window") + L"##ui").c_str(), current.c_str())) {
                    auto applyLang = [&](const std::wstring& code) {
                        edit.uiLangCode = code;
                        edit.uiLang = EffectiveUiLang(edit.uiLangCode);
                        {
                            std::lock_guard<std::mutex> lock(g_mu);
                            g_cfg.uiLangCode = edit.uiLangCode;
                            g_cfg.uiLang = edit.uiLang;
                        }
                        SetUiLang(edit.uiLang);
                        g_cfg.SaveValue(L"General", L"UiLanguage", edit.uiLangCode);
                    };
                    if (ImGui::Selectable(UI(Tr(L"System")).c_str(), edit.uiLangCode.empty())) {
                        applyLang(L"");
                    }
                    for (const UiLangInfo& l : UiLanguages()) {
                        if (ImGui::Selectable(U8(l.native).c_str(), ToLowerAscii(PrimaryLang(edit.uiLangCode)) == l.code)) {
                            applyLang(l.code);
                        }
                    }
                    ImGui::EndCombo();
                }
            }
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
            ImGui::Dummy(ImVec2(0.0f, 4.0f));
            Hint(UI(Tr(L"Keybinds: Nexus → Keybinds → GW2Chatlator.")));
            ImGui::Dummy(ImVec2(0.0f, 6.0f));
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0.0f, 6.0f));
            if (RenderDashboard(edit, opened)) {  // what is on, switched right here; a translator that needs its key
                const std::wstring k = edit.engine == Engine::DeepL ? edit.deeplKey : edit.engine == Engine::Google ? edit.googleKey
                                       : edit.engine == Engine::Microsoft ? edit.msKey : edit.libreKey;
                strncpy_s(keyBuf, U8(k).c_str(), _TRUNCATE);
            }
            // Events: Chat, the chat reader: without it nothing of the chat is read – easy to miss at the first start,
            // so while it is missing it stands here big, with its install button and the hint; once it is loaded only
            // its chip is left, right of "Translation & Languages" (it opens the Technical tab).
            const bool chatThere = g_chatEventsLive || g_chatEventsPresent;
            if (!chatThere) {
                ImGui::Dummy(ImVec2(0.0f, 8.0f));
                RenderChatModule(true);
                if (!g_chatEventsFile && !g_chatEventsInstalling)
                    Hint(UI(Tr(L"Without Events: Chat, GW2Chatlator cannot read the chat – writing and the typing help work anyway.")));
                ImGui::Dummy(ImVec2(0.0f, 8.0f));
            }
            ImGui::Dummy(ImVec2(0.0f, 4.0f));
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0.0f, 6.0f));
            const float rightCol = ImGui::GetContentRegionAvail().x * 0.5f + ImGui::GetStyle().ItemSpacing.x;
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(UI(Tr(L"Translation & Languages")).c_str());
            if (chatThere) {  // the chip as the switches have it, the state on the same line
                ImGui::SameLine(rightCol);
                const ImVec4 ok(0.45f, 0.85f, 0.45f, 1.f);
                const float chipW = std::ceil(ImGui::CalcTextSize("OK").x + ImGui::GetStyle().FramePadding.x * 2 + 6);
                ImGui::PushStyleColor(ImGuiCol_Text, ok);
                ImGui::PushStyleColor(ImGuiCol_Border, ok);
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
                if (ImGui::Button("OK##dash_chatmodule", ImVec2(chipW, 0))) s_delayedTab = 6;
                ImGui::PopStyleVar();
                ImGui::PopStyleColor(2);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", UI(Tr(L"Open")).c_str());
                ImGui::SameLine();
                ImGui::TextUnformatted(UI(Tr(L"Chat module")).c_str());
                ImGui::SameLine();
                ImGui::TextDisabled("%s", UI(Tr(L"ready – Events: Chat is loaded")).c_str());
            }
            ImGui::TextDisabled("%s", UI(Tr(L"Translated is everything except:")).c_str());
            RenderExceptList(edit);
            Hint(UI(Tr(L"Your writing language follows automatically when you switch languages in the chat footer.")));
            ImGui::Dummy(ImVec2(0.0f, 4.0f));
            const bool voiceModuleThere = Modules().voiceModule;  // voice-only switches wait for the voice module
            const std::string needsVoice = UI(Tr(L"Needs the voice module (Options → Voice)."));
            FocusSwitch("dash_voice_trans", UI(Tr(L"Translate what I speak into target language")), &edit.voiceTranslate,
                        voiceModuleThere ? nullptr : needsVoice.c_str(), voiceModuleThere);
            Hint(UI(Tr(L"What you say is translated into the send language set in the footer (whisper translates into English on this PC; other languages use your translator).")));
            ImGui::Dummy(ImVec2(0.0f, 8.0f));
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0.0f, 6.0f));
            ImGui::TextUnformatted(UI(Tr(L"Chat focus & behavior")).c_str());
            {
                std::lock_guard<std::mutex> lock(g_mu);
                bool fullFocus = g_layout.autoOpen && edit.focusOnGameChat && edit.returnFocus;
                std::string fullFocusTip = UI(Tr(L"Enables all 3 focus options: automatically opens, takes over Enter in game, and keeps focus after sending."));
                if (FocusSwitch("foc_full", UI(Tr(L"Complete chat takeover (full focus)")), &fullFocus, fullFocusTip.c_str())) {
                    g_layout.autoOpen = fullFocus;
                    edit.focusOnGameChat = fullFocus;
                    edit.returnFocus = fullFocus;
                    SaveLayout();
                }
            }
            {
                std::lock_guard<std::mutex> lock(g_mu);
                bool autoOpen = g_layout.autoOpen;
                std::string autoOpenTip = UI(Tr(L"Only opens it. The focus key (Nexus > Keybinds) always opens it; taking over the focus is its own setting (Dashboard)."));
                if (FocusSwitch("foc_open", UI(Tr(L"Open again when GW2's chat opens (Enter)")), &autoOpen, autoOpenTip.c_str())) {
                    g_layout.autoOpen = autoOpen;
                    SaveLayout();
                }
            }
            FocusSwitch("foc_enter", UI(Tr(L"Enter in the game opens GW2Chatlator's input")), &edit.focusOnGameChat);
            Hint(UI(Tr(L"Instead of GW2's own chat line: you type here, with the typing help and the translation.")));
            FocusSwitch("foc_return", UI(Tr(L"After sending, keep typing in GW2Chatlator")), &edit.returnFocus);
            Hint(UI(Tr(L"The input keeps the keyboard after Enter – the next message goes right after.")));
            FocusSwitch("foc_handsfree", UI(Tr(L"Hands free (voice only)")), &edit.voiceAutoSend,
                        voiceModuleThere ? nullptr : needsVoice.c_str(), voiceModuleThere);
            Hint(UI(Tr(L"Sends what you say by itself once you stop speaking – with push-to-talk and voice activation alike. Typed text always waits for your Enter.")));
            ImGui::EndChild();
            }
            ImGui::EndTabItem();
        }
        // ---- Window: its building blocks, size, transparency, how the chat text looks
        if (ImGui::BeginTabItem(UI(Tr(L"Window") + L"###tab_window").c_str(), nullptr, TabFlag(1))) {
            if (ImGui::BeginChild("##tab_scroll_win", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysUseWindowPadding)) {
            {
                std::lock_guard<std::mutex> lock(g_mu);
                const std::wstring names[] = {Tr(L"Nexus"), Tr(L"Slate"), Tr(L"Grey"), Tr(L"CBA (your own colours)")};
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
            if (ImGui::SliderInt(UI(Tr(L"Chat text size") + L" (pt)##size").c_str(), &pt, 8, 28)) {
                std::lock_guard<std::mutex> lock(g_mu);
                edit.fontPt = g_cfg.fontPt = pt;
                g_cfg.SaveValue(L"Window", L"FontPt", std::to_wstring(pt));
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", UI(Tr(L"Only chat messages, preview, input and typing help.")).c_str());
            if (ImGui::IsItemDeactivatedAfterEdit()) RequestTextFont(pt, edit.fontFace);  // built once you let go (scaled meanwhile)

            int uiPt = edit.uiFontPt > 0 ? std::clamp(edit.uiFontPt, 11, 24) : FollowUiPt();
            ImGui::SetNextItemWidth(wide);
            if (ImGui::SliderInt(UI(Tr(L"UI & window scale") + L" (pt)##uisize").c_str(), &uiPt, 11, 24)) {
                std::lock_guard<std::mutex> lock(g_mu);
                edit.uiFontPt = g_cfg.uiFontPt = uiPt;
                g_cfg.SaveValue(L"Window", L"UiFontPt", std::to_wstring(uiPt));
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", UI(Tr(L"Header tabs, footer buttons, menus and this options window.")).c_str());
            if (ImGui::IsItemDeactivatedAfterEdit()) RequestUiFont(FollowUiPx());
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
                ImGui::SetNextItemWidth(wide);
                changed |= ImGui::SliderInt(UI(Tr(L"Brightness") + L" (%)##bright").c_str(), &g_look.brightness, 60, 160);
                if (changed) SaveLayout();
            }
            ImGui::EndChild();
            }
            ImGui::EndTabItem();
        }
        // ---- Writing
        if (ImGui::BeginTabItem(UI(Tr(L"Writing") + L"###tab_writing").c_str(), nullptr, TabFlag(2))) {
            if (ImGui::BeginChild("##tab_scroll_write", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysUseWindowPadding)) {
            ImGui::SetNextItemWidth(wide);
            LanguageCombo(UI(Tr(L"I write in") + L"##writein").c_str(), edit.writeIn, Tr(L"System"));
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
            ImGui::EndChild();
            }
            ImGui::EndTabItem();
        }
        // ---- Voice
        if (ImGui::BeginTabItem(UI(Tr(L"Voice") + L"###tab_voice").c_str(), nullptr, TabFlag(3))) {
            if (ImGui::BeginChild("##tab_scroll_voice", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysUseWindowPadding)) {
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

            ImGui::Checkbox(UI(Tr(L"Translate my voice by voicemodel")).c_str(), &edit.voiceTranslate);
            ImGui::SameLine();
            std::vector<std::string> langs{UI(Tr(L"Auto-detect")), UI(Tr(L"Like I write"))};
            std::vector<std::wstring> codes{L"auto", L"write"};
            for (const LangInfo& l : Languages()) {
                if (!DrawnInOurWindow(l)) continue;
                langs.push_back(U8(NativeName(l.code)));
                codes.push_back(l.code);
            }
            int li = 0;
            for (size_t i = 0; i < codes.size(); ++i) {
                if (codes[i] == edit.voiceLang || (codes[i] == L"write" && edit.voiceLang == L"")) {
                    li = static_cast<int>(i);
                    break;
                }
            }
            ImGui::SetNextItemWidth(160);
            if (ImGui::BeginCombo("##vlang", langs[std::clamp(li, 0, static_cast<int>(langs.size()) - 1)].c_str())) {
                for (int i = 0; i < static_cast<int>(langs.size()); ++i) {
                    if (ImGui::Selectable(langs[i].c_str(), i == li)) {
                        li = i;
                        edit.voiceLang = codes[li];
                    }
                }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", UI(Tr(L"Speech recognition language: Auto-detect recognises what you speak automatically.")).c_str());

            Hint(UI(Tr(L"What you say is translated into the send language set in the footer (whisper translates into English on this PC; other languages use your translator).")));

            std::wstring recLang = edit.voiceLang;
            if (recLang.empty() || recLang == L"write" || recLang == L"auto") {
                recLang = edit.writeIn.empty() ? L"en" : edit.writeIn;
            }
            const std::string whisperLang = WhisperLang(recLang);
            const gct::VoiceRecommendation rec = gct::RecommendVoiceModel(catalog, chosen, whisperLang);
            if (!rec.supportedByWhisper) {
                ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.35f, 1.0f), "%s",
                                   UI(Tr(L"Language not supported by speech recognition")).c_str());
            } else {
                std::wstring qWord = gct::VoiceQualityWord(rec.quality);
                ImGui::Text("%s", UI(TrF(L"Recognition of {1}: {2}", {NativeName(recLang), qWord})).c_str());
                if (rec.quality < 2 && rec.recommended) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.35f, 1.0f), "(%s)",
                                       UI(TrF(L"Recommended: {1} ({2} MB RAM)", {W(rec.recommended->name), std::to_wstring(rec.recommended->ramMb)})).c_str());
                }
            }
            const std::wstring sendLang = SendLang();
            if (PrimaryLang(sendLang) == L"EN") {
                ImGui::TextDisabled("%s", UI(Tr(L"Send route: Whisper translates directly to English on this PC.")).c_str());
            } else {
                const bool hasLocalMt = gct::LocalMtModelPresent(edit.dataDir, "opus-mt") || gct::LocalMtModelPresent(edit.dataDir, "nllb-200");
                if (hasLocalMt && (edit.engine == Engine::Local || edit.engine == Engine::Nllb)) {
                    ImGui::TextDisabled("%s", UI(Tr(L"Send route: Speech recognized locally, translated with local MT package.")).c_str());
                } else {
                    ImGui::TextDisabled("%s", UI(Tr(L"Send route: Speech recognized locally, translated with online translator.")).c_str());
                }
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
            ImGui::Checkbox(UI(Tr(L"Live text while speaking (more CPU)")).c_str(), &edit.voiceLive);
            ImGui::SetNextItemWidth(wide);
            ImGui::SliderInt(UI(Tr(L"Longest recording") + L" (s)##max").c_str(), &edit.voiceMaxSec, 5, 60);
            std::vector<std::string> threads{UI(Tr(L"Automatic"))};
            for (int i = 1; i <= 8; ++i) threads.push_back(std::to_string(i));
            int ti = std::clamp(edit.voiceThreads, 0, 8);
            if (combo(UI(Tr(L"CPU threads") + L"##thr").c_str(), threads, &ti)) edit.voiceThreads = ti;
            Hint(UI(Tr(L"Automatic: the least busy cores – the ones GW2 does not use.")));
            Hint(UI(Tr(L"Runs on this PC – no sound leaves it, nothing is recorded to disk.")));
            ImGui::EndChild();
            }
            ImGui::EndTabItem();
        }
        // ---- Microphone
        if (ImGui::BeginTabItem(UI(Tr(L"Microphone") + L"###tab_mic").c_str(), nullptr, TabFlag(4))) {
            if (ImGui::BeginChild("##tab_scroll_mic", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysUseWindowPadding)) {
            int mode = edit.voiceActivation ? 2 : edit.voiceToggle ? 1 : 0;
            static std::vector<std::string> s_modeLabels;
            static UiLang s_modeLang = static_cast<UiLang>(-1);
            if (s_modeLang != g_cfg.uiLang || s_modeLabels.empty()) {
                s_modeLang = g_cfg.uiLang;
                s_modeLabels = {UI(Tr(L"Hold the key while you speak")),
                                UI(Tr(L"Press once – a pause ends it")),
                                UI(Tr(L"Voice activation – starts when you speak"))};
            }
            if (combo(UI(Tr(L"Starting") + L"##mode").c_str(), s_modeLabels, &mode)) {
                edit.voiceToggle = mode == 1;
                edit.voiceActivation = mode == 2;
            }
            {
                const bool voiceModuleThere = Modules().voiceModule;
                const std::string needsVoice = UI(Tr(L"Needs the voice module (Options → Voice)."));
                FocusSwitch("mic_handsfree", UI(Tr(L"Hands free (voice only)")), &edit.voiceAutoSend,
                            voiceModuleThere ? nullptr : needsVoice.c_str(), voiceModuleThere);
            }
            Hint(UI(Tr(L"Sends what you say by itself once you stop speaking – with push-to-talk and voice activation alike. Typed text always waits for your Enter.")));
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
            ImGui::SameLine(0.0f, 12.0f);
            const std::string keyDisplay = g_captureKey
                ? UI(Tr(L"press a key … (Esc: cancel)"))
                : UI(edit.voiceGlobalKey.empty() ? Tr(L"none") : edit.voiceGlobalKey);
            ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.35f, 1.0f), "[ %s ]", keyDisplay.c_str());
            ImGui::SameLine(0.0f, 12.0f);
            if (ImGui::Button(UI(Tr(L"Set key…")).c_str())) g_captureKey = true;
            ImGui::SameLine(0.0f, 6.0f);
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
            ImGui::EndChild();
            }
            ImGui::EndTabItem();
        }
        // ---- Translator
        if (ImGui::BeginTabItem(UI(Tr(L"Translator") + L"###tab_translator").c_str(), nullptr, TabFlag(5))) {
            if (ImGui::BeginChild("##tab_scroll_trans", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysUseWindowPadding)) {
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
            static const Engine kEngines[] = {Engine::Auto, Engine::Basic, Engine::DeepL, Engine::Google, Engine::Microsoft, Engine::Libre, Engine::Local, Engine::Nllb};
            static const wchar_t* kNames[] = {L"Automatic", L"MyMemory (free)", L"DeepL", L"Google", L"Microsoft", L"LibreTranslate", L"OPUS-MT (local)", L"NLLB-200 (local)"};
            int ei = 0;
            for (size_t i = 0; i < std::size(kEngines); ++i)
                if (kEngines[i] == edit.engine) ei = static_cast<int>(i);
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
            ImGui::EndChild();
            }
            ImGui::EndTabItem();
        }
        // ---- Technical
        if (ImGui::BeginTabItem(UI(Tr(L"Technical") + L"###tab_technical").c_str(), nullptr, TabFlag(6))) {
            if (ImGui::BeginChild("##tab_scroll_tech", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysUseWindowPadding)) {
            // Modules first, then the debug tools, the technical details, the send timing folded up, removing last.
            RenderChatModule(false);  // Events: Chat: loaded, install / remove – reading it follows in the details
            ImGui::Separator();
            ImGui::TextUnformatted(UI(Tr(L"Debug & Diagnostics")).c_str());
            if (ImGui::Button(UI(Tr(L"Copy diagnostic report to clipboard")).c_str())) {
                const std::string report = BuildDiagnosticReport();
                ImGui::SetClipboardText(report.c_str());
                SetStatus(Tr(L"Diagnostic report copied to clipboard"), 4000);
            }
            ImGui::SameLine();
            if (ImGui::Button(UI(g_showDebugWindow ? Tr(L"Close Debug Window") : Tr(L"Open Debug Window")).c_str())) {
                g_showDebugWindow = !g_showDebugWindow;
            }
            Hint(UI(Tr(L"Exports system status, engine telemetry, and performance metrics (no chat messages or player names are stored or exported).")));
            // Raw translation, for comparing what our logic adds: the visible chat is translated again in the other way.
            bool raw = g_rawTranslate.load();
            if (FocusSwitch("tech_raw", UI(Tr(L"Raw translation (until restart)")), &raw)) {
                g_rawTranslate = raw;
                std::lock_guard<std::mutex> lock(g_mu);
                g_orchestrator.SetInterjectionSieve(!raw);
                TranslateRecentLines(true);
                SetStatus(raw ? Tr(L"Raw translation on: GW2 terms, names, sentence by sentence, the extra tries, the clean-up and the quality check are off")
                              : Tr(L"Raw translation off: our translation logic is on again"),
                          6000);
            }
            Hint(UI(Tr(L"For comparing: the translator gets each line exactly as written, its result is shown as it comes. Chat codes and links stay protected. Not saved.")));
            ImGui::Separator();
            {
                std::lock_guard<std::mutex> lock(g_mu);
                static Steady s_fps;
                const int fps = static_cast<int>(std::lround(s_fps(g_fps)));
                const int hold = (fps > 0 && edit.send.autoTiming) ? std::clamp(1000 / std::max(fps, 10) + 10, 15, 1010) : edit.send.keyHoldMs;
                ImGui::Text("GW2Chatlator for Nexus %s", GCT_BUILD_DISPLAY_STR);
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
            // The send timing: automatic fits nearly everyone, so the manual values stay folded up.
            if (ImGui::CollapsingHeader(UI(Tr(L"Send timing (advanced)") + L"###sendtiming").c_str())) {
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
            }
            ImGui::Separator();
            RenderRemoveEverything();  // once, at the very bottom
            ImGui::EndChild();
            }
            ImGui::EndTabItem();
        }

        // ---- Save Tab Button: placed directly in the tab row after Technical tab
        {
            const bool dirty = IsOptionsDirty(edit, opened, keyBuf, regionBuf, urlBuf, mailBuf);
            if (dirty) {
                ImGui::PushStyleColor(ImGuiCol_Tab, IM_COL32(200, 38, 38, 240));        // vibrant red
                ImGui::PushStyleColor(ImGuiCol_TabHovered, IM_COL32(230, 50, 50, 255)); // brighter red
                ImGui::PushStyleColor(ImGuiCol_TabActive, IM_COL32(170, 25, 25, 255));  // deep red
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Tab, IM_COL32(34, 38, 48, 160));         // subtle dark
                ImGui::PushStyleColor(ImGuiCol_TabHovered, IM_COL32(48, 55, 72, 220));
                ImGui::PushStyleColor(ImGuiCol_TabActive, IM_COL32(28, 32, 42, 255));
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(160, 168, 182, 190));     // subtle grey
            }
            std::string saveTabLabel = (dirty ? UI(Tr(L"Save")) + " *" : UI(Tr(L"Save"))) + "###tab_save_btn";
            if (ImGui::TabItemButton(saveTabLabel.c_str(), 0)) {
                DoSaveOptions(edit, opened, keyBuf, regionBuf, urlBuf, mailBuf);
            }
            if (dirty && ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", UI(Tr(L"Unsaved changes – click to apply")).c_str());
            }
            ImGui::PopStyleColor(4);
        }

        ImGui::EndTabBar();
        g_optionsTab = -1;
    }
    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar(2);
}

std::string BuildDiagnosticReport() {
    std::ostringstream ss;
    SYSTEMTIME st;
    GetLocalTime(&st);
    char timeBuf[64];
    sprintf_s(timeBuf, "%04d-%02d-%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    ss << "======================================================================\n";
    ss << "GW2Chatlator - Diagnostic Protocol / Prüfprotokoll\n";
    ss << "Generated: " << timeBuf << " (Local Time)\n";
    ss << "Version: GW2Chatlator for Nexus " << GCT_BUILD_DISPLAY_STR << " (Release)\n";
    ss << "======================================================================\n\n";

    ss << "[System & Display Environment]\n";
    ImGuiIO& io = ImGui::GetIO();
    ss << "- Operating System: Windows x64\n";
    ss << "- Display Resolution: " << static_cast<int>(io.DisplaySize.x) << "x" << static_cast<int>(io.DisplaySize.y) << "\n";
    ss << "- Game Framerate: " << g_fps.load() << " FPS (ImGui Framerate: " << std::fixed << std::setprecision(1) << io.Framerate << ")\n";
    ss << "- GW2 UI Size: " << g_uiSize.load() << " (0: Small, 1: Normal, 2: Large, 3: Larger)\n";
    ss << "- In Map: " << (g_inMap ? "Yes" : "No") << " (Map ID: " << g_mapId.load() << ")\n";
    ss << "- Map Open: " << (g_mapOpen ? "Yes" : "No") << ", Game Chat Open: " << (g_gameChatOpen ? "Yes" : "No") << "\n";
    ss << "- Add-on Window Visible: " << (g_show ? "Yes" : "No") << ", Collapsed: " << (g_collapsed ? "Yes" : "No") << "\n";
    ss << "- Auto Timing: " << (g_cfg.send.autoTiming ? "Enabled" : "Manual")
       << " (Key Hold: " << g_cfg.send.keyHoldMs << " ms, Pause Between: " << g_cfg.send.stepDelayMs << " ms)\n";
    gct::HelperInfo helper = gct::GetLocalMtHelperInfo(g_translator);
    if (!helper.build.empty()) {
        ss << "- Local MT Helper Build: " << helper.build << " (proto: " << helper.proto << ")\n";
    }
    ss << "\n";

    ss << "[Translation & Inference Engine]\n";
    ss << "- Configured Engine: ";
    switch (g_cfg.engine) {
        case Engine::Auto: ss << "Auto: bester vorhandener Schlüssel, sonst MyMemory\n"; break;
        case Engine::Local: ss << "Local OPUS-MT (CTranslate2 INT8 Native)\n"; break;
        case Engine::Nllb: ss << "Local NLLB-200 (CTranslate2 INT8 Native)\n"; break;
        case Engine::DeepL: ss << "DeepL API\n"; break;
        case Engine::Google: ss << "Google Cloud Translation API\n"; break;
        case Engine::Microsoft: ss << "Microsoft Azure Translator\n"; break;
        case Engine::Libre: ss << "LibreTranslate Server\n"; break;
        case Engine::Basic: ss << "MyMemory Basic\n"; break;
        default: ss << "Custom\n"; break;
    }
    ss << "- Active Translator Backend: " << (g_translator ? U8(g_translator->Name()) : "None") << "\n";
    ss << "- Typing / Source Language: " << U8(g_cfg.writeIn) << "\n";
    ss << "- Target Send Language: " << U8(SendLang()) << " (Send Original: " << (SendOriginal() ? "Yes" : "No") << ")\n";
    ss << "- Translate Incoming Chat: " << (g_nx.translateChat ? "Enabled" : "Disabled") << "\n";
    ss << "- Translate Writing: " << (g_cfg.translateWriting ? "Enabled" : "Disabled") << "\n";
    if (const std::wstring q = MyMemoryQuotaText(); !q.empty()) {
        ss << "- MyMemory Quota Status: " << U8(q) << "\n";
    }
    ss << "- Last Translation Error: ";
    if (g_lastErrorKind == TranslateErrorKind::None) {
        ss << "None\n";
    } else {
        switch (g_lastErrorKind) {
            case TranslateErrorKind::UnsupportedPair: ss << "Unsupported language pair"; break;
            case TranslateErrorKind::HelperMissing: ss << "Translation helper missing"; break;
            case TranslateErrorKind::HelperCrashed: ss << "Translation helper crashed"; break;
            case TranslateErrorKind::BadOutput: ss << "Invalid translation response"; break;
            case TranslateErrorKind::Network: ss << "Network connection failed"; break;
            case TranslateErrorKind::Timeout: ss << "Network timeout"; break;
            case TranslateErrorKind::Auth: ss << "Authentication / API key invalid"; break;
            case TranslateErrorKind::Quota: ss << "Quota exceeded"; break;
            default: ss << "Other error"; break;
        }
        if (!g_lastErrorTime.empty()) ss << " (" << U8(g_lastErrorTime) << ")";
        ss << "\n";
    }
    ss << "\n";

    ss << "[Smart-Sieve-Manager Telemetry (0% CPU Optimization)]\n";
    const size_t qDepth = g_worker.QueueSize();
    const int effectiveBeam = g_orchestrator.ComputeBeamSize(static_cast<gct::QualityMode>(g_cfg.localQuality), qDepth);
    ss << "- Smart-Sieve Status: " << (g_orchestrator.IsPaused() ? "PAUSED (Panic Switch Ctrl+Alt+T)" : "ACTIVE") << "\n";
    ss << "- Configured Quality Mode: "
       << (g_cfg.localQuality == 1 ? "Fast (Beam 1)" : g_cfg.localQuality == 4 ? "Quality (Beam 4)" : "Balanced (Beam 2)") << "\n";
    ss << "- Current Effective Beam Size: " << effectiveBeam
       << (effectiveBeam < g_cfg.localQuality ? " (Throttled due to backlog pressure)" : " (Nominal)") << "\n";
    ss << "- Worker Translation Backlog: " << qDepth << " queued jobs\n";
    ss << "- Gamer Shorthand Filtered (0% CPU): " << g_orchestrator.SlangFilteredCount() << " expressions (gg, wp, ty, afk, etc.)\n";
    ss << "- Repetition Cache Hits (0 ms): " << g_orchestrator.CacheHitCount() << " hits ("
       << g_orchestrator.CacheSize() << " entries currently cached)\n";
    ss << "- Occlusion Culled Lines: " << g_orchestrator.CulledCount() << " lines deferred while out-of-view\n\n";

    ss << "[Chat Buffer & Bridge Status]\n";
    ss << "- Events: Chat Add-on: " << (g_chatEventsPresent ? "Present in game" : "Not found in modules")
       << (g_chatEventsLive ? " (Hook Live)" : "") << "\n";
    ss << "- Events: Chat DLL on Disk: " << (g_chatEventsFile ? "Installed" : "Missing") << "\n";
    {
        std::lock_guard<std::mutex> lock(g_mu);
        const size_t nLines = g_lines.size();
        wchar_t ramBuf[32];
        swprintf_s(ramBuf, L"%.1f", static_cast<double>(nLines * 750) / (1024.0 * 1024.0));
        ss << "- In-Memory Chat Buffer: " << nLines << " / " << kMaxLines << " lines (~" << U8(ramBuf) << " MB RAM)\n";
        ss << "- Saved Snippets / Text Blocks: " << g_snippets.Items().size() << " / " << gct::Snippets::kMax << "\n";
    }
    ss << "\n";

    ss << "[Render Thread Frame Profiler (kb_cost)]\n";
    FrameCosts fc = CostsNow();
    ss << "- Total Add-on Frame Cost: " << std::fixed << std::setprecision(3) << fc.totalMs << " ms\n";
    ss << "- Subsystem Cost Breakdown:\n";
    ss << "  * Window Layout & Frame:   " << fc.ms[kCostWindow] << " ms\n";
    ss << "  * Chat Lines Rendering:    " << fc.ms[kCostChat] << " ms\n";
    ss << "  * Input & Text Box:        " << fc.ms[kCostInput] << " ms\n";
    ss << "  * Header & Navigation:     " << fc.ms[kCostHeader] << " ms\n";
    ss << "  * Footer & Language Chips: " << fc.ms[kCostFooter] << " ms\n";
    ss << "  * Options & Diagnostics:   " << fc.ms[kCostOptions] << " ms\n";
    ss << "- Line Ingest Overhead:      " << std::fixed << std::setprecision(1) << fc.lineUs << " µs per chat line ("
       << fc.lines << " lines observed across " << fc.frames << " frames)\n\n";

    ss << "[Voice & Speech-to-Text]\n";
    ss << "- Voice System: " << (!g_cfg.voiceEnabled ? "Disabled" : g_voiceReady ? "Ready (Loaded)"
                               : g_voice.Running() ? "Starting..."
                               : g_modules.voiceModule ? "Ready (Loads on first mic use)" : "Module missing") << "\n";
    ss << "- Push-to-Talk Key: " << (!g_cfg.voiceGlobalKey.empty() ? "Registered" : "None") << "\n\n";

    ss << "[Events: Chat Technical Protocol]\n";
    ss << ChatSourceProtocolText() << "\n\n";

    ss << "======================================================================\n";
    ss << "VERIFIED PRIVACY INVARIANT:\n";
    ss << "Zero player message texts or account names are captured, stored, or\n";
    ss << "exported in this diagnostic protocol. (CLAUDE.md / AGENTS.md invariant)\n";
    ss << "======================================================================\n";

    return ss.str();
}

void RenderDebugWindow() {
    if (!g_showDebugWindow) return;
    const float nexusPx = FollowUiPx();
    RequestUiFont(nexusPx);
    ImFont* uf = g_uiFont.load();
    const float dbgW = std::max(640.0f, 640.0f * (nexusPx / 16.0f));
    const float dbgH = std::max(560.0f, 560.0f * (nexusPx / 16.0f));
    ImGui::SetNextWindowSize(ImVec2(dbgW, dbgH), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(480, 360), ImVec2(4000, 4000));

    std::string title = UI(Tr(L"GW2Chatlator – Debug & Diagnostics")) + "###kbdebug";
    const bool visible = ImGui::Begin(title.c_str(), &g_showDebugWindow);
    bool pushedUf = false;
    if (uf && uf->FontSize > 0.0f) {
        ImGui::SetWindowFontScale(nexusPx / uf->FontSize);
        ImGui::PushFont(uf);
        pushedUf = true;
    } else {
        const float currentPx = std::max(1.0f, ImGui::GetFontSize());
        ImGui::SetWindowFontScale(nexusPx / currentPx);
    }
    if (visible) {
        if (ImGui::Button(UI(Tr(L"Copy diagnostic report to clipboard")).c_str())) {
            const std::string report = BuildDiagnosticReport();
            ImGui::SetClipboardText(report.c_str());
            SetStatus(Tr(L"Diagnostic report copied to clipboard"), 4000);
        }
        ImGui::SameLine();
        if (ImGui::Button(UI(Tr(L"Save report to file")).c_str())) {
            const std::string report = BuildDiagnosticReport();
            const std::wstring p = g_cfg.dataDir + L"\\diagnostic-report.txt";
            FILE* f = nullptr;
            if (_wfopen_s(&f, p.c_str(), L"wb") == 0 && f) {
                fwrite(report.data(), 1, report.size(), f);
                fclose(f);
                SetStatus(Tr(L"Diagnostic report saved to file"), 4000);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(UI(Tr(L"Close")).c_str())) {
            g_showDebugWindow = false;
        }

        ImGui::Separator();

        if (ImGui::BeginTabBar("##dbg_tabs")) {
            if (ImGui::BeginTabItem(UI(Tr(L"Live Telemetry & Engine Status") + L"###tab_live").c_str())) {
                FrameCosts fc = CostsNow();
                const size_t qDepth = g_worker.QueueSize();
                const int effectiveBeam = g_orchestrator.ComputeBeamSize(static_cast<gct::QualityMode>(g_cfg.localQuality), qDepth);

                StatusRow(UI(Tr(L"Smart-Sieve")), g_orchestrator.IsPaused() ? 1 : 2,
                          UI(g_orchestrator.IsPaused() ? Tr(L"PAUSED (Ctrl+Alt+T)") : Tr(L"Active (Filtering gamer slang & repeated text)")));

                const std::wstring beam = std::to_wstring(effectiveBeam), backlog = std::to_wstring(qDepth);
                const std::string infText = UI(effectiveBeam < g_cfg.localQuality
                                                   ? TrF(L"Beam {1} (stepped down) · backlog: {2} jobs", {beam, backlog})
                                                   : TrF(L"Beam {1} (as set) · backlog: {2} jobs", {beam, backlog}));
                StatusRow(UI(Tr(L"Inference")), effectiveBeam == 1 ? 1 : 2, infText);

                const bool chatLive = g_chatEventsLive || g_chatEventsPresent;
                StatusRow(UI(Tr(L"Chat module")), chatLive ? 2 : 0,
                          UI(chatLive ? Tr(L"ready – Events: Chat is loaded") : Tr(L"the chat module is missing")));

                ImGui::Separator();
                ImGui::Text("GW2Chatlator for Nexus %s", GCT_BUILD_DISPLAY_STR);
                ImGuiIO& io = ImGui::GetIO();
                wchar_t num[2][32] = {};
                swprintf_s(num[0], L"%.2f", io.Framerate > 0 ? 1000.0f / io.Framerate : 0.0f);
                swprintf_s(num[1], L"%.3f", fc.totalMs);
                ImGui::TextUnformatted(UI(TrF(L"Game: {1} fps (frame time {2} ms)", {std::to_wstring(g_fps.load()), num[0]})).c_str());
                ImGui::TextUnformatted(UI(TrF(L"Add-on drawing per frame: {1} ms", {num[1]})).c_str());
                ImGui::TextUnformatted(UI(TrF(L"Translator: {1}", {g_translator ? g_translator->Name() : std::wstring(L"-")})).c_str());
                ImGui::TextUnformatted(UI(TrF(L"Writing language: {1} → send language: {2}", {g_cfg.writeIn, SendLang()})).c_str());

                ImGui::Separator();
                ImGui::TextUnformatted(UI(Tr(L"Smart-Sieve & AI Telemetry")).c_str());
                ImGui::BulletText("%s", UI(TrF(L"Gamer slang and short forms filtered (no CPU): {1}",
                                               {std::to_wstring(g_orchestrator.SlangFilteredCount())})).c_str());
                ImGui::BulletText("%s", UI(TrF(L"Repeated lines from the cache: {1} (0 ms) · {2} entries",
                                               {std::to_wstring(g_orchestrator.CacheHitCount()),
                                                std::to_wstring(g_orchestrator.CacheSize())})).c_str());
                ImGui::BulletText("%s", UI(TrF(L"Held back while the chat was hidden: {1} lines",
                                               {std::to_wstring(g_orchestrator.CulledCount())})).c_str());

                ImGui::Separator();
                ImGui::TextUnformatted(UI(Tr(L"Performance Profiler (Render Thread)")).c_str());
                ImGui::BulletText("Window: %.3f ms | Chat: %.3f ms | Input: %.3f ms",
                                  fc.ms[kCostWindow], fc.ms[kCostChat], fc.ms[kCostInput]);
                ImGui::BulletText("Header: %.3f ms | Footer: %.3f ms | Options: %.3f ms",
                                  fc.ms[kCostHeader], fc.ms[kCostFooter], fc.ms[kCostOptions]);
                ImGui::BulletText("Line Ingest: %.1f µs per incoming line (%lld lines)",
                                  fc.lineUs, fc.lines);

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem(UI(Tr(L"Diagnostic Protocol Preview") + L"###tab_preview").c_str())) {
                static std::string s_reportPreview;
                static ULONGLONG s_lastReportTime = 0;
                if (s_reportPreview.empty() || GetTickCount64() - s_lastReportTime > 2000) {
                    s_reportPreview = BuildDiagnosticReport();
                    s_lastReportTime = GetTickCount64();
                }
                if (ImGui::Button(UI(Tr(L"Refresh Preview")).c_str())) {
                    s_reportPreview = BuildDiagnosticReport();
                    s_lastReportTime = GetTickCount64();
                }
                ImGui::SameLine();
                if (ImGui::Button(UI(Tr(L"Copy diagnostic report to clipboard")).c_str())) {
                    ImGui::SetClipboardText(s_reportPreview.c_str());
                    SetStatus(Tr(L"Diagnostic report copied to clipboard"), 4000);
                }
                ImGui::Separator();
                ImVec2 avail = ImGui::GetContentRegionAvail();
                ImGui::InputTextMultiline("##dbg_preview", const_cast<char*>(s_reportPreview.c_str()),
                                          s_reportPreview.size(), avail,
                                          ImGuiInputTextFlags_ReadOnly);
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }
    if (pushedUf) ImGui::PopFont();
    ImGui::End();
}

// Nexus → Addons → GW2 Keyboard: only the button that opens / closes our own options window.
void RenderNexusOptions() {
    if (!ImGui::GetCurrentContext()) return;
    ImGuiContext& g = *ImGui::GetCurrentContext();
    if (g.CurrentWindowStack.Size <= 1) {
        SuppressImplicitDebugWindow();
        return;
    }
    ImGuiWindow* cur = ImGui::GetCurrentWindowRead();
    if (!cur || cur->IsFallbackWindow || (cur->Name && (std::strcmp(cur->Name, "Debug##Default") == 0 || std::strcmp(cur->Name, "Debug") == 0))) {
        SuppressImplicitDebugWindow();
        return;
    }
    if (ImGui::GetFont() && ImGui::GetFontSize() > 0.0f)
        s_nexusOptionsFontPx.store(ImGui::GetFontSize());  // includes Nexus' per-window scale and the active Windows/game DPI scale
    if (ImGui::Button(U8(g_showOptions ? Tr(L"Close the GW2Chatlator options") : Tr(L"Open the GW2Chatlator options")).c_str()))
        g_showOptions = !g_showOptions;
}

void SuppressImplicitDebugWindow() {
    if (!ImGui::GetCurrentContext()) return;
    ImGuiContext& g = *ImGui::GetCurrentContext();
    for (int i = 0; i < g.Windows.Size; ++i) {
        ImGuiWindow* w = g.Windows[i];
        if (!w) continue;
        const bool isFallback = w->IsFallbackWindow ||
                                (w->Name && (std::strcmp(w->Name, "Debug##Default") == 0 || std::strcmp(w->Name, "Debug") == 0));
        if (isFallback) {
            w->Active = false;
            w->WasActive = false;
            w->WriteAccessed = false;
            w->Hidden = true;
            w->HiddenFramesCanSkipItems = 10;
            w->HiddenFramesCannotSkipItems = 10;
            w->Flags |= ImGuiWindowFlags_NoSavedSettings;
        }
    }
    if (g.CurrentWindow && (g.CurrentWindow->IsFallbackWindow ||
        (g.CurrentWindow->Name && (std::strcmp(g.CurrentWindow->Name, "Debug##Default") == 0 || std::strcmp(g.CurrentWindow->Name, "Debug") == 0)))) {
        g.CurrentWindow->WriteAccessed = false;
        g.CurrentWindow->Active = false;
    }
    RenderDiscordVoiceMenu();
}

}  // namespace kb
