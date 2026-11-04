// kb_options.cpp — the options window and the modules list.

#include "kb_shared.hpp"
#include "core/glossary.hpp"
#include "core/slang.hpp"
#include "core/word_model.hpp"
#include "imgui/imgui_internal.h"
#include "win/local_mt_packages.hpp"
#include "voice_module.hpp"
#include <cstdio>
#include <type_traits>

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
                 : g_chatEventsFile && g_chatNotTakenIn ? Tr(L"in the add-ons folder, but Nexus has not taken it in – restart GW2 once")
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
    RenderVoiceModule(guide);
    ImGui::PushID("modules");
    if (guide) {
        ImGui::Spacing();
        bool chat;
        {
            std::lock_guard<std::mutex> lock(g_mu);
            chat = g_nx.translateChat;
        }
        if (!chat) {  // reading, sending and the translator: the three settings on Start
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", UI(Tr(L"Translate the chat: Options → Start")).c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton(UI(Tr(L"Open") + L"##tr").c_str())) {
                g_showOptions = true;
                g_optionsTab = kPageStart;
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

// ---- The options window (2.8, user 2026-11-01): a side bar of pages as Nexus has it; on each page framed groups with
// a small title and rows "label | control". Everything applies at once – there is no "Save" (ApplyOptions).

namespace {
constexpr ImU32 kOnGreen = IM_COL32(47, 158, 101, 255);
constexpr ImU32 kOffTrack = IM_COL32(58, 61, 68, 255);
constexpr ImU32 kReadyGreen = IM_COL32(95, 211, 145, 255);
constexpr ImU32 kMissingOrange = IM_COL32(233, 167, 90, 255);
const ImVec4 kSendOrange(0.94f, 0.75f, 0.35f, 1.f);  // the footer's colour for what the chat gets

struct BoxFrame {
    ImVec2 at;
    std::string title;
};
std::vector<BoxFrame> s_boxes;
float s_labelW = 0.0f, s_labelNeed = 0.0f;  // the label column: as wide as the widest label of the frame before
int s_page = kPageStart;

float ControlW() { return std::min(ImGui::GetFontSize() * 15.0f, std::max(ImGui::GetContentRegionAvail().x, 1.0f)); }

// An on/off switch with its label (a click on either flips it); not usable: dimmed, the tooltip says why.
bool Switch(const char* id, const std::string& label, bool* v, const char* tip = nullptr, bool usable = true) {
    const float fs = ImGui::GetFontSize();
    const float frameH = ImGui::GetFrameHeight();
    const float h = std::round(fs), w = std::round(h * 1.85f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushID(id);
    const bool pressed =
        ImGui::InvisibleButton("##switch", ImVec2(w + fs * 0.6f + ImGui::CalcTextSize(label.c_str()).x, frameH));
    ImGui::PopID();
    const bool hovered = ImGui::IsItemHovered();
    if (pressed && usable) *v = !*v;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float y = p.y + std::floor((frameH - h) * 0.5f);
    dl->AddRectFilled(ImVec2(p.x, y), ImVec2(p.x + w, y + h), usable && *v ? kOnGreen : kOffTrack, h * 0.5f);
    dl->AddCircleFilled(ImVec2(*v ? p.x + w - h * 0.5f : p.x + h * 0.5f, y + h * 0.5f), h * 0.5f - 2.0f,
                        !usable ? IM_COL32(120, 122, 128, 255) : *v ? IM_COL32_WHITE : IM_COL32(201, 204, 210, 255), 16);
    dl->AddText(ImGui::GetFont(), fs, ImVec2(p.x + w + fs * 0.6f, p.y + ImGui::GetStyle().FramePadding.y),
                ImGui::GetColorU32(usable ? ImGuiCol_Text : ImGuiCol_TextDisabled), label.c_str());
    if (hovered && tip) ImGui::SetTooltip("%s", tip);
    if (hovered && usable) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    return pressed && usable;
}

}  // namespace

// A row's label in the label column; the control follows on the same line.
void RowLabel(const std::string& label) {
    const float x0 = ImGui::GetCursorPosX();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label.c_str());
    s_labelNeed = std::max(s_labelNeed, ImGui::GetItemRectSize().x);
    ImGui::SameLine();
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), x0 + s_labelW));
}

namespace {

// A grey line under a row, where its control starts.
void RowHint(const std::string& text) {
    ImGui::Indent(s_labelW);
    Hint(text);
    ImGui::Unindent(s_labelW);
}

// A combo over `items` in a row.
bool RowCombo(const char* id, const std::vector<std::string>& items, int* sel) {
    bool changed = false;
    ImGui::SetNextItemWidth(ControlW());
    if (ImGui::BeginCombo(id, items[std::clamp(*sel, 0, static_cast<int>(items.size()) - 1)].c_str())) {
        for (int i = 0; i < static_cast<int>(items.size()); ++i)
            if (ImGui::Selectable(items[i].c_str(), i == *sel)) {
                *sel = i;
                changed = true;
            }
        ImGui::EndCombo();
    }
    return changed;
}

// A slider in a row; `format` shows the value with its unit ("%d pt").
bool RowSlider(const char* id, int* v, int lo, int hi, const char* format) {
    ImGui::SetNextItemWidth(ControlW());
    return ImGui::SliderInt(id, v, lo, hi, format);
}

// The page's name, bigger, and what it is for.
void PageHead(const std::string& title, const std::string& sub) {
    const float fs = ImGui::GetFontSize();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
    dl->AddText(ImGui::GetFont(), fs * 1.2f, p, col, title.c_str());
    dl->AddText(ImGui::GetFont(), fs * 1.2f, ImVec2(p.x + 0.7f, p.y), col, title.c_str());  // a little bolder
    ImGui::Dummy(ImVec2(0.0f, fs * 1.25f));
    ImGui::TextDisabled("%s", sub.c_str());
}

void DrawVectorMicIcon(ImDrawList* dl, ImVec2 c, float u, ImU32 col) {
    dl->AddRectFilled(ImVec2(c.x - 2.5f * u, c.y - 6 * u), ImVec2(c.x + 2.5f * u, c.y + 1.5f * u), col, 2.5f * u);
    dl->PathArcTo(ImVec2(c.x, c.y - 1 * u), 4.5f * u, 0.0f, 3.14159f, 10);
    dl->PathStroke(col, false, 1.4f * u);
    dl->AddLine(ImVec2(c.x, c.y + 3.5f * u), ImVec2(c.x, c.y + 6 * u), col, 1.4f * u);
    dl->AddLine(ImVec2(c.x - 3 * u, c.y + 6 * u), ImVec2(c.x + 3 * u, c.y + 6 * u), col, 1.4f * u);
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

// The side bar's symbols, drawn in a 20 x 20 box around `c` (`s`: pixels per unit).
void DrawPageIcon(ImDrawList* dl, ImVec2 c, float s, int page, ImU32 col) {
    auto P = [&](float x, float y) { return ImVec2(c.x + (x - 10.0f) * s, c.y + (y - 10.0f) * s); };
    const float t = 1.5f * s;
    auto poly = [&](std::initializer_list<ImVec2> pts) {
        for (const ImVec2& q : pts) dl->PathLineTo(q);
        dl->PathStroke(col, true, t);
    };
    switch (page) {
        case kPageStart:  // a house
            poly({P(3, 9.5f), P(10, 3.5f), P(17, 9.5f), P(17, 17), P(12.5f, 17), P(12.5f, 12.5f), P(7.5f, 12.5f),
                  P(7.5f, 17), P(3, 17)});
            break;
        case kPageChat:  // a speech bubble
            poly({P(3, 4.5f), P(17, 4.5f), P(17, 13.5f), P(8, 13.5f), P(4.5f, 16.5f), P(4.5f, 13.5f), P(3, 13.5f)});
            break;
        case kPageTyping:  // a keyboard
            dl->AddRect(P(2, 5), P(18, 15), col, 1.5f * s, ImDrawCornerFlags_All, t);
            for (float x : {5.0f, 8.0f, 11.0f, 14.0f}) dl->AddRectFilled(P(x, 7.8f), P(x + 1.3f, 9.1f), col);
            dl->AddLine(P(6, 11.8f), P(14, 11.8f), col, t);
            break;
        case kPageVoice:
            DrawVectorMicIcon(dl, c, s * 1.2f, col);
            break;
        case kPageTranslator:
            DrawVectorGlobeIcon(dl, c, s * 7.0f / 6.5f, col);
            break;
        case kPageWindow:  // a window with lines
            dl->AddRect(P(2.5f, 3.5f), P(17.5f, 16.5f), col, 1.5f * s, ImDrawCornerFlags_All, t);
            dl->AddLine(P(2.5f, 7), P(17.5f, 7), col, t);
            dl->AddLine(P(6, 10.5f), P(14, 10.5f), col, t);
            dl->AddLine(P(6, 13.5f), P(11, 13.5f), col, t);
            break;
        default:  // Technical: a cog
            dl->AddCircle(c, 2.6f * s, col, 12, t);
            dl->AddCircle(c, 5.4f * s, col, 20, t);
            for (int k = 0; k < 8; ++k) {
                const float a = k * 3.14159265f / 4.0f;
                dl->AddLine(ImVec2(c.x + std::cos(a) * 5.6f * s, c.y + std::sin(a) * 5.6f * s),
                            ImVec2(c.x + std::cos(a) * 8.0f * s, c.y + std::sin(a) * 8.0f * s), col, t * 1.3f);
            }
    }
}

// One page in the side bar: its symbol and name; the page shown is lit.
bool NavItem(int page, const std::string& label, bool selected) {
    const float fs = ImGui::GetFontSize();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = std::max(ImGui::GetContentRegionAvail().x, 1.0f), h = std::round(fs * 2.0f);
    ImGui::PushID(page);
    const bool pressed = ImGui::InvisibleButton("##page", ImVec2(w, h));
    ImGui::PopID();
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (selected || hovered)
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), selected ? IM_COL32(255, 255, 255, 34) : IM_COL32(255, 255, 255, 16), 4.0f);
    const ImU32 col = ImGui::GetColorU32(selected || hovered ? ImGuiCol_Text : ImGuiCol_TextDisabled);
    const float s = fs / 18.0f;
    DrawPageIcon(dl, ImVec2(p.x + fs * 0.6f + 10.0f * s, p.y + h * 0.5f), s, page, col);
    dl->AddText(ImGui::GetFont(), fs, ImVec2(p.x + fs * 1.2f + 20.0f * s, p.y + (h - fs) * 0.5f), col, label.c_str());
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    return pressed;
}

// Start's head: the one switch for the chat's translation, big, and beside it what is ready. True when clicked.
bool Hero(bool on, const std::string& title, const std::string& sub, const std::string& state1, bool ok1,
          const std::string& state2, bool ok2) {
    const float fs = ImGui::GetFontSize();
    ImFont* font = ImGui::GetFont();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = std::max(ImGui::GetContentRegionAvail().x, 1.0f), h = std::round(fs * 3.3f);
    const float smallPx = fs * 0.92f;
    const float rightW = std::max(font->CalcTextSizeA(smallPx, FLT_MAX, 0.0f, state1.c_str()).x,
                                  font->CalcTextSizeA(smallPx, FLT_MAX, 0.0f, state2.c_str()).x) + fs * 1.8f;
    const bool pressed = ImGui::InvisibleButton("##hero", ImVec2(std::max(1.0f, w - rightW), h));
    const bool hovered = ImGui::IsItemHovered();
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), IM_COL32(255, 255, 255, hovered ? 16 : 8), 4.0f);
    dl->AddRect(p, ImVec2(p.x + w, p.y + h), ImGui::GetColorU32(ImGuiCol_Separator), 4.0f);
    const float swH = std::round(fs * 1.125f), swW = std::round(swH * 1.8f);  // three quarters of what it was (Emi)
    const ImVec2 s(p.x + fs * 0.9f, p.y + std::floor((h - swH) * 0.5f));
    dl->AddRectFilled(s, ImVec2(s.x + swW, s.y + swH), on ? kOnGreen : kOffTrack, swH * 0.5f);
    dl->AddCircleFilled(ImVec2(on ? s.x + swW - swH * 0.5f : s.x + swH * 0.5f, s.y + swH * 0.5f), swH * 0.5f - 3.0f,
                        on ? IM_COL32_WHITE : IM_COL32(201, 204, 210, 255), 20);
    const float tx = s.x + swW + fs * 0.9f;
    const ImVec4 clip(tx, p.y, p.x + w - rightW, p.y + h);  // long words in a narrow window: cut, never over the right
    const ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);
    dl->AddText(font, fs * 1.08f, ImVec2(tx, p.y + h * 0.5f - fs * 1.15f), text, title.c_str(), nullptr, 0.0f, &clip);
    dl->AddText(font, fs * 1.08f, ImVec2(tx + 0.7f, p.y + h * 0.5f - fs * 1.15f), text, title.c_str(), nullptr, 0.0f, &clip);
    dl->AddText(font, smallPx, ImVec2(tx, p.y + h * 0.5f + fs * 0.1f), ImGui::GetColorU32(ImGuiCol_TextDisabled),
                sub.c_str(), nullptr, 0.0f, &clip);
    auto state = [&](float y, const std::string& t, bool ok) {
        const float x = p.x + w - fs * 0.8f - font->CalcTextSizeA(smallPx, FLT_MAX, 0.0f, t.c_str()).x;
        dl->AddCircleFilled(ImVec2(x - fs * 0.55f, y + smallPx * 0.55f), fs * 0.22f, ok ? kReadyGreen : kMissingOrange, 12);
        dl->AddText(font, smallPx, ImVec2(x, y), ok ? text : kMissingOrange, t.c_str());
    };
    state(p.y + h * 0.5f - fs * 1.05f, state1, ok1);
    state(p.y + h * 0.5f + fs * 0.1f, state2, ok2);
    return pressed;
}

// The voice models as Start and the Voice page show them: the catalog and what is installed, read again every 2 s.
struct VoiceModels {
    VoiceCatalog catalog;
    std::vector<bool> installed;
    ULONGLONG next = 0;
};
const VoiceModels& VoiceModelsNow(const Config& edit) {
    static VoiceModels s_vm;
    if (GetTickCount64() >= s_vm.next) {
        s_vm.next = GetTickCount64() + 2000;
        s_vm.catalog = LoadVoiceCatalog(edit.dataDir);
        s_vm.installed.clear();
        for (const VoiceModelInfo& vm : s_vm.catalog.models) s_vm.installed.push_back(VoiceModelInstalled(edit.VoiceModelDir(), vm));
    }
    return s_vm;
}

// The language whisper recognizes, as the Voice page says it.
std::wstring RecognitionLang(const Config& edit) {
    std::wstring rec = edit.voiceLang;
    if (rec.empty() || rec == L"write" || rec == L"auto") rec = edit.writeIn.empty() ? L"en" : edit.writeIn;
    return rec;
}
}  // namespace

// A framed group with its title on the top line (Nexus' look); EndBox draws the frame around what came between.
void BeginBox(const std::string& title) {
    const float fs = ImGui::GetFontSize();
    ImGui::Dummy(ImVec2(0.0f, fs * 0.45f));
    s_boxes.push_back({ImGui::GetCursorScreenPos(), title});
    ImGui::Dummy(ImVec2(0.0f, fs * 0.4f));
    ImGui::Indent(fs * 0.75f);
    ImGui::BeginGroup();
}

void EndBox() {
    ImGui::EndGroup();
    const float fs = ImGui::GetFontSize();
    ImGui::Unindent(fs * 0.75f);
    if (s_boxes.empty()) return;
    const BoxFrame b = s_boxes.back();
    s_boxes.pop_back();
    const float bottom = std::floor(ImGui::GetItemRectMax().y + fs * 0.45f);
    const float right = std::floor(ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x - 1.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 line = ImGui::GetColorU32(ImGuiCol_Separator);
    const ImVec2 ts = ImGui::CalcTextSize(b.title.c_str());
    const float tx = b.at.x + fs * 0.6f;
    dl->AddLine(b.at, ImVec2(tx - 4.0f, b.at.y), line);
    dl->AddLine(ImVec2(tx + ts.x + 4.0f, b.at.y), ImVec2(right, b.at.y), line);
    dl->AddLine(b.at, ImVec2(b.at.x, bottom), line);
    dl->AddLine(ImVec2(right, b.at.y), ImVec2(right, bottom), line);
    dl->AddLine(ImVec2(b.at.x, bottom), ImVec2(right + 1.0f, bottom), line);
    dl->AddText(ImGui::GetFont(), fs, ImVec2(tx, b.at.y - std::floor(ts.y * 0.5f)), ImGui::GetColorU32(ImGuiCol_Text),
                b.title.c_str());
    ImGui::SetCursorScreenPos(ImVec2(b.at.x, bottom + 1.0f));
    ImGui::Dummy(ImVec2(0.0f, fs * 0.25f));
}

// The language the chat is read in, its primary part ("DE").
std::wstring ReadingLangNow() {
    std::lock_guard<std::mutex> lock(g_mu);
    return PrimaryLang(ReadLang());
}

void SwitchOptionsTab(int tab) {
    s_delayedTab = tab;
    g_showOptions = true;
}

// The translators in the order the choosers list them, by their names.
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

namespace {
// A translator that cannot run before its key (LibreTranslate: its server) is there.
bool NeedsKey(const Config& c, Engine e) {
    return (e == Engine::DeepL && c.deeplKey.empty()) || (e == Engine::Google && c.googleKey.empty()) ||
           (e == Engine::Microsoft && c.msKey.empty()) || (e == Engine::Libre && c.libreUrl.empty());
}
bool UsesKey(Engine e) { return e == Engine::DeepL || e == Engine::Google || e == Engine::Microsoft || e == Engine::Libre; }
std::wstring& KeyOf(Config& c, Engine e) {
    return e == Engine::DeepL ? c.deeplKey : e == Engine::Google ? c.googleKey : e == Engine::Microsoft ? c.msKey : c.libreKey;
}

// A text field in a row: shows the setting while you are not in it, takes what you wrote when you leave it.
void RowText(const char* id, char* buf, size_t size, std::wstring& target, ImGuiInputTextFlags flags = 0,
             const char* hint = nullptr) {
    if (ImGui::GetActiveID() != ImGui::GetID(id)) strncpy_s(buf, size, U8(target).c_str(), _TRUNCATE);
    ImGui::SetNextItemWidth(ControlW());
    if (hint) ImGui::InputTextWithHint(id, hint, buf, size, flags);
    else ImGui::InputText(id, buf, size, flags);
    if (ImGui::IsItemDeactivatedAfterEdit()) target = Trim(W(buf));
}

// The translators, grouped by what they need: nothing, this PC, your own key (a note where one needs more).
bool EngineItems(const Config& edit, const LocalMtSummary& local, Engine current, Engine* chosen) {
    bool picked = false;
    auto item = [&](Engine e, const std::wstring& note) {
        std::string text = UI(Tr(EngineLabel(e)));
        if (!note.empty()) text += "   " + UI(note);
        if (ImGui::Selectable(text.c_str(), current == e)) {
            *chosen = e;
            picked = true;
        }
    };
    ImGui::TextDisabled("%s", UI(Tr(L"Ready right away")).c_str());
    item(Engine::Basic, {});
    item(Engine::Auto, {});
    ImGui::Separator();
    ImGui::TextDisabled("%s", UI(Tr(L"On this PC (offline)")).c_str());
    item(Engine::Nllb, local.module && local.nllb ? std::wstring() : Tr(L"install, about 600 MB"));
    item(Engine::Local, local.module && local.opus ? std::wstring() : Tr(L"choose a language pair first"));
    ImGui::Separator();
    ImGui::TextDisabled("%s", UI(Tr(L"With your own API key")).c_str());
    const std::wstring keyNeeded = Tr(L"key needed");
    item(Engine::DeepL, edit.deeplKey.empty() ? keyNeeded : std::wstring());
    item(Engine::Google, edit.googleKey.empty() ? keyNeeded : std::wstring());
    item(Engine::Microsoft, edit.msKey.empty() ? keyNeeded : std::wstring());
    item(Engine::Libre, edit.libreUrl.empty() ? Tr(L"server needed") : std::wstring());
    return picked;
}

// The translator and, for one on this PC, its quality – the same rows on Start and on the Translator page. What a
// choice still needs is asked right here: NLLB-200's download (its size first), a key (the field below – it switches
// over once the key is there). `full` (the Translator page): every key field and the MyMemory address.
void RenderTranslatorRows(Config& edit, bool full) {
    static bool s_confirmNllb = false, s_wantSet = false;
    static Engine s_want = Engine::Basic;
    static char keyBuf[256] = {}, regionBuf[64] = {}, urlBuf[256] = {}, mailBuf[128] = {};
    const LocalMtSummary local = LocalMtNow(edit.dataDir);
    const bool nllbReady = local.module && local.nllb;
    if (nllbReady || g_localMtBusy) s_confirmNllb = false;
    if (s_wantSet && (s_want == edit.engine || !NeedsKey(edit, s_want))) {
        if (s_want != edit.engine) edit.engine = s_want;  // the key is there: it switches over
        s_wantSet = false;
    }
    const Engine shown = s_wantSet ? s_want : edit.engine;
    const bool isLocal = shown == Engine::Nllb || shown == Engine::Local;
    const bool localReady = local.module && (shown == Engine::Nllb ? local.nllb : local.opus);
    RowLabel(UI(Tr(L"Translator")));
    Engine chosen = shown;
    bool picked = false;
    const float fs = ImGui::GetFontSize();
    const bool quality = isLocal && localReady;
    ImGui::SetNextItemWidth(quality ? std::max(fs * 6.0f, ControlW() - fs * 7.5f) : ControlW());
    if (ImGui::BeginCombo("##engine", UI(Tr(EngineLabel(shown))).c_str())) {
        picked = EngineItems(edit, local, shown, &chosen);
        ImGui::EndCombo();
    }
    if (quality) {  // the three steps: their names fit in every language
        static const int kQuality[3] = {1, 2, 4};
        const std::wstring step[3] = {Tr(L"Fast"), Tr(L"Balanced"), Tr(L"Best")};
        const std::wstring tip[3] = {Tr(L"Beam 1 – about 350 ms per line, the lowest CPU load"),
                                     Tr(L"Beam 2 – about 500 ms per line, recommended"),
                                     Tr(L"Beam 4 – about 900 ms per line, the finest nuance")};
        const int qi = edit.localQuality == 1 ? 0 : edit.localQuality == 4 ? 2 : 1;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(fs * 7.0f);
        if (ImGui::BeginCombo("##quality", UI(step[qi]).c_str())) {
            for (int q = 0; q < 3; ++q)
                if (ImGui::Selectable((UI(step[q]) + "   " + UI(tip[q])).c_str(), q == qi)) edit.localQuality = kQuality[q];
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", UI(tip[qi]).c_str());
    }
    if (picked && chosen != shown) {
        s_wantSet = false;
        if (chosen == Engine::Nllb && !nllbReady) s_confirmNllb = true;  // the size first, then OK installs and switches
        else if (chosen == Engine::Local && !(local.module && local.opus) && !full) SwitchOptionsTab(kPageTranslator);
        else if (NeedsKey(edit, chosen)) {
            s_wantSet = true;  // the key field below; the translator switches once the key is there
            s_want = chosen;
        } else edit.engine = chosen;
    }
    // What runs and how – one grey line.
    std::wstring how;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        if (isLocal && !localReady) how = Tr(L"The translation model is missing – Options → Translator.");
        else if (isLocal && local.helper.status == gct::HelperStatus::Outdated)
            how = TrF(L"Helper is outdated ({1})", {local.helper.outdatedReason});
        else if (isLocal) how = TrF(L"Offline on this PC · Beam {1}", {std::to_wstring(edit.localQuality)});
        else if (shown == Engine::Basic || shown == Engine::Auto) {
            how = MyMemoryQuotaText();
            if (how.empty()) how = Tr(L"Free and online – works without setup");
        } else how = Tr(L"Online with your API key");
    }
    RowHint(UI(how));
    if (shown == Engine::Nllb && !nllbReady && !s_confirmNllb && !g_localMtBusy) {
        ImGui::Indent(s_labelW);
        if (ImGui::Button(UI(Tr(L"Install NLLB-200 (local)")).c_str())) s_confirmNllb = true;
        ImGui::Unindent(s_labelW);
    }
    if (s_confirmNllb) {  // what comes, and how big, before anything is downloaded
        ImGui::Indent(s_labelW);
        ImGui::TextUnformatted(UI(Tr(L"Install NLLB-200 (local)")).c_str());
        Hint(UI(Tr(L"Local Translation installs the local neural machine translation engine and NLLB-200 multilingual model.")));
        ImGui::TextColored(ImVec4(0.51f, 0.76f, 0.63f, 1.f), "%s", UI(Tr(L"Download size: about 600 MB · translates offline on this PC")).c_str());
        if (ImGui::Button((UI(Tr(L"OK")) + "##nllbok").c_str())) {
            InstallLocalMtQuickSetup(edit.dataDir);  // becomes the translator once the files are there
            s_confirmNllb = false;
        }
        ImGui::SameLine();
        if (ImGui::Button((UI(Tr(L"Cancel")) + "##nllbno").c_str())) s_confirmNllb = false;
        ImGui::Unindent(s_labelW);
    }
    if (g_localMtBusy) {
        const int pct = g_localMtPct.load();
        ImGui::Indent(s_labelW);
        ImGui::ProgressBar(pct >= 0 ? pct / 100.0f : 0.0f, ImVec2(ControlW(), 0.0f),
                           pct >= 0 ? (std::to_string(pct) + " %").c_str() : UI(Tr(L"Installing…")).c_str());
        Hint(UI(Tr(L"Downloading in background – game remains playable")));
        ImGui::Unindent(s_labelW);
    }
    // Keys: on the Translator page for a translator that uses one, on Start while one is still missing.
    if (UsesKey(shown) && (full || s_wantSet)) {
        if (shown != Engine::Libre || full) {
            RowLabel(UI(Tr(L"API key")));
            RowText("##key", keyBuf, sizeof keyBuf, KeyOf(edit, shown), ImGuiInputTextFlags_Password);
        }
        if (shown == Engine::Microsoft) {
            RowLabel(UI(Tr(L"Region")));
            RowText("##region", regionBuf, sizeof regionBuf, edit.msRegion);
        }
        if (shown == Engine::Libre) {
            RowLabel(UI(Tr(L"Server")));
            RowText("##url", urlBuf, sizeof urlBuf, edit.libreUrl);
        }
        if (s_wantSet) RowHint(UI(Tr(L"Enter the key – then it switches over.")));
    }
    if (full && (shown == Engine::Auto || shown == Engine::Basic)) {
        RowLabel("MyMemory");
        RowText("##mail", mailBuf, sizeof mailBuf, edit.basicEmail, 0, UI(Tr(L"E-mail for MyMemory (more free text per day)")).c_str());
        RowHint(UI(Tr(L"Any made-up address works – MyMemory only counts the free text per address (5,000 characters a "
                      L"day without one, 50,000 with one).")));
    }
}

// "I read in": the same setting as in the chat tab's menu.
void RenderReadRow(Config& edit) {
    RowLabel(UI(Tr(L"I read in")));
    ImGui::SetNextItemWidth(ControlW());
    const std::string shown = edit.readLang.empty()  // "System (Deutsch)": which language that is
                                  ? UI(Tr(L"System")) + " (" + U8(NativeName(ReadingLangNow())) + ")"
                                  : U8(NativeName(edit.readLang));
    if (ImGui::BeginCombo("##readlang", shown.c_str())) {
        if (ImGui::Selectable(UI(Tr(L"System")).c_str(), edit.readLang.empty())) edit.readLang.clear();
        for (const LangInfo& lg : Languages())
            if (DrawnInOurWindow(lg) && ImGui::Selectable(U8(NativeName(lg.code)).c_str(), edit.readLang == lg.code))
                edit.readLang = lg.code;
        ImGui::EndCombo();
    }
}

// "I understand": the one list of what is never translated by itself (core/translate_policy) – your reading language
// first (it follows when you change it; removable and back with + – [Translate] SkipTypingLang), then the languages
// you understand ([Translate] Understood).
void RenderUnderstood(Config& edit) {
    static const wchar_t* kCommon[] = {L"EN", L"DE", L"FR", L"ES", L"IT", L"PT", L"NL", L"PL",
                                       L"RU", L"TR"};  // what our window draws (no CJK, no Arabic)
    const std::wstring reading = ReadingLangNow();
    ImGui::BeginGroup();  // a new line of chips starts under the first, not at the label
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ImGui::GetFontSize());
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
    ImGui::PushID("understood");
    if (edit.skipTypingLang && chip(U8(NativeName(reading)) + "  x##mine")) edit.skipTypingLang = false;
    auto& list = edit.understoodLangs;
    for (size_t i = 0; i < list.size(); ++i)
        if (chip(U8(NativeName(list[i])) + "  x##" + U8(list[i]))) {
            list.erase(list.begin() + static_cast<std::ptrdiff_t>(i));
            break;
        }
    if (!first) ImGui::SameLine();
    if (ImGui::SmallButton(" + ")) ImGui::OpenPopup("add");
    ImGui::PopStyleVar();
    if (ImGui::BeginPopup("add")) {
        if (!edit.skipTypingLang && ImGui::Selectable(UI(TrF(L"My reading language ({1})", {reading})).c_str()))
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
    ImGui::EndGroup();
}

// "I send": the footer's "Send" for everything but a whisper tab with its own – for typing and speaking alike.
void RenderSendRow() {
    RowLabel(UI(Tr(L"I send")));
    std::lock_guard<std::mutex> lock(g_mu);
    const int n = static_cast<int>(g_cfg.writeLangs.size());
    const std::string shown = g_sendIdx >= 0 && g_sendIdx < n ? U8(NativeName(g_cfg.writeLangs[static_cast<size_t>(g_sendIdx)]))
                                                              : UI(Tr(L"as written"));
    ImGui::SetNextItemWidth(ControlW());
    ImGui::PushStyleColor(ImGuiCol_Text, kSendOrange);
    const bool open = ImGui::BeginCombo("##send", shown.c_str());
    ImGui::PopStyleColor();
    if (open) {
        SendLanguageItems(false);
        ImGui::EndCombo();
    }
}

// The language of this window (Start and the Window page).
void RenderUiLanguageRow(Config& edit) {
    RowLabel(UI(Tr(L"Language of this window")));
    std::string current = UI(Tr(L"System"));  // only the languages this window has texts in (core/i18n: UiLanguages)
    for (const UiLangInfo& l : UiLanguages())  // "System (Deutsch)": the language "System" stands for now
        if (l.lang == GetUiLang()) current += " (" + U8(l.native) + ")";
    for (const UiLangInfo& l : UiLanguages())
        if (ToLowerAscii(PrimaryLang(edit.uiLangCode)) == l.code) current = U8(l.native);  // "DE" of old too
    ImGui::SetNextItemWidth(ControlW());
    if (ImGui::BeginCombo("##uilang", current.c_str())) {
        if (ImGui::Selectable(UI(Tr(L"System")).c_str(), edit.uiLangCode.empty())) edit.uiLangCode.clear();
        for (const UiLangInfo& l : UiLanguages())
            if (ImGui::Selectable(U8(l.native).c_str(), ToLowerAscii(PrimaryLang(edit.uiLangCode)) == l.code))
                edit.uiLangCode = l.code;
        ImGui::EndCombo();
    }
}

// The voice module on Start: what it is, how well it knows your language, and the one step left – install (its size
// first), switch on, load the model; once it runs, its model. g_mu NOT held.
void RenderVoiceStart(Config& edit) {
    static bool s_confirm = false;
    const ModuleState& m = Modules();
    const bool installed = gct::VoiceModulePresent(KeyboardDir()) || m.voiceModule;
    const bool active = installed && edit.voiceEnabled;
    const bool busy = g_voiceModuleInstalling || g_downloadPct >= 0;
    if (installed || busy) s_confirm = false;
    const VoiceModels& vms = VoiceModelsNow(edit);
    const VoiceModelInfo* model = vms.catalog.Find(U8(edit.voiceModel));
    if (!model) model = vms.catalog.Recommended();
    bool modelThere = false;
    for (size_t k = 0; k < vms.catalog.models.size(); ++k)
        if (model && vms.catalog.models[k].id == model->id) modelThere = vms.installed[k];
    const int modelMb = model ? static_cast<int>(model->bytes / (1024 * 1024)) : 0;
    const std::wstring recLang = RecognitionLang(edit);
    const std::string whisperLang = WhisperLang(recLang);
    const gct::VoiceRecommendation rec = gct::RecommendVoiceModel(vms.catalog, model, whisperLang);
    BeginBox(UI(installed ? Tr(L"Voice module") : Tr(L"Voice module") + L" · " + Tr(L"optional")));
    if (busy) {
        const int pct = g_downloadPct >= 0 ? g_downloadPct.load() : g_voiceModulePct.load();
        ImGui::TextUnformatted(UI(g_voiceModuleInstalling ? Tr(L"Downloading whisper speech engine…")
                                                          : Tr(L"Downloading speech recognition model…")).c_str());
        ImGui::ProgressBar(pct >= 0 ? pct / 100.0f : 0.0f, ImVec2(ControlW(), 0.0f),
                           pct >= 0 ? (std::to_string(pct) + " %").c_str() : UI(Tr(L"Installing…")).c_str());
        Hint(UI(Tr(L"Downloading in background – game remains playable")));
    } else if (s_confirm) {  // what comes, and how big, before anything is downloaded
        ImGui::TextUnformatted(UI(Tr(L"Download Voice Module")).c_str());
        Hint(UI(TrF(L"The voice module installs the local whisper speech recognition and the model {1}.",
                    {model ? W(model->name) : std::wstring(L"-")})));
        ImGui::TextColored(ImVec4(0.51f, 0.76f, 0.63f, 1.f), "%s",
                           UI(TrF(L"Download size: about {1} MB · recognizes on this PC", {std::to_wstring(modelMb + 7)})).c_str());
        if (ImGui::Button((UI(Tr(L"OK")) + "##voiceok").c_str())) {
            InstallVoiceQuickSetup();
            edit.voiceEnabled = true;
            s_confirm = false;
        }
        ImGui::SameLine();
        if (ImGui::Button((UI(Tr(L"Cancel")) + "##voiceno").c_str())) s_confirm = false;
    } else {
        VoiceRun run;
        bool handsFree = false, voiceAct = false;
        {
            std::lock_guard<std::mutex> lock(g_mu);
            if (model)
                if (auto it = g_voiceRuns.find(W(model->id)); it != g_voiceRuns.end()) run = it->second;
            handsFree = g_cfg.voiceAutoSend;
            voiceAct = g_cfg.voiceActivation;
        }
        ImGui::TextUnformatted(((model ? model->name : std::string("-")) + "  ·  " + std::to_string(modelMb) + " MB").c_str());
        if (active) {  // the microphone's mode, in the accent colour
            const std::wstring mode = (voiceAct ? Tr(L"Voice activation") : Tr(L"Push-to-Talk")) +
                                      (handsFree ? L" · " + TrF(L"Hands free: {1}", {Tr(L"on")}) : std::wstring());
            ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_CheckMark), "%s", UI(mode).c_str());
        }
        Hint(UI(rec.supportedByWhisper ? TrF(L"Recognition of {1}: {2}", {NativeName(recLang), gct::VoiceQualityWord(rec.quality)})
                                       : Tr(L"Language not supported by speech recognition")));
        std::wstring third;
        if (!installed) third = Tr(L"Recognizes on this PC – no sound leaves it.");
        else if (!active) third = Tr(L"Installed – switched off.");
        else if (!modelThere) third = TrF(L"The model is not downloaded yet ({1} MB).", {std::to_wstring(modelMb)});
        else if (run.rec > 0)
            third = TrF(L"peak {1} % CPU (highest {2} %) · {3} s for {4} s speech",
                        {std::to_wstring(run.peak), std::to_wstring(run.maxPeak), FormatSeconds(run.rms), FormatSeconds(run.rec)});
        else third = TrF(L"Speed: {1}", {Tr(L"not measured yet")});
        Hint(UI(third));
        if (!installed) {
            if (ImGui::Button((UI(Tr(L"Install")) + "##voiceinstall").c_str())) s_confirm = true;
        } else if (!active) {
            if (ImGui::Button((UI(Tr(L"Enable")) + "##voiceenable").c_str())) edit.voiceEnabled = true;
        } else if (!modelThere && model) {
            if (ImGui::Button(UI(TrF(L"Load model ({1} MB)", {std::to_wstring(modelMb)})).c_str())) StartModelDownload(*model);
        }
        if (active) {  // the model: every one with its size and how well it knows your language
            RowLabel(UI(Tr(L"Model")));
            ImGui::SetNextItemWidth(ControlW());
            if (ImGui::BeginCombo("##startmodel", model ? model->name.c_str() : "-")) {
                ImGui::TextDisabled("%s", UI(TrF(L"Voice model · recognition of {1}", {NativeName(recLang)})).c_str());
                for (size_t k = 0; k < vms.catalog.models.size(); ++k) {
                    const VoiceModelInfo& vm = vms.catalog.models[k];
                    std::string text = vm.name + "   " + std::to_string(vm.bytes / (1024 * 1024)) + " MB   " +
                                       UI(gct::VoiceQualityWord(vm.QualityFor(whisperLang)));
                    if (vms.installed[k]) text += "   " + UI(Tr(L"installed"));
                    if (rec.recommended && rec.recommended->id == vm.id) text += "   " + UI(Tr(L"recommended"));
                    if (ImGui::Selectable(text.c_str(), model && model->id == vm.id)) edit.voiceModel = W(vm.id);
                }
                ImGui::EndCombo();
            }
        }
    }
    EndBox();
}
}  // namespace

// The word lists (Technical tab): how many entries each holds – the built-in ones compiled in, yours and the learned
// ones to look at and correct here. Typo patterns only counted: correcting them by hand would fight what typing learns.
void RenderWordLists() {
    if (!ImGui::CollapsingHeader(UI(Tr(L"Word lists")).c_str())) return;  // folded until you open it
    size_t never = 0;
    for (const BuiltinTerm& bt : BuiltinTerms())
        if (bt.kind != TermKind::Word) ++never;
    ImGui::TextDisabled("%s", UI(TrF(L"Built-in GW2 terms: {1} (never translated: {2})",
                                     {std::to_wstring(BuiltinTerms().size()), std::to_wstring(never)})).c_str());
    ImGui::TextDisabled("%s", UI(TrF(L"Official names: {1} in en/de/fr/es", {std::to_wstring(BuiltinNames(L"en").size())})).c_str());
    ImGui::TextDisabled("%s", UI(TrF(L"Usual words: {1}", {std::to_wstring(BuiltinWordCount())})).c_str());
    if (!g_spell) return;
    auto chips = [](const std::vector<std::wstring>& items, auto&& onRemove) {  // one entry each, "x" removes it
        for (size_t i = 0; i < items.size(); ++i) {
            const std::string label = U8(items[i]) + "  x##" + std::to_string(i);
            const float w = ImGui::CalcTextSize(label.c_str(), nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2;
            if (i > 0) {
                ImGui::SameLine();
                if (ImGui::GetContentRegionAvail().x < w) ImGui::NewLine();
            }
            if (ImGui::SmallButton(label.c_str())) onRemove(items[i]);
        }
    };
    // a list's file in your editor; after saving it there, "Reload after editing" takes it in
    auto fileButtons = [](const char* id, const std::wstring& path, auto&& reload) {
        ImGui::SameLine();
        if (ImGui::SmallButton((UI(Tr(L"Open")) + "##open" + id).c_str()) && !path.empty())
            ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        ImGui::SameLine();
        if (ImGui::SmallButton((UI(Tr(L"Reload after editing")) + "##reload" + id).c_str())) g_worker.Post(reload);
    };
    const std::vector<std::wstring> mine = g_spell->MyTerms();
    ImGui::TextUnformatted(UI(TrF(L"Your terms: {1}", {std::to_wstring(mine.size())})).c_str());
    fileButtons("terms", g_spell->TermsFile(), [] { if (g_spell) g_spell->ReloadTerms(); });
    ImGui::PushID("myterms");
    chips(mine, [](const std::wstring& m) { g_worker.Post([m] { if (g_spell) g_spell->RemoveTerm(m); }); });
    static char addBuf[64] = {};
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
    const bool enter = ImGui::InputText("##addterm", addBuf, sizeof(addBuf), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if ((ImGui::SmallButton(UI(Tr(L"Add")).c_str()) || enter) && addBuf[0]) {
        const std::wstring t = Trim(W(addBuf));
        addBuf[0] = 0;
        g_worker.Post([t] { if (g_spell) g_spell->AddTerm(t); });
    }
    ImGui::PopID();
    // the learned words: the most used first (40), a search finds any; "x" forgets one
    const auto snap = g_spell->Model().GetSnapshot();
    ImGui::TextUnformatted(UI(TrF(L"Learned words: {1} – the most used; x forgets one",
                                  {std::to_wstring(snap->words.size())})).c_str());
    fileButtons("learned", g_spell->LearnedFile(), [] { if (g_spell) g_spell->ReloadLearned(); });
    static char findBuf[32] = {};
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
    ImGui::InputTextWithHint("##findlearned", UI(Tr(L"Find a word")).c_str(), findBuf, sizeof(findBuf));
    const std::wstring find = CaseFold(W(findBuf));
    std::vector<const WordModelSnapshot::Word*> found;
    for (const auto& [key, w] : snap->words)
        if (find.empty() || key.find(find) != std::wstring::npos) found.push_back(&w);
    const size_t n = std::min<size_t>(found.size(), 40);
    std::partial_sort(found.begin(), found.begin() + static_cast<std::ptrdiff_t>(n), found.end(),
                      [](const WordModelSnapshot::Word* a, const WordModelSnapshot::Word* b) { return a->count > b->count; });
    std::vector<std::wstring> shown;
    for (size_t i = 0; i < n; ++i) shown.push_back(found[i]->form);
    ImGui::PushID("learned");
    chips(shown, [](const std::wstring& w) { g_worker.Post([w] { if (g_spell) g_spell->Forget(w); }); });
    ImGui::PopID();
    ImGui::TextDisabled("%s", UI(TrF(L"Typo patterns learned: {1}", {std::to_wstring(g_spell->TypoPatterns())})).c_str());
}

// The settings this window edits through its copy (`edit`); sizes, the layout and the looks are taken and saved at
// once where they are drawn. One list for both: what counts as changed here, and what goes into the settings.
#define KB_VOICE_OPTIONS(X)                                                                                            \
    X(voiceEnabled) X(voiceModel) X(voiceLang) X(voiceToggle) X(voiceMaxSec) X(voiceLive) X(voiceThreads)              \
    X(voiceDevice) X(voiceAutoSend) X(voiceBoost) X(voiceActivation) X(voicePauseMs) X(voicePreRollMs)                \
    X(voiceSensitivity) X(voiceMinMs) X(voiceGlobalKey) X(voiceChatPause)
#define KB_OPTIONS(X)                                                                                                  \
    X(engine) X(writeIn) X(backTranslate) X(understoodLangs) X(skipTypingLang) X(readLang) X(showOriginal) X(noJunk)   \
    X(localQuality) X(mtThreads) X(basicEmail) X(deeplKey) X(googleKey) X(msKey) X(msRegion) X(libreUrl) X(libreKey)   \
    X(uiLangCode) X(spellEnabled) X(autoCorrect) X(suggestions) X(learnWords) X(learnLater) X(returnFocus)            \
    X(copyOnly) X(focusOnGameChat) X(send.autoTiming) X(send.keyHoldMs) X(send.stepDelayMs) KB_VOICE_OPTIONS(X)

namespace {
bool OptionsEdited(const Config& edit, const Config& opened) {
#define KB_DIFFERS(f) if (!(edit.f == opened.f)) return true;
    KB_OPTIONS(KB_DIFFERS)
#undef KB_DIFFERS
    return false;
}

// What was changed here goes into the settings at once – only that: what the footer or a menu changed meanwhile
// stays. Then what depends on it follows: the translator, the typing help, the voice module (g_mu NOT held).
void ApplyOptions(Config& edit, Config& opened) {
    bool typingChanged, learnChanged, voiceChanged, voiceTuned = false, pttChanged, listenChanged;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        Config next = g_cfg;
#define KB_TAKE(f) if (!(edit.f == opened.f)) next.f = edit.f;
        KB_OPTIONS(KB_TAKE)
#undef KB_TAKE
        next.uiLang = EffectiveUiLang(next.uiLangCode);
        const Config& old = g_cfg;
        typingChanged = next.writeIn != old.writeIn || next.spellEnabled != old.spellEnabled;
        learnChanged = next.learnWords != old.learnWords || next.learnLater != old.learnLater;
        voiceChanged = next.voiceEnabled != old.voiceEnabled;
#define KB_TUNED(f) voiceTuned = voiceTuned || !(next.f == old.f);
        KB_VOICE_OPTIONS(KB_TUNED)
#undef KB_TUNED
        pttChanged = next.voiceGlobalKey != old.voiceGlobalKey;
        listenChanged = voiceChanged || next.voiceActivation != old.voiceActivation;
        const bool engineChanged = next.engine != old.engine;
        // Only a changed translator or key builds a new one: a new local helper loads its model again.
        const bool translatorChanged = engineChanged || next.deeplKey != old.deeplKey || next.googleKey != old.googleKey ||
                                       next.msKey != old.msKey || next.msRegion != old.msRegion ||
                                       next.libreUrl != old.libreUrl || next.libreKey != old.libreKey ||
                                       next.basicEmail != old.basicEmail || next.mtThreads != old.mtThreads;
        const bool qualityChanged = next.localQuality != old.localQuality;
        const bool readChanged = next.readLang != old.readLang || next.understoodLangs != old.understoodLangs ||
                                 next.skipTypingLang != old.skipTypingLang;
        const bool uiChanged = next.uiLang != old.uiLang;
        g_cfg = next;
        g_cfg.SaveAll();
        if (uiChanged) {
            SetUiLang(g_cfg.uiLang);
            PrepareFaultAlert();  // the alert after a fault in the language chosen now
        }
        if (engineChanged) g_myMemorySessionFallback = false;  // a translator you choose is the one that runs
        if (translatorChanged || !g_translator) {
            g_translator = MakeTranslator(g_cfg);
            WarmUpTranslator(g_translator);
        }
        if (translatorChanged || typingChanged) {
            g_preview.clear();
            ++g_inputGen;
            g_inputChanged = GetTickCount64();
        }
        // the chat in view comes again (a line remembers the target and beam it was made with)
        if (g_nx.translateChat && (translatorChanged || typingChanged || qualityChanged || readChanged))
            TranslateRecentLines(false);
        if (engineChanged)
            SetStatus(TrF(L"Translator: {1}", {g_translator ? g_translator->Name() : std::wstring(L"-")}), 3000);
        else if (qualityChanged)
            SetStatus(TrF(L"Quality set: beam {1} – the visible chat is translated again", {std::to_wstring(g_cfg.localQuality)}), 4000);
        edit = opened = g_cfg;
    }
    if (typingChanged) g_worker.Post([] {
        FreeWorkerState();
        InitSpelling();
    });
    else if (learnChanged) g_worker.Post([learn = edit.learnWords] { if (g_spell) g_spell->SetLearnChoices(learn); });
    if (pttChanged) ApplyPushToTalkKey(edit.voiceGlobalKey);
    if (listenChanged) {
        if (!edit.voiceActivation || !edit.voiceEnabled) StopListening();
        else g_listenUserOff = false;
    }
    if (voiceChanged && edit.voiceEnabled && !g_voice.Running() && GetFileAttributesW(HelperExe().c_str()) != INVALID_FILE_ATTRIBUTES)
        g_worker.Post([dir = edit.dataDir] {
            Config::FlushWrites();
            std::lock_guard<std::mutex> lock(g_mu);
            if (!g_voice.Running()) g_voice.Start(HelperExe(), dir);
        });
    else if (voiceTuned && g_voice.Running())
        VoiceReload();
}

// ---- The pages

void RenderStartPage(Config& edit) {
    bool chatOn;
    std::wstring running;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        chatOn = g_nx.translateChat;
        running = g_translator ? g_translator->Name() : std::wstring();
    }
    // Events: Chat, the chat reader: without it nothing of the chat is read – easy to miss at the first start, so
    // while it is missing it stands here first, with its install button.
    const bool chatThere = g_chatEventsLive || g_chatEventsPresent;
    if (!chatThere) {
        RenderChatModule(true);
        if (!g_chatEventsFile && !g_chatEventsInstalling)
            Hint(UI(Tr(L"Without Events: Chat, GW2Chatlator cannot read the chat – writing and the typing help work anyway.")));
        ImGui::Spacing();
    }
    const LocalMtSummary local = LocalMtNow(edit.dataDir);
    const bool isLocal = edit.engine == Engine::Nllb || edit.engine == Engine::Local;
    const bool localReady = local.module && (edit.engine == Engine::Nllb ? local.nllb : local.opus);
    const std::string title = UI(chatOn ? Tr(L"Chat translation is on") : Tr(L"Chat translation is off"));
    const std::string sub = UI(chatOn ? TrF(L"Foreign lines appear in {1} · Ctrl+Alt+T switches", {NativeName(ReadingLangNow())})
                                      : Tr(L"A click on a line translates just that one · Ctrl+Alt+T switches"));
    if (Hero(chatOn, title, sub, UI(Tr(L"Chat module")), chatThere, UI(running.empty() ? Tr(EngineLabel(edit.engine)) : running),
             !isLocal || localReady)) {
        std::lock_guard<std::mutex> lock(g_mu);
        SetChatTranslation(!g_nx.translateChat);
    }
    BeginBox(UI(Tr(L"Translation")));
    RenderReadRow(edit);
    RowHint(UI(Tr(L"Foreign lines appear in this language.")));
    RowLabel(UI(Tr(L"I understand")));
    RenderUnderstood(edit);
    RowHint(UI(Tr(L"Lines in these languages stay as they are.")));
    RenderSendRow();
    RowHint(UI(Tr(L"For typing and speaking – the same choice as \"Send\" in the chat window.")));
    RenderTranslatorRows(edit, false);
    EndBox();
    RenderVoiceStart(edit);
    BeginBox(UI(Tr(L"Window & keys")));
    RenderUiLanguageRow(edit);
    auto badge = [](const char* key, const std::string& what) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
        ImGui::PushStyleColor(ImGuiCol_Text, kSendOrange);
        ImGui::SmallButton(key);
        ImGui::PopStyleColor(2);
        ImGui::SameLine(0, ImGui::GetFontSize() * 0.35f);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", what.c_str());
    };
    badge("Ctrl+Alt+K", UI(Tr(L"Window")));
    ImGui::SameLine(0, ImGui::GetFontSize() * 0.9f);
    badge("Focus", UI(Tr(L"Input")));
    ImGui::SameLine(0, ImGui::GetFontSize() * 0.9f);
    badge("Ctrl+Alt+T", UI(Tr(L"Translation")));
    if (edit.voiceEnabled) {
        ImGui::SameLine(0, ImGui::GetFontSize() * 0.9f);
        badge("PTT", UI(Tr(L"Voice")));
    }
    Hint(UI(Tr(L"Keybinds: Nexus → Keybinds → GW2Chatlator.")));
    EndBox();
}

void RenderChatPage(Config& edit) {
    BeginBox(UI(Tr(L"Display")));
    Switch("orig", UI(Tr(L"Original under the translation")), &edit.showOriginal);
    Switch("unsure", UI(Tr(L"Unsure: show the original")), &edit.noJunk);
    Hint(UI(Tr(L"A click on a line always translates it. A click on a name opens a whisper tab that answers in "
               L"that player's language.")));
    EndBox();
    BeginBox(UI(Tr(L"Focus")));
    {
        std::lock_guard<std::mutex> lock(g_mu);
        bool full = g_layout.autoOpen && edit.focusOnGameChat && edit.returnFocus;
        const std::string tip = UI(Tr(L"Enables all 3 focus options: automatically opens, takes over Enter in game, and keeps focus after sending."));
        if (Switch("full", UI(Tr(L"Complete chat takeover (full focus)")), &full, tip.c_str())) {
            g_layout.autoOpen = full;
            edit.focusOnGameChat = full;
            edit.returnFocus = full;
            SaveLayout();
        }
        if (Switch("open", UI(Tr(L"Open again when GW2's chat opens (Enter)")), &g_layout.autoOpen)) SaveLayout();
    }
    Switch("enter", UI(Tr(L"Enter in the game opens GW2Chatlator's input")), &edit.focusOnGameChat);
    Hint(UI(Tr(L"Instead of GW2's own chat line: you type here, with the typing help and the translation.")));
    Switch("return", UI(Tr(L"After sending, keep typing in GW2Chatlator")), &edit.returnFocus);
    Hint(UI(Tr(L"The input keeps the keyboard after Enter – the next message goes right after.")));
    EndBox();
    BeginBox(UI(Tr(L"Sending")));
    RowLabel(UI(Tr(L"Enter does")));
    int enterDoes = edit.copyOnly ? 1 : 0;
    if (RowCombo("##enter", {UI(Tr(L"Send into the GW2 chat")), UI(Tr(L"Only copy (not a single key reaches the game)"))}, &enterDoes))
        edit.copyOnly = enterDoes == 1;
    Switch("back", UI(Tr(L"Show the back-translation of my message")), &edit.backTranslate);
    EndBox();
}

void RenderTypingPage(Config& edit) {
    static bool s_forgetAsked = false;
    BeginBox(UI(Tr(L"Typing help")));
    RowLabel(UI(Tr(L"I write in")));
    ImGui::SetNextItemWidth(ControlW());
    LanguageCombo("##writein", edit.writeIn, Tr(L"System") + L" (" + NativeName(TypingLang()) + L")");
    RowHint(UI(Tr(L"Only suggestions and the dictionary – the language of your text is recognized.")));
    Switch("sugg", UI(Tr(L"Word suggestions while typing")), &edit.suggestions);
    Switch("spell", UI(Tr(L"Spell checking (Windows, offline)")), &edit.spellEnabled);
    RowLabel(UI(Tr(L"Autocorrection")));
    int ac = static_cast<int>(edit.autoCorrect);
    if (RowCombo("##ac", {UI(Tr(L"Off")), UI(Tr(L"Safe")), UI(Tr(L"Like a phone keyboard"))}, &ac))
        edit.autoCorrect = static_cast<AutoCorrectMode>(ac);
    EndBox();
    BeginBox(UI(Tr(L"Learning")));
    RowLabel(UI(Tr(L"Learn")));
    int learn = !edit.learnWords ? 2 : edit.learnLater ? 1 : 0;
    if (RowCombo("##learn", {UI(Tr(L"Learn from what I send – while playing")), UI(Tr(L"Learn from what I send – later")),
                             UI(Tr(L"Do not learn"))}, &learn)) {
        edit.learnWords = learn != 2;
        edit.learnLater = learn == 1;
    }
    if (!s_forgetAsked) {
        if (ImGui::Button(UI(Tr(L"Delete everything learned…")).c_str())) s_forgetAsked = true;
    } else {
        ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.35f, 1.f), "%s", UI(Tr(L"Really delete everything learned?")).c_str());
        ImGui::SameLine();
        if (ImGui::Button(UI(Tr(L"Yes")).c_str())) {
            g_worker.Post([] { if (g_spell) g_spell->ForgetAll(); });
            s_forgetAsked = false;
        }
        ImGui::SameLine();
        if (ImGui::Button(UI(Tr(L"No")).c_str())) s_forgetAsked = false;
    }
    EndBox();
}

void RenderVoicePage(Config& edit, bool entered) {
    static std::vector<AudioDevice> s_mics;
    if (entered || s_mics.empty()) s_mics = AudioCapture::Devices();
    const bool voiceModuleThere = Modules().voiceModule;  // voice-only switches wait for the voice module
    const std::string needsVoice = UI(Tr(L"Needs the voice module (Options → Voice)."));
    BeginBox(UI(Tr(L"Voice module")));
    Switch("voiceon", UI(Tr(L"Voice input")), &edit.voiceEnabled);
    RenderVoiceModule(false);
    const VoiceModels& vms = VoiceModelsNow(edit);
    const VoiceModelInfo* chosen = vms.catalog.Find(U8(edit.voiceModel));
    if (!chosen) chosen = vms.catalog.Recommended();
    RowLabel(UI(Tr(L"Model")));
    ImGui::SetNextItemWidth(ControlW());
    if (ImGui::BeginCombo("##model", chosen ? chosen->name.c_str() : "-")) {
        for (size_t k = 0; k < vms.catalog.models.size(); ++k) {
            const VoiceModelInfo& m = vms.catalog.models[k];
            const std::string label = m.name + "  (" + std::to_string(m.bytes / (1024 * 1024)) + " MB)" + (vms.installed[k] ? "  [OK]" : "");
            if (ImGui::Selectable(label.c_str(), chosen && chosen->id == m.id)) edit.voiceModel = W(m.id);
        }
        ImGui::EndCombo();
    }
    if (chosen && !VoiceModelInstalled(edit.VoiceModelDir(), *chosen)) {
        const int pct = g_downloadPct;
        ImGui::Indent(s_labelW);
        if (pct >= 0) ImGui::ProgressBar(pct / 100.f, ImVec2(ControlW(), 0));
        else if (ImGui::Button(UI(Tr(L"Download the model")).c_str())) StartModelDownload(*chosen);
        ImGui::Unindent(s_labelW);
    }
    // The language you speak: whisper recognizes it; where it goes is "I send" (Start) – for typing and speaking alike.
    RowLabel(UI(Tr(L"Language you speak")));
    {
        std::vector<std::string> langs{UI(Tr(L"Auto-detect")), UI(Tr(L"Like I write"))};
        std::vector<std::wstring> codes{L"auto", L"write"};
        for (const LangInfo& l : Languages()) {
            if (!DrawnInOurWindow(l)) continue;
            langs.push_back(U8(NativeName(l.code)));
            codes.push_back(l.code);
        }
        int li = 0;
        for (size_t i = 0; i < codes.size(); ++i)
            if (codes[i] == edit.voiceLang || (codes[i] == L"write" && edit.voiceLang.empty())) {
                li = static_cast<int>(i);
                break;
            }
        if (RowCombo("##vlang", langs, &li)) edit.voiceLang = codes[static_cast<size_t>(li)];
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", UI(Tr(L"Speech recognition language: Auto-detect recognises what you speak automatically.")).c_str());
    }
    {
        const std::wstring recLang = RecognitionLang(edit);
        const gct::VoiceRecommendation rec = gct::RecommendVoiceModel(vms.catalog, chosen, WhisperLang(recLang));
        if (!rec.supportedByWhisper) {
            ImGui::Indent(s_labelW);
            ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.35f, 1.0f), "%s", UI(Tr(L"Language not supported by speech recognition")).c_str());
            ImGui::Unindent(s_labelW);
        } else {
            std::wstring line = TrF(L"Recognition of {1}: {2}", {NativeName(recLang), gct::VoiceQualityWord(rec.quality)});
            if (rec.quality < 2 && rec.recommended)
                line += L"  (" + TrF(L"Recommended: {1} ({2} MB RAM)", {W(rec.recommended->name), std::to_wstring(rec.recommended->ramMb)}) + L")";
            RowHint(UI(line));
        }
        const std::wstring sendLang = SendLang();
        if (PrimaryLang(sendLang) == L"EN") {
            RowHint(UI(Tr(L"Send route: Whisper translates directly to English on this PC.")));
        } else {
            const bool hasLocalMt = gct::LocalMtModelPresent(edit.dataDir, "opus-mt") || gct::LocalMtModelPresent(edit.dataDir, "nllb-200");
            RowHint(UI(hasLocalMt && (edit.engine == Engine::Local || edit.engine == Engine::Nllb)
                           ? Tr(L"Send route: Speech recognized locally, translated with local MT package.")
                           : Tr(L"Send route: Speech recognized locally, translated with online translator.")));
        }
    }
    // The installed models: what each needed when it last recognized you; use one, or remove what does not suit.
    static std::string s_removeAsked;
    for (size_t k = 0; k < vms.catalog.models.size(); ++k) {
        const VoiceModelInfo& m = vms.catalog.models[k];
        if (!vms.installed[k]) continue;
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
        ImGui::TextDisabled("%s", UI(run.rec > 0 ? TrF(L"peak {1} % CPU (highest {2} %) · {3} s for {4} s speech",
                                                        {std::to_wstring(run.peak), std::to_wstring(run.maxPeak),
                                                         FormatSeconds(run.rms), FormatSeconds(run.rec)})
                                                 : Tr(L"not measured yet")).c_str());
        if (!inUse) {
            ImGui::SameLine();
            if (ImGui::SmallButton(UI(Tr(L"Use")).c_str())) edit.voiceModel = W(m.id);
        }
        ImGui::SameLine();
        if (s_removeAsked != m.id) {
            if (ImGui::SmallButton(UI(Tr(L"Remove")).c_str())) s_removeAsked = m.id;
        } else {
            if (ImGui::SmallButton(UI(Tr(L"Really remove")).c_str())) {
                const bool ok = DeleteVoiceModel(edit.VoiceModelDir(), m);
                std::lock_guard<std::mutex> lock(g_mu);
                SetStatus(ok ? TrF(L"{1} removed", {W(m.name)}) : TrF(L"{1} is in use – try again after Unload", {W(m.name)}), 8000);
                s_removeAsked.clear();
                g_modulesNext = 0;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(UI(Tr(L"No")).c_str())) s_removeAsked.clear();
        }
        ImGui::PopID();
    }
    {
        std::lock_guard<std::mutex> lock(g_mu);
        if (Switch("fallback", UI(Tr(L"Too slow twice in a row: use the next smaller model")), &g_nx.voiceFallback)) SaveNexusOptions();
    }
    Hint(UI(Tr(L"Too slow = recognizing took longer than you spoke (and over 2 s). Peak = the highest share of the "
               L"whole CPU the voice module used.")));
    Switch("live", UI(Tr(L"Live text while speaking (more CPU)")), &edit.voiceLive);
    RowLabel(UI(Tr(L"Longest recording")));
    RowSlider("##max", &edit.voiceMaxSec, 5, 60, "%d s");
    RowLabel(UI(Tr(L"CPU threads")));
    {
        std::vector<std::string> threads{UI(Tr(L"Automatic"))};
        for (int i = 1; i <= 8; ++i) threads.push_back(std::to_string(i));
        int ti = std::clamp(edit.voiceThreads, 0, 8);
        if (RowCombo("##thr", threads, &ti)) edit.voiceThreads = ti;
    }
    RowHint(UI(Tr(L"Automatic: the least busy cores – the ones GW2 does not use.")));
    // Where it all lies.
    auto folder = [&](const std::wstring& label, const std::wstring& path) {
        RowLabel(UI(label));
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
    Hint(UI(Tr(L"Runs on this PC – no sound leaves it, nothing is recorded to disk.")));
    EndBox();

    BeginBox(UI(Tr(L"Microphone")));
    RowLabel(UI(Tr(L"Starting")));
    int mode = edit.voiceActivation ? 2 : edit.voiceToggle ? 1 : 0;
    if (RowCombo("##mode", {UI(Tr(L"Hold the key while you speak")), UI(Tr(L"Press once – a pause ends it")),
                            UI(Tr(L"Voice activation – starts when you speak"))}, &mode)) {
        edit.voiceToggle = mode == 1;
        edit.voiceActivation = mode == 2;
    }
    Switch("handsfree", UI(Tr(L"Hands free (voice only)")), &edit.voiceAutoSend, voiceModuleThere ? nullptr : needsVoice.c_str(),
           voiceModuleThere);
    Hint(UI(Tr(L"Sends what you say by itself once you stop speaking – with push-to-talk and voice activation alike. Typed text always waits for your Enter.")));
    // The push-to-talk key: ours (GW2 does not get it). "Set key ..." takes the next key you press.
    {
        std::lock_guard<std::mutex> lock(g_mu);
        if (!g_capturedKey.empty()) {
            if (g_capturedKey != L"-") edit.voiceGlobalKey = g_capturedKey;
            g_capturedKey.clear();
        }
    }
    RowLabel(UI(Tr(L"Push-to-talk key")));
    const std::string keyDisplay = g_captureKey ? UI(Tr(L"press a key … (Esc: cancel)"))
                                                : UI(edit.voiceGlobalKey.empty() ? Tr(L"none") : edit.voiceGlobalKey);
    ImGui::TextColored(kSendOrange, "[ %s ]", keyDisplay.c_str());
    ImGui::SameLine();
    if (ImGui::Button(UI(Tr(L"Set key…")).c_str())) g_captureKey = true;
    ImGui::SameLine();
    if (ImGui::Button(UI(Tr(L"None")).c_str())) edit.voiceGlobalKey.clear();
    RowHint(UI(Tr(L"Works while GW2 is in front; GW2 does not get this key. The Nexus keybind (Keybinds > GW2 Keyboard) works too.")));
    RowLabel(UI(Tr(L"Microphone")));
    {
        std::vector<std::string> names{UI(Tr(L"Windows default"))};
        int mi = 0;
        for (size_t i = 0; i < s_mics.size(); ++i) {
            names.push_back(U8(s_mics[i].name));
            if (s_mics[i].id == edit.voiceDevice) mi = static_cast<int>(i) + 1;
        }
        if (RowCombo("##dev", names, &mi)) edit.voiceDevice = mi == 0 ? std::wstring() : s_mics[static_cast<size_t>(mi - 1)].id;
    }
    RowLabel(UI(Tr(L"Microphone boost")));
    int boost = std::clamp(edit.voiceBoost, 0, 3);
    if (RowCombo("##boost", {UI(Tr(L"Off")), "+6 dB", "+12 dB", UI(Tr(L"Automatic"))}, &boost)) edit.voiceBoost = boost;
    RowLabel(UI(Tr(L"Pause to end")));
    RowSlider("##pause", &edit.voicePauseMs, 400, 3000, "%d ms");
    RowHint(UI(Tr(L"Right-click on the microphone: on until a pause.")));
    EndBox();

    BeginBox(UI(Tr(L"Voice activation")));
    if (!edit.voiceActivation) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
    Hint(UI(Tr(L"Voice activation is not exclusive: everything you say goes into the box, and Discord keeps hearing it.")));
    RowLabel(UI(Tr(L"Sensitivity")));
    RowSlider("##sens", &edit.voiceSensitivity, 0, 100, "%d");
    RowLabel(UI(Tr(L"Pre-roll")));
    RowSlider("##pre", &edit.voicePreRollMs, 0, 1000, "%d ms");
    RowLabel(UI(Tr(L"Speech to start")));
    RowSlider("##min", &edit.voiceMinMs, 100, 1000, "%d ms");
    Switch("vcpause", UI(Tr(L"Pause while a voice chat uses the microphone (Discord, TeamSpeak …)")), &edit.voiceChatPause);
    if (!edit.voiceActivation) ImGui::PopStyleVar();
    EndBox();
}

void RenderTranslatorPage(Config& edit) {
    BeginBox(UI(Tr(L"Translator")));
    RenderTranslatorRows(edit, true);
    EndBox();
    RenderLocalMtOptions(edit);  // the local translation module, NLLB-200, the quality, OPUS-MT – each in its box
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
        ImGui::Spacing();
        // From top to bottom, as they stand in the window; each one can go – in two columns.
        bool changed = false;
        int col = 0;
        auto part = [&](const char* id, const std::wstring& label, bool* v, const std::wstring& tip) {
            if (col++ % 2) ImGui::SameLine(fs * 14.0f);
            const std::string t = UI(tip);
            changed |= Switch(id, UI(label), v, tip.empty() ? nullptr : t.c_str());
        };
        part("header", Tr(L"Header"), &g_layout.header, Tr(L"Hidden: a right-click on the window opens the menu (tabs, minimise, options)."));
        part("chat", Tr(L"Chat"), &g_layout.chat, {});
        part("preview", Tr(L"Translation preview"), &g_layout.preview, {});
        part("words", Tr(L"Word bar"), &g_layout.words, {});
        part("footer", Tr(L"Footer"), &g_layout.footer, {});
        part("minbtn", Tr(L"Minimize button"), &g_layout.minButton,
             Tr(L"The small > in the footer (without the footer: left of the input). Hidden: three quick clicks on the window minimise it, or the header, or the right-click menu."));
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

void RenderTechnicalPage(Config& edit) {
    BeginBox(UI(Tr(L"Modules")));
    RenderChatModule(false);  // Events: Chat: loaded, install / remove – reading it follows in the details
    EndBox();
    BeginBox(UI(Tr(L"Debug & diagnostics")));
    if (ImGui::Button(UI(Tr(L"Copy diagnostic report to clipboard")).c_str())) {
        const std::string report = BuildDiagnosticReport();
        ImGui::SetClipboardText(report.c_str());
        SetStatus(Tr(L"Diagnostic report copied to clipboard"), 4000);
    }
    ImGui::SameLine();
    if (ImGui::Button(UI(g_showDebugWindow ? Tr(L"Close Debug Window") : Tr(L"Open Debug Window")).c_str()))
        g_showDebugWindow = !g_showDebugWindow;
    Hint(UI(Tr(L"Exports system status, engine telemetry, and performance metrics (no chat messages or player names are stored or exported).")));
    // Raw translation, for comparing what our logic adds: the visible chat is translated again in the other way.
    bool raw = g_rawTranslate.load();
    if (Switch("raw", UI(Tr(L"Raw translation (until restart)")), &raw)) {
        g_rawTranslate = raw;
        std::lock_guard<std::mutex> lock(g_mu);
        g_orchestrator.SetInterjectionSieve(!raw);
        TranslateRecentLines(true);
        SetStatus(raw ? Tr(L"Raw translation on: GW2 terms, names, sentence by sentence, the extra tries, the clean-up and the quality check are off")
                      : Tr(L"Raw translation off: our translation logic is on again"),
                  6000);
    }
    Hint(UI(Tr(L"For comparing: the translator gets each line exactly as written, its result is shown as it comes. Chat codes and links stay protected. Not saved.")));
    EndBox();
    RenderWordLists();
    BeginBox(UI(Tr(L"Status")));
    {
        std::lock_guard<std::mutex> lock(g_mu);
        static Steady s_fps;
        const int fps = static_cast<int>(std::lround(s_fps(g_fps)));
        const int hold = (fps > 0 && edit.send.autoTiming) ? std::clamp(1000 / std::max(fps, 10) + 10, 15, 1010) : edit.send.keyHoldMs;
        ImGui::Text("GW2Chatlator for Nexus %s", GCT_BUILD_DISPLAY_STR);
        ImGui::Text("%s", UI(TrF(L"Game: {1} fps → keys held {2} ms", {std::to_wstring(fps), std::to_wstring(hold)})).c_str());
        ImGui::Text("%s", UI(TrF(L"Translator: {1}", {g_translator ? g_translator->Name() : std::wstring(L"-")})).c_str());
        if (const std::wstring q = MyMemoryQuotaText(); !q.empty() && g_nx.translateChat) ImGui::Text("%s", UI(q).c_str());
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
    {  // Timings, the box's last rows under a line of their own: what our frame costs, how keys are sent – both folded
        const float fs = ImGui::GetFontSize();
        ImGui::Dummy(ImVec2(0.0f, fs * 0.3f));
        ImGui::TextDisabled("%s", UI(Tr(L"Timings")).c_str());
        const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        const float right = std::floor(ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x - fs * 0.75f);
        const float y = std::floor((a.y + b.y) * 0.5f);
        if (right > b.x + fs * 0.4f)
            ImGui::GetWindowDrawList()->AddLine(ImVec2(b.x + fs * 0.4f, y), ImVec2(right, y), ImGui::GetColorU32(ImGuiCol_Separator));
    }
    if (ImGui::TreeNode(UI(Tr(L"Frame timings") + L"###frametimings").c_str())) {
        RenderFrameTimings();
        ImGui::TreePop();
    }
    // The send timing: automatic fits nearly everyone, so the manual values stay folded up.
    if (ImGui::TreeNode(UI(Tr(L"Send timing (advanced)") + L"###sendtiming").c_str())) {
        Switch("autotiming", UI(Tr(L"Automatic timing (FPS-adapted)")), &edit.send.autoTiming);
        Hint(UI(Tr(L"Calculates key hold and step delays dynamically based on the game's current framerate.")));
        // The values by hand are for a diagnosis only (a slow-motion send): greyed out while the automatic is on, so a
        // stray edit can no longer switch it off unnoticed.
        const bool manual = !edit.send.autoTiming;
        if (!manual) {
            ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.45f);
        }
        ImGui::SetNextItemWidth(120);
        ImGui::InputInt(UI(Tr(L"Key hold when sending (ms)") + L"##hold").c_str(), &edit.send.keyHoldMs);
        ImGui::SetNextItemWidth(120);
        ImGui::InputInt(UI(Tr(L"Pause between keys (ms)") + L"##step").c_str(), &edit.send.stepDelayMs);
        if (!manual) {
            ImGui::PopStyleVar();
            ImGui::PopItemFlag();
        }
        edit.send.keyHoldMs = std::clamp(edit.send.keyHoldMs, 10, 500);
        edit.send.stepDelayMs = std::clamp(edit.send.stepDelayMs, 10, 1000);
        std::wstring trace;  // each key and whether GW2's chat line was open after it, with the ms since the start
        {
            std::lock_guard<std::mutex> lock(g_mu);
            trace = g_lastSendTrace;
        }
        if (!trace.empty()) {
            ImGui::TextDisabled("%s", UI(Tr(L"The last send, step by step (chat line open/shut, ms):")).c_str());
            ImGui::TextWrapped("%s", UI(trace).c_str());
        }
        ImGui::TreePop();
    }
    EndBox();
    ImGui::Spacing();
    RenderRemoveEverything();  // once, at the very bottom
}

std::wstring PageName(int page) {
    switch (page) {
        case kPageStart: return Tr(L"Start");
        case kPageChat: return Tr(L"Chat");
        case kPageTyping: return Tr(L"Typing help");
        case kPageVoice: return Tr(L"Voice");
        case kPageTranslator: return Tr(L"Translator");
        case kPageWindow: return Tr(L"Window");
        default: return Tr(L"Technical");
    }
}

std::wstring PageSub(int page) {
    switch (page) {
        case kPageStart: return Tr(L"Three settings – then it runs.");
        case kPageChat: return Tr(L"How lines appear and where Enter goes.");
        case kPageTyping: return Tr(L"Suggestions and spelling while you type.");
        case kPageVoice: return Tr(L"Voice module and microphone – runs on this PC.");
        case kPageTranslator: return Tr(L"Who translates – on this PC or online.");
        case kPageWindow: return Tr(L"How the chat window looks and what it shows.");
        default: return Tr(L"Modules, diagnostics, lists – for the curious.");
    }
}
}  // namespace

// Our options window: the pages in the side bar, the one chosen beside it. Everything applies at once.
void RenderOptionsContent() {
    static Config edit, opened;  // `opened`: the settings as this window took them; what differs in `edit` is applied
    static bool loaded = false;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        if (!loaded || !OptionsEdited(edit, opened)) edit = opened = g_cfg;  // what the footer and the menus changed too
        loaded = true;
    }
    const float fs = ImGui::GetFontSize();
    s_labelW = std::clamp(s_labelNeed + fs * 0.8f, fs * 6.0f, fs * 14.0f);
    s_labelNeed = 0.0f;
    if (s_delayedTab >= 0) {
        g_optionsTab = s_delayedTab;
        s_delayedTab = -1;
    }
    static int s_shown = -1;
    if (g_optionsTab >= 0 && g_optionsTab < kPageCount) s_page = g_optionsTab;
    g_optionsTab = -1;
    const bool entered = s_page != s_shown;
    s_shown = s_page;

    // The side bar: a symbol and a name per page, as wide as the longest name.
    std::string names[kPageCount];
    float longest = 0.0f;
    for (int i = 0; i < kPageCount; ++i) {
        names[i] = UI(PageName(i));
        longest = std::max(longest, ImGui::CalcTextSize(names[i].c_str()).x);
    }
    const float sideW = std::ceil(longest + fs * 3.8f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 70));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(fs * 0.35f, fs * 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(fs * 0.3f, fs * 0.15f));
    if (ImGui::BeginChild("##side", ImVec2(sideW, 0.0f), false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysUseWindowPadding)) {
        for (int i = 0; i < kPageCount; ++i)
            if (NavItem(i, names[i], s_page == i)) s_page = i;
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(fs * 1.0f, fs * 0.6f));
    if (ImGui::BeginChild("##page", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_AlwaysUseWindowPadding)) {
        PageHead(names[s_page], UI(PageSub(s_page)));
        switch (s_page) {
            case kPageStart: RenderStartPage(edit); break;
            case kPageChat: RenderChatPage(edit); break;
            case kPageTyping: RenderTypingPage(edit); break;
            case kPageVoice: RenderVoicePage(edit, entered); break;
            case kPageTranslator: RenderTranslatorPage(edit); break;
            case kPageWindow: RenderWindowPage(edit); break;
            default: RenderTechnicalPage(edit); break;
        }
        while (!s_boxes.empty()) EndBox();  // never a frame left open
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    // Applied once you let go: not while a slider is held or a field is being written in.
    const ImGuiContext& g = *GImGui;
    const bool editing = g.ActiveId != 0 && g.ActiveIdWindow && g.ActiveIdWindow->RootWindow == ImGui::GetCurrentWindow()->RootWindow;
    if (!editing && OptionsEdited(edit, opened)) ApplyOptions(edit, opened);
}
#undef KB_OPTIONS
#undef KB_VOICE_OPTIONS

namespace {
// The report's text: ss << "a" << 1 << Fixed{3} << 2.5 – without the weight of the C++ streams in the DLL.
struct Fixed {
    int digits;
};
class ReportText {
public:
    ReportText& operator<<(const char* s) {
        text_ += s;
        return *this;
    }
    ReportText& operator<<(const std::string& s) {
        text_ += s;
        return *this;
    }
    template <class T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
    ReportText& operator<<(T v) {
        text_ += std::to_string(v);
        return *this;
    }
    ReportText& operator<<(Fixed f) {  // for the numbers after it, like std::fixed with std::setprecision
        digits_ = f.digits;
        return *this;
    }
    ReportText& operator<<(double v) {
        char b[64];
        std::snprintf(b, sizeof(b), "%.*f", digits_, v);
        text_ += b;
        return *this;
    }
    const std::string& str() const { return text_; }

private:
    std::string text_;
    int digits_ = 3;
};
}  // namespace

std::string BuildDiagnosticReport() {
    ReportText ss;
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
    ss << "- Game Framerate: " << g_fps.load() << " FPS (ImGui Framerate: " << Fixed{1} << io.Framerate << ")\n";
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
    ss << "- Total Add-on Frame Cost: " << Fixed{3} << fc.totalMs << " ms\n";
    ss << "- Subsystem Cost Breakdown:\n";
    ss << "  * Window Layout & Frame:   " << fc.ms[kCostWindow] << " ms\n";
    ss << "  * Chat Lines Rendering:    " << fc.ms[kCostChat] << " ms\n";
    ss << "  * Input & Text Box:        " << fc.ms[kCostInput] << " ms\n";
    ss << "  * Header & Navigation:     " << fc.ms[kCostHeader] << " ms\n";
    ss << "  * Footer & Language Chips: " << fc.ms[kCostFooter] << " ms\n";
    ss << "  * Options & Diagnostics:   " << fc.ms[kCostOptions] << " ms\n";
    ss << "- Line Ingest Overhead:      " << Fixed{1} << fc.lineUs << " µs per chat line ("
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
