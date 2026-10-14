// main_window.hpp — the translator window. Owns the services and wires them:
//
//  incoming:  ChatReader (screen + OCR, worker) ──snapshot──> BuildMessages ──> ChatStream (new lines only)
//               └─> language check / cache ──> batch Translator (worker) ──> ChatLogView (tabs Chat / Flüstern)
//
//  outgoing:  InputBox (spelling, autocorrect) ──> ProtectForTranslation ──> Translator (worker)
//               └─> PreviewView (+ back-translation) ──Enter──> SendToGw2Chat ──> ChatLogView
//
// Everything below this class is UI-free; everything in it runs on the UI
// thread. Worker threads only post results back.
#pragma once

#include <windows.h>

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "app/chat_log_view.hpp"
#include "app/chat_reader.hpp"
#include "app/config.hpp"
#include "app/input_box.hpp"
#include "app/preview_view.hpp"
#include "app/settings_dialog.hpp"
#include "app/spell_service.hpp"
#include "app/suggestion_bar.hpp"
#include "core/languagetool_protocol.hpp"
#include "app/theme.hpp"
#include "core/chat_stream.hpp"
#include "core/corrections.hpp"
#include "core/my_words.hpp"
#include "core/names.hpp"
#include "core/glossary.hpp"
#include "core/translator.hpp"
#include "win/bridge_pipe.hpp"
#include "win/mumble_link.hpp"
#include "win/online_translators.hpp"

namespace gct {

struct NamesMsg;
struct TranslatedMsg;
struct IncomingMsg;
struct GrammarMsg;

class MainWindow {
public:
    // `cmdLine`: --wait-for-gw2 (autostart: hidden until GW2 runs),
    // --mark-chat / --cover-chat (continue the setup after installing).
    int Run(HINSTANCE inst, const std::wstring& cmdLine);
    // After Run(): a command line to start (the installed copy), or empty.
    const std::wstring& RestartCommand() const { return restartCommand_; }

private:
    enum class Tone { Muted, Ok, Warn, Error };

    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);

    // ---- setup
    void InitServices();
    // The heavy part of the start (spell checker, Windows' prediction, the learned words, GW2 name lists, file
    // clean-up): at once when started by hand, with the autostart only when the window is first needed.
    void WarmUp();
    bool warmedUp_ = false;
    void CleanUpFiles();
    void ChooseEngine(bool announce);
    void CreateChildren();
    void StartReader();

    // ---- layout & painting
    void Layout();
    // Window moved to a monitor with another scale (or started on one).
    void ApplyDpi(int dpi, const RECT* suggested);
    void Paint();
    LRESULT HitTest(LPARAM lp) const;
    void InvalidateChrome();
    void OnClick(POINT pt);
    bool IsClickable(POINT pt) const;

    // ---- status line
    void SetStatus(const std::wstring& text, Tone tone, UINT autoClearMs = 0);
    bool BackgroundNoticeAllowed() const;

    // ---- languages, tabs, menus
    bool WriteOriginal() const;
    std::wstring WriteLang() const;  // empty when sending the original
    bool WriteNeedsChatLang() const;  // writing language GW2 cannot show (Arabic ...)
    std::wstring SendLang() const;    // what goes into the chat: WriteLang or chatLang_
    void ShowChatLangMenu();
    void CycleWrite();
    void SetWriteIndex(size_t idx);
    void SetReadLang(const std::wstring& code);
    // ---- tabs (cfg_.tabs; each shows a set of channels, like in GW2)
    void SwitchTab(size_t idx);
    void ApplyTabFilter();
    void ShowTabMenu(size_t idx, POINT screen);
    void AddTab(const ChatTab& preset);
    void RemoveTab(size_t idx);
    void MoveTab(size_t idx, int delta);
    void ResetTabs();
    void OnTabsChanged();
    // The channel the active tab writes to; Unknown = the one active in GW2,
    // Whisper = /r or /w whisperTarget_.
    Channel SendChannel() const;
    void SetSendChannel(Channel c);

    void ShowReadMenu();
    void ShowWriteMenu();
    void ShowChannelMenu();
    void ShowMainMenu();
    void ShowMemoryMenu();           // the chat memory circle: save or clear
    std::wstring MemoryText() const;  // "Chat memory: 312/500"
    void ShowTour();  // "What can this tool do?" – once by itself, then in the menu
    void SetUiLanguage(UiLang lang);
    void SetSaveCaptures(bool on);
    void RestartReader();

    // ---- settings, setup, tray, grammar
    std::wstring ConnectionStatus();
    void OpenSettings(SettingsPage page);
    void RunSetup();
    void HandleDialogAction(const DialogResult& r);
    void ApplySettings(const Config& next);
    void AddTrayIcon();
    void UpdateTrayTip();
    void RemoveTrayIcon();
    void ShowTrayMenu();
    void StartGrammarCheck();
    void OnGrammar(GrammarMsg* msg);
    void OnKeyboardLanguage(const std::wstring& locale);
    void OnWordForgotten(const std::wstring& word);
    void OpenWhisperTab(const std::wstring& name);
    void Retranslate(uint64_t id, const std::wstring& text);
    void ToggleReading();
    void TranslateOnce();
    void EndTranslateOnce();
    // "Keyboard only": the tool is just the typing help – nothing of the chat is read.
    bool ReadingWanted() const { return !cfg_.keyboardOnly && (cfg_.readerEnabled || once_); }
    // The language you type in: chosen in Settings -> Writing, else the keyboard layout's ("de-DE").
    std::wstring TypingLocale() const;
    ReaderOptions MakeReaderOptions() const;
    // Settings -> Technical: one picture of the chat area through every text recognition, side by side.
    void CompareRecognition(HWND notify, UINT message);
    void SaveF16Capture();
    std::wstring ReadingAdvice() const;  // resolution and the text recognition that fits it
    // Correction memory: right-click "Correct this translation" -> remembered for that text and target language.
    void CorrectEntry(const ChatEntry& e);
    void LoadCorrections();
    void SaveCorrections();
    std::wstring CorrectionsPath() const;
    std::wstring CorrectionsInfo() const;
    // My words: right-click "Explain this word" in the input box; the list in Settings -> Writing.
    void ExplainWord(const std::wstring& word);
    void LoadMyWords();
    void SaveMyWords();
    void SaveOcrFixes();
    void ShowMyMemoryNotice();
    void UseChatArea(const RECT& r);
    RECT ChatSearchArea() const;
    void StartChatDetection();
    ReaderOptions DetectOptions() const;
    static RECT FindChatIn(const Image& img, const RECT& at, const ReaderOptions& o);
    void OnChatFound(RECT* r);
    bool WindowCaptureAllowed() const;
    bool Understood(const std::wstring& lang) const;
    void CountMyMemory(size_t chars, bool chat = false);
    // One translator for an engine choice (Auto resolved, missing key -> MyMemory with a note).
    std::shared_ptr<Translator> MakeEngine(Engine wanted, Engine* resolved, std::wstring* note);
    std::wstring MyMemoryQuotaText() const;
    std::wstring TechnicalStatus();
    std::wstring ChannelChipText() const;
    std::wstring WriteChipText() const;
    std::wstring TypeChipText() const;   // "Write: Deutsch" – the language you type in (spelling, suggestions)
    void ShowTypeLangMenu();
    void ApplyTypingLanguage();          // spelling, learned words and suggestions follow cfg_.writeIn

    // ---- glossary (official GW2 names)
    void RefreshGlossary();
    bool EnsureNames(const std::string& lang);
    void StartNameFetch(const std::string& lang);
    void OnNamesFetched(NamesMsg* msg);
    void UpdateSpellWords();

    // ---- outgoing
    std::wstring ComposePrefix() const;
    void OnInputChanged();
    void StartTranslation();
    void OnTranslated(TranslatedMsg* msg);
    void StartBackTranslation();
    void Romanize();
    void SetPreviewBody(const std::wstring& prefix, const std::wstring& body);
    bool PreviewIsCurrent() const;
    void UpdatePreview();
    void OnEnter(bool sendOriginal);
    void SendNextPart();
    bool DoSend(const std::wstring& line, const std::wstring& original);
    void ResetCompose();

    // ---- incoming
    void OnSnapshot(ReaderSnapshot* snap);
    // Party/squad lines from the optional Nexus add-on (exact text from Unofficial Extras, docs/NEXUS_PLAN.md).
    void OnBridge(WPARAM kind, BridgeMessage* msg);
    // The bridge delivers party/squad: OCR lines of these channels are dropped so nothing comes twice.
    bool BridgeCarriesParty() const { return bridgeConnected_ && bridgeLines_ > 0; }
    void HandleIncoming(const ChatMessage& m);
    // Is this chat line yours? A line sent through this window coming back
    // fills in what only the game knows (the channel it went to, the
    // whisper partner) and is not shown twice.
    enum class Own { No, SentHere, TypedInGame };
    Own ClassifyOwn(const ChatMessage& m);
    bool NeedsTranslation(const std::wstring& text, std::wstring* detected) const;
    void PumpIncoming();
    void OnIncomingTranslated(IncomingMsg* msg);
    void UpdateHint();

    // ---- game tracking & visibility
    void PollGame();
    RECT ChatArea() const;
    RECT ChatFrame() const;  // the chat frame as set (screen pixels), without the reading room below it
    // detect: find the GW2 chat in the picture first and show the found frame (edges can be dragged); else the frame
    // set so far is shown to adjust.
    void PickRegion(bool detect = false);
    void PickFreeArea();         // any screen text, no chat rules
    void SetFreeArea(bool on);
    RECT GameClientRect() const;  // screen coordinates, empty without GW2
    // ---- docking ("an GW2 andocken")
    RECT DockTarget() const;
    void UpdateDockFromWindow();
    void SetDock(bool on);
    void CoverChat();
    // The reader saw our own translations: Windows does not keep this
    // window out of the capture. Stop reading while covering the chat.
    void OnSelfRead();
    void ShowOverlay();
    void HideOverlay();
    void ReturnToGame();
    void ToggleCollapse();
    // Foreground switch; in copy-only mode without the synthetic Alt fallback.
    bool Front(HWND h);

    HINSTANCE inst_ = nullptr;
    HWND hwnd_ = nullptr;
    Config cfg_;
    Theme theme_;
    SpellService spell_;
    InputBox input_;
    PreviewView preview_;
    SuggestionBar words_;
    ChatLogView log_;
    bool hotkeyOk_ = false;
    // Hidden from screen captures only while covering the chat area, so the
    // reader never reads its own window; otherwise screenshots show it.
    bool excludedFromCapture_ = false, affinityUnsupported_ = false;
    bool moving_ = false;                // window is being dragged / resized
    ULONGLONG ignoreSnapshotsBefore_ = 0;  // captures that may contain our own window

    // translation backends
    std::shared_ptr<Translator> translator_;      // what you write
    std::shared_ptr<Translator> chatTranslator_;  // the chat (the same object unless chosen otherwise)
    Engine chatEngine_ = Engine::Basic;
    std::shared_ptr<LlmTranslator> llm_;  // when configured (also used for romanization)
    Engine engine_ = Engine::Basic;

    // languages
    std::wstring readLang_;                 // "DE" — incoming chat is translated into this
    std::vector<std::wstring> writeLangs_;  // "Senden als" favourites
    size_t writeIdx_ = 0;
    std::wstring chatLang_ = L"EN-GB";      // sent when the writing language is not shown by GW2
    std::wstring kbdLocale_;                // active keyboard layout, "de-DE"
    bool kbdRtl_ = false;

    // tabs & compose target
    struct TabState {
        int unread = 0;
        Channel send = Channel::Unknown;  // chip of this tab
    };
    std::vector<TabState> tabState_;        // parallel to cfg_.tabs
    size_t tab_ = 0;                        // active tab
    uint32_t nextTabId_ = 1;
    std::vector<RECT> tabRects_;
    std::wstring whisperTarget_;            // empty = reply to the last whisper (/r)
    std::deque<std::wstring> whisperers_;   // recent partners, newest first
    std::deque<std::wstring> chatWords_;    // words of the recent chat for completions, newest first
    void NoteChatWords(const std::wstring& text);
    void CountTyping();
    // "Show only translations" – except while this window lies over the GW2 chat: it *is* your chat then.
    bool HideUntranslated() const { return cfg_.onlyTranslations && !(excludedFromCapture_ && !cfg_.freeArea); }

    // glossary
    std::map<std::string, NameTable> names_;
    std::set<std::string> fetching_, fetchFailed_;
    Glossary glossary_;
    std::vector<GlossaryMatch> lastHits_;

    // outgoing state; inputGen_ changes with every edit / language change
    uint64_t inputGen_ = 1, inflightGen_ = 0, previewGen_ = 0, backGen_ = 0;
    bool grammarOffer_ = false;
    // The game's frame rate from MumbleLink's frame counter (no access to the game): GW2 reads the keyboard once per
    // frame, so the key timing for sending follows it (30 fps needs keys held far longer than 120 fps).
    bool gameReady_ = false;  // GW2 is in the 3D world (not launcher / login): the autostart window appears now
    double gameFps_ = 0;
    int lastOpenMs_ = -1, lastConfirmMs_ = -1, sends_ = 0, sendsConfirmed_ = 0;
    ULONGLONG fpsSince_ = 0;
    uint32_t fpsTick_ = 0;
    SendOptions SendTiming() const;
    // An LLM answers one request after the other: while one translation of your text is on its way, newer text waits
    // (only the newest is sent when it comes back) instead of piling up requests behind it.
    bool llmBusy_ = false, llmAgain_ = false;  // the preview offers to switch on the grammar check (a click does it)
    bool previewOk_ = false, sendPending_ = false;
    std::wstring previewPrefix_, previewBody_, backText_;
    std::vector<std::wstring> parts_;  // chat lines to send, prefix included
    size_t partIdx_ = 0;
    struct Sent {
        std::wstring exact;
        std::wstring normalized;
        ULONGLONG tick;
        uint64_t entryId;   // the "Du: ..." line in the log
        bool echoed;        // seen in the game chat already
    };
    std::deque<Sent> recentSent_;  // to recognise our own lines when they come back through OCR
    // The reading check: every own line that comes back through OCR is compared letter by letter with what was really
    // sent – an exact hit rate of the reading on this screen (the gate for taking over the chat later). Last 50.
    std::deque<bool> readChecks_;
    uint64_t readCheckChars_ = 0, readCheckErrors_ = 0;
    void NoteReadCheck(const std::wstring& sent, const std::wstring& read);
    std::wstring ReadCheckText() const;  // "read exactly 48/50", empty before the first check
    std::map<std::pair<std::wstring, std::wstring>, int> fixConfirmations_;

    // incoming state
    ChatReader reader_;
    BridgePipe bridge_;
    bool bridgeConnected_ = false;
    std::wstring bridgeVersion_;
    uint64_t bridgeLines_ = 0;  // party/squad lines received in this connection
    ChatStream stream_;
    NameList speakers_;  // speakers seen in the chat: their names are never translated
    bool streamPrimed_ = false;  // the first picture was fed (old history skipped)
    bool once_ = false;          // "translate once": reading runs for one look, then stops again
    size_t onceFound_ = 0;       // lines that look brought
    int onceShots_ = 0;          // pictures read during that look
    bool wasInMap_ = false;      // MumbleLink: on a map (reading allowed)
    uint32_t lastMapId_ = 0;     // MumbleLink map id: a change empties the log (unless kept)
    bool detecting_ = false;     // looking for the GW2 chat (no area set yet)
    bool placedForSetup_ = false;  // first start: put at the usual chat place once
    ULONGLONG lastDetect_ = 0;
    static constexpr int kFreeActiveLines = 6;  // free area after the first picture: 3 people × 2 lines per look
    static constexpr std::ptrdiff_t kStartLines = 3;  // of the history shown at the start, translate the last ...
    TranslationCache cache_;
    CorrectionMemory corrections_;  // your corrected translations (all translators)
    MyWords myWords_;               // your slang with its meaning ("finds = finde es"), replaced before translating
    MyWords ocrFixes_;              // learned recognition errors ("rnain = main"), fixed right after reading
    // Free screen area: the last paragraphs shown, so a paragraph that grows (typing, streaming text) updates its
    // entry instead of adding a new one each time.
    struct FreeParagraph {
        uint64_t id;
        std::wstring text;
    };
    std::deque<FreeParagraph> recentFree_;
    struct PendingLine {
        uint64_t entryId;
        std::wstring text;
    };
    std::deque<PendingLine> inQueue_;
    int inFlight_ = 0;  // translation requests on their way (incoming chat)
    struct Stats {      // for the technical page (no chat text in here)
        ULONGLONG since = 0;
        uint64_t pictures = 0, messages = 0, dropped = 0, confirmed = 0, translated = 0, failed = 0;
        uint64_t secondLooks = 0, secondFixes = 0;  // words read a second time / taken from the second reading
        uint64_t bridgeLines = 0;  // party/squad lines from the Nexus add-on
        int lastMs = 0;
        double avgMs = 0, avgTranslateMs = 0;
        size_t lastLines = 0;
    } stats_;
    ULONGLONG inPauseUntil_ = 0;
    std::wstring readerError_;
    bool readerReported_ = false;
    bool readingActive_ = false;
    bool overlapsChat_ = false;

    // game
    MumbleLink mumble_;
    MumbleState mumbleState_;
    HWND gw2_ = nullptr;
    ULONGLONG lastFind_ = 0;
    bool autoHidden_ = false, userHidden_ = false, picking_ = false;

    // chrome
    std::wstring status_;
    Tone tone_ = Tone::Muted;
    RECT chatRect_{};
    RECT typeRect_{};  // footer: "Write: …"
    RECT readRect_{}, menuRect_{}, closeRect_{}, collapseRect_{}, channelRect_{}, writeRect_{};
    RECT readDotRect_{};  // small dot in the header: reading on (grey) / off (black)
    RECT memoryRect_{};   // small circle in the header that fills up: the chat memory (500), click = save / clear
    HWND tip_ = nullptr;  // its explanation on mouse-over
    std::wstring tipText_;
    size_t memoryShown_ = 0;  // the log size the circle shows
    bool previewVisible_ = false;  // the preview only takes space while you type
    bool collapsed_ = false;
    int expandedHeight_ = 0;
    // Keyboard only: the window is just preview + input (no chat area); the full height comes back with the reader.
    bool compactKbd_ = false;
    int fullHeight_ = 0;
    void FitKeyboardWindow();
    void SaveWindow();  // the window's place; while compact with its full height (what the reader mode uses)

    // self-read guard
    ULONGLONG selfReadTick_ = 0;
    int selfReadHits_ = 0;

    // start / tray / misc
    bool waitForGame_ = false;      // started by the autostart: hidden until GW2 runs
    bool trayOk_ = false;
    bool loadingNames_ = false;
    std::wstring restartCommand_;
    std::wstring lastOcrEngine_;
    std::wstring lastCaptureMethod_;
    CaptureStatus lastCaptureStatus_;
    bool lastTextboxFocus_ = false;
    bool grammarInFlight_ = false;
    RateLimiter grammarLimiter_{18};
};

}  // namespace gct
