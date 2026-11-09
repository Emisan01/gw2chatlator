// kb_options_voice.cpp — the Voice page and the voice part of Start.

#include "kb_shared.hpp"
#include "kb_options_parts.hpp"
#include "win/local_mt_packages.hpp"
#include "voice_module.hpp"

namespace kb {

namespace {
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

void RenderVoicePage(Config& edit, bool entered) {
    static std::vector<AudioDevice> s_mics;
    if (entered || s_mics.empty()) s_mics = AudioCapture::Devices();
    const bool voiceModuleThere = Modules().voiceModule;  // voice-only switches wait for the voice module
    const std::string needsVoice = UI(Tr(L"Needs the voice module (Options → Voice)."));
    BeginBox(UI(Tr(L"Voice module")));
    Switch("voiceon", UI(Tr(L"Voice input")), &edit.voiceEnabled);
    RenderVoiceModule();
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
        ImGui::Indent(g_labelW);
        if (pct >= 0) ImGui::ProgressBar(pct / 100.f, ImVec2(ControlW(), 0));
        else if (ImGui::Button(UI(Tr(L"Download the model")).c_str())) StartModelDownload(*chosen);
        ImGui::Unindent(g_labelW);
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
            ImGui::Indent(g_labelW);
            ImGui::TextColored(UiWarm(), "%s", UI(Tr(L"Language not supported by speech recognition")).c_str());
            ImGui::Unindent(g_labelW);
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
            const bool hasLocalMt = gct::LocalMtModelPresent(edit.dataDir, "opus-mt") || gct::LocalMtModelPresent(edit.dataDir, "nllb-200") ||
                                    gct::LocalMtModelPresent(edit.dataDir, "m2m100");
            RowHint(UI(hasLocalMt && IsLocalEngine(edit.engine)
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
    Hint(UI(Tr(L"No time limit: only a safety net of 10 minutes against a stuck key. One Enter sends at most 3 chat lines, about 45 seconds of speech.")));
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
    ImGui::TextColored(UiAccent(), "[ %s ]", keyDisplay.c_str());
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

}  // namespace kb
