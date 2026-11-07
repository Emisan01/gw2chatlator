// kb_options_debug.cpp — the debug window and its tech palette: states, numbers and counts, never a chat text or a name.

#include "kb_shared.hpp"
#include "kb_options_parts.hpp"
#include "core/slang.hpp"
#include "win/local_mt_packages.hpp"

namespace kb {

namespace {
// ---- The tech palette: below the translator's live view. States, numbers and counts, never a chat text or a name.
struct CardMeter {
    bool present = false;
    std::string name;
    unsigned load = 0, clock = 0, maxClock = 0, peakLoad = 0, peakClock = 0;
    unsigned long long usedMb = 0, totalMb = 0, peakMb = 0;
};
std::mutex s_cardMeterMu;
CardMeter s_cardMeter;
std::atomic<bool> s_cardMeterAsked{false};

// On the worker, every few seconds while the section is open: NVML comes with the driver and is open only for this moment.
void ReadCardMeter() {
    CardMeter now;
    if (HMODULE nvml = LoadLibraryExW(L"nvml.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)) {
        struct Memory {
            unsigned long long total, free, used;
        };
        struct Utilization {
            unsigned gpu, memory;
        };
        const auto init = reinterpret_cast<int (*)()>(GetProcAddress(nvml, "nvmlInit_v2"));
        const auto shutdown = reinterpret_cast<int (*)()>(GetProcAddress(nvml, "nvmlShutdown"));
        const auto handle = reinterpret_cast<int (*)(unsigned, void**)>(GetProcAddress(nvml, "nvmlDeviceGetHandleByIndex_v2"));
        const auto memory = reinterpret_cast<int (*)(void*, Memory*)>(GetProcAddress(nvml, "nvmlDeviceGetMemoryInfo"));
        const auto rates = reinterpret_cast<int (*)(void*, Utilization*)>(GetProcAddress(nvml, "nvmlDeviceGetUtilizationRates"));
        const auto clock = reinterpret_cast<int (*)(void*, int, unsigned*)>(GetProcAddress(nvml, "nvmlDeviceGetClockInfo"));
        const auto maxClock = reinterpret_cast<int (*)(void*, int, unsigned*)>(GetProcAddress(nvml, "nvmlDeviceGetMaxClockInfo"));
        const auto name = reinterpret_cast<int (*)(void*, char*, unsigned)>(GetProcAddress(nvml, "nvmlDeviceGetName"));
        if (init && shutdown && handle && memory && rates && clock && maxClock && name && init() == 0) {
            void* device = nullptr;
            if (handle(0, &device) == 0) {
                now.present = true;
                Memory m{};
                if (memory(device, &m) == 0) {
                    now.usedMb = m.used / (1024 * 1024);
                    now.totalMb = m.total / (1024 * 1024);
                }
                Utilization u{};
                if (rates(device, &u) == 0) now.load = u.gpu;
                clock(device, 0, &now.clock);
                maxClock(device, 0, &now.maxClock);
                char text[96] = {};
                if (name(device, text, sizeof(text)) == 0) now.name = text;
            }
            shutdown();
        }
        FreeLibrary(nvml);
    }
    std::lock_guard<std::mutex> lock(s_cardMeterMu);
    now.peakLoad = std::max(s_cardMeter.peakLoad, now.load);
    now.peakClock = std::max(s_cardMeter.peakClock, now.clock);
    now.peakMb = std::max(s_cardMeter.peakMb, now.usedMb);
    s_cardMeter = now;
    s_cardMeterAsked = false;
}

bool PaletteHead(const std::wstring& title) {
    if (g_test.noSend) ImGui::SetNextItemOpen(true, ImGuiCond_Appearing);  // the self-test draws them all
    return ImGui::CollapsingHeader(UI(title).c_str());
}

void PaletteNote(const std::wstring& text) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", UI(text).c_str());
    ImGui::PopTextWrapPos();
}

void PaletteState(const std::wstring& label, float col, bool should, bool is) {
    ImGui::TextDisabled("%s", UI(label).c_str());
    ImGui::SameLine(col);
    if (should && is) ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.f), "%s", UI(Tr(L"on")).c_str());
    else if (should) ImGui::TextColored(ImVec4(0.94f, 0.75f, 0.35f, 1.f), "%s", UI(Tr(L"should be on, is not")).c_str());
    else ImGui::TextDisabled("%s", UI(Tr(L"off")).c_str());
}

void PaletteModules(float col, const std::shared_ptr<Translator>& translator, bool local) {
    bool translateChat, useGpu, voice, suggestions, spell, back, along, down, up;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        translateChat = g_nx.translateChat;
        useGpu = g_cfg.useGpu;
        voice = g_cfg.voiceEnabled;
        suggestions = g_cfg.suggestions;
        spell = g_cfg.spellEnabled;
        back = g_cfg.backTranslate;
        along = g_cfg.thinkAlong;
        down = g_cfg.beamStepDown;
        up = g_cfg.beamStepUp;
    }
    const gct::LocalMtCard card = gct::GetLocalMtCard(translator);
    const gct::HelperInfo helper = gct::GetLocalMtHelperInfo(translator);
    PaletteState(Tr(L"Reading the chat"), col, g_chatEventsPresent.load(), g_chatEventsLive.load());
    PaletteState(Tr(L"Chat translation"), col, translateChat, translator != nullptr);
    if (local) PaletteState(Tr(L"Translator helper"), col, true, helper.status == gct::HelperStatus::Ok);
    if (local) PaletteState(Tr(L"Graphics card"), col, useGpu, card.known && card.onCard);
    PaletteState(Tr(L"Voice module"), col, voice, Modules().voiceModule);
    PaletteState(Tr(L"Voice model"), col, voice, Modules().model);
    PaletteState(Tr(L"Word suggestions"), col, suggestions, suggestions);
    PaletteState(Tr(L"Spell checking"), col, spell, spell);
    PaletteState(Tr(L"Back-translation"), col, back, back);
    PaletteState(Tr(L"Translate along"), col, along, along);
    PaletteState(Tr(L"Step down"), col, down, down);
    PaletteState(Tr(L"Step up"), col, up, up);
}

void PaletteSteps(float fs) {
    MtRun last;
    bool have = false;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        for (auto it = g_mtRuns.rbegin(); it != g_mtRuns.rend(); ++it)
            if (!it->cached && !it->greeting) {
                last = *it;
                have = true;
                break;
            }
    }
    if (!have) {
        PaletteNote(Tr(L"No line translated yet."));
        return;
    }
    const long long model = last.run.modelMs, rest = std::max(0LL, last.ms - last.prepMs - model);
    const long long parts[] = {last.queueMs, last.prepMs, model, rest};
    const std::wstring names[] = {Tr(L"Queue"), Tr(L"Prepared"), Tr(L"Model"), Tr(L"Rest")};
    const ImVec4 colors[] = {ImVec4(0.94f, 0.75f, 0.35f, 1.f), ImVec4(0.45f, 0.70f, 0.95f, 1.f), UiAccent(), ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)};
    long long total = 0;
    for (const long long p : parts) total += p;
    const float w = std::max(fs * 10.0f, ImGui::GetContentRegionAvail().x), h = fs * 0.8f;
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(w, h));
    float x = at.x;
    for (int i = 0; i < 4; ++i) {
        const float part = total > 0 ? w * static_cast<float>(parts[i]) / static_cast<float>(total) : 0.0f;
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(x, at.y), ImVec2(x + part, at.y + h), ImGui::GetColorU32(colors[i]));
        x += part;
    }
    for (int i = 0; i < 4; ++i) {
        if (i) ImGui::SameLine(0.0f, fs);
        ImGui::TextColored(colors[i], "%s %lld ms", UI(names[i]).c_str(), parts[i]);
    }
}

void PaletteThreads(float fs) {
    bool keep = g_gameThreadsKeep.load();
    if (ImGui::Checkbox(UI(Tr(L"Keep measuring while this window is closed")).c_str(), &keep)) g_gameThreadsKeep = keep;
    ImGui::SameLine();
    if (ImGui::SmallButton(UI(Tr(L"Reset") + L"##threads").c_str())) GameThreadsReset();
    const GameThreads g = GameThreadsNow();
    if (!g.history.empty()) ImGui::PlotLines("##threadshistory", g.history.data(), static_cast<int>(g.history.size()), 0, nullptr, 0.0f, 100.0f, ImVec2(ImGui::GetContentRegionAvail().x, fs * 3.0f));
    wchar_t total[24], longest[24], measured[24];
    swprintf_s(total, L"%.0f", static_cast<double>(g.totalS));
    swprintf_s(longest, L"%.1f", static_cast<double>(g.longestS));
    swprintf_s(measured, L"%.0f", static_cast<double>(g.measuredS));
    if (g.atLimit) ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.35f, 1.f), "%s", UI(Tr(L"At its limit right now")).c_str());
    PaletteNote(TrF(L"At its limit (90 % of a core): {1} times, {2} s in all, longest {3} s, over {4} s measured",
                    {std::to_wstring(g.episodes), total, longest, measured}));
    if (g.fpsHelperN || g.fpsIdleN) {
        PaletteNote(TrF(L"Frames per second at its limit: {1} while we translate, {2} without",
                        {g.fpsHelperN ? std::to_wstring(static_cast<int>(g.fpsHelperSum / g.fpsHelperN)) : std::wstring(L"-"),
                         g.fpsIdleN ? std::to_wstring(static_cast<int>(g.fpsIdleSum / g.fpsIdleN)) : std::wstring(L"-")}));
    }
    for (const GameThreadRow& row : g.top) {
        ImGui::TextDisabled("%5.1f %%", static_cast<double>(row.core));
        ImGui::SameLine(fs * 4.5f);
        const std::string name = row.name.empty() ? std::to_string(row.id) : U8(row.name);
        const std::string note = row.render ? UI(Tr(L"draws the game")) : row.ours ? UI(Tr(L"ours")) : std::string();
        ImGui::TextUnformatted(note.empty() ? name.c_str() : (name + " · " + note).c_str());
    }
    PaletteNote(Tr(L"Only Windows' numbers, no game memory."));
}

void KeyCell(int vk, const char* label, float w, float h, uint32_t most) {
    const uint32_t count = g_keys.presses[vk];
    const float t = most ? static_cast<float>(count) / static_cast<float>(most) : 0.0f;
    const ImVec4 from = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg), to = UiAccent();
    const ImVec4 fill(from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t, from.z + (to.z - from.z) * t, 1.f);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(w, h));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(at, ImVec2(at.x + w, at.y + h), ImGui::GetColorU32(fill), 2.0f);
    const ImVec2 size = ImGui::CalcTextSize(label);
    dl->AddText(ImVec2(at.x + (w - size.x) * 0.5f, at.y + (h - size.y) * 0.5f), ImGui::GetColorU32(t > 0.6f ? ImGuiCol_WindowBg : ImGuiCol_Text), label);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%u", count);
}

void PaletteKeyboard(float fs) {
    uint32_t most = 1;
    for (const uint32_t count : g_keys.presses) most = std::max(most, count);
    const float cell = fs * 1.7f, gap = fs * 0.15f;
    const auto row = [&](const char* keys, float indent) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);
        for (const char* k = keys; *k; ++k) {
            if (k != keys) ImGui::SameLine(0.0f, gap);
            const char label[2] = {*k, 0};
            KeyCell(*k, label, cell, cell, most);
        }
    };
    row("1234567890", 0.0f);
    ImGui::SameLine(0.0f, gap);
    KeyCell(VK_BACK, "<-", cell * 2.0f, cell, most);
    row("QWERTZUIOP", cell * 0.5f);
    row("ASDFGHJKL", cell * 0.8f);
    ImGui::SameLine(0.0f, gap);
    KeyCell(VK_RETURN, "Enter", cell * 2.2f, cell, most);
    row("YXCVBNM", cell * 1.2f);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + cell * 2.5f);
    KeyCell(VK_SPACE, "Space", cell * 6.0f, cell, most);
    const uint32_t total = std::max<uint32_t>(g_keys.total, 1);
    PaletteNote(TrF(L"{1} keys so far · Backspace {2} %", {std::to_wstring(g_keys.total), std::to_wstring(g_keys.presses[VK_BACK] * 100 / total)}));
    PaletteNote(TrF(L"Space wrote a word {1} times, you took it back {2} times · {3} words clicked",
                    {std::to_wstring(g_keys.spaceWrote), std::to_wstring(g_keys.spaceUndone), std::to_wstring(g_keys.wordClicked)}));
    PaletteNote(TrF(L"Kept during sends: {1} keys in {2} sends", {std::to_wstring(g_keys.buffered), std::to_wstring(g_keys.bufferRuns)}));
    if (g_keys.spaceWrote >= 10 && g_keys.spaceUndone * 5 >= g_keys.spaceWrote) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(ImVec4(0.94f, 0.75f, 0.35f, 1.f), "%s", UI(Tr(L"You often take back what Space wrote. If it fights you, turn off the word suggestions on the Typing page.")).c_str());
        ImGui::PopTextWrapPos();
    }
    PaletteNote(Tr(L"Counts per key only, never their order, never saved."));
}

void PaletteMachine() {
    static unsigned s_peakRam = 0;
    MEMORYSTATUSEX ram{};
    ram.dwLength = sizeof(ram);
    if (GlobalMemoryStatusEx(&ram)) {
        s_peakRam = std::max<unsigned>(s_peakRam, ram.dwMemoryLoad);
        ImGui::TextUnformatted(UI(TrF(L"RAM: {1} % in use, highest {2} %", {std::to_wstring(ram.dwMemoryLoad), std::to_wstring(s_peakRam)})).c_str());
    }
    static ULONGLONG s_next = 0;
    const ULONGLONG now = GetTickCount64();
    if (NvidiaDriverPresent() && now >= s_next && !g_test.noSend && !s_cardMeterAsked.exchange(true)) {
        s_next = now + 3000;
        g_worker.Post(ReadCardMeter);
    }
    CardMeter card;
    {
        std::lock_guard<std::mutex> lock(s_cardMeterMu);
        card = s_cardMeter;
    }
    if (!card.present) {
        PaletteNote(Tr(L"No NVIDIA card measured."));
        return;
    }
    ImGui::TextUnformatted(UI(TrF(L"Card: {1} · load {2} %, highest {3} %", {FromUtf8(card.name), std::to_wstring(card.load), std::to_wstring(card.peakLoad)})).c_str());
    ImGui::TextUnformatted(UI(TrF(L"Video memory: {1} of {2} MB, highest {3} MB", {std::to_wstring(card.usedMb), std::to_wstring(card.totalMb), std::to_wstring(card.peakMb)})).c_str());
    ImGui::TextUnformatted(UI(TrF(L"Clock: {1} of {2} MHz, highest {3} MHz", {std::to_wstring(card.clock), std::to_wstring(card.maxClock), std::to_wstring(card.peakClock)})).c_str());
}

void PaletteLoadTest(float fs) {
    LoadTest& t = g_loadTest;
    ImGui::SetNextItemWidth(fs * 10.0f);
    ImGui::SliderInt(UI(Tr(L"Lines per second")).c_str(), &t.perSecond, 1, 20);
    const ULONGLONG now = GetTickCount64();
    if (!t.on) {
        if (ImGui::Button(UI(Tr(L"Start for 30 s")).c_str())) {
            t.on = true;
            t.sent = 0;
            t.deepest = 0;
            t.nextAt = now;
            t.until = now + 30000;
            TechLog('T', L"load test: " + std::to_wstring(t.perSecond) + L" lines per second");
        }
    } else if (ImGui::Button(UI(Tr(L"Stop")).c_str())) {
        t.until = 0;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", UI(TrF(L"Sent {1} · waiting now {2} · the queue at most {3}",
                                     {std::to_wstring(t.sent), std::to_wstring(g_worker.QueueSize()), std::to_wstring(t.deepest)})).c_str());
    PaletteNote(Tr(L"Made-up lines through the real translator; nothing goes to the chat. The CPU or the card will work hard."));
}

void PaletteLogbook(float fs) {
    static int s_filter = 0;
    const char areas[] = {0, 'T', 'H', 'C', 'K', 'S', 'O', 'N'};
    const std::string names[] = {UI(Tr(L"All")), UI(Tr(L"Translation")), UI(Tr(L"Helper")), UI(Tr(L"Chat")),
                                 UI(Tr(L"Keys")), UI(Tr(L"Sending")), UI(Tr(L"Options")), UI(Tr(L"Nexus"))};
    ImGui::SetNextItemWidth(fs * 9.0f);
    if (ImGui::BeginCombo("##logarea", names[s_filter].c_str())) {
        for (int i = 0; i < 8; ++i)
            if (ImGui::Selectable(names[i].c_str(), i == s_filter)) s_filter = i;
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    const std::vector<TechEntry> entries = TechLogNow();
    const ULONGLONG now = GetTickCount64();
    if (ImGui::SmallButton(UI(Tr(L"Copy") + L"##log").c_str())) {
        std::string all;
        for (const TechEntry& e : entries)
            if (e.area != 'C') all += std::to_string((now - e.at) / 1000) + "s [" + std::string(1, e.area) + "] " + U8(e.text) + "\n";
        ImGui::SetClipboardText(all.c_str());
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(UI(Tr(L"Clear") + L"##log").c_str())) TechLogClear();
    ImGui::BeginChild("##techlog", ImVec2(0.0f, fs * 14.0f), true);
    int shown = 0;
    for (auto it = entries.rbegin(); it != entries.rend() && shown < 200; ++it) {  // the newest on top
        if (areas[s_filter] && it->area != areas[s_filter]) continue;
        ++shown;
        ImGui::TextDisabled("%llus %c", static_cast<unsigned long long>((now - it->at) / 1000), it->area);
        ImGui::SameLine(fs * 4.5f);
        ImGui::TextUnformatted(U8(it->text).c_str());
    }
    ImGui::EndChild();
}
}  // namespace

void RenderTechPalette() {
    std::shared_ptr<Translator> translator;
    bool local;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        translator = g_translator;
        local = IsLocalMtTranslator(g_translator);
    }
    const float fs = ImGui::GetFontSize();
    ImGui::Spacing();
    if (PaletteHead(Tr(L"Modules: should and is"))) PaletteModules(fs * 12.0f, translator, local);
    if (PaletteHead(Tr(L"Work steps of the last line"))) PaletteSteps(fs);
    if (PaletteHead(Tr(L"The game's threads"))) PaletteThreads(fs);
    if (PaletteHead(Tr(L"Keyboard"))) PaletteKeyboard(fs);
    if (PaletteHead(Tr(L"Machine"))) PaletteMachine();
    if (PaletteHead(Tr(L"Load test"))) PaletteLoadTest(fs);
    if (PaletteHead(Tr(L"Logbook"))) PaletteLogbook(fs);
}

void RenderMtLiveWindow() {
    if (!g_showMtLive) return;
    const float nexusPx = FollowUiPx();
    RequestUiFont(nexusPx);
    ImFont* uf = g_uiFont.load();
    ImGui::SetNextWindowSize(ImVec2(nexusPx * 40.0f, nexusPx * 46.0f), ImGuiCond_FirstUseEver);
    const std::string title = UI(Tr(L"Translator live")) + "###kbmtlive";
    const bool visible = ImGui::Begin(title.c_str(), &g_showMtLive);
    bool pushedUf = false;
    if (uf && uf->FontSize > 0.0f) {
        ImGui::SetWindowFontScale(nexusPx / uf->FontSize);
        ImGui::PushFont(uf);
        pushedUf = true;
    }
    if (visible) {
        RenderTranslatorLive();
        RenderTechPalette();
    }
    if (pushedUf) ImGui::PopFont();
    ImGui::End();
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
            if (ImGui::BeginTabItem(UI(Tr(L"Live status") + L"###tab_live").c_str())) {
                FrameCosts fc = CostsNow();
                const size_t qDepth = g_worker.QueueSize();
                const int effectiveBeam = g_cfg.beamStepDown ? g_orchestrator.ComputeBeamSize(static_cast<gct::QualityMode>(g_cfg.localQuality), qDepth) : g_cfg.localQuality;

                StatusRow(UI(Tr(L"Line filter")), 2,
                          UI(Tr(L"Active (Filtering gamer slang & repeated text)")));

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
                ImGui::TextUnformatted(UI(Tr(L"Line filter: what it saved")).c_str());
                ImGui::BulletText("%s", UI(TrF(L"Gamer slang and short forms filtered (no CPU): {1}",
                                               {std::to_wstring(g_orchestrator.SlangFilteredCount())})).c_str());
                ImGui::BulletText("%s", UI(TrF(L"Repeated lines from the cache: {1} (0 ms) · {2} entries",
                                               {std::to_wstring(g_orchestrator.CacheHitCount()),
                                                std::to_wstring(g_orchestrator.CacheSize())})).c_str());

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

}  // namespace kb
