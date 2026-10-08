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
    // Live text for the technical page: parameters and numbers (no chat text).
    std::function<std::wstring()> technicalStatus;
    // Live preview while a slider moves: opacity (alpha 0..255), text size (percent).
    std::function<void(int opacity, int fontPercent, const std::wstring& fontFace)> preview;
    // True when GW2 is currently running.
    std::function<bool()> isGw2Running;
    // Deletes everything learned from sent messages (all languages).
    std::function<void()> forgetLearned;
    // Correction memory: "12 lines, 3 phrases"; delete all; export / import (merge) a file. Return a status text.
    std::function<std::wstring()> correctionsInfo;
    std::function<void()> clearCorrections;
    std::function<std::wstring(const std::wstring& path)> exportCorrections;
    std::function<std::wstring(const std::wstring& path)> importCorrections;
    // "My words" (slang with its meaning): the list as text, and storing an edited one.
    std::function<std::wstring()> myWordsText;
    std::function<void(const std::wstring&)> setMyWords;
    // Learns your own texts (a .txt file) into the typing profile; returns a status text.
    std::function<std::wstring(const std::wstring& path)> learnFromFile;
    // Learned recognition fixes ("rnain = main") as text, and storing an edited list.
    std::function<std::wstring()> ocrFixesText;
    std::function<void(const std::wstring&)> setOcrFixes;
    // Game / screen resolution and which text recognition fits it (for the reading page).
    std::function<std::wstring()> readingAdvice;
    // One picture of the chat through every text recognition; the result (text) is posted to `notify` as
    // LPARAM = new std::wstring.
    std::function<void(HWND notify, UINT message)> compareOcr;
};

enum class SettingsPage { General = 0, Reading, Writing, Translator, Game, Technical };

// Modal. `cfg` is changed only when the user confirms.
DialogResult ShowSettingsDialog(HWND owner, HINSTANCE inst, Config& cfg, const DialogContext& ctx,
                                SettingsPage start = SettingsPage::General);

// Modal three-step setup: language & install, preparing the GW2 chat,
// marking the chat area. Sets cfg.setupDone.
DialogResult ShowSetupWizard(HWND owner, HINSTANCE inst, Config& cfg, const DialogContext& ctx);

// Modal: shows the original, lets you edit the translation. True when you confirmed a non-empty text.
bool AskCorrection(HWND owner, HINSTANCE inst, const std::wstring& original, std::wstring* translation);
// Modal: what one of your words means ("finds" -> "finde es"). True when confirmed (meaning may be empty).
bool AskWordMeaning(HWND owner, HINSTANCE inst, const std::wstring& word, std::wstring* meaning);
// Modal: the whole list of your words, "word = meaning" per line. True when confirmed.
bool EditMyWords(HWND owner, HINSTANCE inst, std::wstring* text);
// Modal: the learned recognition fixes, "as read = correct" per line. True when confirmed.
bool EditOcrFixes(HWND owner, HINSTANCE inst, std::wstring* text);

}  // namespace gct
