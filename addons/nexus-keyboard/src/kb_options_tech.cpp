// kb_options_tech.cpp — the Technical page, the translator's live view and the diagnostic report.

#include "win/cpu_cores.hpp"
#include "kb_shared.hpp"
#include "kb_options_parts.hpp"
#include "core/slang.hpp"
#include "imgui/imgui_internal.h"
#include "win/local_mt_packages.hpp"
#include <cstdio>
#include <type_traits>

namespace kb {

namespace {
// The translator, live: what is set, what the model really did, whether it stepped down – numbers only, never a text.
void LiveCells(int total, int on, const ImVec4& color) {
    const float s = ImGui::GetFontSize() * 0.62f, gap = s * 0.3f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (int i = 0; i < total; ++i) {
        const ImVec2 a(p.x + i * (s + gap), p.y + ImGui::GetFontSize() * 0.2f), b(a.x + s, a.y + s);
        if (i < on) dl->AddRectFilled(a, b, ImGui::GetColorU32(color), s * 0.15f);
        else dl->AddRect(a, b, ImGui::GetColorU32(ImGuiCol_TextDisabled), s * 0.15f);
    }
    ImGui::Dummy(ImVec2(total * (s + gap), ImGui::GetTextLineHeight()));
    ImGui::SameLine();
}

ImVec4 LiveColor(const MtRun& r) {
    const ImVec4 ok(0.45f, 0.85f, 0.45f, 1.f), warn(0.94f, 0.75f, 0.35f, 1.f), bad(0.95f, 0.45f, 0.35f, 1.f), info(0.45f, 0.70f, 0.95f, 1.f);
    if (r.failed || r.doubt) return bad;
    if (r.cached || r.greeting) return info;
    return r.beamAsked < r.beamSet ? warn : ok;
}

std::wstring LiveFlags(const MtRun& r) {
    std::wstring f;
    const auto add = [&f](const std::wstring& s) { f += (f.empty() ? L"" : L" · ") + s; };
    if (r.failed) add(Tr(L"failed") + (r.error.empty() ? L"" : L": " + r.error));
    if (r.cached) add(Tr(L"from the cache"));
    if (r.greeting) add(Tr(L"greeting table"));
    if (r.second) add(Tr(L"second try"));
    if (r.doubt) add(Tr(L"doubtful"));
    if (r.versions > 0) add(Tr(L"Other versions") + L" " + std::to_wstring(r.versions));
    return f;
}

// What we cost the CPU, now and the highest of this session: the place to look for a bottleneck.
void RenderCpuLoad() {
    const float fs = ImGui::GetFontSize(), col = fs * 8.0f, barW = fs * 8.0f;
    const float nowAt = col + barW + fs * 0.6f, peakAt = nowAt + fs * 4.0f;
    ImGui::TextDisabled("%s", UI(Tr(L"CPU load (share of all processors)")).c_str());
    ImGui::SameLine(nowAt);
    ImGui::TextDisabled("%s", UI(Tr(L"now")).c_str());
    ImGui::SameLine(peakAt);
    ImGui::TextDisabled("%s", UI(Tr(L"peak")).c_str());
    const auto bar = [&](const std::wstring& label, float now, float peak, const ImVec4& color) {
        ImGui::TextDisabled("%s", UI(label).c_str());
        ImGui::SameLine(col);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = fs * 0.62f, y = p.y + fs * 0.22f;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(ImVec2(p.x, y), ImVec2(p.x + barW, y + h), ImGui::GetColorU32(ImGuiCol_FrameBg));
        dl->AddRectFilled(ImVec2(p.x, y), ImVec2(p.x + barW * std::min(now, 100.0f) / 100.0f, y + h), ImGui::GetColorU32(color));
        const float tick = p.x + barW * std::min(peak, 100.0f) / 100.0f;
        dl->AddLine(ImVec2(tick, y - 1.0f), ImVec2(tick, y + h + 1.0f), ImGui::GetColorU32(ImVec4(0.95f, 0.45f, 0.35f, 1.f)), 2.0f);
        ImGui::Dummy(ImVec2(barW, ImGui::GetTextLineHeight()));
        ImGui::SameLine(nowAt);
        ImGui::Text("%.1f %%", now);
        ImGui::SameLine(peakAt);
        ImGui::Text("%.1f %%", peak);
    };
    const ImVec4 ours(0.45f, 0.85f, 0.45f, 1.f), other(0.55f, 0.75f, 0.95f, 1.f);
    bar(Tr(L"Translator helper"), g_cpuNow.helper, g_cpuPeak.helper, ours);
    bar(Tr(L"Voice helper"), g_cpuNow.voice, g_cpuPeak.voice, ours);
    bar(Tr(L"Our threads"), g_cpuNow.threads, g_cpuPeak.threads, ours);
    bar(Tr(L"Our drawing"), g_cpuNow.draw, g_cpuPeak.draw, ours);
    bar(Tr(L"We together"), g_cpuNow.ours, g_cpuPeak.ours, UiAccent());
    bar(Tr(L"The game"), g_cpuNow.game, g_cpuPeak.game, other);
    bar(Tr(L"The whole PC"), g_cpuNow.pc, g_cpuPeak.pc, other);
    if (g_cpuPeakAt) {
        const unsigned logical = std::max(1u, std::thread::hardware_concurrency());
        wchar_t cores[16], game[16], pc[16];
        swprintf_s(cores, L"%.1f", g_cpuAtPeak.ours * logical / 100.0f);
        swprintf_s(game, L"%.1f", g_cpuAtPeak.game);
        swprintf_s(pc, L"%.1f", g_cpuAtPeak.pc);
        std::wstring during;
        {
            std::lock_guard<std::mutex> lock(g_mu);
            during = g_cpuPeakDuring.empty() ? std::wstring(L"-") : g_cpuPeakDuring;
        }
        ImGui::TextWrapped("%s", UI(TrF(L"Our highest, {1} s ago: {2} cores · game {3} % · PC {4} % · {5}",
                                        {std::to_wstring((GetTickCount64() - g_cpuPeakAt) / 1000), cores, game, pc, during})).c_str());
        if (ImGui::SmallButton(UI(Tr(L"Reset the peak")).c_str())) {
            g_cpuPeak = {};
            g_cpuAtPeak = {};
            g_cpuPeakAt = 0;
        }
    }
    ImGui::Spacing();
}
}  // namespace

void RenderTranslatorLive() {
    std::vector<MtRun> runs;
    std::wstring name, dataDir;
    int quality, threadsSet;
    bool stepDown, local;
    std::shared_ptr<Translator> translator;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        runs.assign(g_mtRuns.begin(), g_mtRuns.end());
        quality = g_cfg.localQuality;
        threadsSet = g_cfg.mtThreads;
        stepDown = g_cfg.beamStepDown;
        dataDir = g_cfg.dataDir;
        name = g_translator ? g_translator->Name() : std::wstring(L"-");
        local = IsLocalMtTranslator(g_translator);
        translator = g_translator;
    }
    const LocalMtSummary summary = LocalMtNow(dataDir);
    const float fs = ImGui::GetFontSize(), col = fs * 8.0f;
    const unsigned logical = std::max(1u, std::thread::hardware_concurrency());
    const size_t waitingNow = g_worker.QueueSize();
    const auto row = [&](const std::wstring& label) {
        ImGui::TextDisabled("%s", UI(label).c_str());
        ImGui::SameLine(col);
    };
    RenderCpuLoad();
    ImGui::TextUnformatted(UI(name).c_str());
    if (local && !summary.helper.build.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("helper %s · proto %d", summary.helper.build.substr(0, 7).c_str(), summary.helper.proto);
        const gct::LocalMtCard card = gct::GetLocalMtCard(translator);
        if (card.known) {
            ImGui::SameLine();
            ImGui::TextDisabled("· %s", card.onCard ? card.name.c_str() : ("CPU: " + card.why).c_str());
        }
    }
    const MtRun* last = nullptr;
    for (auto it = runs.rbegin(); it != runs.rend() && !last; ++it)
        if (!it->cached && !it->greeting && it->kind != 'B') last = &*it;
    int loadMs = 0;
    for (const MtRun& r : runs)
        if (r.run.loadMs) loadMs = r.run.loadMs;
    if (local) {
        row(Tr(L"Set"));
        LiveCells(8, quality, ImVec4(0.55f, 0.75f, 0.95f, 1.f));
        ImGui::Text("Beam %d", quality);
        const int threadsNow = last && last->run.threads ? last->run.threads : threadsSet > 0 ? threadsSet : gct::LocalAutoThreads(quality, static_cast<int>(gct::PhysicalCores().size()));
        const int coresNow = last && last->run.cores ? last->run.cores : threadsNow;
        row(Tr(L"Threads"));
        LiveCells(static_cast<int>(std::min(logical, 16u)), threadsNow, ImVec4(0.55f, 0.75f, 0.95f, 1.f));
        ImGui::TextUnformatted(UI(TrF(L"{1} threads on {2} cores", {std::to_wstring(threadsNow), std::to_wstring(coresNow)})).c_str());
        row(Tr(L"Queue"));
        LiveCells(8, static_cast<int>(std::min<size_t>(waitingNow, 8)), UiAccent());
        const std::wstring waitingText = std::to_wstring(waitingNow);
        ImGui::TextUnformatted(UI(stepDown ? TrF(L"{1} waiting, steps down above 2", {waitingText})
                                           : TrF(L"{1} waiting, stepping down is off", {waitingText})).c_str());
    }
    if (!last) {
        ImGui::TextDisabled("%s", UI(Tr(L"Nothing translated yet.")).c_str());
    } else {
        ImGui::Spacing();
        const int used = last->run.beam ? last->run.beam : last->beamAsked;
        if (local) {
            row(Tr(L"Last line"));
            LiveCells(8, used, LiveColor(*last));
            const std::wstring how = last->beamAsked < last->beamSet ? TrF(L"stepped down: {1} lines were waiting", {std::to_wstring(last->waiting)}) : Tr(L"as set");
            ImGui::TextColored(LiveColor(*last), "Beam %d/%d · %s", used, last->beamSet, UI(how).c_str());
        }
        row(Tr(L"Time"));
        wchar_t perToken[32] = L"-";
        if (last->run.tokensOut > 0) swprintf_s(perToken, L"%.1f", static_cast<double>(last->run.modelMs) / last->run.tokensOut);
        ImGui::TextUnformatted(UI(TrF(L"line {1} ms · model {2} ms · {3} tokens · {4} ms per token · {5} requests",
                                      {std::to_wstring(last->ms), std::to_wstring(last->run.modelMs),
                                       std::to_wstring(last->run.tokensIn) + L"→" + std::to_wstring(last->run.tokensOut), perToken,
                                       std::to_wstring(last->run.requests)})).c_str());
        row(Tr(L"Quality string"));
        ImGui::TextUnformatted(UI(last->tag).c_str());
        if (last->scored) {
            row(Tr(L"Score"));
            char label[32];
            snprintf(label, sizeof(label), "%.2f", last->score);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, last->score < gct::kUnsureScore ? ImVec4(0.95f, 0.45f, 0.35f, 1.f) : ImVec4(0.45f, 0.85f, 0.45f, 1.f));
            ImGui::ProgressBar(std::clamp(static_cast<float>(1.0 + last->score / 2.0), 0.0f, 1.0f), ImVec2(fs * 10.0f, fs * 0.95f), label);
            ImGui::PopStyleColor();
        }
        const std::wstring flags = LiveFlags(*last);
        if (!flags.empty()) ImGui::TextColored(LiveColor(*last), "%s", UI(flags).c_str());
    }
    if (runs.empty()) return;
    // The last lines as bars: height the time, colour what happened.
    ImGui::Spacing();
    {
        const size_t n = std::min<size_t>(runs.size(), 60);
        const float w = std::max(fs * 10.0f, ImGui::GetContentRegionAvail().x), h = fs * 3.0f;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##mtbars", ImVec2(w, h));
        const bool hovered = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        long long most = 1;
        for (size_t i = runs.size() - n; i < runs.size(); ++i) most = std::max(most, runs[i].ms);
        const float bar = w / 60.0f;
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), ImGui::GetColorU32(ImGuiCol_FrameBg));
        for (size_t i = 0; i < n; ++i) {
            const MtRun& r = runs[runs.size() - n + i];
            const float x = p.x + w - (n - i) * bar;
            const float top = p.y + h - std::max(1.0f, h * static_cast<float>(r.ms) / static_cast<float>(most));
            dl->AddRectFilled(ImVec2(x + 1.0f, top), ImVec2(x + bar - 1.0f, p.y + h), ImGui::GetColorU32(LiveColor(r)));
            if (hovered && ImGui::GetIO().MousePos.x >= x && ImGui::GetIO().MousePos.x < x + bar)
                ImGui::SetTooltip("%lld ms · Beam %d/%d · %s\n%s", r.ms, r.run.beam ? r.run.beam : r.beamAsked, r.beamSet, UI(r.tag).c_str(), UI(LiveFlags(r)).c_str());
        }
        char most_[32];
        snprintf(most_, sizeof(most_), "%lld ms", most);
        dl->AddText(ImVec2(p.x + fs * 0.2f, p.y), ImGui::GetColorU32(ImGuiCol_TextDisabled), most_);
    }
    if (loadMs) ImGui::TextDisabled("%s", UI(TrF(L"Model load: {1} ms", {std::to_wstring(loadMs)})).c_str());
    int stepped = 0, seconds = 0, doubtful = 0, cached = 0;
    for (const MtRun& r : runs) {
        stepped += r.beamAsked < r.beamSet;
        seconds += r.second;
        doubtful += r.doubt;
        cached += r.cached;
    }
    ImGui::TextDisabled("%s", UI(TrF(L"Lines so far: {1} · stepped down {2} · second tries {3} · doubtful {4} · from the cache {5}",
                                     {std::to_wstring(runs.size()), std::to_wstring(stepped), std::to_wstring(seconds), std::to_wstring(doubtful), std::to_wstring(cached)})).c_str());
    ImGui::Spacing();
    ImGui::TextDisabled("%s", UI(Tr(L"Last lines (numbers only, never a text)")).c_str());
    const float at[] = {0.0f, 3.5f, 7.0f, 11.5f, 14.5f, 17.0f, 20.5f, 24.0f, 27.5f, 30.0f};
    const char* const heads[] = {"ago", "kind", "lang", "beam", "thr", "ms", "model", "tok", "req", "score"};
    for (int c = 0; c < 10; ++c) {
        if (c) ImGui::SameLine(at[c] * fs);
        ImGui::TextDisabled("%s", heads[c]);
    }
    const ULONGLONG nowTick = GetTickCount64();
    int shown = 0;
    for (auto it = runs.rbegin(); it != runs.rend() && shown < 12; ++it, ++shown) {
        const MtRun& r = *it;
        const ImVec4 color = LiveColor(r);
        char cell[10][48];
        snprintf(cell[0], sizeof(cell[0]), "%llus", static_cast<unsigned long long>((nowTick - r.at) / 1000));
        snprintf(cell[1], sizeof(cell[1]), "%s", r.kind == 'C' ? "chat" : r.kind == 'I' ? "input" : "back");
        snprintf(cell[2], sizeof(cell[2]), "%s>%s", U8(r.from).c_str(), U8(r.to).c_str());
        snprintf(cell[3], sizeof(cell[3]), "%d/%d", r.run.beam ? r.run.beam : r.beamAsked, r.beamSet);
        snprintf(cell[4], sizeof(cell[4]), "%d", r.run.threads);
        snprintf(cell[5], sizeof(cell[5]), "%lld", r.ms);
        snprintf(cell[6], sizeof(cell[6]), "%d", r.run.modelMs);
        snprintf(cell[7], sizeof(cell[7]), "%d>%d", r.run.tokensIn, r.run.tokensOut);
        snprintf(cell[8], sizeof(cell[8]), "%d", r.run.requests);
        snprintf(cell[9], sizeof(cell[9]), r.scored ? "%.2f" : "-", r.score);
        for (int c = 0; c < 10; ++c) {
            if (c) ImGui::SameLine(at[c] * fs);
            ImGui::TextColored(color, "%s", cell[c]);
        }
        const std::wstring flags = LiveFlags(r);
        if (!flags.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", UI(flags).c_str());
    }
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
    if (ImGui::TreeNode(UI(Tr(L"Translator live") + L"###mtlive").c_str())) {
        RenderTranslatorLive();
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
        const gct::LocalMtCard card = gct::GetLocalMtCard(g_translator);
        if (card.known) {
            ss << "- Model runs on: " << (card.onCard ? "card" : "CPU") << " | " << card.name << " | driver " << card.driver << " | " << card.freeMb << " of " << card.totalMb
               << " MB free, needs " << card.needMb << " MB | " << card.why << " " << card.detail << "\n";
        }
    }
    ss << "\n";

    ss << "[Translation & Inference Engine]\n";
    ss << "- Configured Engine: ";
    switch (g_cfg.engine) {
        case Engine::Auto: ss << "Auto: bester vorhandener Schlüssel, sonst MyMemory\n"; break;
        case Engine::Local: ss << "Local OPUS-MT (CTranslate2 INT8 Native)\n"; break;
        case Engine::Nllb: ss << "Local NLLB-200 (CTranslate2 INT8 Native)\n"; break;
        case Engine::M2m: ss << "Local M2M100 (CTranslate2 Native)\n"; break;
        case Engine::DeepL: ss << "DeepL API\n"; break;
        case Engine::Google: ss << "Google Cloud Translation API\n"; break;
        case Engine::Microsoft: ss << "Microsoft Azure Translator\n"; break;
        case Engine::Supertext: ss << "Supertext API\n"; break;
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
    const int effectiveBeam = g_cfg.beamStepDown ? g_orchestrator.ComputeBeamSize(static_cast<gct::QualityMode>(g_cfg.localQuality), qDepth) : g_cfg.localQuality;
    ss << "- Smart-Sieve Status: " << (g_orchestrator.IsPaused() ? "PAUSED (Panic Switch Ctrl+Alt+T)" : "ACTIVE") << "\n";
    ss << "- Configured Quality Mode: Beam " << g_cfg.localQuality << (g_cfg.beamStepDown ? " (steps down under load)" : " (never steps down)") << "\n";
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

    // What the tech palette knows, for a test protocol: numbers, settings and times, never a chat text or a name.
    ss << "\n--- Translator settings ---\n";
    ss << "- Engine: " << U8(EngineLabel(g_cfg.engine)) << ", beam " << g_cfg.localQuality << ", threads " << g_cfg.mtThreads << " (0 automatic: "
       << gct::LocalAutoThreads(g_cfg.localQuality, static_cast<int>(gct::PhysicalCores().size())) << "), GPU switch " << (g_cfg.useGpu ? 1 : 0)
       << ", versions " << g_cfg.versions << ", step down " << (g_cfg.beamStepDown ? 1 : 0) << ", step up " << (g_cfg.beamStepUp ? 1 : 0)
       << ", translate along " << (g_cfg.thinkAlong ? 1 : 0) << "\n";
    ss << "\n--- CPU, percent of all processors: now / peak of this session ---\n" << Fixed{1};
    ss << "- helper " << static_cast<double>(g_cpuNow.helper) << " / " << static_cast<double>(g_cpuPeak.helper) << ", voice " << static_cast<double>(g_cpuNow.voice) << " / " << static_cast<double>(g_cpuPeak.voice)
       << ", threads " << static_cast<double>(g_cpuNow.threads) << " / " << static_cast<double>(g_cpuPeak.threads) << ", drawing " << static_cast<double>(g_cpuNow.draw) << " / " << static_cast<double>(g_cpuPeak.draw)
       << ", we " << static_cast<double>(g_cpuNow.ours) << " / " << static_cast<double>(g_cpuPeak.ours) << ", game " << static_cast<double>(g_cpuNow.game) << " / " << static_cast<double>(g_cpuPeak.game)
       << ", PC " << static_cast<double>(g_cpuNow.pc) << " / " << static_cast<double>(g_cpuPeak.pc) << "\n";
    ss << "- keys " << g_keys.total << ", Backspace " << g_keys.presses[VK_BACK] << ", Space wrote " << g_keys.spaceWrote << ", undone " << g_keys.spaceUndone
       << ", kept in the buffer " << g_keys.buffered << " in " << g_keys.bufferRuns << " sends\n";
    ss << "\n--- Logbook, the last entries (seconds ago) ---\n";
    {
        const std::vector<TechEntry> entries = TechLogNow();
        const ULONGLONG nowTick = GetTickCount64();
        int left = 150;
        for (auto it = entries.rbegin(); it != entries.rend() && left > 0; ++it) {
            if (it->area == 'C') continue;  // the chat's lines: only how many, below
            ss << static_cast<long long>((nowTick - it->at) / 1000) << "s [" << std::string(1, it->area) << "] " << U8(it->text) << "\n";
            --left;
        }
    }
    return ss.str();
}

}  // namespace kb
