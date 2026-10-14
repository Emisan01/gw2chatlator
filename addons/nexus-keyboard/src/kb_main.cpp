// kb_main.cpp — "GW2 Keyboard" as a Nexus add-on (docs/NEXUS_KEYBOARD_PLAN.md), modelled on the exe (src/kbd).
//
// An ImGui window inside the game:
//   - the chat from Vonsh's "Events: Chat" (every channel) or Unofficial Extras (party/squad), foreign lines translated
//     into your language; a click on a name answers with a whisper;
//   - the input line with the translation as you type, the word bar, the grey completion (Tab), autocorrection on
//     Space (Backspace undoes it), learned words, the history (Up/Down);
//   - Enter sends the translation into the GW2 chat – the exe's send path (clipboard + keys, foreground checks,
//     invariants 1–3), only on your Enter, through the send guard.
//
// Inside the game process: the render callback only draws and copies; translations, spelling and sending run on our
// own worker thread; every callback is guarded (try/catch + SEH) – after an error the add-on switches itself off for
// the session. Nothing of other players is stored.

#include <windows.h>
#include <objbase.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "../../nexus/src/chat_events.hpp"
#include "app/config.hpp"
#include "app/spell_service.hpp"
#include "core/chat_line.hpp"
#include "core/chat_stream.hpp"
#include "core/gw2_text.hpp"
#include "core/i18n.hpp"
#include "core/langs.hpp"
#include "core/languages.hpp"
#include "core/protect.hpp"
#include "core/send_guard.hpp"
#include "core/slang.hpp"
#include "core/text.hpp"
#include "core/translator.hpp"
#include "imgui/imgui.h"
#include "nexus/Nexus.h"
#include "unofficial_extras/Definitions.h"
#include "version.h"
#include "win/deepl_translator.hpp"
#include "win/els.hpp"
#include "win/files.hpp"
#include "win/gw2_sender.hpp"
#include "win/mumble_link.hpp"
#include "win/online_translators.hpp"

using namespace gct;

namespace {

constexpr const char* kName = "GW2 Keyboard";
constexpr const char* kToggleBind = "KB_GW2KEYBOARD_TOGGLE";
constexpr const char* kFocusBind = "KB_GW2KEYBOARD_FOCUS";
constexpr size_t kMaxLines = 200;
constexpr size_t kHistoryMax = 50;

AddonDefinition_t g_def = {};
AddonAPI_t* g_api = nullptr;
HMODULE g_self = nullptr;
std::atomic<bool> g_disabled{false};  // set after an error: the add-on stays quiet for the rest of the session
std::atomic<bool> g_chatEventsLive{false};
std::atomic<bool> g_keyboardFree{true};  // ImGui does not hold the keyboard (keys reach the game)
std::atomic<int> g_fps{0};
bool g_show = true;

void Log(ELogLevel level, const char* text) {
    if (g_api && g_api->Log) g_api->Log(level, kName, text);
}

std::string U8(const std::wstring& w) { return ToUtf8(w); }
std::wstring W(const std::string& s) { return FromUtf8(s); }
size_t U8Offset(const std::wstring& w, size_t idx) { return ToUtf8(w.substr(0, std::min(idx, w.size()))).size(); }

// ---------------------------------------------------------------------------------------------------------------
// One worker thread: translations, spelling and sending never run on the game's threads.
class Worker {
public:
    void Start() {
        stop_ = false;
        thread_ = std::thread([this] { Run(); });
    }
    // Pending jobs are dropped; `last` still runs on the worker (it frees what the worker owns) before it ends.
    void Stop(std::function<void()> last = {}) {
        {
            std::lock_guard<std::mutex> lock(mu_);
            jobs_.clear();
            if (last) jobs_.push_back(std::move(last));
            stop_ = true;
        }
        cv_.notify_all();
        if (thread_.joinable()) thread_.join();
    }
    void Post(std::function<void()> job) {
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (jobs_.size() > 96) jobs_.pop_front();  // a flood (WvW): the oldest wait no longer
            jobs_.push_back(std::move(job));
        }
        cv_.notify_one();
    }

private:
    void Run() {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);  // language detection, spell checker, word prediction
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lock(mu_);
                cv_.wait(lock, [this] { return stop_ || !jobs_.empty(); });
                if (jobs_.empty()) break;  // stopped, nothing left
                job = std::move(jobs_.front());
                jobs_.pop_front();
            }
            try {
                job();
            } catch (...) {
                Log(LOGL_WARNING, "a background job failed");
            }
        }
        CoUninitialize();
    }
    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<std::function<void()>> jobs_;
    bool stop_ = false;
    std::thread thread_;
};

Worker g_worker;

// Only the worker touches these (no lock needed).
SpellService* g_spell = nullptr;
SendGuard g_guard;
MumbleLink* g_mumbleWorker = nullptr;

// ---------------------------------------------------------------------------------------------------------------
// State shared between the game thread (events, drawing) and the worker. All behind g_mu.
struct ChatLine {
    uint64_t id = 0;
    Channel channel = Channel::Unknown;
    bool outgoing = false;
    bool own = false;
    std::wstring speaker, text, translated;
};

struct TypingHelp {  // the worker's answer for one text + caret
    std::wstring text;
    size_t caret = 0;
    WordSuggestions bar;
    WordChoices choices;
};

std::mutex g_mu;
Config g_cfg;
std::shared_ptr<Translator> g_translator;
std::deque<ChatLine> g_lines;
uint64_t g_nextId = 1;
bool g_scrollDown = false;
std::vector<std::wstring> g_context;  // speakers and words of the chat (offered as completions, never learned)
std::wstring g_ownName;               // your character (MumbleLink)
// The input line.
char g_input[1024] = {};
std::string g_lastInput;
ULONGLONG g_inputChanged = 0;
uint64_t g_inputGen = 0;
std::wstring g_preview;  // the translation of the input
uint64_t g_previewGen = 0;
bool g_sendPending = false;  // Enter before the translation was there
std::wstring g_status;
ULONGLONG g_statusUntil = 0;
TypingHelp g_help;
bool g_helpRequested = false;
std::deque<std::wstring> g_history;
int g_historyPos = -1;
// The channel to write in and the send language.
Channel g_channel = Channel::Unknown;  // Unknown = the channel active in GW2
std::wstring g_whisperTo;
int g_sendIdx = 0;                     // index into g_cfg.writeLangs; == size: send as written
bool g_sending = false;
bool g_focusInput = false;
// An edit the worker or a click wants in the input (applied by the input callback on the game thread).
struct PendingEdit {
    bool active = false;
    size_t startByte = 0, lenBytes = 0;
    std::string insert;
} g_edit;
struct AutoUndo {  // the last autocorrection: Backspace right after it brings the typed word back
    bool active = false;
    std::string after;   // the buffer right after the correction (with its space)
    std::string before;  // the buffer as typed (without the space)
    std::wstring original;
} g_undo;

void SetStatus(const std::wstring& s, ULONGLONG ms = 6000) {  // g_mu held
    g_status = s;
    g_statusUntil = ms ? GetTickCount64() + ms : 0;
}

std::wstring TypingLang() {  // the language you write and read in
    if (!g_cfg.writeIn.empty()) return PrimaryLang(g_cfg.writeIn);
    wchar_t locale[LOCALE_NAME_MAX_LENGTH] = {};
    GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH);
    return PrimaryLang(locale);
}
std::wstring TypingLocale() {
    if (g_cfg.writeIn.empty()) {
        wchar_t locale[LOCALE_NAME_MAX_LENGTH] = {};
        GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH);
        return locale;
    }
    const std::wstring p = ToLowerAscii(PrimaryLang(g_cfg.writeIn));
    return p + L"-" + (p == L"en" ? std::wstring(L"US") : ToUpperAscii(p));
}
bool SendOriginal() { return g_sendIdx >= static_cast<int>(g_cfg.writeLangs.size()); }
std::wstring SendLang() { return SendOriginal() ? std::wstring() : g_cfg.writeLangs[g_sendIdx]; }
bool Translating() { return !SendOriginal() && PrimaryLang(SendLang()) != TypingLang(); }
std::wstring ComposePrefix() {
    if (g_channel == Channel::Whisper) return g_whisperTo.empty() ? L"/r " : L"/w " + g_whisperTo + L", ";
    if (const wchar_t* cmd = ChannelCommand(g_channel)) return std::wstring(cmd) + L" ";
    return L"";
}

std::shared_ptr<Translator> MakeTranslator(const Config& c) {
    if (!c.deeplKey.empty()) return MakeDeepLTranslator(c.deeplKey);
    if (!c.googleKey.empty()) return MakeGoogleTranslator(c.googleKey);
    if (!c.msKey.empty()) return MakeMicrosoftTranslator(c.msKey, c.msRegion);
    if (!c.libreUrl.empty()) return MakeLibreTranslator(c.libreUrl, c.libreKey);
    return MakeMyMemoryTranslator(c.basicEmail);
}

// ---------------------------------------------------------------------------------------------------------------
// Worker jobs.

void InitSpelling() {
    Config c;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        c = g_cfg;
    }
    g_spell = new SpellService();
    wchar_t locale[LOCALE_NAME_MAX_LENGTH] = {};
    GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH);
    std::wstring typing;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        typing = TypingLocale();
    }
    if (c.spellEnabled) {
        std::vector<std::wstring> tags = SpellTagCandidates(PrimaryLang(typing), typing);
        if (c.writeIn.empty())
            for (const std::wstring& t : SpellTagCandidates(PrimaryLang(locale), locale)) tags.push_back(t);
        g_spell->Init(tags, c.UserWordsPath());
    }
    EnsureDir(c.LearnedDir());
    g_spell->UseLearnedLanguage(PrimaryLang(typing), c.LearnedDir());
    g_spell->SetLearnChoices(c.learnWords);
    g_mumbleWorker = new MumbleLink();
}

void FreeWorkerState() {
    if (g_spell) {
        g_spell->SaveLearned();
        delete g_spell;
        g_spell = nullptr;
    }
    delete g_mumbleWorker;
    g_mumbleWorker = nullptr;
}

// A chat line into your language, when its language is sure and not yours (nor one you understand).
void TranslateLine(uint64_t id, std::wstring text) {
    std::shared_ptr<Translator> tr;
    std::wstring target;
    std::vector<std::wstring> understood;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        tr = g_translator;
        target = TypingLang();
        understood = g_cfg.understoodLangs;
    }
    if (!tr) return;
    ProtectedText p = ProtectForTranslation(text, nullptr, g_spell ? &g_spell->KeepWords() : &BuiltinKeepWords());
    if (!HasTranslatableText(p.segments)) return;
    const std::wstring detected = SureLanguage(text, DetectLanguage(text));
    if (detected.empty() || PrimaryLang(detected) == target) return;
    for (const std::wstring& u : understood)
        if (PrimaryLang(u) == PrimaryLang(detected)) return;
    const TranslateResult r = tr->Translate(p.segments, SourceCode(detected), target);
    if (!r.ok || r.text.empty()) return;
    const std::wstring t = SanitizeChatText(r.text);
    std::lock_guard<std::mutex> lock(g_mu);
    for (ChatLine& l : g_lines)
        if (l.id == id && NormalizeForCompare(t) != NormalizeForCompare(l.text)) l.translated = t;
}

void SendNow(std::vector<std::wstring> parts, std::wstring typed, std::wstring learnText);

// The input into the chat's language.
void TranslateInput(uint64_t gen, std::wstring text) {
    std::shared_ptr<Translator> tr;
    std::wstring target, typing;
    bool translating;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        tr = g_translator;
        target = SendLang();
        typing = TypingLang();
        translating = Translating();
    }
    const ChatSplit split = SplitChatCommand(text);
    const std::wstring body = split.prefix.empty() ? text : split.body;
    std::wstring result = body;
    std::wstring error;
    if (translating && tr) {
        ProtectedText p = ProtectForTranslation(body, nullptr, g_spell ? &g_spell->KeepWords() : &BuiltinKeepWords());
        if (HasTranslatableText(p.segments)) {
            std::wstring source = DetectLanguage(body);
            if (source.empty()) source = typing;
            const TranslateResult r = tr->Translate(p.segments, source, target);
            if (r.ok) result = r.text;
            else error = r.error;
        }
    }
    std::vector<std::wstring> parts;
    bool sendNow = false;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        if (gen != g_inputGen && !g_sendPending) return;  // the text changed meanwhile
        g_preview = SanitizeChatText(result);
        g_previewGen = gen;
        if (!error.empty()) SetStatus(error);
        if (g_sendPending && gen == g_inputGen) {
            g_sendPending = false;
            const std::wstring prefix = split.prefix.empty() ? ComposePrefix() : split.prefix;
            parts = SplitForChat(g_preview, prefix, static_cast<size_t>(g_cfg.maxLength));
            sendNow = true;
        }
    }
    if (sendNow) SendNow(std::move(parts), text, body);
}

// The word bar, the grey completion and what Space writes, for the newest text + caret.
void ComputeTyping() {
    std::wstring text;
    size_t caret;
    AutoCorrectMode mode;
    std::vector<std::wstring> context;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        g_helpRequested = false;
        text = W(g_input);
        caret = text.size();  // the help works at the end of the line (where you type)
        mode = g_cfg.autoCorrect;
        context = g_context;
    }
    if (!g_spell || !g_cfg.suggestions) return;
    g_spell->SetContext(std::move(context));
    TypingHelp h;
    h.text = text;
    h.caret = caret;
    h.bar = g_spell->Suggestions(text, caret, mode);
    h.choices = g_spell->Choices(text, caret, mode);
    std::lock_guard<std::mutex> lock(g_mu);
    if (W(g_input) == text) g_help = std::move(h);
}

// Enter: the parts go into the GW2 chat – only now, on your Enter (invariants 1–3).
void SendNow(std::vector<std::wstring> parts, std::wstring typed, std::wstring learnText) {
    if (parts.empty()) return;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        g_sending = true;
        SetStatus(L"…", 0);
    }
    // ImGui must let go of the keyboard first, else our own input line would eat the keys.
    for (int i = 0; i < 50 && !g_keyboardFree; ++i) Sleep(10);
    HWND gw2 = FindGw2Window(GetCurrentProcessId());
    SendOptions so;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        so = g_cfg.send;
    }
    if (const int fps = g_fps; fps > 0 && so.keyHoldMs == 30) {  // GW2 reads the keyboard once per frame
        const int action = std::clamp(1000 / fps + 10, 15, 1010);
        so.keyHoldMs = action;
        if (so.stepDelayMs == 80) so.stepDelayMs = action;
    }
    std::wstring error;
    bool ok = gw2 != nullptr;
    if (!gw2) error = Tr(L"GW2 window not found");
    for (const std::wstring& part : parts) {
        if (!ok) break;
        const MumbleState ms = g_mumbleWorker ? g_mumbleWorker->Read() : MumbleState{};
        const SendOutcome out = SendToGw2Chat(gw2, part, so, ms.live ? g_mumbleWorker : nullptr);
        if (!out.ok) {
            ok = false;
            error = out.error;
        }
    }
    if (ok && g_spell) {
        Config c;
        {
            std::lock_guard<std::mutex> lock(g_mu);
            c = g_cfg;
        }
        if (c.learnWords) g_spell->Learn(learnText);  // the word model learns only from what you send
    }
    std::lock_guard<std::mutex> lock(g_mu);
    g_sending = false;
    if (ok) {
        SetStatus(L"");
        g_history.push_front(typed);
        while (g_history.size() > kHistoryMax) g_history.pop_back();
        if (g_cfg.returnFocus) g_focusInput = true;
    } else {
        SetStatus(error, 10000);
        if (g_input[0] == 0) {  // nothing lost: the text comes back
            const std::string back = U8(typed);
            strncpy_s(g_input, back.c_str(), _TRUNCATE);
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Game thread: a line arrives – copy it, queue its translation.
void AddLine(Channel ch, bool outgoing, const char* speaker, const char* text) {
    if (!text || !*text) return;
    ChatLine l;
    l.channel = ch;
    l.outgoing = outgoing;
    l.speaker = speaker ? SanitizeChatText(W(speaker)) : std::wstring();
    l.text = SanitizeChatText(W(text));
    if (l.text.empty()) return;
    uint64_t id;
    bool own;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        own = outgoing || (!g_ownName.empty() && CaseFold(Trim(l.speaker)) == CaseFold(g_ownName));
        l.own = own;
        l.id = id = g_nextId++;
        if (!own) {  // completions offer the names and words of the chat (never learned – invariant 10)
            if (!l.speaker.empty()) g_context.insert(g_context.begin(), l.speaker);
            for (const Span& s : WordSpans(l.text))
                if (s.length >= 4) g_context.insert(g_context.begin(), l.text.substr(s.start, s.length));
            if (g_context.size() > 80) g_context.resize(80);
        }
        g_lines.push_back(l);
        while (g_lines.size() > kMaxLines) g_lines.pop_front();
        g_scrollDown = true;
    }
    if (!own) g_worker.Post([id, t = l.text] { TranslateLine(id, t); });
}

void OnChatEvents(void* args) {
    if (g_disabled) return;
    try {
        const auto* m = static_cast<const bridge::ChatEventsMessage*>(args);
        if (!m) return;
        g_chatEventsLive = true;
        using T = bridge::ChatEventsType;
        Channel ch;
        switch (m->type) {
            case T::Party: ch = Channel::Party; break;
            case T::Squad: ch = Channel::Squad; break;
            case T::SquadMessage:
            case T::SquadBroadcast: AddLine(Channel::Squad, false, "", m->squadMessage); return;
            case T::Map: ch = Channel::Map; break;
            case T::Local: ch = Channel::Say; break;
            case T::Whisper: ch = Channel::Whisper; break;
            case T::Guild: ch = Channel::Guild; break;
            case T::TeamPvP:
            case T::TeamWvW: ch = Channel::Team; break;
            default: return;
        }
        AddLine(ch, m->type == T::Whisper && (m->flags & bridge::kChatEventsWhisperFromMe) != 0,
                m->generic.characterName, m->generic.content);
    } catch (...) {
        g_disabled = true;
    }
}

void OnExtrasChat(void* args) {
    if (g_disabled || g_chatEventsLive) return;  // Events: Chat reports party/squad itself
    try {
        const auto* m = static_cast<const SquadMessageInfo*>(args);
        if (!m || !m->Text) return;
        AddLine(m->Type == ChannelType::Squad ? Channel::Squad : Channel::Party, false, m->CharacterName, m->Text);
    } catch (...) {
        g_disabled = true;
    }
}

void OnToggle(const char*, bool release) {
    if (!release) g_show = !g_show;
}
void OnFocusKey(const char*, bool release) {
    if (release) return;
    std::lock_guard<std::mutex> lock(g_mu);
    g_show = true;
    g_focusInput = true;
}

// ---------------------------------------------------------------------------------------------------------------
// Drawing (game thread, g_mu held).
ImVec4 ColorOf(Channel ch) {
    for (const ChannelColor& cc : g_cfg.palette)
        if (cc.channel == ch) return ImVec4(cc.rgb.r / 255.f, cc.rgb.g / 255.f, cc.rgb.b / 255.f, 1.f);
    return ImVec4(0.9f, 0.9f, 0.92f, 1.f);
}

// The rest of a completion after what is typed ("komm" + "t" for "kommt"); empty if it does not continue it.
std::wstring GhostRest() {
    const std::wstring text = W(g_input);
    if (g_help.text != text || text.empty()) return {};
    const WordSuggestions& b = g_help.bar;
    if (b.kind == WordSuggestions::Kind::Next && !b.words.empty() && text.back() == L' ')
        return b.phrase.empty() ? b.words[0] : b.phrase;
    if (b.kind != WordSuggestions::Kind::Completion || b.autoIndex < 0 || b.autoIndex >= static_cast<int>(b.words.size()))
        return {};
    const std::wstring& word = b.words[b.autoIndex];
    if (b.replace.end() != text.size()) return {};
    const std::wstring typed = text.substr(b.replace.start, b.replace.length);
    if (typed.size() >= word.size() || CaseFold(word.substr(0, typed.size())) != CaseFold(typed)) return {};
    return word.substr(typed.size());
}

// A word of the bar replaces the word at the end (or goes after it), followed by a space.
void TakeWord(const std::wstring& word) {
    const std::wstring text = W(g_input);
    const WordSuggestions& b = g_help.bar;
    Span r = b.replace;
    if (g_help.text != text || r.end() > text.size()) r = Span{text.size(), 0};
    g_edit.active = true;
    g_edit.startByte = U8Offset(text, r.start);
    g_edit.lenBytes = U8Offset(text, r.end()) - g_edit.startByte;
    g_edit.insert = U8(word + L" ");
    g_focusInput = true;
    if (g_spell) g_worker.Post([text, start = r.start, word] { if (g_spell) g_spell->Chose(text, start, word, true); });
}

int InputCallback(ImGuiInputTextCallbackData* d) {
    switch (d->EventFlag) {
        case ImGuiInputTextFlags_CallbackCompletion: {  // Tab: take the grey completion / the next word
            const std::wstring rest = GhostRest();
            if (!rest.empty() && d->CursorPos == d->BufTextLen) {
                const std::string add = U8(rest) + (g_help.bar.kind == WordSuggestions::Kind::Next ? "" : " ");
                d->InsertChars(d->CursorPos, add.c_str());
            }
            break;
        }
        case ImGuiInputTextFlags_CallbackHistory: {  // Up / Down: what you sent
            if (g_history.empty()) break;
            if (d->EventKey == ImGuiKey_UpArrow) g_historyPos = std::min<int>(g_historyPos + 1, static_cast<int>(g_history.size()) - 1);
            else if (d->EventKey == ImGuiKey_DownArrow) g_historyPos = std::max(g_historyPos - 1, -1);
            d->DeleteChars(0, d->BufTextLen);
            if (g_historyPos >= 0) d->InsertChars(0, U8(g_history[g_historyPos]).c_str());
            break;
        }
        case ImGuiInputTextFlags_CallbackAlways: {
            if (g_edit.active) {  // a word of the bar or an autocorrection
                g_edit.active = false;
                if (g_edit.startByte + g_edit.lenBytes <= static_cast<size_t>(d->BufTextLen)) {
                    d->DeleteChars(static_cast<int>(g_edit.startByte), static_cast<int>(g_edit.lenBytes));
                    d->InsertChars(static_cast<int>(g_edit.startByte), g_edit.insert.c_str());
                    d->CursorPos = d->SelectionStart = d->SelectionEnd = d->BufTextLen;
                }
                break;
            }
            const std::string now(d->Buf, d->BufTextLen);
            // Backspace right after an autocorrection: the word as you typed it comes back (and is not corrected
            // that way again).
            if (g_undo.active && now.size() + 1 == g_undo.after.size() && g_undo.after.compare(0, now.size(), now) == 0) {
                g_undo.active = false;
                d->DeleteChars(0, d->BufTextLen);
                d->InsertChars(0, g_undo.before.c_str());
                g_worker.Post([orig = g_undo.original] { if (g_spell) g_spell->RejectCorrection(orig); });
                break;
            }
            if (g_undo.active && now != g_undo.after) g_undo.active = false;
            // Space after a word: what Space writes (the strong choice for the word just finished).
            if (!now.empty() && now.back() == ' ' && now != g_lastInput && now.size() == g_lastInput.size() + 1 &&
                g_cfg.autoCorrect != AutoCorrectMode::Off) {
                const std::wstring before = W(g_lastInput);
                if (g_help.text == before && g_help.choices.Changes() && g_help.choices.highlight >= 0 &&
                    g_help.choices.highlight < static_cast<int>(g_help.choices.words.size())) {
                    const Span r = g_help.choices.replace;
                    const std::wstring word = g_help.choices.words[g_help.choices.highlight];
                    if (r.end() <= before.size()) {
                        const std::wstring fixed = before.substr(0, r.start) + word + before.substr(r.end()) + L" ";
                        g_undo = {true, U8(fixed), g_lastInput, before.substr(r.start, r.length)};
                        d->DeleteChars(0, d->BufTextLen);
                        d->InsertChars(0, g_undo.after.c_str());
                    }
                }
            }
            break;
        }
        default:
            break;
    }
    return 0;
}

void StartSend() {  // Enter (g_mu held)
    const std::wstring text = SanitizeChatText(W(g_input));
    if (text.empty() || g_sending) return;
    const SendGuard::Verdict v = g_guard.Check(text, false, false, GetTickCount64());
    if (!v.allow) {
        SetStatus(v.reason == SendGuard::Reason::Repeat ? Tr(L"Not sent: the same line too often in a short time.")
                  : v.reason == SendGuard::Reason::Rate  ? Tr(L"Not sent: more lines than a person writes in a minute.")
                                                         : Tr(L"Sending is switched off for this session (misuse detected). "
                                                              L"Restart the program to send again."),
                  10000);
        return;
    }
    const ChatSplit split = SplitChatCommand(text);
    const std::wstring body = split.prefix.empty() ? text : split.body;
    const std::wstring prefix = split.prefix.empty() ? ComposePrefix() : split.prefix;
    g_input[0] = 0;
    g_lastInput.clear();
    g_historyPos = -1;
    g_undo.active = false;
    ImGui::SetWindowFocus(nullptr);  // let go of the keyboard: the keys must reach the game
    if (!Translating()) {
        g_worker.Post([parts = SplitForChat(body, prefix, static_cast<size_t>(g_cfg.maxLength)), text, body] {
            SendNow(parts, text, body);
        });
    } else if (g_previewGen == g_inputGen && !g_preview.empty()) {
        g_worker.Post([parts = SplitForChat(g_preview, prefix, static_cast<size_t>(g_cfg.maxLength)), text, body] {
            SendNow(parts, text, body);
        });
    } else {  // the translation is not there yet: sent when it arrives
        g_sendPending = true;
        SetStatus(Tr(L"translating and sending …"), 0);
        g_worker.Post([gen = g_inputGen, text] { TranslateInput(gen, text); });
    }
}

void RenderChannelPicker() {
    static const Channel kChannels[] = {Channel::Unknown, Channel::Say,   Channel::Map,  Channel::Party,
                                        Channel::Squad,   Channel::Guild, Channel::Team, Channel::Whisper};
    std::string label = g_channel == Channel::Unknown ? U8(Tr(L"Chat")) : U8(ChannelLabel(g_channel));
    if (g_channel == Channel::Whisper && !g_whisperTo.empty()) label += " " + U8(g_whisperTo);
    ImGui::SetNextItemWidth(110);
    if (ImGui::BeginCombo("##channel", label.c_str())) {
        for (Channel c : kChannels) {
            const std::string name = c == Channel::Unknown ? U8(Tr(L"Chat")) : U8(ChannelLabel(c));
            if (ImGui::Selectable(name.c_str(), c == g_channel)) {
                g_channel = c;
                if (c != Channel::Whisper) g_whisperTo.clear();
                g_focusInput = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    std::string lang = SendOriginal() ? U8(Tr(L"as written")) : U8(ToUpperAscii(PrimaryLang(SendLang())));
    ImGui::SetNextItemWidth(90);
    if (ImGui::BeginCombo("##sendlang", lang.c_str())) {
        for (size_t i = 0; i <= g_cfg.writeLangs.size(); ++i) {
            const std::string n = i == g_cfg.writeLangs.size() ? U8(Tr(L"as written"))
                                                                : U8(ToUpperAscii(PrimaryLang(g_cfg.writeLangs[i])));
            if (ImGui::Selectable(n.c_str(), static_cast<int>(i) == g_sendIdx)) {
                g_sendIdx = static_cast<int>(i);
                ++g_inputGen;
                g_preview.clear();
                g_inputChanged = GetTickCount64();
                g_focusInput = true;
            }
        }
        ImGui::EndCombo();
    }
}

void RenderWindow() {
    if (!ImGui::GetCurrentContext()) return;  // failsafe: no context, no drawing
    ImGuiIO& io = ImGui::GetIO();
    if (io.DisplaySize.x <= 0 || io.DisplaySize.y <= 0) return;
    g_keyboardFree = !io.WantCaptureKeyboard;
    g_fps = static_cast<int>(io.Framerate);
    {
        static ULONGLONG nextName = 0;  // your character, from MumbleLink (every 2 s)
        static MumbleLink mumble;
        if (GetTickCount64() > nextName) {
            nextName = GetTickCount64() + 2000;
            const MumbleState ms = mumble.Read();
            std::lock_guard<std::mutex> lock(g_mu);
            if (ms.live) g_ownName = ms.identity.name;
        }
    }
    if (!g_show) return;
    ImGui::SetNextWindowSize(ImVec2(500, 320), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("GW2 Keyboard", &g_show)) {
        ImGui::End();
        return;
    }
    std::lock_guard<std::mutex> lock(g_mu);
    if (g_statusUntil && GetTickCount64() > g_statusUntil) {
        g_status.clear();
        g_statusUntil = 0;
    }
    // ---- the chat: name white (a click answers with a whisper), text in the channel colour, translated if foreign
    const float bottomH = ImGui::GetFrameHeightWithSpacing() * 3 + ImGui::GetTextLineHeightWithSpacing() * 2;
    ImGui::BeginChild("chat", ImVec2(0, -bottomH), false);
    for (const ChatLine& l : g_lines) {
        ImGui::PushID(static_cast<int>(l.id));
        if (!l.speaker.empty()) {
            const std::string name = U8((l.outgoing ? L"→ " : L"") + l.speaker + L":");
            if (ImGui::SmallButton(name.c_str()) && !l.own) {
                g_channel = Channel::Whisper;
                g_whisperTo = l.speaker;
                g_focusInput = true;
            }
            ImGui::SameLine();
        }
        const std::wstring& shown = l.translated.empty() ? l.text : l.translated;
        ImGui::PushStyleColor(ImGuiCol_Text, ColorOf(l.channel));
        ImGui::TextWrapped("%s", U8(shown).c_str());
        ImGui::PopStyleColor();
        if (!l.translated.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", U8(l.text).c_str());
        ImGui::PopID();
    }
    if (g_scrollDown) {
        ImGui::SetScrollHereY(1.0f);
        g_scrollDown = false;
    }
    ImGui::EndChild();
    // ---- the translation of what you type
    const bool previewCurrent = g_previewGen == g_inputGen && !g_preview.empty();
    if (Translating())
        ImGui::TextDisabled("%s", previewCurrent ? U8(ComposePrefix() + g_preview).c_str() : " ");
    else
        ImGui::TextDisabled("%s", " ");
    // ---- the word bar
    if (g_cfg.suggestions && g_help.text == W(g_input) && !g_help.bar.words.empty()) {
        for (size_t i = 0; i < g_help.bar.words.size(); ++i) {
            if (i) ImGui::SameLine();
            const std::string w = U8(g_help.bar.words[i]) + "##w" + std::to_string(i);
            if (ImGui::SmallButton(w.c_str())) TakeWord(g_help.bar.words[i]);
        }
    } else {
        ImGui::TextUnformatted(" ");
    }
    // ---- channel, send language, input
    RenderChannelPicker();
    ImGui::SameLine();
    if (g_focusInput && !g_sending) {
        ImGui::SetKeyboardFocusHere();
        g_focusInput = false;
    }
    ImGui::SetNextItemWidth(-60);
    const ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCompletion |
                                      ImGuiInputTextFlags_CallbackHistory | ImGuiInputTextFlags_CallbackAlways;
    const bool enter = ImGui::InputText("##input", g_input, sizeof(g_input), flags, InputCallback);
    const ImVec2 inputMin = ImGui::GetItemRectMin();
    // The grey rest of the completion right after the text.
    if (const std::wstring rest = GhostRest(); !rest.empty() && ImGui::IsItemActive()) {
        const ImVec2 typed = ImGui::CalcTextSize(g_input);
        const ImGuiStyle& st = ImGui::GetStyle();
        ImGui::GetWindowDrawList()->AddText(ImVec2(inputMin.x + st.FramePadding.x + typed.x, inputMin.y + st.FramePadding.y),
                                            ImGui::GetColorU32(ImGuiCol_TextDisabled), U8(rest).c_str());
    }
    ImGui::SameLine();
    const size_t n = CodePointCount(W(g_input));
    ImGui::TextDisabled("%zu/%d", n, g_cfg.maxLength);
    // Changes of the text: a pause translates it; the typing help follows every key.
    const std::string now = g_input;
    if (now != g_lastInput) {
        g_lastInput = now;
        g_inputChanged = GetTickCount64();
        ++g_inputGen;
        if (!g_helpRequested) {
            g_helpRequested = true;
            g_worker.Post(ComputeTyping);
        }
    }
    if (g_inputChanged && GetTickCount64() - g_inputChanged > 400 && !now.empty() && Translating()) {
        g_inputChanged = 0;
        g_worker.Post([gen = g_inputGen, t = SanitizeChatText(W(now))] { TranslateInput(gen, t); });
    }
    if (enter) StartSend();
    // ---- status
    ImGui::TextDisabled("%s", g_status.empty() ? (g_chatEventsLive ? "" : U8(Tr(L"Waiting for chat lines (Events: Chat or Unofficial Extras)")).c_str())
                                               : U8(g_status).c_str());
    ImGui::End();
}

void RenderOptions() {
    if (!ImGui::GetCurrentContext()) return;
    ImGui::TextWrapped("GW2 Keyboard for Nexus %s – the chat in your language, and what you write in the chat's "
                       "language. The chat comes from Events: Chat (every channel) or Unofficial Extras (party and "
                       "squad). Nothing is sent without your Enter; nothing of other players is stored.",
                       GCT_VERSION_STR);
    ImGui::Checkbox("Show the window", &g_show);
    ImGui::TextDisabled(g_chatEventsLive ? "Events: Chat delivers." : "Waiting for chat lines (Events: Chat or "
                                                                      "Unofficial Extras).");
}

// Every callback runs behind these two: a C++ exception or an access violation ends our add-on, never the game.
void GuardedRender(void (*fn)()) {
    if (g_disabled) return;
    try {
        fn();
    } catch (...) {
        g_disabled = true;
        Log(LOGL_CRITICAL, "an error – switched off for this session");
    }
}
void SafeRender() {
    __try {
        GuardedRender(RenderWindow);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_disabled = true;
    }
}
void SafeOptions() {
    __try {
        GuardedRender(RenderOptions);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_disabled = true;
    }
}

std::wstring OwnDataDir() {  // <addons>\GW2Keyboard\ – never the game's folder
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(g_self, path, MAX_PATH);
    std::wstring p = path;
    p = p.substr(0, p.find_last_of(L"\\/") + 1) + L"GW2Keyboard";
    CreateDirectoryW(p.c_str(), nullptr);
    return p;
}

void AddonLoad(AddonAPI_t* api) {
    g_api = api;
    g_disabled = false;
    g_chatEventsLive = false;
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(api->ImguiContext));
    ImGui::SetAllocatorFunctions(reinterpret_cast<void* (*)(size_t, void*)>(api->ImguiMalloc),
                                 reinterpret_cast<void (*)(void*, void*)>(api->ImguiFree));
    {
        std::lock_guard<std::mutex> lock(g_mu);
        g_cfg.Load(OwnDataDir());
        if (g_cfg.writeLangs.empty()) g_cfg.writeLangs = {L"EN-GB"};
        SetUiLang(g_cfg.uiLang);
        g_translator = MakeTranslator(g_cfg);
        g_sendIdx = g_cfg.translateWriting ? 0 : static_cast<int>(g_cfg.writeLangs.size());
    }
    g_worker.Start();
    g_worker.Post(InitSpelling);
    if (api->GUI_Register) {
        api->GUI_Register(RT_Render, SafeRender);
        api->GUI_Register(RT_OptionsRender, SafeOptions);
    }
    if (api->InputBinds_RegisterWithString) {
        api->InputBinds_RegisterWithString(kToggleBind, OnToggle, "CTRL+ALT+K");
        api->InputBinds_RegisterWithString(kFocusBind, OnFocusKey, "(null)");
    }
    if (api->Events_Subscribe) {
        api->Events_Subscribe(bridge::kChatEventsEvent, OnChatEvents);
        api->Events_Subscribe("EV_UNOFFICIAL_EXTRAS_CHAT_MESSAGE", OnExtrasChat);
    }
    Log(LOGL_INFO, "loaded");
}

void AddonUnload() {
    if (g_api) {
        if (g_api->Events_Unsubscribe) {
            g_api->Events_Unsubscribe(bridge::kChatEventsEvent, OnChatEvents);
            g_api->Events_Unsubscribe("EV_UNOFFICIAL_EXTRAS_CHAT_MESSAGE", OnExtrasChat);
        }
        if (g_api->InputBinds_Deregister) {
            g_api->InputBinds_Deregister(kToggleBind);
            g_api->InputBinds_Deregister(kFocusBind);
        }
        if (g_api->GUI_Deregister) {
            g_api->GUI_Deregister(SafeRender);
            g_api->GUI_Deregister(SafeOptions);
        }
    }
    g_worker.Stop(FreeWorkerState);  // saved and freed on the worker (it owns them), before the DLL goes
    std::lock_guard<std::mutex> lock(g_mu);
    g_lines.clear();
    g_translator.reset();
    g_api = nullptr;
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) g_self = module;
    return TRUE;
}

extern "C" __declspec(dllexport) AddonDefinition_t* GetAddonDef() {
    g_def.Signature = static_cast<uint32_t>(-0x474B4244);  // random negative: not hosted on Raidcore (yet)
    g_def.APIVersion = NEXUS_API_VERSION;
    g_def.Name = kName;
    g_def.Version.Major = GCT_VER_MAJOR;
    g_def.Version.Minor = GCT_VER_MINOR;
    g_def.Version.Build = GCT_VER_PATCH;
    g_def.Version.Revision = 0;
    g_def.Author = "Emisan01";
    g_def.Description = "Typing help for the GW2 chat: the chat in your language, what you write translated and sent "
                        "on your Enter. Needs \"Events: Chat\" (or Unofficial Extras) for the chat.";
    g_def.Load = AddonLoad;
    g_def.Unload = AddonUnload;
    g_def.Flags = AF_None;
    g_def.Provider = UP_None;
    return &g_def;
}
