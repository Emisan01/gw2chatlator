// chat_log_view.hpp — the translated chat (owner-drawn, GW2 look).
//
// One model for all tabs; the active tab is a filter (channel set, plus the
// lines you sent from that tab). Entries are added as soon as a line is read
// and updated in place when its translation arrives.
#pragma once

#include <windows.h>

#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "app/theme.hpp"
#include "core/chat_line.hpp"
#include "core/chat_tabs.hpp"

namespace gct {

struct ChatEntry {
    enum class Kind { Incoming, Outgoing, System };
    enum class State { Plain, Pending, Translated, Failed };

    uint64_t id = 0;  // assigned by Add
    Kind kind = Kind::Incoming;
    State state = State::Plain;
    Channel channel = Channel::Unknown;
    bool whisperOut = false;  // "An Name: ..." (a whisper you sent)
    uint32_t tabId = 0;       // outgoing: the tab it was written in (stays visible there)
    std::wstring speaker;
    std::wstring lang;        // detected source language ("FR"), if known
    std::wstring main;        // translation, or the text itself
    std::wstring original;    // what was written, when `main` is a translation
    std::wstring note;        // error or info shown in place of the original
    bool splitSend = false;   // outgoing: only the first of several chat lines (not correctable as a whole)
    bool hasColor = false;    // sampled text colour (incoming), for calibration
    Rgb color;
};

class ChatLogView {
public:
    struct Callbacks {
        std::function<void(const std::wstring& speaker)> onReply;    // whisper back
        std::function<void(Channel)> onUseChannel;                    // answer in that channel
        std::function<void(Channel, Rgb)> onCalibrate;                // "this colour is ..."
        std::function<void()> onHintClick;                            // click on the empty-state hint
        std::function<void(const std::wstring& speaker)> onSpeakerClick;  // click on a name: whisper tab
        // Click on a line that was not translated: translate it now.
        std::function<void(uint64_t id, const std::wstring& text)> onRetranslate;
        // "Correct this translation": the entry as it is now (correction memory).
        std::function<void(const ChatEntry& entry)> onCorrect;
    };

    static bool Register(HINSTANCE inst);
    bool Create(HWND parent, HINSTANCE inst, const Theme* theme, Callbacks cb);
    HWND Hwnd() const { return hwnd_; }

    uint64_t Add(ChatEntry e);
    // Changes an entry in place (no-op if it was dropped meanwhile).
    void Update(uint64_t id, const std::function<void(ChatEntry&)>& change);
    void Clear();

    // Show the entries of these channels plus those written in tab `tabId`;
    // with `person`, only whispers from and to that person (a whisper tab).
    void SetFilter(ChannelMask channels, uint32_t tabId, const std::wstring& person = std::wstring());
    void SetPalette(const std::vector<ChannelColor>& palette);
    void SetEmptyHint(const std::wstring& text, bool clickable);
    void ThemeChanged();  // fonts changed: measure everything again

    // True if `text` is (nearly) a translation this view is showing — i.e.
    // the chat reader is reading our own window.
    bool ShowsTranslation(const std::wstring& text) const;

    static bool Matches(const ChatEntry& e, ChannelMask channels, uint32_t tabId, const std::wstring& person);

private:
    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);

    struct Row {
        size_t index;  // into entries_
        int top, height;
        RECT name{};   // where "Name: " is drawn (content coordinates), for clicks
    };
    void OnClick(POINT client);

    std::wstring MainLine(const ChatEntry& e) const;
    std::wstring SecondaryLine(const ChatEntry& e) const;
    COLORREF MainColor(const ChatEntry& e) const;
    void Relayout();
    int MaxScroll() const;
    void ScrollTo(int y);
    void Paint();
    int RowAt(int clientY) const;
    void ShowMenu(POINT screen);
    void CopyText(const std::wstring& s);
    static std::vector<std::wstring> EntryLinks(const ChatEntry& e);
    void OpenLinkAsking(const std::wstring& link);
    void OfferLinks(const ChatEntry& e, POINT screen);

    static constexpr size_t kMaxEntries = 500;

    HWND hwnd_ = nullptr;
    const Theme* theme_ = nullptr;
    Callbacks cb_;
    std::deque<ChatEntry> entries_;
    uint64_t nextId_ = 1;
    ChannelMask filterChannels_ = 0xFFFF;
    uint32_t filterTab_ = 0;
    std::wstring filterPerson_;
    std::vector<ChannelColor> palette_;
    std::wstring hint_;
    bool hintClickable_ = false;
    RECT hintRect_{};

    std::vector<Row> rows_;  // visible entries for the current filter and width
    int layoutWidth_ = -1;
    bool layoutDirty_ = true;
    int total_ = 0;
    int scroll_ = 0;
    bool stickToBottom_ = true;
};

}  // namespace gct
