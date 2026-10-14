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
#include "core/send_guard.hpp"
#include "core/voice_learn.hpp"
#include "kbd/voice_session.hpp"
#include "win/gw2_sender.hpp"
#include "win/bridge_pipe.hpp"
#include "win/cpu_cores.hpp"
#include "win/mic_sessions.hpp"
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

    // ---- voice input: hold the microphone button (or the key), speak, release – the text goes into the input box,
    // you check it, Enter sends. Never sent by itself.
    bool StartVoice(bool autoEnd = false);  // autoEnd: a pause ends it (key anywhere, press mode)
    bool StartListening();                   // voice activation on
    void StopListening();
    void OnVoiceHotkey();                    // the key anywhere
    void ApplyVoiceGlobalKey();
    std::wstring VoiceChatOpen();
    HWND tip_ = nullptr;      // the microphone's explanation on mouse-over
    std::wstring tipText_;            // a listed voice chat has the microphone open ("" = none)
    std::wstring voiceChat_;          // the voice chat that paused voice activation ("" = none)
    bool voiceChatOverride_ = false;  // switched on anyway by a click / the key while that chat is open
    int voiceChatTicks_ = 0;
    bool voiceGlobalOk_ = false;      // the key anywhere is registered
    bool voiceFromAnywhere_ = false;  // the running recording came from the key anywhere: take the focus afterwards
    bool voiceListenPaused_ = false;  // voice activation switched off by a click (guards "window in front"/"always")
    bool voiceListenFailed_ = false;  // could not start listening: not again until the settings change
    void StopVoice();
    void ToggleVoice();
    void CancelVoice();  // drops a recording / recognition in progress, no text
    void OnVoiceEvent(VoiceEvent* e);
    void InsertDictation(const std::wstring& text);
    // Settings for a recording with `c` (the current or the dialog's settings); false when the model is not there.
    bool BuildVoiceSettings(const Config& c, VoiceSettings* s, std::wstring* modelName) const;
    std::string VoicePrompt(const std::string& language) const;  // GW2, your names, the words it misheard once – never chat text
    void LearnVoiceFixes(const std::wstring& sent);
    void SaveVoiceFixes();
    void RefreshVoiceReady();
    void ApplyVoiceKey();
    void DrawMic(HDC dc, const RECT& r) const;

    // ---- languages, channel, menus
    std::wstring TypingLocale() const;  // the language you type in ("de-DE")
    std::wstring TypingLocaleFor(const Config& c) const;
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
    void SetCompact(bool on);
    int normalHeight_ = 0;  // the height before the compact view
    // What you sent this session (yours only, in memory): the circle in the header, save as text, learn later.
    struct SentLine {
        SYSTEMTIME time;
        std::wstring typed, sent;
    };
    static constexpr size_t kSentLogMax = 500;
    std::deque<SentLine> sentLog_;
    RECT logRect_{};
    size_t pendingLearn_ = 0;  // lines waiting in learn-later.txt
    void ShowLogMenu();
    void LearnLaterNow();
    void DrawLogCircle(HDC dc, const RECT& r) const;
    // The live view (Settings → Technical): every technical value, refreshed every second, in its own window.
    HWND liveWnd_ = nullptr;
    void ShowLiveView();
    std::wstring LiveStatus();
    static LRESULT CALLBACK LiveProc(HWND h, UINT msg, WPARAM wp, LPARAM lp);
    unsigned long long procCpuLast_ = 0, procWallLast_ = 0;
    double procCpu_ = 0;
    // The text translator, measured: requests, last time, errors.
    unsigned long long translateStart_ = 0;
    int lastTranslateMs_ = 0, translateCount_ = 0, translateErrors_ = 0;
    std::wstring lastTranslateError_;
    void ShowTour();
    void SetUiLanguage(UiLang lang);
    // "Activate chat translation": the full GW2 Chat Translator (reading the chat) takes over – started with reading
    // on, this window ends (both use the same settings and learned words).
    void ActivateChatTranslation();
    void LoadExtension();             // "Load extension": downloads the chat translation (OCR) on your click
    void OnExtensionLoaded(std::wstring* result, bool ok);
    bool extensionLoading_ = false;
    std::wstring restartCommand_;  // started after this program ended (the installed copy, the downloaded extension)
public:
    const std::wstring& RestartCommand() const { return restartCommand_; }
private:

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
    // Party/squad chat from the optional Nexus add-on (GW2 Chat Bridge, exact text from Unofficial Extras): the
    // newest lines in a slim band above the preview, translated into your language when they are foreign.
    struct IncomingLine {
        uint64_t id = 0;
        bool squad = false;
        std::wstring speaker, text, translated;
        bool showOriginal = false;  // a click switches between translation and original
        ULONGLONG tick = 0;
        RECT rect{};
    };
    void OnBridge(WPARAM kind, BridgeMessage* msg);
    void TranslateIncoming(const IncomingLine& line);
    int IncomingHeight() const;
    std::deque<IncomingLine> incoming_;
    uint64_t incomingNextId_ = 1;
    BridgePipe bridge_;
    bool bridgeConnected_ = false;
    bool bridgeExtras_ = false;  // the add-on reported Unofficial Extras as loaded
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
    std::unique_ptr<VoiceSession> voice_;  // created with the window, ended in WM_DESTROY
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
    RECT menuRect_{}, closeRect_{}, channelRect_{}, typeRect_{}, sendRect_{}, micRect_{};

    // voice input
    SendGuard sendGuard_;        // no bot / macro / spam use (core/send_guard)
    VoiceFixBook voiceFixes_;  // per language
    std::wstring voiceModelName_;
    std::wstring dictLang_;   // language of the dictated text in the box ("EN" when whisper translated); "" = typed
    std::wstring routeNote_;  // who did what with the current text: "MyMemory: DE → EN", "Whisper: DE → EN · ..."
    std::wstring InputLang() const;  // the language of what is in the box (dictation, else the typing language)
    CoreLoad coreLoad_;  // load per core (sampled with the game poll), for the voice recognizer's cores  // the model of the running / last recording (for the status)      // learned misheard words (<data>\voice-fixes.txt)
    std::wstring lastDictated_;  // the input text right after the last voice input (learning compares it when sent)
    size_t lastDictFrom_ = 0, lastDictLen_ = 0;  // where that voice input went (a new attempt replaces it)
    unsigned voiceSession_ = 0;
    bool voiceLocked_ = false;    // held + right click: the mouse is free, a pause ends the recording
    bool lockRightClick_ = false; // the right click that locked: its button-up is no settings click
    bool voiceHolding_ = false;  // the microphone button is held with the mouse (captured)
    bool voiceReady_ = false;    // recognizer files and the model are there (the button is drawn faint otherwise)
    int voiceIdleTicks_ = 0;     // game polls since the last unload check
};

}  // namespace gct
