// kb_options_start.cpp — the Start page and the modules list (what is ready, with the buttons that load what is missing).

#include "kb_shared.hpp"
#include "kb_options_parts.hpp"
#include "win/local_mt_packages.hpp"
#include "voice_module.hpp"

namespace kb {

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

// Events: Chat – loaded, installed, missing; install (g_mu NOT held). Options > Technical, the dashboard and
// the guide.
void RenderChatModule() {
    ImGui::PushID("modules");
    const bool chatLive = g_chatEventsLive || g_chatEventsPresent;
    StatusRow(UI(Tr(L"Chat module")), chatLive ? 2 : g_chatEventsInstalling || g_chatEventsFile ? 1 : 0,
              UI(chatLive ? Tr(L"ready – Events: Chat is loaded")
                 : g_chatEventsInstalling ? Tr(L"loading Events: Chat …")
                 : g_chatEventsFile && g_chatNotTakenIn ? Tr(L"in the add-ons folder – Nexus loads a new add-on only after your OK: Nexus → Addons → Events: Chat → Load (once; not listed yet: after the next game start)")
                 : g_chatEventsFile ? Tr(L"installed – Nexus loads it (else: Nexus → Addons → Events: Chat → Load)")
                                    : Tr(L"Events: Chat is missing")));
    if (!chatLive && !g_chatEventsFile && !g_chatEventsInstalling) {
        ImGui::SameLine();
        if (ImGui::SmallButton(UI(Tr(L"Install Events: Chat")).c_str())) InstallChatEventsNow();
    }  // no remove button: Events: Chat is removed in Nexus only (deleting its file made Nexus crash at every start)
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
    RenderChatModule();
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
            ImGui::TextUnformatted(UI(Tr(L"This deletes the folder GW2Keyboard in your addons folder: settings, learned words, my words, the voice and local translation modules and their models. It cannot be undone.")).c_str());
            // Never Events: Chat: deleting its file left Nexus crashing at every start. Nexus removes it cleanly.
            Hint(UI(Tr(L"Events: Chat stays: it is its own add-on, other add-ons may use it. To remove it: Nexus → Addons → Events: Chat → Uninstall.")));
            if (g_localMtBusy) {
                ImGui::TextDisabled("%s", UI(Tr(L"Wait for the local translation operation to finish.")).c_str());
            } else if (ImGui::Button(UI(Tr(L"Remove everything")).c_str())) {
                ImGui::CloseCurrentPopup();
                RemoveEverythingNow();
            }
            ImGui::SameLine();
            if (ImGui::Button(UI(Tr(L"Cancel")).c_str()) || ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Escape)))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }
    ImGui::PopID();
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

// ---- The pages

// The offline translator and, for NVIDIA cards, its module for the card: offered here while missing – one click each, the
// size said first. Once there, the Translation box above shows them, the card's switch too.
void RenderOfflineOffer(Config& edit) {
    const LocalMtSummary local = LocalMtNow(edit.dataDir);
    const bool nllb = local.module && local.nllb;
    const bool cardMissing = NvidiaDriverPresent() && CardCanRunHelper() && !CardModuleComplete();
    if (nllb && !cardMissing) return;
    BeginBox(UI(Tr(L"On this PC (offline)")));
    if (g_localMtBusy) {
        ImGui::TextDisabled("%s", UI(Tr(L"Installing…")).c_str());
    } else if (!nllb) {
        Hint(UI(Tr(L"Local Translation installs the local neural machine translation engine and NLLB-200 multilingual model.")));
        ImGui::TextColored(ImVec4(0.51f, 0.76f, 0.63f, 1.f), "%s", UI(Tr(L"Download size: about 600 MB · translates offline on this PC")).c_str());
        if (ImGui::Button(UI(Tr(L"Install NLLB-200 (local)")).c_str())) InstallLocalMtQuickSetup(edit.dataDir);
        if (cardMissing) Hint(UI(Tr(L"For NVIDIA cards from GTX 16 and RTX 20 upwards: the translator runs on the card and leaves the CPU to the game. Holds NVIDIA's libraries, so it is a large download.")));
    } else {
        Hint(UI(Tr(L"For NVIDIA cards from GTX 16 and RTX 20 upwards: the translator runs on the card and leaves the CPU to the game. Holds NVIDIA's libraries, so it is a large download.")));
        // The card goes on once its module is there – not now: that would restart the CPU's helper for nothing.
        if (ImGui::Button(UI(Tr(L"Download graphics card module")).c_str())) InstallCardModule(edit.dataDir, true);
    }
    EndBox();
}

// A small picture of the window, from the top: header, chat, preview, word bar, input, footer. What a layout shows is
// filled, the rest only outlined – the window's parts are seen at once.
void LayoutPicture(ImDrawList* dl, ImVec2 p, float w, float h, const bool parts[6], ImU32 on, ImU32 off) {
    static const float kRows[6] = {1.2f, 4.6f, 1.2f, 1.0f, 1.4f, 1.0f};
    const float u = h / 12.0f;
    float y = p.y;
    for (int i = 0; i < 6; ++i) {
        const ImVec2 a(p.x, y), b(p.x + w, y + kRows[i] * u);
        if (parts[i]) dl->AddRectFilled(a, b, on, u * 0.3f);
        else dl->AddRect(a, b, off, u * 0.3f);
        y += kRows[i] * u + u * 0.3f;
    }
}

bool LayoutTile(const std::string& name, const bool parts[6], bool chosen, float tileW) {
    const float fs = ImGui::GetFontSize();
    const ImVec2 size(tileW, fs * 6.4f), p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton("##tile", size);
    const bool hovered = ImGui::IsItemHovered();
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 accent = ImGui::GetColorU32(UiAccent());
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), IM_COL32(255, 255, 255, hovered ? 16 : 8), 4.0f);
    dl->AddRect(p, ImVec2(p.x + size.x, p.y + size.y), chosen ? accent : ImGui::GetColorU32(ImGuiCol_Separator), 4.0f, 0, chosen ? 2.0f : 1.0f);
    const float pad = fs * 0.7f;
    LayoutPicture(dl, ImVec2(p.x + pad * 1.6f, p.y + pad), size.x - pad * 3.2f, size.y - pad - fs * 1.7f, parts,
                  chosen ? accent : ImGui::GetColorU32(ImGuiCol_TextDisabled), ImGui::GetColorU32(ImGuiCol_Separator));
    const float nameW = ImGui::CalcTextSize(name.c_str()).x;
    dl->AddText(ImVec2(p.x + (size.x - nameW) * 0.5f, p.y + size.y - fs * 1.35f), ImGui::GetColorU32(chosen ? ImGuiCol_Text : ImGuiCol_TextDisabled),
                name.c_str());
    return pressed;
}

// The window's layouts as pictures: all its parts, the compact view, the mini mode, the subtitles – a click takes one. Each part on its
// own is on the Window page (a part changed there: none of these is chosen, it is your own layout).
void RenderLayoutTiles() {
    static const bool kParts[4][6] = {{true, true, true, true, true, true}, {true, true, false, false, true, true},
                                      {false, false, false, false, true, false}, {false, true, false, false, false, false}};
    int chosen;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        const Layout& l = Lay();
        chosen = l.header && l.chat && l.preview && l.words && l.footer ? 0 : ProfileOn(0) ? 1 : ProfileOn(1) ? 2 : ProfileOn(2) ? 3 : -1;
    }
    BeginBox(UI(Tr(L"Window layout")));
    const std::string names[4] = {UI(Tr(L"All parts")), UI(Tr(L"Compact view")), UI(Tr(L"Mini mode")), UI(Tr(L"Subtitle mode"))};
    const float fs = ImGui::GetFontSize();
    float tileW = fs * 6.0f;
    for (const std::string& n : names) tileW = std::max(tileW, ImGui::CalcTextSize(n.c_str()).x + fs * 1.4f);
    for (int i = 0; i < 4; ++i) {
        if (i && ImGui::GetContentRegionAvail().x > 0.0f) {
            ImGui::SameLine();
            if (ImGui::GetContentRegionAvail().x < tileW) ImGui::NewLine();  // a narrow window: the next tile below
        }
        ImGui::PushID(i);
        const bool picked = LayoutTile(names[i], kParts[i], chosen == i, tileW);
        if (i == 3 && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", UI(Tr(L"Only the chat's lines, see-through over the game. The input shows while you write and fades after sending.")).c_str());
        if (picked && chosen != i) {
            std::lock_guard<std::mutex> lock(g_mu);
            if (i == 0) {
                Layout& l = Lay();
                l.header = l.chat = l.preview = l.words = l.footer = l.minButton = true;
                l.subtitles = false;
                SaveLayout();
            } else {
                SetProfile(i - 1, true);
            }
        }
        ImGui::PopID();
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", UI(Tr(L"Each part on its own:")).c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton(UI(Tr(L"Window") + L" →").c_str())) SwitchOptionsTab(kPageWindow);
    EndBox();
}

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
        RenderChatModule();
        if (!g_chatEventsFile && !g_chatEventsInstalling)
            Hint(UI(Tr(L"Without Events: Chat, GW2Chatlator cannot read the chat – writing and the typing help work anyway.")));
        ImGui::Spacing();
    }
    const LocalMtSummary local = LocalMtNow(edit.dataDir);
    const bool isLocal = IsLocalEngine(edit.engine);
    const bool localReady = local.module && LocalModelThere(local, edit.engine);
    const std::string title = UI(chatOn ? Tr(L"Chat translation is on") : Tr(L"Chat translation is off"));
    const std::string sub = UI(chatOn ? TrF(L"Foreign lines appear in {1}", {NativeName(ReadingLangNow())})
                                      : Tr(L"A click on a line translates just that one"));
    if (Hero(chatOn, title, sub, UI(Tr(L"Chat module")), chatThere, UI(running.empty() ? Tr(EngineLabel(edit.engine)) : running),
             !isLocal || localReady)) {
        std::lock_guard<std::mutex> lock(g_mu);
        SetChatTranslation(!g_nx.translateChat);
    }
    // The local translator first, while it is missing: it keeps every line on this PC – before MyMemory, which works online.
    RenderOfflineOffer(edit);
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
    RenderLayoutTiles();
    BeginBox(UI(Tr(L"Window & keys")));
    RenderUiLanguageRow(edit);
    // No key is set by us: the actions you can bind, in Nexus → Keybinds → GW2Chatlator.
    std::wstring binds = Tr(L"Window") + L" · " + Tr(L"Input") + L" · " + Tr(L"Translation");
    if (edit.voiceEnabled) binds += L" · " + Tr(L"Voice");
    ImGui::TextDisabled("%s", UI(binds).c_str());
    Hint(UI(Tr(L"Keybinds: Nexus → Keybinds → GW2Chatlator.")));
    EndBox();
}

}  // namespace kb
