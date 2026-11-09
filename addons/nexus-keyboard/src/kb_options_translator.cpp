// kb_options_translator.cpp — the Translator page and the translator rows Start shows too.

#include "win/cpu_cores.hpp"
#include "kb_shared.hpp"
#include "kb_options_parts.hpp"
#include "imgui/imgui_internal.h"
#include "win/local_mt_packages.hpp"

namespace kb {

// The language the chat is read in, its primary part ("DE").
std::wstring ReadingLangNow() {
    std::lock_guard<std::mutex> lock(g_mu);
    return PrimaryLang(ReadLang());
}

// The translators in the order the choosers list them, by their names.
const wchar_t* EngineLabel(Engine e) {
    switch (e) {
        case Engine::Auto: return L"Automatic";
        case Engine::Basic: return L"MyMemory (free)";
        case Engine::DeepL: return L"DeepL";
        case Engine::Google: return L"Google";
        case Engine::Microsoft: return L"Microsoft";
        case Engine::Supertext: return L"Supertext";
        case Engine::Libre: return L"LibreTranslate";
        case Engine::Local: return L"OPUS-MT (local)";
        case Engine::Nllb: return L"NLLB-200 (local)";
        case Engine::M2m: return L"M2M100 (local)";
        default: return L"MyMemory (free)";
    }
}

bool IsLocalEngine(Engine e) { return e == Engine::Nllb || e == Engine::Local || e == Engine::M2m; }
bool LocalModelThere(const LocalMtSummary& local, Engine e) {
    return e == Engine::Nllb ? local.nllb : e == Engine::M2m ? local.m2m : local.opus;
}

namespace {
// A translator that cannot run before its key (LibreTranslate: its server) is there.
bool NeedsKey(const Config& c, Engine e) {
    return (e == Engine::DeepL && c.deeplKey.empty()) || (e == Engine::Google && c.googleKey.empty()) ||
           (e == Engine::Microsoft && c.msKey.empty()) || (e == Engine::Supertext && c.supertextKey.empty()) ||
           (e == Engine::Libre && c.libreUrl.empty());
}
bool UsesKey(Engine e) {
    return e == Engine::DeepL || e == Engine::Google || e == Engine::Microsoft || e == Engine::Supertext || e == Engine::Libre;
}
std::wstring& KeyOf(Config& c, Engine e) {
    return e == Engine::DeepL ? c.deeplKey : e == Engine::Google ? c.googleKey : e == Engine::Microsoft ? c.msKey : e == Engine::Supertext ? c.supertextKey : c.libreKey;
}

// A text field in a row: shows the setting while you are not in it, takes what you wrote when you leave it.
void RowText(const char* id, char* buf, size_t size, std::wstring& target, ImGuiInputTextFlags flags = 0,
             const char* hint = nullptr) {
    if (ImGui::GetActiveID() != ImGui::GetID(id)) CopyUtf8(buf, size, U8(target));
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
    ImGui::TextDisabled("%s", UI(Tr(L"Local, on this PC")).c_str());
    item(Engine::Nllb, local.module && local.nllb ? std::wstring() : Tr(L"install, about 600 MB"));
    item(Engine::Local, local.module && local.opus ? std::wstring() : Tr(L"choose a language pair first"));
    item(Engine::M2m, local.module && local.m2m ? std::wstring() : Tr(L"missing (about 470 MB)"));
    ImGui::Separator();
    ImGui::TextDisabled("%s", UI(Tr(L"With your own API key")).c_str());
    const std::wstring keyNeeded = Tr(L"key needed");
    item(Engine::DeepL, edit.deeplKey.empty() ? keyNeeded : std::wstring());
    item(Engine::Google, edit.googleKey.empty() ? keyNeeded : std::wstring());
    item(Engine::Microsoft, edit.msKey.empty() ? keyNeeded : std::wstring());
    item(Engine::Supertext, edit.supertextKey.empty() ? keyNeeded : std::wstring());
    item(Engine::Libre, edit.libreUrl.empty() ? Tr(L"server needed") : std::wstring());
    return picked;
}
}  // namespace

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
    // "Automatic" takes the first key you entered (DeepL, Google, Microsoft, Supertext, a LibreTranslate server) and only
    // without one MyMemory – the row and its line say which (MakeTranslator).
    const bool anyKey = !edit.deeplKey.empty() || !edit.googleKey.empty() || !edit.msKey.empty() || !edit.supertextKey.empty() ||
                        !edit.libreUrl.empty();
    const bool myMemory = shown == Engine::Basic || (shown == Engine::Auto && !anyKey);
    const bool isLocal = IsLocalEngine(shown);
    const bool localReady = local.module && LocalModelThere(local, shown);
    // The row says what it is (the page and its box say "Translator" already): a local module and its size, or online.
    {
        std::wstring what;
        if (isLocal && localReady) {
            const uint64_t bytes = shown == Engine::Nllb ? local.nllbBytes : shown == Engine::M2m ? local.m2mBytes : local.opusBytes;
            what = TrF(L"Local module · {1} MB", {std::to_wstring(bytes / (1024 * 1024))});
        } else if (isLocal) {
            what = Tr(L"Local module · missing");
        } else {
            what = myMemory ? Tr(L"Online · free") : Tr(L"Online · your key");
        }
        RowLabel(UI(what));
    }
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
        const int qi = edit.localQuality == 1 ? 0 : edit.localQuality == 4 ? 2 : edit.localQuality == 2 ? 1 : -1;  // -1: a beam set by hand
        ImGui::SameLine();
        ImGui::SetNextItemWidth(fs * 7.0f);
        if (ImGui::BeginCombo("##quality", UI(qi >= 0 ? step[qi] : TrF(L"Beam {1}", {std::to_wstring(edit.localQuality)})).c_str())) {
            for (int q = 0; q < 3; ++q)
                if (ImGui::Selectable((UI(step[q]) + "   " + UI(tip[q])).c_str(), q == qi)) edit.localQuality = kQuality[q];
            ImGui::EndCombo();
        }
        if (qi >= 0 && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", UI(tip[qi]).c_str());
    }
    // On Start the card's switch stands in this line when there is room, else in its own row. Its module missing: the
    // list "What is still missing" above offers it.
    if (!full && isLocal && localReady && NvidiaDriverPresent() && CardModuleComplete()) {
        const std::string gpuLabel = UI(Tr(L"Calculate on the graphics card (NVIDIA)"));
        const float need = std::round(fs * 1.125f) * 1.8f + fs * 0.6f + ImGui::CalcTextSize(gpuLabel.c_str()).x + fs;
        ImGui::SameLine();
        if (ImGui::GetContentRegionAvail().x < need) {
            ImGui::NewLine();
            RowLabel(UI(Tr(L"Graphics card")));
        }
        Switch("usegpustart", gpuLabel, &edit.useGpu,
               UI(Tr(L"Takes about 0.7 GB of video memory with M2M100, 0.9 GB with NLLB-200. Without room it falls back to the CPU.")).c_str());
    }
    if (picked && chosen != shown) {
        s_wantSet = false;
        if (chosen == Engine::Nllb && !nllbReady) s_confirmNllb = true;  // the size first, then OK installs and switches
        else if (chosen == Engine::M2m && !(local.module && local.m2m)) SwitchOptionsTab(kPageTranslator);  // its box there has the download
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
        else if (isLocal) how = TrF(L"Local on this PC · Beam {1}", {std::to_wstring(edit.localQuality)});
        else if (myMemory) {
            how = MyMemoryQuotaText();
            if (how.empty()) how = Tr(L"Free and online – works without setup");
        } else how = Tr(L"Online with your API key");
    }
    RowHint(UI(how));
    if (shown == Engine::Nllb && !nllbReady && !s_confirmNllb && !g_localMtBusy) {
        ImGui::Indent(g_labelW);
        if (ImGui::Button(UI(Tr(L"Install NLLB-200 (local)")).c_str())) s_confirmNllb = true;
        ImGui::Unindent(g_labelW);
    }
    if (s_confirmNllb) {  // what comes, and how big, before anything is downloaded
        ImGui::Indent(g_labelW);
        ImGui::TextUnformatted(UI(Tr(L"Install NLLB-200 (local)")).c_str());
        Hint(UI(Tr(L"Local Translation installs the local neural machine translation engine and NLLB-200 multilingual model.")));
        ImGui::TextColored(ImVec4(0.51f, 0.76f, 0.63f, 1.f), "%s", UI(Tr(L"Download size: about 600 MB · translates locally on this PC")).c_str());
        if (ImGui::Button((UI(Tr(L"OK")) + "##nllbok").c_str())) {
            InstallLocalMtQuickSetup(edit.dataDir);  // becomes the translator once the files are there
            s_confirmNllb = false;
        }
        ImGui::SameLine();
        if (ImGui::Button((UI(Tr(L"Cancel")) + "##nllbno").c_str())) s_confirmNllb = false;
        ImGui::Unindent(g_labelW);
    }
    if (g_localMtBusy) {
        const int pct = g_localMtPct.load();
        ImGui::Indent(g_labelW);
        ImGui::ProgressBar(pct >= 0 ? pct / 100.0f : 0.0f, ImVec2(ControlW(), 0.0f),
                           pct >= 0 ? (std::to_string(pct) + " %").c_str() : UI(Tr(L"Installing…")).c_str());
        Hint(UI(Tr(L"Downloading in background – game remains playable")));
        ImGui::Unindent(g_labelW);
    }
    // Keys: on the Translator page for a translator that uses one, on Start while one is still missing.
    if (UsesKey(shown) && (full || s_wantSet)) {
        if (shown != Engine::Libre || full) {
            RowLabel(UI(Tr(L"API key")));
            RowText("##key", keyBuf, sizeof keyBuf, KeyOf(edit, shown), ImGuiInputTextFlags_Password);
        }
        if (shown == Engine::DeepL) {  // its free way: a key ending in :fx goes to DeepL's free server by itself (deepl_translator)
            RowHint(UI(Tr(L"Free: DeepL API Free – 500,000 characters a month with a free DeepL account (DeepL may ask for a card to check who you are, without charging it). A key ending in :fx works here as it is.")));
            ImGui::Indent(g_labelW);
            static const wchar_t* kDeepLApi = L"https://www.deepl.com/pro-api";
            if (ImGui::SmallButton(UI(Tr(L"Get a free key")).c_str())) SafeOpenUrl(kDeepLApi);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", UI(TrF(L"Opens {1} in your browser.", {kDeepLApi})).c_str());
            ImGui::Unindent(g_labelW);
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
    RowLabel(UI(Tr(L"I read the chat in")));
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
    RowLabel(UI(Tr(L"Translate to")));
    std::lock_guard<std::mutex> lock(g_mu);
    const int n = static_cast<int>(g_cfg.writeLangs.size());
    const std::string shown = g_sendIdx >= 0 && g_sendIdx < n ? U8(NativeName(g_cfg.writeLangs[static_cast<size_t>(g_sendIdx)]))
                                                              : UI(Tr(L"as written"));
    ImGui::SetNextItemWidth(ControlW());
    ImGui::PushStyleColor(ImGuiCol_Text, UiAccent());
    const bool open = ImGui::BeginCombo("##send", shown.c_str());
    ImGui::PopStyleColor();
    if (open) {
        SendLanguageItems(false);
        ImGui::EndCombo();
    }
}

namespace {
// For those who want every knob: every beam, stepping down, versions to choose from, the live view.
void RenderLocalAdvanced(Config& edit) {
    BeginBox(UI(Tr(L"Advanced local translation")));
    const auto beamText = [](int beam) {
        std::string t = std::to_string(beam);
        if (beam == 1) t += "   " + UI(Tr(L"Fast"));
        if (beam == 2) t += "   " + UI(Tr(L"Balanced"));
        if (beam == 4) t += "   " + UI(Tr(L"Best"));
        return t;
    };
    RowLabel(UI(Tr(L"Beam size")));
    ImGui::SetNextItemWidth(ControlW());
    if (ImGui::BeginCombo("##beamsize", beamText(edit.localQuality).c_str())) {
        for (int beam = 1; beam <= 8; ++beam)
            if (ImGui::Selectable(beamText(beam).c_str(), edit.localQuality == beam)) edit.localQuality = beam;
        ImGui::EndCombo();
    }
    RowHint(UI(Tr(L"How many candidates the model follows at once. More costs time and CPU and rarely changes the result – the live view shows it.")));
    if (edit.localQuality > 4) RiskHint(Tr(L"Above 4 we measured no gain, and lines wait longer."));
    const int threads = edit.mtThreads > 0 ? edit.mtThreads : gct::LocalAutoThreads(edit.localQuality, static_cast<int>(gct::PhysicalCores().size()));
    RowHint(UI(edit.mtThreads > 0 ? TrF(L"CPU threads now: {1} (set by hand)", {std::to_wstring(threads)})
                                  : TrF(L"CPU threads now: {1} (automatic, more with a higher beam)", {std::to_wstring(threads)})));
    if (static_cast<unsigned>(threads) * 2 > std::thread::hardware_concurrency()) RiskHint(Tr(L"More than half of your processors: the game can stutter."));
    if (NvidiaDriverPresent() && CardModuleComplete()) {
        Switch("usegpu", UI(Tr(L"Calculate on the graphics card (NVIDIA)")), &edit.useGpu,
               UI(Tr(L"Takes about 0.7 GB of video memory with M2M100, 0.9 GB with NLLB-200. Without room it falls back to the CPU.")).c_str());
        std::shared_ptr<Translator> translator;
        {
            std::lock_guard<std::mutex> lock(g_mu);
            translator = g_translator;
        }
        const gct::LocalMtCard card = gct::GetLocalMtCard(translator);
        if (edit.useGpu && card.known) {
            if (card.onCard) {
                RowHint(UI(TrF(L"On {1}: {2} MB of {3} MB free when the model came, driver {4}.",
                               {FromUtf8(card.name), std::to_wstring(card.freeMb), std::to_wstring(card.totalMb), FromUtf8(card.driver)})));
            } else if (card.why == "vram") {
                RiskHint(TrF(L"On the CPU for now: {1} MB free on the card, the model and the game's reserve need {2} MB.", {std::to_wstring(card.freeMb), std::to_wstring(card.needMb)}));
            } else if (card.why == "card") {
                RiskHint(Tr(L"On the CPU: this card or its driver cannot run the model. Cards from GTX 16 and RTX 20 upwards work."));
            } else {
                RiskHint(Tr(L"On the CPU: the helper for the graphics card failed, try reinstalling it."));
            }
        }
    }
    Switch("beamstep", UI(Tr(L"Step down under load")), &edit.beamStepDown,
           UI(Tr(L"Many lines waiting: lines that come by themselves run at beam 2, so the chat keeps up.")).c_str());
    Switch("beamup", UI(Tr(L"Step up when in doubt")), &edit.beamStepUp,
           UI(Tr(L"A line the check doubts is tried again at beam 4.")).c_str());
    Switch("thinkalong", UI(Tr(L"Translate along while typing")), &edit.thinkAlong,
           UI(Tr(L"At every word's end, so that Enter often finds it ready. Costs CPU while you type.")).c_str());
    RowLabel(UI(Tr(L"Versions to choose from")));
    ImGui::SetNextItemWidth(ControlW());
    const std::string versionsNow = edit.versions > 1 ? std::to_string(edit.versions) : UI(Tr(L"Off"));
    if (ImGui::BeginCombo("##versions", versionsNow.c_str())) {
        if (ImGui::Selectable(UI(Tr(L"Off")).c_str(), edit.versions < 2)) edit.versions = 0;
        for (int n = 2; n <= 5; ++n)
            if (ImGui::Selectable(std::to_string(n).c_str(), edit.versions == n)) edit.versions = n;
        ImGui::EndCombo();
    }
    RowHint(UI(Tr(L"The model hands back its runners-up too: choose another one in the preview or in a chat line's menu. Runs at least at that beam.")));
    ImGui::Indent(g_labelW);
    if (ImGui::Button(UI(g_showMtLive ? Tr(L"Hide the live view") : Tr(L"Show the live view")).c_str())) g_showMtLive = !g_showMtLive;
    ImGui::Unindent(g_labelW);
    EndBox();
}

// A setting in hundredths: takes effect when you let go, a new value translates the visible chat again.
void M2mSlider(const char* id, const std::wstring& label, const std::wstring& hint, int& value, int lo, int hi, const std::wstring& risk = {}) {
    RowLabel(UI(label));
    static float s_draft = 0.0f;
    float shown = ImGui::GetActiveID() == ImGui::GetID(id) ? s_draft : value / 100.0f;
    ImGui::SetNextItemWidth(ControlW());
    ImGui::SliderFloat(id, &shown, lo / 100.0f, hi / 100.0f, "%.2f");
    if (ImGui::IsItemActive()) s_draft = shown;
    if (ImGui::IsItemDeactivatedAfterEdit()) value = std::clamp(static_cast<int>(std::lround(shown * 100.0f)), lo, hi);
    RowHint(UI(hint));
    RiskHint(risk);
}

// M2M100 as CTranslate2 offers it. Left out on purpose: sampling (random, a translation should not be), a prefix bias
// (needs a target to lean on) and the vocabulary map (needs a file the model does not come with).
void RenderM2mBox(Config& edit) {
    BeginBox(UI(Tr(L"M2M100 settings")));
    M2mSlider("##m2mrep", Tr(L"Repetition penalty"), Tr(L"Above 1: words already written count less – against a translation that stutters."),
              edit.m2m.repetitionPenalty, 100, 200,
              edit.m2m.repetitionPenalty > 120 ? Tr(L"High values made translations worse in our test.") : std::wstring());
    RowLabel(UI(Tr(L"No repeats")));
    ImGui::SetNextItemWidth(ControlW());
    const std::string ngramNow = edit.m2m.noRepeatNgram > 0 ? std::to_string(edit.m2m.noRepeatNgram) : UI(Tr(L"Off"));
    if (ImGui::BeginCombo("##m2mngram", ngramNow.c_str())) {
        for (int n = 0; n <= 6; ++n) {
            const std::string text = n > 0 ? std::to_string(n) : UI(Tr(L"Off"));
            if (ImGui::Selectable(text.c_str(), edit.m2m.noRepeatNgram == n)) edit.m2m.noRepeatNgram = n;
        }
        ImGui::EndCombo();
    }
    RowHint(UI(Tr(L"No run of this many word pieces twice in one translation.")));
    M2mSlider("##m2mlen", Tr(L"Length penalty"), Tr(L"Above 1: longer translations win, below 1: shorter ones."), edit.m2m.lengthPenalty, 0, 300,
              edit.m2m.lengthPenalty < 80 || edit.m2m.lengthPenalty > 150 ? Tr(L"Far from 1 is untested: translations can get too short or too long.")
              : edit.m2m.lengthPenalty != 100 ? Tr(L"Not at 1 the score says nothing about certainty: no line is marked unsure.") : std::wstring());
    M2mSlider("##m2mcov", Tr(L"Coverage penalty"), Tr(L"Above 0: a translation that leaves source words out loses."), edit.m2m.coveragePenalty, 0, 100,
              edit.m2m.coveragePenalty > 0 ? Tr(L"Changed 38 of 106 lines in our test, none for the better; no line is marked unsure.") : std::wstring());
    M2mSlider("##m2mpat", Tr(L"Patience"), Tr(L"The search goes on longer before it settles – slower, sometimes better."), edit.m2m.patience, 100, 400,
              edit.m2m.patience > 200 ? Tr(L"Up to 2 no line got better in our test; more only costs time.") : std::wstring());
    RowLabel(UI(Tr(L"No unknown words")));
    ImGui::Checkbox("##m2munk", &edit.m2m.disableUnk);
    RowHint(UI(Tr(L"The model never writes its placeholder for a word it does not know.")));
    RowLabel(UI(Tr(L"Calculation")));
    ImGui::SetNextItemWidth(ControlW());
    if (ImGui::BeginCombo("##m2mcompute", U8(edit.m2mCompute).c_str())) {
        for (const wchar_t* compute : {L"int8", L"int8_float32", L"int16", L"float32"})
            if (ImGui::Selectable(U8(compute).c_str(), edit.m2mCompute == compute)) edit.m2mCompute = compute;
        ImGui::EndCombo();
    }
    RowHint(UI(Tr(L"int8 is the fastest. A change starts the translator anew.")));
    if (edit.m2mCompute == L"float32") RiskHint(Tr(L"2.5 times slower in our test, and no better."));
    ImGui::Indent(g_labelW);
    if (ResetButton(UI(Tr(L"Back to the measured values")))) {
        edit.m2m = gct::M2mRecommended();
        edit.m2mCompute = L"int8";
    }
    ImGui::Unindent(g_labelW);
    EndBox();
}
}  // namespace

void RenderTranslatorPage(Config& edit) {
    BeginBox(UI(Tr(L"Translator")));
    RenderTranslatorRows(edit, true);
    EndBox();
    if (edit.engine == Engine::M2m && LocalMtNow(edit.dataDir).m2m) RenderM2mBox(edit);
    if (IsLocalEngine(edit.engine)) RenderLocalAdvanced(edit);
    RenderLocalMtOptions(edit);  // the local translation module, NLLB-200, the quality, OPUS-MT – each in its box
}

}  // namespace kb
