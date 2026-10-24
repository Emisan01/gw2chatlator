// kb_local_mt.cpp — OPUS-MT helper and language packages, installed or removed only on the user's click.
#include "kb_shared.hpp"

#include <filesystem>

namespace kb {

std::atomic<bool> g_localMtBusy{false};
std::atomic<int> g_localMtPct{-1};
std::thread g_localMtThread;

namespace {
enum class LocalMtJob { InstallModule, RemoveModule, InstallModel, RemoveModel };

struct LocalMtState {
    bool module = false;
    bool nllbInstalled = false;
    uint64_t nllbBytes = 0;
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
    s_state.nllbInstalled = gct::LocalMtModelPresent(dataDir, "nllb-200");
    s_state.nllbBytes = s_state.nllbInstalled ? FolderBytes(dataDir + L"\\translate\\nllb-200") : 0;
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

void StartLocalMtJob(LocalMtJob job, std::wstring dataDir, std::string pair = {}) {
    if (g_localMtBusy.exchange(true)) return;
    if (g_localMtThread.joinable()) g_localMtThread.join();
    g_localMtPct = (job == LocalMtJob::InstallModule || job == LocalMtJob::InstallModel) ? 0 : -1;
    g_localMtThread = std::thread([job, dataDir = std::move(dataDir), pair = std::move(pair)] {
        ExtrasInstallResult result;
        bool removed = false;
        const bool guarded = Shielded([&] {
            try {
                const auto progress = [](uint64_t done, uint64_t total) {
                    if (total) g_localMtPct = static_cast<int>(done * 100 / total);
                    return !g_disabled.load();
                };
                switch (job) {
                    case LocalMtJob::InstallModule:
                        result = gct::InstallLocalMtModule(KeyboardDir(), W(GCT_VERSION_STR), progress);
                        break;
                    case LocalMtJob::RemoveModule:
                        for (int i = 0; i < 10 && !removed; ++i) {
                            removed = gct::RemoveLocalMtModule(KeyboardDir());
                            if (!removed) Sleep(300);
                        }
                        break;
                    case LocalMtJob::InstallModel:
                        result = gct::InstallLocalMtModel(dataDir, pair, W(GCT_VERSION_STR), progress);
                        break;
                    case LocalMtJob::RemoveModel:
                        for (int i = 0; i < 10 && !removed; ++i) {
                            removed = gct::RemoveLocalMtModel(dataDir, pair);
                            if (!removed) Sleep(300);
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
        g_localMtPct = -1;
        g_localMtBusy = false;
        std::wstring status;
        if (job == LocalMtJob::RemoveModule || job == LocalMtJob::RemoveModel) {
            status = removed ? Tr(L"Local translation files removed") : TrF(L"Removing {1} failed", {LocalMtJobName(job)});
        } else if (result.code == ExtrasInstallCode::Ok) {
            status = job == LocalMtJob::InstallModule ? Tr(L"Local translation module ready") : Tr(L"Language model installed");
        } else if (result.code == ExtrasInstallCode::NoAnswer) {
            status = TrF(L"GitHub did not answer ({1}) – please try again later", {result.detail});
        } else {
            status = TrF(L"Loading {1} failed: {2}", {LocalMtJobName(job), result.detail});
        }
        std::lock_guard<std::mutex> lock(g_mu);
        SetStatus(status, 10000);
    });
}

std::string PairLabel(const gct::LocalMtModelInfo& model) {
    const std::string pair(model.pair);
    const size_t dash = pair.find('-');
    if (dash == std::string::npos) return pair;
    return U8(NativeName(W(pair.substr(0, dash)))) + " -> " + U8(NativeName(W(pair.substr(dash + 1))));
}
}  // namespace

void RenderLocalMtOptions(Config& edit) {
    RefreshLocalMtState(edit.dataDir);
    bool inUse = false;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        inUse = g_cfg.engine == Engine::Local || g_cfg.engine == Engine::Nllb;
    }

    ImGui::TextUnformatted(UI(Tr(L"Local translation module")).c_str());
    const int pct = g_localMtPct.load();
    StatusRow(UI(Tr(L"Local translation module")),
              g_localMtBusy ? 1 : s_state.module ? 2 : 0,
              UI(g_localMtBusy && pct >= 0 ? TrF(L"loading … {1} %", {std::to_wstring(pct)})
                   : g_localMtBusy ? Tr(L"working …")
                   : s_state.module ? Tr(L"installed") : Tr(L"missing")));
    if (!s_state.module && !g_localMtBusy) {
        ImGui::SameLine();
        if (ImGui::SmallButton(UI(Tr(L"Install local translation module")).c_str()))
            StartLocalMtJob(LocalMtJob::InstallModule, edit.dataDir);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", UI(Tr(L"Downloads the isolated CPU helper from this version's GitHub release. SHA-256 is checked." )).c_str());
    } else if (s_state.module && !g_localMtBusy) {
        if (inUse) {
            ImGui::TextDisabled("%s", UI(Tr(L"Choose another translator and save before removing local translation files.")).c_str());
        } else if (RemoveButton("localmtmodule")) {
            StartLocalMtJob(LocalMtJob::RemoveModule, edit.dataDir);
        }
    }

    ImGui::Separator();
    ImGui::TextUnformatted(UI(Tr(L"Universal model (NLLB-200)")).c_str());
    ImGui::TextDisabled("%s", UI(Tr(L"Meta NLLB-200-distilled-600M - direct translation between 200+ languages without English pivot.")).c_str());
    StatusRow(UI(Tr(L"Universal model")),
              g_localMtBusy ? 1 : s_state.nllbInstalled ? 2 : 0,
              UI(s_state.nllbInstalled ? TrF(L"installed ({1} MB)", {Mb(s_state.nllbBytes)})
                                       : Tr(L"missing (about 600 MB)")));
    if (!g_localMtBusy) {
        if (s_state.nllbInstalled) {
            if (inUse) {
                ImGui::TextDisabled("%s", UI(Tr(L"Choose another translator and save before removing local translation files.")).c_str());
            } else {
                static std::string s_armedNllb;
                static ULONGLONG s_untilNllb = 0;
                const bool armed = s_armedNllb == "nllb-200" && GetTickCount64() < s_untilNllb;
                if (ImGui::SmallButton((UI(armed ? Tr(L"Click again to remove") : Tr(L"Remove")) + "##localmtnllb").c_str())) {
                    if (armed) {
                        s_armedNllb.clear();
                        StartLocalMtJob(LocalMtJob::RemoveModel, edit.dataDir, "nllb-200");
                    } else {
                        s_armedNllb = "nllb-200";
                        s_untilNllb = GetTickCount64() + 4000;
                    }
                }
            }
        } else if (s_state.module) {
            if (ImGui::SmallButton(UI(Tr(L"Download NLLB-200 universal model")).c_str()))
                StartLocalMtJob(LocalMtJob::InstallModel, edit.dataDir, "nllb-200");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", UI(Tr(L"Downloads the INT8 NLLB-200 package for direct many-to-many translation.")).c_str());
        } else {
            ImGui::TextDisabled("%s", UI(Tr(L"Install the local translation module first.")).c_str());
        }
    }

    ImGui::Separator();
    ImGui::TextUnformatted(UI(Tr(L"Inference Quality & Speed")).c_str());
    const char* qualityItems[] = {
        "Fast / Beam 1 (~350ms, lowest CPU)",
        "Balanced / Beam 2 (~500ms, recommended)",
        "Quality / Beam 4 (~900ms, maximum nuance)"
    };
    int currentQ = edit.localQuality == 1 ? 0 : edit.localQuality == 4 ? 2 : 1;
    ImGui::SetNextItemWidth(300);
    if (ImGui::Combo(UI(Tr(L"Quality Mode") + L"##localmtquality").c_str(), &currentQ, qualityItems, 3)) {
        edit.localQuality = currentQ == 0 ? 1 : currentQ == 2 ? 4 : 2;
    }
    ImGui::TextDisabled("%s", UI(Tr(L"Under heavy chat load, the orchestrator automatically steps down to Beam 1.")).c_str());

    ImGui::Separator();
    ImGui::TextUnformatted(UI(Tr(L"OPUS-MT language models")).c_str());
    const auto& catalog = gct::LocalMtModelCatalog();
    static int selected = 0;
    selected = std::clamp(selected, 0, static_cast<int>(catalog.size()) - 1);
    static std::vector<std::string> labels;
    if (labels.size() != catalog.size()) {
        labels.clear();
        for (const auto& model : catalog) labels.push_back(PairLabel(model));
    }
    ImGui::SetNextItemWidth(260);
    if (ImGui::BeginCombo(UI(Tr(L"Select model pair") + L"##localmtpair").c_str(), labels[selected].c_str())) {
        for (int i = 0; i < static_cast<int>(catalog.size()); ++i) {
            const bool active = selected == i;
            if (ImGui::Selectable((labels[i] + " (" + catalog[i].pair + ")").c_str(), active)) selected = i;
            if (active) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    const auto& model = catalog[selected];
    ImGui::TextDisabled("%s  -  %s", model.model, model.license);  // ASCII: Nexus' font lacks the bullet
    // its size and where it lives; the installed pairs with their sizes (user, 2026-10-22)
    ImGui::TextDisabled("%s", UI(s_state.models[selected]
                                     ? TrF(L"Size on disk: {1} MB", {Mb(s_state.bytes[selected])})
                                     : TrF(L"Download: about {1} MB", {L"75"})).c_str());
    ImGui::TextDisabled("%s", UI(TrF(L"Installed in: {1}", {edit.dataDir + L"\\translate\\" + FromUtf8(model.pair)})).c_str());
    size_t installed = 0;
    uint64_t total = 0;
    std::wstring list;
    for (size_t i = 0; i < catalog.size(); ++i) {
        if (!s_state.models[i]) continue;
        ++installed;
        total += s_state.bytes[i];
        list += (list.empty() ? L"" : L", ") + FromUtf8(catalog[i].pair) + L" (" + Mb(s_state.bytes[i]) + L" MB)";
    }
    ImGui::TextDisabled("%s", UI(TrF(L"Installed pairs: {1} of {2}",
                                      {std::to_wstring(installed), std::to_wstring(catalog.size())})).c_str());
    if (!list.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", UI(list + L"  -  " + TrF(L"together {1} MB", {Mb(total)})).c_str());
        ImGui::PopTextWrapPos();
    }

    if (g_localMtBusy) return;
    if (s_state.models[selected]) {
        if (inUse) {
            ImGui::TextDisabled("%s", UI(Tr(L"Choose another translator and save before removing local translation files.")).c_str());
        } else {
            static std::string s_armed;
            static ULONGLONG s_until = 0;
            const bool armed = s_armed == model.pair && GetTickCount64() < s_until;
            if (ImGui::SmallButton((UI(armed ? Tr(L"Click again to remove") : Tr(L"Remove")) + "##localmtmodel").c_str())) {
                if (armed) {
                    s_armed.clear();
                    StartLocalMtJob(LocalMtJob::RemoveModel, edit.dataDir, model.pair);
                } else {
                    s_armed = model.pair;
                    s_until = GetTickCount64() + 4000;
                }
            }
        }
    } else if (s_state.module) {
        if (ImGui::SmallButton(UI(Tr(L"Download selected model")).c_str()))
            StartLocalMtJob(LocalMtJob::InstallModel, edit.dataDir, model.pair);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", UI(Tr(L"Model packages must be present in the current GitHub release." )).c_str());
    } else {
        ImGui::TextDisabled("%s", UI(Tr(L"Install the local translation module first.")).c_str());
    }
}

}  // namespace kb
