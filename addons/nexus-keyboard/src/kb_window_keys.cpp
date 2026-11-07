// kb_window_keys.cpp — the main window's keys: what was typed during a send, the layers of Esc, the voice's automatic Enter.

#include "kb_shared.hpp"
#include "imgui/imgui_internal.h"  // GetActiveID, ClearActiveID

namespace kb {

void ProcessBufferedKeys() {
    // Your keys since a send let go of the keyboard go into our input once it is done – or once it stopped moving
    // for longer than BufferSafetyMs. Never thrown away.
    if (g_bufferingKeys.load()) {
        const bool stalled = GetTickCount64() - g_bufferingStartTime.load() >= static_cast<ULONGLONG>(BufferSafetyMs());
        if (!stalled && (g_sendInFlight.load() || g_sending)) return;
    } else {
        std::lock_guard<std::mutex> lock(g_keyBufferMu);
        if (g_keyBuffer.empty()) return;  // else: kept keys of a send that stopped moving
    }
    std::vector<BufferedKey> keys;
    bool replayToInput = false;
    {
        std::lock_guard<std::mutex> lock(g_keyBufferMu);
        keys = std::move(g_keyBuffer);
        g_keyBuffer.clear();
        g_bufferingKeys = false;
        replayToInput = g_bufferHadInputFocus.load();
        g_bufferHadInputFocus = false;
    }
    if (!keys.empty()) {
        g_keys.buffered += static_cast<uint32_t>(keys.size());
        ++g_keys.bufferRuns;
        TechLog('K', std::to_wstring(keys.size()) + (replayToInput ? L" keys kept during a send went into the input" : L" keys kept during a send, no input had the keyboard"));
    }
    if (keys.empty() || !replayToInput) return;
    bool triggerSend = false;
    for (size_t i = 0; i < keys.size(); ++i) {
        const auto& k = keys[i];
        if (k.msg == WM_CHAR) {
            wchar_t ch = static_cast<wchar_t>(k.wp);
            if (ch >= 0xD800 && ch <= 0xDBFF && i + 1 < keys.size() && keys[i + 1].msg == WM_CHAR) {
                wchar_t low = static_cast<wchar_t>(keys[i + 1].wp);
                if (low >= 0xDC00 && low <= 0xDFFF) {
                    wchar_t pair[2] = {ch, low};
                    std::string u8 = ToUtf8(std::wstring(pair, 2));
                    strncat_s(g_input, sizeof(g_input), u8.c_str(), _TRUNCATE);
                    ++i;
                    continue;
                }
            }
            if (ch >= 32) {
                std::string u8 = ToUtf8(std::wstring(1, ch));
                strncat_s(g_input, sizeof(g_input), u8.c_str(), _TRUNCATE);
            }
        } else if (k.msg == WM_KEYDOWN) {
            if (k.wp == VK_BACK) {
                size_t len = std::strlen(g_input);
                if (len > 0) {
                    while (len > 0 && (static_cast<unsigned char>(g_input[len - 1]) & 0xC0) == 0x80) --len;
                    if (len > 0) --len;
                    g_input[len] = '\0';
                }
            } else if (k.wp == VK_RETURN) {
                triggerSend = true;
            }
        }
    }
    const std::string flat = Flat(g_input);
    g_flatInput = flat;
    g_wFlatInput = W(flat);
    if (triggerSend && g_input[0]) {
        StartSend();
    }
}

int EscLayer() {
    // 1: Popup / Untermenü / Dialog offen
    if (IsVoiceMenuOpen()) return 1;
    if (!g_openLink.empty()) return 1;
    if (ImGui::GetCurrentContext() && ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) return 1;

    // 2: Aufnahme oder Zuhören läuft
    if (g_recording || g_micLocked) return 2;

    // 3: Hands free wartet aufs Senden
    if (g_autoSendPending) return 3;

    // 4: Eingabefeld hat Fokus
    if (g_edit.active || g_focusInput) return 4;
    if (ImGui::GetCurrentContext() && ImGui::GetActiveID() != 0) return 4;

    // 5: sonst
    return 5;
}

bool HandleEsc() {
    const int layer = EscLayer();
    if (layer == 1) {
        {
            std::lock_guard<std::mutex> lock(g_mu);
            CloseVoiceMenu();
            g_openLink.clear();
        }
        if (ImGui::GetCurrentContext()) {
            ImGui::ClosePopupToLevel(0, true);
        }
        return true;
    }
    if (layer == 2) {
        {
            std::lock_guard<std::mutex> lock(g_mu);
            g_voiceStoppedManually = true;
        }
        TalkUp();
        return true;
    }
    if (layer == 3) {
        {
            std::lock_guard<std::mutex> lock(g_mu);
            CancelAutoEnter();
        }
        return true;
    }
    if (layer == 4) {
        {
            std::lock_guard<std::mutex> lock(g_mu);
            g_focusInput = false;
            g_edit.active = false;
        }
        if (ImGui::GetCurrentContext()) {
            ImGui::ClearActiveID();
        }
        if (g_gameHwnd) {
            SetFocus(g_gameHwnd);
        }
        return true;
    }
    return false;
}

void CheckAutoSend() {
    if (!IsAutoEnterPending() || g_edit.active || !g_input[0]) return;
    if (g_gameChatOpen && !g_sending) {
        CancelAutoEnter();
        SetStatus(Tr(L"Auto-send cancelled: GW2 chat is open"), 4000);
        return;
    }
    static ULONGLONG s_lastAutoSend = 0;
    const ULONGLONG now = GetTickCount64();
    if (Translating() && !SendOriginal() && !IsPlainCommand(g_wFlatInput)) {  // a command goes as it is, no waiting
        if (g_lastErrorKind != TranslateErrorKind::None) {
            CancelAutoEnter();
            SetStatus(Tr(L"Auto-send cancelled: translation failed"), 6000);
            return;
        }
        if (g_previewGen != g_inputGen || g_preview.empty()) {
            if (g_inputChanged) {
                g_inputChanged = 0;
                g_worker.Post([gen = g_inputGen, t = SanitizeChatText(g_wFlatInput)] { TranslateInput(gen, t); });
            }
            SetStatus(Tr(L"Waiting for translation …"), 1000);
            return;  // translation still in flight
        }
    }
    if (now - s_lastAutoSend < 2000) {
        SetStatus(Tr(L"Auto-send rate limited (max 1 per 2 s)"), 1000);
        return;
    }
    s_lastAutoSend = now;
    CancelAutoEnter();
    StartSend();
}

}  // namespace kb
