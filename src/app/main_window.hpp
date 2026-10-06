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
#include "app/spell_service.hpp"
#include "app/theme.hpp"
#include "core/chat_stream.hpp"
#include "core/glossary.hpp"
#include "core/translator.hpp"
#include "win/mumble_link.hpp"
#include "win/online_translators.hpp"

namespace gct {

struct NamesMsg;
struct TranslatedMsg;
struct IncomingMsg;

class MainWindow {
public:
    int Run(HINSTANCE inst);

private:
    enum class Tone { Muted, Ok, Warn, Error };

    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);

    // ---- setup
    void InitServices();
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
    void SetEngine(Engine e);
    void OnKeyboardLanguage(const std::wstring& locale);
    std::wstring ChannelChipText() const;
    std::wstring WriteChipText() const;

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
    void PickRegion();
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
    // Foreground switch; in copy-only mode without the synthetic Alt fallback.
    bool Front(HWND h);

    HINSTANCE inst_ = nullptr;
    HWND hwnd_ = nullptr;
    Config cfg_;
    Theme theme_;
    SpellService spell_;
    InputBox input_;
    PreviewView preview_;
    ChatLogView log_;
    bool hotkeyOk_ = false;
    // Hidden from screen captures only while covering the chat area, so the
    // reader never reads its own window; otherwise screenshots show it.
    bool excludedFromCapture_ = false, affinityUnsupported_ = false;
    bool moving_ = false;                // window is being dragged / resized
    ULONGLONG ignoreSnapshotsBefore_ = 0;  // captures that may contain our own window

    // translation backends
    std::shared_ptr<Translator> translator_;
    std::shared_ptr<LlmTranslator> llm_;  // when configured (also used for romanization)
    Engine engine_ = Engine::Basic;

    // languages
    std::wstring readLang_;                 // "DE" — incoming chat is translated into this
    std::vector<std::wstring> writeLangs_;  // "Senden als" favourites
    size_t writeIdx_ = 0;                   // == writeLangs_.size(): send the original
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

    // glossary
    std::map<std::string, NameTable> names_;
    std::set<std::string> fetching_, fetchFailed_;
    Glossary glossary_;
    std::vector<GlossaryMatch> lastHits_;

    // outgoing state; inputGen_ changes with every edit / language change
    uint64_t inputGen_ = 1, inflightGen_ = 0, previewGen_ = 0, backGen_ = 0;
    bool previewOk_ = false, sendPending_ = false;
    std::wstring previewPrefix_, previewBody_, backText_;
    std::vector<std::wstring> parts_;  // chat lines to send, prefix included
    size_t partIdx_ = 0;
    struct Sent {
        std::wstring normalized;
        ULONGLONG tick;
        uint64_t entryId;   // the "Du: ..." line in the log
        bool echoed;        // seen in the game chat already
    };
    std::deque<Sent> recentSent_;  // to recognise our own lines when they come back through OCR

    // incoming state
    ChatReader reader_;
    ChatStream stream_;
    TranslationCache cache_;
    struct PendingLine {
        uint64_t entryId;
        std::wstring text;
    };
    std::deque<PendingLine> inQueue_;
    bool inFlight_ = false;
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
    RECT readRect_{}, menuRect_{}, closeRect_{}, channelRect_{}, writeRect_{};
    bool previewVisible_ = false;  // the preview only takes space while you type

    // self-read guard
    ULONGLONG selfReadTick_ = 0;
    int selfReadHits_ = 0;
};

}  // namespace gct
