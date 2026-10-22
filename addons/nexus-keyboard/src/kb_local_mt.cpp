// kb_local_mt.cpp — OPUS-MT helper and language packages, installed or removed only on the user's click.
#include "kb_shared.hpp"

#include <filesystem>

namespace kb {

std::atomic<bool> g_localMtBusy{false};
std::atomic<int> g_localMtPct{-1};
std::thread g_localMtThread;
std::wstring g_localMtBusyText;
std::wstring g_localMtStatus;
ULONGLONG g_localMtStatusUntil = 0;

namespace {
enum class LocalMtJob { InstallModule, RemoveModule, InstallModel, RemoveModel };

struct LocalMtTask {
    LocalMtJob job;
    std::string pair;
    std::wstring label;
};

struct LocalMtState {
    bool module = false;
    std::vector<bool> models;
    std::vector<uint64_t> bytes;  // each installed pair's folder on disk
    ULONGLONG nextCheck = 0;
} s_state;

uint64_t FolderBytes(const std::wstring& dir) {  // everything below it; 0 when missing
    std::error_code ec;
    uint64_t sum = 0;
    for (auto it = std::filesystem::recursive_directory_iterator(dir, ec);
         !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
        if (it->is_regular_file(ec)) sum += it->file_size(ec);
    return sum;
}
std::wstring Mb(uint64_t bytes) { return std::to_wstring((bytes + 512 * 1024) / (1024 * 1024)); }

void RefreshLocalMtState(const std::wstring& dataDir) {
    static bool wasBusy = false;
    const bool busy = g_localMtBusy.load();
    if (wasBusy != busy) {
        s_state.nextCheck = 0;
        wasBusy = busy;
    }
    if (GetTickCount64() < s_state.nextCheck) return;
    s_state.nextCheck = GetTickCount64() + 2000;
    s_state.module = gct::LocalMtModulePresent(KeyboardDir());
    const auto& catalog = gct::LocalMtModelCatalog();
    s_state.models.assign(catalog.size(), false);
    s_state.bytes.assign(catalog.size(), 0);
    for (size_t i = 0; i < catalog.size(); ++i) {
        s_state.models[i] = gct::LocalMtModelPresent(dataDir, catalog[i].pair);
        if (s_state.models[i]) s_state.bytes[i] = FolderBytes(dataDir + L"\\translate\\" + FromUtf8(catalog[i].pair));
    }
}

std::wstring LocalMtJobName(LocalMtJob job) {
    switch (job) {
        case LocalMtJob::InstallModule: return Tr(L"the local translation module");
        case LocalMtJob::RemoveModule: return Tr(L"the local translation module");
        case LocalMtJob::InstallModel: return Tr(L"the selected language model");
        case LocalMtJob::RemoveModel: return Tr(L"the selected language model");
    }
    return Tr(L"local translation files");
}

void StartLocalMtTasks(std::vector<LocalMtTask> tasks, std::wstring dataDir, std::wstring bundleName = {}) {
    bool expected = false;
    if (!g_localMtBusy.compare_exchange_strong(expected, true)) return;
    if (g_localMtThread.joinable()) g_localMtThread.join();
    g_localMtPct = 0;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        g_localMtStatus.clear();
        g_localMtStatusUntil = 0;
        g_localMtBusyText.clear();
    }
    g_localMtThread = std::thread([tasks = std::move(tasks), dataDir = std::move(dataDir), bundleName = std::move(bundleName)] {
        ExtrasInstallResult lastResult;
        bool allOk = true;
        std::wstring lastItemName;
        const size_t totalTasks = tasks.size();

        for (size_t i = 0; i < tasks.size(); ++i) {
            const auto& t = tasks[i];
            lastItemName = t.pair.empty() ? LocalMtJobName(t.job) : FromUtf8(t.pair);
            {
                std::lock_guard<std::mutex> lock(g_mu);
                if (!bundleName.empty()) {
                    g_localMtBusyText = TrF(L"{1}: Pair {2} of {3} – {4}",
                                            {bundleName, std::to_wstring(i + 1), std::to_wstring(totalTasks), FromUtf8(t.pair)});
                } else if (!t.pair.empty()) {
                    g_localMtBusyText = TrF(L"Loading {1} …", {FromUtf8(t.pair)});
                } else {
                    g_localMtBusyText = Tr(L"Installing local translation module …");
                }
            }

            ExtrasInstallResult result;
            bool removed = false;
            const bool guarded = Shielded([&] {
                try {
                    const auto progress = [](uint64_t done, uint64_t total) {
                        if (total) g_localMtPct = static_cast<int>(done * 100 / total);
                        return !g_disabled.load();
                    };
                    switch (t.job) {
                        case LocalMtJob::InstallModule:
                            result = gct::InstallLocalMtModule(KeyboardDir(), W(GCT_VERSION_STR), progress);
                            break;
                        case LocalMtJob::RemoveModule:
                            for (int r = 0; r < 10 && !removed; ++r) {
                                removed = gct::RemoveLocalMtModule(KeyboardDir());
                                if (!removed) Sleep(300);
                            }
                            if (!removed) {
                                result.code = ExtrasInstallCode::DownloadFailed;
                                result.detail = L"could not remove files";
                            }
                            break;
                        case LocalMtJob::InstallModel:
                            if (gct::LocalMtModelPresent(dataDir, t.pair)) {
                                result.code = ExtrasInstallCode::Ok;
                            } else {
                                result = gct::InstallLocalMtModel(dataDir, t.pair, W(GCT_VERSION_STR), progress);
                            }
                            break;
                        case LocalMtJob::RemoveModel:
                            for (int r = 0; r < 10 && !removed; ++r) {
                                removed = gct::RemoveLocalMtModel(dataDir, t.pair);
                                if (!removed) Sleep(300);
                            }
                            if (!removed) {
                                result.code = ExtrasInstallCode::DownloadFailed;
                                result.detail = L"could not remove files";
                            }
                            break;
                    }
                } catch (const std::exception& e) {
                    result.code = ExtrasInstallCode::DownloadFailed;
                    result.detail = W(e.what());
                } catch (...) {
                    result.code = ExtrasInstallCode::DownloadFailed;
                    result.detail = L"unexpected local package error";
                }
            });

            if (!guarded) {
                result.code = ExtrasInstallCode::DownloadFailed;
                result.detail = L"the operation stopped unexpectedly";
            }
            lastResult = result;
            if (result.code != ExtrasInstallCode::Ok) {
                allOk = false;
                break;
            }
        }

        g_localMtPct = -1;
        g_localMtBusy = false;
        std::wstring status;
        if (allOk) {
            if (!bundleName.empty()) {
                status = TrF(L"[OK] {1} installed", {bundleName});
            } else if (!lastItemName.empty()) {
                status = TrF(L"[OK] {1} installed", {lastItemName});
            } else {
                status = L"[OK] " + Tr(L"Language model installed");
            }
        } else {
            const std::wstring reason = lastResult.code == ExtrasInstallCode::NoAnswer
                                            ? TrF(L"GitHub did not answer ({1})", {lastResult.detail})
                                            : lastResult.detail;
            status = TrF(L"[--] Error: {1}", {reason});
        }

        {
            std::lock_guard<std::mutex> lock(g_mu);
            g_localMtBusyText.clear();
            g_localMtStatus = status;
            g_localMtStatusUntil = GetTickCount64() + 5000;
            SetStatus(status, 10000);
        }
    });
}

void StartLocalMtJob(LocalMtJob job, std::wstring dataDir, std::string pair = {}) {
    StartLocalMtTasks({{job, std::move(pair), {}}}, std::move(dataDir));
}

std::string PairLabel(const gct::LocalMtModelInfo& model) {
    const std::string pair(model.pair);
    const size_t dash = pair.find('-');
    if (dash == std::string::npos) return pair;
    return U8(NativeName(W(pair.substr(0, dash)))) + " -> " + U8(NativeName(W(pair.substr(dash + 1))));
}
}  // namespace

void RenderLocalMtModule(bool guide) {
    std::wstring dataDir;
    bool inUse = false;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        dataDir = g_cfg.dataDir;
        inUse = g_cfg.engine == Engine::Local;
    }
    RefreshLocalMtState(dataDir);

    const int pct = g_localMtPct.load();
    const bool busy = g_localMtBusy.load();

    StatusRow(UI(Tr(L"Local translation module")),
              busy ? 1 : s_state.module ? 2 : 0,
              UI(busy && pct >= 0 ? TrF(L"loading … {1} %", {std::to_wstring(pct)})
                   : busy ? Tr(L"working …")
                   : s_state.module ? Tr(L"installed") : Tr(L"missing")));

    if (!busy) {
        if (!s_state.module) {
            ImGui::SameLine();
            if (ImGui::SmallButton(UI(Tr(L"Install local translation module")).c_str()))
                StartLocalMtJob(LocalMtJob::InstallModule, dataDir);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", UI(Tr(L"Downloads the isolated CPU helper from this version's GitHub release. SHA-256 is checked." )).c_str());
        } else if (!guide) {
            if (inUse) {
                ImGui::TextDisabled("%s", UI(Tr(L"Choose another translator and save before removing local translation files.")).c_str());
            } else if (RemoveButton("localmtmodule")) {
                StartLocalMtJob(LocalMtJob::RemoveModule, dataDir);
            }
        }
    }
}

void RenderLocalMtOptions(const Config& edit) {
    RefreshLocalMtState(edit.dataDir);

    ImGui::TextUnformatted(UI(Tr(L"Local translation module")).c_str());
    RenderLocalMtModule(false);

    bool inUse = false;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        inUse = g_cfg.engine == Engine::Local;
    }

    const int pct = g_localMtPct.load();
    const bool busy = g_localMtBusy.load();

    std::wstring currentBusyText;
    std::wstring currentStatus;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        currentBusyText = g_localMtBusyText;
        if (g_localMtStatusUntil && GetTickCount64() < g_localMtStatusUntil) {
            currentStatus = g_localMtStatus;
        } else {
            g_localMtStatus.clear();
            g_localMtStatusUntil = 0;
        }
    }

    ImGui::Separator();
    ImGui::TextUnformatted(UI(Tr(L"OPUS-MT language models")).c_str());
    const auto& catalog = gct::LocalMtModelCatalog();

    // Progress bar or 5-second completion status
    if (busy) {
        if (!currentBusyText.empty()) {
            ImGui::TextUnformatted(UI(currentBusyText).c_str());
        }
        const float fraction = pct >= 0 ? (static_cast<float>(pct) / 100.0f) : 0.0f;
        std::string overlay = pct >= 0 ? (std::to_string(pct) + " %") : "";
        ImGui::ProgressBar(fraction, ImVec2(320.0f, 0.0f), overlay.empty() ? nullptr : overlay.c_str());
    } else if (!currentStatus.empty()) {
        const bool isOk = currentStatus.find(L"[OK]") != std::wstring::npos;
        ImGui::TextColored(isOk ? ImVec4(0.45f, 0.85f, 0.45f, 1.f) : ImVec4(0.95f, 0.55f, 0.35f, 1.f),
                           "%s", UI(currentStatus).c_str());
    }

    // Helper functions for pair discovery & bundle tasks
    auto isPairInstalled = [&](const std::string& pair) -> bool {
        for (size_t i = 0; i < catalog.size(); ++i) {
            if (catalog[i].pair == pair) return s_state.models[i];
        }
        return false;
    };

    auto makeBundleTasks = [&](const std::vector<std::string>& pairs) -> std::vector<LocalMtTask> {
        std::vector<LocalMtTask> tasks;
        for (const auto& p : pairs) {
            if (!isPairInstalled(p)) {
                tasks.push_back({LocalMtJob::InstallModel, p, {}});
            }
        }
        return tasks;
    };

    // My writing language pairs
    std::wstring myLangCode;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        myLangCode = gct::ToLowerAscii(gct::PrimaryLang(TypingLang()));
    }
    const std::string myLang = gct::ToUtf8(myLangCode);
    std::vector<std::string> myLangPairs;
    uint64_t myLangBytes = 0;
    if (!myLang.empty() && myLang != "en") {
        for (const auto& m : catalog) {
            if (m.pair == (myLang + "-en") || m.pair == ("en-" + myLang)) {
                myLangPairs.push_back(m.pair);
                myLangBytes += m.sizeBytes;
            }
        }
    }
    if (myLangPairs.empty()) {
        myLangBytes = 154121063; // ~147 MB default (de <-> en)
    }

    // Bundles
    std::vector<std::string> allPairs;
    uint64_t allPairsBytes = 0;
    for (const auto& m : catalog) {
        allPairs.push_back(m.pair);
        allPairsBytes += m.sizeBytes;
    }

    std::vector<std::string> westPairs = {
        "de-en", "en-de", "fr-en", "en-fr", "es-en", "en-es", "it-en", "en-it", "pt-en", "en-pt"
    };
    uint64_t westBytes = 0;
    for (const auto& m : catalog) {
        for (const auto& wp : westPairs) {
            if (m.pair == wp) westBytes += m.sizeBytes;
        }
    }

    std::vector<std::string> eastPairs = {
        "pl-en", "en-pl", "ru-en", "en-ru", "tr-en", "en-tr"
    };
    uint64_t eastBytes = 0;
    for (const auto& m : catalog) {
        for (const auto& ep : eastPairs) {
            if (m.pair == ep) eastBytes += m.sizeBytes;
        }
    }

    if (!busy && s_state.module) {
        // Button a: Download my language
        std::vector<LocalMtTask> myTasks = makeBundleTasks(myLangPairs);
        const bool myDone = myTasks.empty() && !myLangPairs.empty();
        const std::wstring myMbStr = std::to_wstring((myLangBytes + 512 * 1024) / (1024 * 1024));
        const std::wstring myBtnText = myDone
            ? Tr(L"My language (installed)")
            : TrF(L"Download my language (approx. {1} MB)", {myMbStr});
        if (!myDone) {
            if (ImGui::Button(UI(myBtnText).c_str())) {
                StartLocalMtTasks(myTasks, edit.dataDir, Tr(L"My language"));
            }
        } else {
            ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.f), "%s", UI(myBtnText).c_str());
        }

        ImGui::SameLine();

        // Button b: Download Europe package
        std::vector<LocalMtTask> europeTasks = makeBundleTasks(allPairs);
        const bool europeDone = europeTasks.empty();
        const std::wstring europeBtnText = europeDone
            ? Tr(L"Europe package (installed)")
            : Tr(L"Download Europe package (approx. 1.2 GB)");
        if (!europeDone) {
            if (ImGui::Button(UI(europeBtnText).c_str())) {
                StartLocalMtTasks(europeTasks, edit.dataDir, Tr(L"Europe package"));
            }
        } else {
            ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.f), "%s", UI(europeBtnText).c_str());
        }

        ImGui::TextDisabled("%s", UI(Tr(L"Translates any supported language to any other (via English).")).c_str());

        // Regional bundles
        std::vector<LocalMtTask> westTasks = makeBundleTasks(westPairs);
        const bool westDone = westTasks.empty();
        const std::wstring westMbStr = std::to_wstring((westBytes + 512 * 1024) / (1024 * 1024));
        const std::wstring westBtnText = westDone
            ? Tr(L"West & Central Europe (installed)")
            : TrF(L"West & Central Europe (approx. {1} MB)", {westMbStr});
        if (!westDone) {
            if (ImGui::Button(UI(westBtnText).c_str())) {
                StartLocalMtTasks(westTasks, edit.dataDir, Tr(L"West & Central Europe"));
            }
        } else {
            ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.f), "%s", UI(westBtnText).c_str());
        }

        ImGui::SameLine();

        std::vector<LocalMtTask> eastTasks = makeBundleTasks(eastPairs);
        const bool eastDone = eastTasks.empty();
        const std::wstring eastMbStr = std::to_wstring((eastBytes + 512 * 1024) / (1024 * 1024));
        const std::wstring eastBtnText = eastDone
            ? Tr(L"Eastern Europe (installed)")
            : TrF(L"Eastern Europe (approx. {1} MB)", {eastMbStr});
        if (!eastDone) {
            if (ImGui::Button(UI(eastBtnText).c_str())) {
                StartLocalMtTasks(eastTasks, edit.dataDir, Tr(L"Eastern Europe"));
            }
        } else {
            ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.f), "%s", UI(eastBtnText).c_str());
        }
    } else if (!s_state.module) {
        ImGui::TextDisabled("%s", UI(Tr(L"Install the local translation module first.")).c_str());
    }

    size_t installedCount = 0;
    uint64_t totalBytes = 0;
    for (size_t i = 0; i < catalog.size(); ++i) {
        if (!s_state.models[i]) continue;
        ++installedCount;
        totalBytes += s_state.bytes[i];
    }
    ImGui::TextDisabled("%s", UI(TrF(L"Installed pairs: {1} of {2}",
                                      {std::to_wstring(installedCount), std::to_wstring(catalog.size())})).c_str());

    // Single language pairs folded into TreeNode
    if (ImGui::TreeNode(UI(Tr(L"Single language pairs")).c_str())) {
        // Sorted: first all English -> X, then all X -> English
        std::vector<size_t> sortedIndices;
        for (size_t i = 0; i < catalog.size(); ++i) {
            if (std::string(catalog[i].pair).rfind("en-", 0) == 0) sortedIndices.push_back(i);
        }
        for (size_t i = 0; i < catalog.size(); ++i) {
            if (std::string(catalog[i].pair).rfind("en-", 0) != 0) sortedIndices.push_back(i);
        }

        for (size_t idx : sortedIndices) {
            const auto& m = catalog[idx];
            const bool isInstalled = s_state.models[idx];
            const std::string pairStr = m.pair;
            const uint64_t sz = isInstalled ? s_state.bytes[idx] : m.sizeBytes;
            const std::wstring szMb = Mb(sz);

            ImGui::PushID(pairStr.c_str());
            ImGui::AlignTextToFramePadding();
            if (isInstalled) {
                ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.f), "[OK]");
            } else {
                ImGui::TextColored(ImVec4(0.70f, 0.70f, 0.70f, 1.f), "[--]");
            }
            ImGui::SameLine();
            ImGui::Text("%s (%s)", PairLabel(m).c_str(), pairStr.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("(%s MB)", UI(szMb).c_str());

            if (!busy && s_state.module) {
                ImGui::SameLine();
                if (isInstalled) {
                    if (inUse) {
                        ImGui::TextDisabled("%s", UI(Tr(L"In use")).c_str());
                    } else if (RemoveButton("rmpair")) {
                        StartLocalMtJob(LocalMtJob::RemoveModel, edit.dataDir, pairStr);
                    }
                } else {
                    if (ImGui::SmallButton(UI(Tr(L"Download")).c_str())) {
                        StartLocalMtJob(LocalMtJob::InstallModel, edit.dataDir, pairStr);
                    }
                }
            }
            ImGui::PopID();
        }
        ImGui::TreePop();
    }
}

}  // namespace kb
