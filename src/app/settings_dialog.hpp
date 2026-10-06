// settings_dialog.hpp — every setting in one window (no ini editing needed),
// and the guided first-start setup. Native Windows controls; mirrored for
// right-to-left UI languages.
#pragma once

#include <windows.h>

#include <functional>
#include <string>

#include "app/config.hpp"

namespace gct {

struct DialogResult {
    enum class Action { None, PickRegion, CoverChat, RunSetup, RestartInto };
    bool saved = false;          // cfg was changed and should be applied
    Action action = Action::None;
    std::wstring restartExe;     // RestartInto: start this exe and quit
    bool markChatAfterRestart = false;  // ... and let it mark the chat area (--mark-chat)
    bool coverAfterRestart = false;     // ... and lay itself over the chat (--cover-chat)
};

struct DialogContext {
    // Live text for "Connections": GW2 window, MumbleLink, add-ons, OCR, translator.
    std::function<std::wstring()> connectionStatus;
    // Live preview while a slider moves: opacity (alpha 0..255), text size (percent).
    std::function<void(int opacity, int fontPercent)> preview;
    // True when GW2 is currently running.
    std::function<bool()> isGw2Running;
    // Deletes everything learned from sent messages (all languages).
    std::function<void()> forgetLearned;
};

enum class SettingsPage { General = 0, Reading, Writing, Translator, Game };

// Modal. `cfg` is changed only when the user confirms.
DialogResult ShowSettingsDialog(HWND owner, HINSTANCE inst, Config& cfg, const DialogContext& ctx,
                                SettingsPage start = SettingsPage::General);

// Modal three-step setup: language & install, preparing the GW2 chat,
// marking the chat area. Sets cfg.setupDone.
DialogResult ShowSetupWizard(HWND owner, HINSTANCE inst, Config& cfg, const DialogContext& ctx);

}  // namespace gct
