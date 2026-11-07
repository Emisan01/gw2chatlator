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
    const bool isLocal = IsLocalEngine(edit.engine);
    const bool localReady = local.module && LocalModelThere(local, edit.engine);
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
        ImGui::PushStyleColor(ImGuiCol_Text, UiAccent());
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

}  // namespace kb
