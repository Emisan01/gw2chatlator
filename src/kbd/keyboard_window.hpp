// keyboard_window.hpp — GW2 Keyboard: only the typing help, nothing of the chat is read.
//
//   InputBox (spelling, learned words, typo memory, Windows' prediction) ──> ProtectForTranslation ──> Translator
//     └─> PreviewView (+ back-translation in the language you write in) ──Enter──> SendToGw2Chat (timed by the fps)
//
// Same settings, learned words and corrections as the full GW2 Chat Translator (one data folder). Everything below
// this class is UI-free; everything in it runs on the UI thread. Worker threads only post results back.
#pragma once

#include <windows.h>

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "app/config.hpp"
#include "app/input_box.hpp"
#include "app/preview_view.hpp"
#include "app/settings_dialog.hpp"
#include "app/spell_service.hpp"
#include "app/suggestion_bar.hpp"
#include "app/theme.hpp"
#include "core/corrections.hpp"
#include "core/glossary.hpp"
#include "core/languagetool_protocol.hpp"
#include "core/my_words.hpp"
#include "core/translator.hpp"
#include "win/gw2_sender.hpp"
#include "win/mumble_link.hpp"
#include "win/online_translators.hpp"

namespace gct {

struct KbdTranslatedMsg;
struct KbdNamesMsg;
struct KbdGrammarMsg;

class KeyboardWindow {
public:
    // `cmdLine`: --wait-for-gw2 (started with Windows: hidden until GW2 is in the 3D world).
    int Run(HINSTANCE inst, const std::wstring& cmdLine);

private:
    enum class Tone { Muted, Ok, Warn, Error };

    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);

    // ---- start
    void InitServices();
    void WarmUp();  // spell checker, prediction, learned words, glossary – when the window is first needed
    void CreateChildren();
    void ChooseEngine(bool announce);
    std::shared_ptr<Translator> MakeEngine(Engine wanted, Engine* resolved, std::wstring* note);

    // ---- window
    void Layout();
    void ApplyDpi(int dpi, const RECT* suggested);
    void Paint();
    LRESULT HitTest(LPARAM lp) const;
    bool IsClickable(POINT pt) const;
    void OnClick(POINT pt);
    void InvalidateChrome();
    void SetStatus(const std::wstring& text, Tone tone, UINT autoClearMs = 0);
    bool BackgroundNoticeAllowed() const;
    void ShowWindowNow();
    void HideWindowNow();
    void ReturnToGame();
    bool Front(HWND h);

    // ---- languages, channel, menus
    std::wstring TypingLocale() const;  // the language you type in ("de-DE")
    bool SendOriginal() const;          // send what you typed, only corrected
    std::wstring SendLang() const;      // empty when sending the original
    bool Translating() const;           // send language differs from the typing language
    void SetSendIndex(size_t idx);
    void CycleSend();
    void ApplyTypingLanguage();
    void OnKeyboardLanguage(const std::wstring& locale);
    std::wstring ChannelChipText() const;
    std::wstring TypeChipText() const;
    std::wstring SendChipText() const;
    void ShowChannelMenu();
    void ShowTypeLangMenu();
    void ShowSendMenu();
    void ShowMainMenu();
    void ShowTour();
    void SetUiLanguage(UiLang lang);
    // "Activate chat translation": the full GW2 Chat Translator (reading the chat) takes over – started with reading
    // on, this window ends (both use the same settings and learned words).
    void ActivateChatTranslation();

    // ---- glossary (official GW2 names) and your words
    void RefreshGlossary();
    bool EnsureNames(const std::string& lang);
    void StartNameFetch(const std::string& lang);
    void OnNamesFetched(KbdNamesMsg* msg);
    void UpdateSpellWords();
    void ExplainWord(const std::wstring& word);

    // ---- writing, translating, sending
    std::wstring ComposePrefix() const;
    void OnInputChanged();
    void ResetCompose();
    void StartTranslation();
    void OnTranslated(KbdTranslatedMsg* msg);
    void StartBackTranslation();
    void Romanize();
    void SetPreviewBody(const std::wstring& prefix, const std::wstring& body);
    bool PreviewIsCurrent() const;
    void UpdatePreview();
    void OnEnter(bool sendOriginal);
    void SendNextPart();
    bool DoSend(const std::wstring& line);
    void CountTyping();
    void CountMyMemory(size_t chars);
    std::wstring MyMemoryQuotaText() const;
    void ShowMyMemoryNotice();
    SendOptions SendTiming() const;
    void StartGrammarCheck();
    void OnGrammar(KbdGrammarMsg* msg);

    // ---- game, docking, tray, settings
    void PollGame();
    RECT GameClientRect() const;
    RECT DockTarget() const;
    void UpdateDockFromWindow();
    void SetDock(bool on);
    void AddTrayIcon();
    void UpdateTrayTip();
    void RemoveTrayIcon();
    void ShowTrayMenu();
    void OpenSettings(SettingsPage page);
    void ApplySettings(const Config& next);
    std::wstring TechnicalStatus();

    HINSTANCE inst_ = nullptr;
    HWND hwnd_ = nullptr;
    Config cfg_;
    Theme theme_;
    SpellService spell_;
    InputBox input_;
    PreviewView preview_;
    SuggestionBar words_;
    bool hotkeyOk_ = false, trayOk_ = false, warmedUp_ = false, waitForGame_ = false;

    // translator
    std::shared_ptr<Translator> translator_;
    std::shared_ptr<LlmTranslator> llm_;
    Engine engine_ = Engine::Basic;
    bool llmBusy_ = false, llmAgain_ = false;  // an LLM answers one request after the other

    // languages and channel
    std::vector<std::wstring> sendLangs_;  // "Send as" favourites ([Translate] WriteLangs)
    size_t sendIdx_ = 0;                   // == sendLangs_.size(): send the original
    std::wstring kbdLocale_;
    bool kbdRtl_ = false;
    Channel channel_ = Channel::Unknown;  // Unknown = the channel active in GW2
    std::wstring whisperTarget_;          // empty = reply to the last whisper (/r)
    std::deque<std::wstring> whisperers_; // whom you whispered recently (typed /w Name)

    // glossary, corrections, your words
    std::map<std::string, NameTable> names_;
    std::set<std::string> fetching_, fetchFailed_;
    Glossary glossary_;
    std::vector<GlossaryMatch> lastHits_;
    CorrectionMemory corrections_;
    MyWords myWords_;

    // compose state; inputGen_ changes with every edit / language change
    uint64_t inputGen_ = 1, inflightGen_ = 0, previewGen_ = 0, backGen_ = 0;
    bool previewOk_ = false, sendPending_ = false, grammarOffer_ = false, grammarInFlight_ = false;
    std::wstring previewPrefix_, previewBody_, backText_;
    std::vector<std::wstring> parts_;
    size_t partIdx_ = 0;
    RateLimiter grammarLimiter_{18};

    // game
    MumbleLink mumble_;
    MumbleState mumbleState_;
    HWND gw2_ = nullptr;
    ULONGLONG lastFind_ = 0;
    bool gameReady_ = false, lastTextboxFocus_ = false, autoHidden_ = false, userHidden_ = false, moving_ = false;
    double gameFps_ = 0;
    ULONGLONG fpsSince_ = 0;
    uint32_t fpsTick_ = 0;
    int lastOpenMs_ = -1, lastConfirmMs_ = -1, sends_ = 0, sendsConfirmed_ = 0;

    // chrome
    std::wstring status_;
    Tone tone_ = Tone::Muted;
    RECT menuRect_{}, closeRect_{}, channelRect_{}, typeRect_{}, sendRect_{};
};

}  // namespace gct
