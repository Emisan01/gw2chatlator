// kb_options_start.cpp — the Start page and the modules list (what is ready, with the buttons that load what is missing).

#include "kb_shared.hpp"
#include "kb_options_parts.hpp"
#include "core/json.hpp"
#include "win/files.hpp"
#include "win/local_mt_packages.hpp"
#include "voice_module.hpp"

namespace kb {

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

// Events: Chat in detail – loaded, installed, missing; install, or Load in Nexus (g_mu NOT held). Options > Technical;
// Start and the empty chat show it in "What is still missing".
void RenderChatModule() {
    ImGui::PushID("modules");
    const bool chatLive = g_chatEventsLive || g_chatEventsPresent;
    StatusRow(UI(Tr(L"Chat module")), chatLive ? 2 : g_chatEventsInstalling || g_chatEventsFile ? 1 : 0,
              UI(chatLive ? Tr(L"ready – Events: Chat is loaded")
                 : g_chatEventsInstalling ? Tr(L"loading Events: Chat …")
                 : g_chatEventsFile && g_chatInstalledAt ? Tr(L"installed – Nexus is taking it in …")
                 // Nexus loads an add-on it meets for the first time only when its own list already marks it as loaded; a
                 // new one waits for the user's Load there – a restart alone does not load it (its loader, 2026-11-08).
                 : g_chatEventsFile ? Tr(L"in the add-ons folder, not loaded – Nexus loads a new add-on only after your OK: Events: Chat → Load (once)")
                                    : Tr(L"Events: Chat is missing")));
    if (!chatLive && !g_chatEventsFile && !g_chatEventsInstalling) {
        ImGui::SameLine();
        if (ImGui::SmallButton(UI(Tr(L"Install Events: Chat")).c_str())) InstallChatEventsNow();
    } else if (!chatLive && g_chatEventsFile && !g_chatEventsInstalling && !g_chatInstalledAt) {
        ImGui::SameLine();
        if (ImGui::SmallButton(UI(Tr(L"Load in Nexus")).c_str())) OpenNexusAddons();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", UI(Tr(L"Nexus' own Addons window – there: Events: Chat → Load. Not in its list yet? After the next game start it is.")).c_str());
    }  // no remove button: Events: Chat is removed in Nexus only (a file deleted behind Nexus' back stays in its list)
    ImGui::PopID();
}

// The voice module and its model in detail – load, unload, remove (g_mu NOT held). Options > Voice; Start and the empty
// chat show it in "What is still missing". Returns whether voice input is on.
bool RenderVoiceModule() {
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
        if (m.voiceModule && !g_voiceModuleInstalling && RemoveButton("voicemodule")) RemoveVoiceModuleNow();
        const int dl = g_downloadPct;
        StatusRow(UI(Tr(L"Voice model")), m.model ? 2 : dl >= 0 ? 1 : 0,
                  dl >= 0 ? UI(TrF(L"downloading … {1} %", {std::to_wstring(dl)}))
                          : m.modelName + (m.model ? "" : "  " + UI(Tr(L"missing"))));
        if (m.model && dl < 0) {
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

// What is still missing: one row per piece, one button for its next step, the same words on Start and in the empty
// chat (Emi, 2026-11-08). Missing / loading … / ready – and a ready piece has no row; sizes, versions and removing stay
// on their tabs. `edit`: the Options page's copy of the settings (null in the chat), which must say what the switch now
// says, else applying the page would take it back. Render thread, g_mu NOT held. Returns whether a row was shown.
static bool RenderMissing(Config* edit) {
    std::wstring dataDir, voiceModel;  // only what the rows need – this runs every frame the list is in view
    bool voiceEnabled;
    Engine engine;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        dataDir = g_cfg.dataDir;
        voiceModel = g_cfg.voiceModel;
        voiceEnabled = g_cfg.voiceEnabled;
        engine = edit ? edit->engine : g_cfg.engine;  // on Start: the translator as the page shows it
    }
    ImGui::PushID("missing");
    bool any = false;
    // Events: Chat – without it nothing of the chat is read. Nexus loads an add-on new to it only after the user's Load.
    if (!(g_chatEventsLive || g_chatEventsPresent)) {
        any = true;
        const std::string name = UI(Tr(L"Chat module"));
        if (g_chatEventsInstalling) {
            StatusRow(name, 1, UI(Tr(L"loading …")));
        } else if (g_chatEventsFile && g_chatInstalledAt) {
            StatusRow(name, 1, UI(Tr(L"installed – Nexus is taking it in …")));
        } else if (g_chatEventsFile) {
            StatusRow(name, 1, UI(Tr(L"not loaded – Nexus waits for your OK")));
            ImGui::SameLine();
            if (ImGui::SmallButton(UI(Tr(L"Load in Nexus") + L"##chat").c_str())) OpenNexusAddons();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", UI(Tr(L"Nexus' own Addons window – there: Events: Chat → Load. Not in its list yet? After the next game start it is.")).c_str());
        } else {
            StatusRow(name, 0, UI(Tr(L"missing – without it no chat is read")));
            ImGui::SameLine();
            if (ImGui::SmallButton(UI(Tr(L"Install") + L"##chat").c_str())) InstallChatEventsNow();
        }
    }
    // The offline translator (NLLB-200), then – once it is there – its module for NVIDIA cards
    const LocalMtSummary local = LocalMtNow(dataDir);
    if (g_localMtBusy) {
        any = true;
        const int pct = g_localMtPct;
        StatusRow(UI(Tr(L"Local translator")), 1,
                  UI(pct >= 0 ? TrF(L"loading … {1} %", {std::to_wstring(pct)}) : Tr(L"loading …")));
    } else if (!(local.module && local.nllb)) {
        any = true;
        StatusRow(UI(Tr(L"Local translator")), 0, UI(Tr(L"optional – nothing leaves your PC (about 600 MB)")));
        ImGui::SameLine();
        if (ImGui::SmallButton(UI(Tr(L"Install") + L"##nllb").c_str())) InstallLocalMtQuickSetup(dataDir);
    } else if (NvidiaDriverPresent() && CardCanRunHelper() && !CardModuleComplete()) {
        any = true;
        StatusRow(UI(Tr(L"Graphics card")), 0, UI(Tr(L"optional – the translator on your NVIDIA card, the CPU stays with the game")));
        ImGui::SameLine();
        // the card goes on once its module is there – not now: that would restart the CPU's helper for nothing
        if (ImGui::SmallButton(UI(Tr(L"Switch on") + L"##card").c_str())) InstallCardModule(dataDir, true);
    }
    // The translator you chose runs on this PC but its model is not there (OPUS-MT's pair, M2M100) – the switch's tile
    // shows it orange, so it stands here too; its download is on the Translator tab. NLLB has its row above.
    const bool nllbRow = !g_localMtBusy && !(local.module && local.nllb);
    if (!g_localMtBusy && IsLocalEngine(engine) && !LocalModelThere(local, engine) && !(engine == Engine::Nllb && nllbRow)) {
        any = true;
        StatusRow(UI(Tr(L"Translator")), 0, UI(TrF(L"the model for {1} is missing", {Tr(EngineLabel(engine))})));
        ImGui::SameLine();
        if (ImGui::SmallButton(UI(Tr(L"Open") + L"##model").c_str())) {
            if (edit) {
                SwitchOptionsTab(kPageTranslator);
            } else {
                std::lock_guard<std::mutex> lock(g_mu);
                g_showOptions = true;
                g_optionsTab = kPageTranslator;
            }
        }
    }
    // Voice input: not there – one click loads the voice module and its model. Switched off by you with both there: no row.
    const ModuleState& m = Modules();
    if (g_voiceModuleInstalling || g_downloadPct >= 0) {
        any = true;
        const int pct = g_downloadPct >= 0 ? g_downloadPct.load() : g_voiceModulePct.load();
        StatusRow(UI(Tr(L"Voice input")), 1, UI(TrF(L"loading … {1} %", {std::to_wstring(std::max(pct, 0))})));
    } else if (!m.voiceModule || (voiceEnabled && !m.model)) {
        any = true;
        static int s_mb = 0;  // the module (about 7 MB) and the chosen or recommended model, looked up every 10 s at most
        static ULONGLONG s_mbNext = 0;
        if (GetTickCount64() >= s_mbNext) {
            s_mbNext = GetTickCount64() + 10000;
            const VoiceCatalog catalog = LoadVoiceCatalog(dataDir);
            const VoiceModelInfo* vm = catalog.Find(U8(voiceModel));
            if (!vm) vm = catalog.Recommended();
            s_mb = vm ? static_cast<int>(vm->bytes / (1024 * 1024)) + 7 : 0;
        }
        StatusRow(UI(Tr(L"Voice input")), 0,
                  UI(voiceEnabled ? Tr(L"missing") : TrF(L"off, optional (about {1} MB)", {std::to_wstring(s_mb)})));
        ImGui::SameLine();
        if (ImGui::SmallButton(UI((voiceEnabled ? Tr(L"Install") : Tr(L"Switch on")) + L"##voice").c_str())) {
            InstallVoiceQuickSetup();
            if (edit) edit->voiceEnabled = true;
        }
    }
    ImGui::PopID();
    return any;
}

// The empty chat: what is still missing, and where translation is switched on (g_mu NOT held).
void RenderModules() {
    RenderMissing(nullptr);
    ImGui::PushID("modules");
    {
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
            // Never Events: Chat: a file deleted behind Nexus' back stays in its list. Nexus removes it cleanly.
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

// The window's layouts as pictures, from everything to little: all its parts, the compact view, the subtitles, the mini
// mode – a click takes one. Each part on its
// own is on the Window page (a part changed there: none of these is chosen, it is your own layout).
void RenderLayoutTiles() {
    static const bool kParts[4][6] = {{true, true, true, true, true, true}, {true, true, false, false, true, true},
                                      {false, true, false, false, false, false}, {false, false, false, false, true, false}};
    static const int kProfile[4] = {-1, 0, 2, 1};  // the tile's profile (SetProfile): all parts, compact, subtitles, mini
    int chosen;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        const Layout& l = Lay();
        chosen = ProfileOn(2) ? 2 : l.header && l.chat && l.preview && l.words && l.footer ? 0 : ProfileOn(0) ? 1 : ProfileOn(1) ? 3 : -1;
    }
    BeginBox(UI(Tr(L"Window layout")));
    const std::string names[4] = {UI(Tr(L"All parts")), UI(Tr(L"Compact view")), UI(Tr(L"Subtitle mode")), UI(Tr(L"Mini mode"))};
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
        if (i == 2 && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", UI(Tr(L"Only the chat's lines, see-through over the game – no info texts. A left click in the text opens the input, as Enter does; it fades after sending. Right-click: the menu.")).c_str());
        if (i == 3 && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", UI(Tr(L"Nothing but the input with its help. Three quick clicks on it minimise it; right-click in it: Menu … brings the rest back.")).c_str());
        if (picked && chosen != i) {
            std::lock_guard<std::mutex> lock(g_mu);
            if (i == 0) {
                Layout& l = Lay();
                l.header = l.chat = l.preview = l.words = l.footer = l.minButton = true;
                l.subtitles = false;
                SaveLayout();
            } else {
                SetProfile(kProfile[i], true);
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

// The key Nexus keeps for one of our actions, read from its own InputBinds.json – only read, never written: the keys are
// yours to set in Nexus. Empty when none is set. Looked at every 2 s at most while Start is open.
static std::string NexusKeyOf(const char* identifier) {
    static std::vector<std::pair<std::string, std::string>> s_keys;
    static ULONGLONG s_next = 0;
    if (GetTickCount64() >= s_next) {
        s_next = GetTickCount64() + 2000;
        s_keys.clear();
        std::string raw;
        JsonValue binds;
        if (ReadFileBytes(AddonsDir() + L"Nexus\\InputBinds.json", raw) && ParseJson(raw, binds) && binds.IsArray()) {
            for (const JsonValue& b : binds.arr) {
                const std::string id = b.GetString("Identifier");
                const JsonValue* type = b.Get("Type");
                const JsonValue* code = b.Get("Code");
                if (id.rfind("KB_GW2KEYBOARD_", 0) != 0 || !type || !code || type->n != 1 || code->n <= 0 || code->n > 0xFFFF)
                    continue;  // 1: a key; 0: none set
                const unsigned scan = static_cast<unsigned>(code->n);
                wchar_t name[64] = {};
                const LONG lp = static_cast<LONG>(((scan & 0xFFu) << 16) | ((scan & 0xFF00u) ? (1u << 24) : 0u));
                if (GetKeyNameTextW(lp, name, 64) <= 0) continue;
                std::wstring key = (b.GetBool("Ctrl") ? L"Ctrl+" : L"") + std::wstring(b.GetBool("Alt") ? L"Alt+" : L"") +
                                   (b.GetBool("Shift") ? L"Shift+" : L"") + name;
                s_keys.emplace_back(id, ToUtf8(key));
            }
        }
    }
    for (const auto& [id, key] : s_keys)
        if (id == identifier) return key;
    return std::string();
}

// One action's row: its name, the key you gave it in Nexus (or "–"), and the way to Nexus' Keybinds.
static void KeyRow(const std::wstring& action, const char* identifier) {
    ImGui::PushID(identifier);
    RowLabel(UI(action));
    const std::string key = NexusKeyOf(identifier);
    ImGui::AlignTextToFramePadding();
    if (key.empty()) ImGui::TextDisabled("%s", UI(L"–").c_str());
    else ImGui::TextColored(UiAccent(), "%s", key.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton(UI(Tr(L"Set in Nexus")).c_str())) OpenNexusMenu();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", UI(Tr(L"Opens Nexus' menu – there: Keybinds → GW2Chatlator. We set no key; the one you choose stays yours.")).c_str());
    ImGui::PopID();
}

void RenderStartPage(Config& edit) {
    bool chatOn;
    std::wstring running;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        chatOn = g_nx.translateChat;
        running = g_translator ? g_translator->Name() : std::wstring();
    }
    // What is still missing comes first: one row per piece with the one button for its next step – or "All ready".
    // An orange state in the switch's tile scrolls back up here.
    static bool s_toMissing = false;
    if (s_toMissing) {
        ImGui::SetScrollHereY(0.0f);
        s_toMissing = false;
    }
    const bool chatThere = g_chatEventsLive || g_chatEventsPresent;
    BeginBox(UI(Tr(L"What is still missing")));
    if (!RenderMissing(&edit)) StatusRow(UI(Tr(L"All ready")), 2, "");
    EndBox();
    const LocalMtSummary local = LocalMtNow(edit.dataDir);
    const bool isLocal = IsLocalEngine(edit.engine);
    const bool localReady = local.module && LocalModelThere(local, edit.engine);
    const std::string title = UI(chatOn ? Tr(L"Chat translation is on") : Tr(L"Chat translation is off"));
    const std::string sub = UI(chatOn ? TrF(L"Foreign lines appear in {1}", {NativeName(ReadingLangNow())})
                                      : Tr(L"A click on a line translates just that one"));
    bool toMissing = false;
    if (Hero(chatOn, title, sub, UI(Tr(L"Chat module")), chatThere, UI(running.empty() ? Tr(EngineLabel(edit.engine)) : running),
             !isLocal || localReady, &toMissing)) {
        std::lock_guard<std::mutex> lock(g_mu);
        SetChatTranslation(!g_nx.translateChat);
    }
    if (toMissing) s_toMissing = true;
    // The three choices; what each means shows when the pointer is on it (the page stays short).
    auto tip = [](const std::wstring& t) {
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", UI(t).c_str());
    };
    // From the top (Emi, 2026-11-09): who translates, the language you read the chat in and the one you write in (one
    // setting at first – the writing follows the reading until you choose it on its own, also on the Chat page), what
    // your messages are translated to, and what stays as it is.
    BeginBox(UI(Tr(L"Translation")));
    RenderTranslatorRows(edit, false);
    const std::wstring readBefore = edit.readLang;
    RenderReadRow(edit);
    tip(Tr(L"Foreign lines appear in this language."));
    if (edit.readLang != readBefore && edit.writeIn == readBefore) edit.writeIn = edit.readLang;  // still one setting: it follows
    {
        const std::string writeLabel = UI(Tr(L"I write in"));
        const float comboW = ImGui::GetFontSize() * 8.0f;
        ImGui::SameLine();
        if (ImGui::GetContentRegionAvail().x < ImGui::CalcTextSize(writeLabel.c_str()).x + comboW + ImGui::GetFontSize()) {
            ImGui::NewLine();  // a narrow window: its own row
            RowLabel(writeLabel);
            ImGui::SetNextItemWidth(ControlW());
        } else {
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", writeLabel.c_str());
            ImGui::SameLine();
            ImGui::SetNextItemWidth(comboW);
        }
        LanguageCombo("##writestart", edit.writeIn, Tr(L"System") + L" (" + NativeName(TypingLang()) + L")");
        tip(Tr(L"The language you type in – the typing help and the translation start from it. It follows the chat's language until you choose it on its own."));
    }
    RenderSendRow();
    tip(Tr(L"For typing and speaking – the same choice as \"Send\" in the chat window."));
    RowLabel(UI(Tr(L"I understand")));
    RenderUnderstood(edit);
    tip(Tr(L"Lines in these languages stay as they are."));
    EndBox();
    RenderLayoutTiles();
    BeginBox(UI(Tr(L"Window & keys")));
    RenderUiLanguageRow(edit);
    // No key is set by us (Emi): each action, the key you gave it in Nexus, and a button to Nexus' Keybinds.
    KeyRow(Tr(L"Window"), kToggleBind);
    KeyRow(Tr(L"Input"), kFocusBind);
    KeyRow(Tr(L"Translation"), kTranslateBind);
    if (edit.voiceEnabled) KeyRow(Tr(L"Voice"), kTalkBind);
    EndBox();
}

}  // namespace kb
