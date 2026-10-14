// kb_main.cpp — "GW2 Keyboard" as a Nexus add-on (docs/NEXUS_KEYBOARD_PLAN.md), modelled on the exe (src/kbd).
//
// An ImGui window inside the game:
//   - the chat from Vonsh's "Events: Chat" (every channel; installed on a click when missing), foreign lines translated
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
#include <psapi.h>

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
#include "core/json.hpp"
#include "core/langs.hpp"
#include "core/voice.hpp"
#include "core/languages.hpp"
#include "core/protect.hpp"
#include "core/send_guard.hpp"
#include "core/slang.hpp"
#include "core/text.hpp"
#include "core/translator.hpp"
#include "core/voice_learn.hpp"
#include "imgui/imgui.h"
#include "nexus/Nexus.h"
#include "version.h"
#include "win/deepl_translator.hpp"
#include "win/els.hpp"
#include "win/extras_install.hpp"
#include "win/files.hpp"
#include "win/gw2_sender.hpp"
#include "win/mumble_link.hpp"
#include "win/audio_capture.hpp"
#include "win/voice_models.hpp"
#include "win/online_translators.hpp"

using namespace gct;

namespace {

constexpr const char* kName = "GW2 Keyboard";
constexpr const char* kToggleBind = "KB_GW2KEYBOARD_TOGGLE";
constexpr const char* kFocusBind = "KB_GW2KEYBOARD_FOCUS";
constexpr const char* kTalkBind = "KB_GW2KEYBOARD_TALK";  // push to talk: held while you speak
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
bool g_showOptions = false;  // our own options window (menu ≡ / the button on the Nexus page)

void Log(ELogLevel level, const char* text) {
    if (g_api && g_api->Log) g_api->Log(level, kName, text);
}

std::string U8(const std::wstring& w) { return ToUtf8(w); }
std::wstring W(const std::string& s) { return FromUtf8(s); }
size_t U8Offset(const std::wstring& w, size_t idx) { return ToUtf8(w.substr(0, std::min(idx, w.size()))).size(); }
// For drawing UI text: Nexus' font has no ≡ → … ▾ and curly quotes – plain stand-ins instead of "?".
std::string UI(std::wstring w) {
    static const std::pair<const wchar_t*, const wchar_t*> kSwap[] = {
        {L"\u2026", L"..."}, {L"\u2192", L">"}, {L"\u2190", L"<"}, {L"\u2261", L"menu"}, {L"\u25BE", L""},
        {L"\u201C", L"\""}, {L"\u201D", L"\""}, {L"\u201E", L"\""}, {L"\u2018", L"'"}, {L"\u2019", L"'"}};
    for (const auto& [from, to] : kSwap)
        for (size_t at; (at = w.find(from)) != std::wstring::npos;) w.replace(at, std::wcslen(from), to);
    return ToUtf8(w);
}

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
std::wstring g_back;     // ... and that translated back into your language (small, under it – like the exe)
bool g_inMap = false;    // MumbleLink: a map is loaded (not character select, not a loading screen)
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

// Voice (stage 4): the helper process gw2kb_voice.exe runs the recognizer outside the game.
bool g_voiceReady = false;       // the helper said "ready"
bool g_recording = false;        // a recording runs
std::wstring g_voiceLive;        // the live text while speaking (only drawn, never written into the input)
std::wstring g_lastDictated;     // the last recognized text as it came (learning misheard words on send)
std::wstring g_dictLang;         // its language
VoiceFixBook g_voiceFixes;       // learned misheard words (<data>\voice-fixes.txt), per language
bool g_autoSendPending = false;
bool g_voiceStartPending = false;  // a press came while the helper was still starting  // "send after speaking": the dictation arrived, Enter follows by itself

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

std::shared_ptr<Translator> MakeTranslator(const Config& c) {  // the exe's choice: the engine you set, else the best key
    if (c.engine == Engine::Basic) return MakeMyMemoryTranslator(c.basicEmail);
    if (c.engine == Engine::DeepL && !c.deeplKey.empty()) return MakeDeepLTranslator(c.deeplKey);
    if (c.engine == Engine::Google && !c.googleKey.empty()) return MakeGoogleTranslator(c.googleKey);
    if (c.engine == Engine::Microsoft && !c.msKey.empty()) return MakeMicrosoftTranslator(c.msKey, c.msRegion);
    if (c.engine == Engine::Libre && !c.libreUrl.empty()) return MakeLibreTranslator(c.libreUrl, c.libreKey);
    if (!c.deeplKey.empty()) return MakeDeepLTranslator(c.deeplKey);
    if (!c.googleKey.empty()) return MakeGoogleTranslator(c.googleKey);
    if (!c.msKey.empty()) return MakeMicrosoftTranslator(c.msKey, c.msRegion);
    if (!c.libreUrl.empty()) return MakeLibreTranslator(c.libreUrl, c.libreKey);
    return MakeMyMemoryTranslator(c.basicEmail);
}

bool UsingMyMemory() { return g_translator && g_translator->Name().rfind(L"MyMemory", 0) == 0; }  // g_mu held

std::wstring Today() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t day[16];
    swprintf(day, 16, L"%04u%02u%02u", st.wYear, st.wMonth, st.wDay);
    return day;
}

void CountMyMemory(size_t chars) {  // the free contingent of a day (g_mu held)
    if (!UsingMyMemory() || chars == 0) return;
    const std::wstring day = Today();
    if (g_cfg.myMemoryDay != day) {
        g_cfg.myMemoryDay = day;
        g_cfg.myMemoryUsed = 0;
        g_cfg.SaveValue(L"Basic", L"UsedDay", day);
    }
    g_cfg.myMemoryUsed += static_cast<int>(chars);
    g_cfg.SaveValue(L"Basic", L"UsedChars", std::to_wstring(g_cfg.myMemoryUsed));
}

std::wstring MyMemoryQuotaText() {  // "MyMemory 25 / 5.0k today" (g_mu held)
    if (!UsingMyMemory()) return {};
    const int used = g_cfg.myMemoryDay == Today() ? g_cfg.myMemoryUsed : 0;
    const int limit = Trim(g_cfg.basicEmail).empty() ? 5000 : 50000;
    auto k = [](int v) {
        wchar_t b[16];
        if (v < 1000) swprintf(b, 16, L"%d", v);
        else swprintf(b, 16, L"%.1fk", v / 1000.0);
        return std::wstring(b);
    };
    return TrF(L"MyMemory {1} / {2} today", {k(std::min(used, limit)), k(limit)});
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
    {
        std::lock_guard<std::mutex> lock(g_mu);
        CountMyMemory(CodePointCount(text));
    }
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
            {
                std::lock_guard<std::mutex> lock(g_mu);
                CountMyMemory(CodePointCount(body));
            }
            const TranslateResult r = tr->Translate(p.segments, source, target);
            if (r.ok) result = r.text;
            else error = r.error;
        }
    }
    // The back-translation: what your message means in your language again (small under it, like the exe).
    std::wstring back;
    bool wantBack;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        wantBack = g_cfg.backTranslate && gen == g_inputGen && !g_sendPending;
    }
    if (translating && tr && wantBack && error.empty() && result != body) {
        ProtectedText p = ProtectForTranslation(result, nullptr, g_spell ? &g_spell->KeepWords() : &BuiltinKeepWords());
        if (HasTranslatableText(p.segments)) {
            {
                std::lock_guard<std::mutex> lock(g_mu);
                CountMyMemory(CodePointCount(result));
            }
            const TranslateResult r = tr->Translate(p.segments, SourceCode(target), typing);
            if (r.ok) back = SanitizeChatText(r.text);
        }
    }
    std::vector<std::wstring> parts;
    bool sendNow = false;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        if (gen != g_inputGen && !g_sendPending) return;  // the text changed meanwhile
        g_preview = SanitizeChatText(result);
        g_back = back;
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
    bool copyOnly;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        copyOnly = g_cfg.copyOnly;
    }
    if (ok && copyOnly) {  // not a single key reaches the game (invariant 1): only the clipboard
        std::wstring all;
        for (const std::wstring& part : parts) all += (all.empty() ? L"" : L" ") + part;
        ok = CopyTextToClipboard(gw2, all);
        if (!ok) error = Tr(L"The clipboard is blocked");
        parts.clear();
    }
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
    {  // misheard words: what was dictated vs. what you finally sent ("heard = meant", per language)
        std::lock_guard<std::mutex> lock(g_mu);
        if (ok && !g_lastDictated.empty()) {
            if (!g_voiceFixes.Learn(g_lastDictated, learnText, g_dictLang).empty())
                WriteFileAtomic(g_cfg.dataDir + L"\\voice-fixes.txt", ToUtf8(g_voiceFixes.Serialize()));
            g_lastDictated.clear();
        }
    }
    std::lock_guard<std::mutex> lock(g_mu);
    g_sending = false;
    if (ok) {
        SetStatus(copyOnly ? Tr(L"Copied – paste it into the GW2 chat with Ctrl+V") : L"");
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
// The voice helper: a hidden process with pipes; its events are read on our own thread.
class VoiceHelper {
public:
    bool Start(const std::wstring& exe, const std::wstring& dataDir) {
        Stop();
        SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
        HANDLE inRead = nullptr, outWrite = nullptr;
        if (!CreatePipe(&inRead, &inWrite_, &sa, 0)) return false;
        if (!CreatePipe(&outRead_, &outWrite, &sa, 0)) {
            CloseHandle(inRead);
            Close();
            return false;
        }
        SetHandleInformation(inWrite_, HANDLE_FLAG_INHERIT, 0);  // our ends stay ours
        SetHandleInformation(outRead_, HANDLE_FLAG_INHERIT, 0);
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = inRead;
        si.hStdOutput = outWrite;
        si.hStdError = outWrite;
        std::wstring cmd = L"\"" + exe + L"\" --data \"" + dataDir + L"\"";
        PROCESS_INFORMATION pi{};
        const BOOL ok = CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, TRUE,
                                       CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, nullptr, nullptr, &si, &pi);
        CloseHandle(inRead);
        CloseHandle(outWrite);
        if (!ok) {
            Close();
            return false;
        }
        CloseHandle(pi.hThread);
        process_ = pi.hProcess;
        reader_ = std::thread([this] { Read(); });
        return true;
    }
    void Send(const char* cmd) {
        if (!inWrite_) return;
        const std::string line = std::string(cmd) + "\n";
        DWORD n = 0;
        WriteFile(inWrite_, line.data(), static_cast<DWORD>(line.size()), &n, nullptr);
    }
    void Stop() {
        if (process_) {
            Send("quit");
            if (WaitForSingleObject(process_, 2000) != WAIT_OBJECT_0) TerminateProcess(process_, 0);
        }
        Close();
        if (reader_.joinable()) reader_.join();  // its pipe is closed: ReadFile returns
    }
    bool Running() const { return process_ != nullptr; }

private:
    void Close() {
        if (inWrite_) CloseHandle(inWrite_);
        if (process_) CloseHandle(process_);
        inWrite_ = process_ = nullptr;
        if (outRead_ && !reader_.joinable()) {
            CloseHandle(outRead_);
            outRead_ = nullptr;
        }
    }
    void Read() {
        std::string pending;
        char buf[2048];
        DWORD n = 0;
        while (ReadFile(outRead_, buf, sizeof(buf), &n, nullptr) && n > 0) {
            pending.append(buf, n);
            size_t nl;
            while ((nl = pending.find('\n')) != std::string::npos) {
                const std::string line = pending.substr(0, nl);
                pending.erase(0, nl + 1);
                try {
                    OnLine(line);
                } catch (...) {
                }
            }
        }
        CloseHandle(outRead_);
        outRead_ = nullptr;
        std::lock_guard<std::mutex> lock(g_mu);
        g_voiceReady = g_recording = false;
        g_voiceLive.clear();
    }
    static void OnLine(const std::string& line);
    HANDLE process_ = nullptr, inWrite_ = nullptr, outRead_ = nullptr;
    std::thread reader_;
};

VoiceHelper g_voice;

// A recognized text goes into the input (after what is there, with a space), checked by you, sent by your Enter.
void VoiceHelper::OnLine(const std::string& line) {
    JsonValue v;
    if (!ParseJson(line, v) || !v.IsObject()) return;
    const std::string t = v.GetString("t");
    std::lock_guard<std::mutex> lock(g_mu);
    if (t == "ready") {
        g_voiceReady = true;
        if (g_voiceStartPending) {  // the press that started the helper: its recording now
            g_voiceStartPending = false;
            g_voice.Send("begin-auto");
        }
    } else if (t == "started") {
        g_recording = true;
        g_voiceLive.clear();
        SetStatus(Tr(L"Hearing you … release the key when you are done"), 0);
    } else if (t == "partial") {
        g_voiceLive = W(v.GetString("stable")) + W(v.GetString("unsure"));
    } else if (t == "final") {
        g_recording = false;
        g_voiceLive.clear();
        const std::wstring text = Trim(W(v.GetString("text")));
        if (v.GetBool("empty") || text.empty()) {
            SetStatus(Tr(L"Nothing recognized – hold the key while you speak"), 5000);
            return;
        }
        g_dictLang = g_cfg.voiceTranslate ? std::wstring(L"EN") : ToUpperAscii(W(v.GetString("lang")));
        const std::wstring fixed = g_voiceFixes.Apply(text, g_dictLang);
        g_lastDictated = fixed;
        const size_t len = std::strlen(g_input);
        g_edit.active = true;
        g_edit.startByte = len;
        g_edit.lenBytes = 0;
        g_edit.insert = (len && g_input[len - 1] != ' ' ? " " : "") + U8(fixed);
        g_focusInput = true;
        g_autoSendPending = g_cfg.voiceAutoSend;  // your key release was the press; off by default
        SetStatus(L"");
    } else if (t == "error") {
        g_recording = false;
        g_voiceLive.clear();
        SetStatus(W(v.GetString("msg")), 10000);
    }
}

std::wstring HelperExe() {  // <addons>\GW2Keyboard\gw2kb_voice.exe
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(g_self, path, MAX_PATH);
    std::wstring p = path;
    return p.substr(0, p.find_last_of(L"\\/") + 1) + L"GW2Keyboard\\gw2kb_voice.exe";
}

// Push to talk: pressed starts, released ends (the recognized text then waits for your Enter).
void TalkDown() {
    {
        std::lock_guard<std::mutex> lock(g_mu);
        if (!g_cfg.voiceEnabled) {
            SetStatus(Tr(L"Voice input is off (options of GW2 Keyboard)"));
            return;
        }
        if (g_recording) return;
        if (!g_voiceReady) {
            // Loaded on first use: the helper starts now; the recording begins as soon as it is ready (a pause ends it).
            g_voiceStartPending = true;
            SetStatus(Tr(L"Voice input is starting – a moment"), 0);
        }
    }
    if (!g_voiceReady) {
        if (!g_voice.Running()) {
            if (GetFileAttributesW(HelperExe().c_str()) == INVALID_FILE_ATTRIBUTES ||
                !g_voice.Start(HelperExe(), g_cfg.dataDir)) {
                std::lock_guard<std::mutex> lock(g_mu);
                g_voiceStartPending = false;
                SetStatus(Tr(L"The voice module is missing"), 10000);
            }
        }
        return;
    }
    g_voice.Send(g_cfg.voiceToggle ? "begin-auto" : "begin");
}
void TalkUp() { g_voice.Send("end"); }
void TalkLocked() {  // right-click on the microphone: on until a pause (or the next right-click)
    {
        std::lock_guard<std::mutex> lock(g_mu);
        if (!g_cfg.voiceEnabled || !g_voiceReady || g_recording) return;
    }
    g_voice.Send("begin-auto");
}

void OnTalkKey(const char*, bool release) {
    if (g_disabled) return;
    bool toggle;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        toggle = g_cfg.voiceToggle;
    }
    if (toggle) {  // press once: starts; a pause ends it (or the next press)
        if (release) return;
        bool rec;
        {
            std::lock_guard<std::mutex> lock(g_mu);
            rec = g_recording;
        }
        if (rec) TalkUp();
        else TalkDown();
        return;
    }
    if (release) TalkUp();
    else TalkDown();
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

// The grey text after the caret, like the exe's DrawGhost: what Space writes (a completion shows its rest, a
// correction "> word"); after a space the sure next word or phrase. `replace` gets what Tab takes.
std::wstring GhostRest(bool* correction = nullptr) {
    if (correction) *correction = false;
    const std::wstring text = W(g_input);
    if (g_help.text != text || text.empty()) return {};
    const WordChoices& c = g_help.choices;
    if (text.back() != L' ' && c.highlight >= 0 && c.highlight < static_cast<int>(c.words.size()) &&
        c.replace.end() == text.size() && !(c.firstIsTyped && c.highlight == 0)) {
        const std::wstring& word = c.words[c.highlight];
        const std::wstring typed = text.substr(c.replace.start, c.replace.length);
        if (typed.size() < word.size() && CaseFold(word.substr(0, typed.size())) == CaseFold(typed))
            return word.substr(typed.size());
        if (correction) *correction = true;
        return L"  > " + word;
    }
    const WordSuggestions& b = g_help.bar;
    if (b.kind == WordSuggestions::Kind::Next && !b.words.empty() && text.back() == L' ')
        return b.phrase.empty() ? b.words[0] : b.phrase;
    return {};
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
        case ImGuiInputTextFlags_CallbackCompletion: {  // Tab: take the grey word (completion, correction, next word)
            bool correction = false;
            const std::wstring rest = GhostRest(&correction);
            if (rest.empty() || d->CursorPos != d->BufTextLen) break;
            if (correction) {
                const std::wstring text = W(std::string(d->Buf, d->BufTextLen));
                const Span r = g_help.choices.replace;
                const std::wstring word = g_help.choices.words[g_help.choices.highlight];
                const int startByte = static_cast<int>(U8Offset(text, r.start));
                d->DeleteChars(startByte, d->BufTextLen - startByte);
                d->InsertChars(startByte, U8(word + L" ").c_str());
                g_worker.Post([text, start = r.start, word] { if (g_spell) g_spell->Chose(text, start, word, true); });
            } else {
                const bool next = !g_input[0] || g_input[std::strlen(g_input) - 1] == ' ';
                d->InsertChars(d->CursorPos, (U8(rest) + (next ? "" : " ")).c_str());
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

std::wstring NativeName(const std::wstring& code) {
    if (const LangInfo* l = FindLanguage(code)) return l->native;
    return ToUpperAscii(code);
}

// The footer chips of the exe: channel | "Write: German" | "Send: English (UK) · MyMemory" (orange) | auto | counter.
void RenderFooter() {  // g_mu held
    static const Channel kChannels[] = {Channel::Unknown, Channel::Say,   Channel::Map,  Channel::Party,
                                        Channel::Squad,   Channel::Guild, Channel::Team, Channel::Whisper};
    auto chipWidth = [](const std::string& s) { return ImGui::CalcTextSize(s.c_str()).x + ImGui::GetFrameHeight() + 12; };
    // Channel.
    std::string label = g_channel == Channel::Unknown ? UI(Tr(L"Active channel")) : UI(ChannelLabel(g_channel));
    if (g_channel == Channel::Whisper && !g_whisperTo.empty()) label += " " + U8(g_whisperTo);
    ImGui::SetNextItemWidth(chipWidth(label));
    if (ImGui::BeginCombo("##channel", label.c_str())) {
        for (Channel c : kChannels) {
            const std::string name = c == Channel::Unknown ? UI(Tr(L"Active channel")) : UI(ChannelLabel(c));
            if (ImGui::Selectable(name.c_str(), c == g_channel)) {
                g_channel = c;
                if (c != Channel::Whisper) g_whisperTo.clear();
                g_focusInput = true;
            }
        }
        ImGui::EndCombo();
    }
    // The language you write in (spelling, learned words and the prediction follow it).
    ImGui::SameLine();
    const std::string write = UI(TrF(L"Write: {1}", {g_cfg.writeIn.empty() ? Tr(L"like Windows") : NativeName(g_cfg.writeIn)}));
    ImGui::SetNextItemWidth(chipWidth(write));
    if (ImGui::BeginCombo("##write", write.c_str())) {
        std::wstring chosen = g_cfg.writeIn;
        if (ImGui::Selectable(UI(Tr(L"like Windows")).c_str(), chosen.empty())) chosen.clear();
        for (const LangInfo& l : Languages())
            if (l.latinScript && ImGui::Selectable(U8(l.native).c_str(), g_cfg.writeIn == l.code)) chosen = l.code;
        if (chosen != g_cfg.writeIn) {
            g_cfg.writeIn = chosen;
            g_cfg.SaveValue(L"Spelling", L"WriteIn", chosen);
            g_worker.Post([] {  // spelling and learned words follow the language you write in
                FreeWorkerState();
                InitSpelling();
            });
            ++g_inputGen;
            g_inputChanged = GetTickCount64();
        }
        ImGui::EndCombo();
    }
    // What the chat gets, and who translates (orange like the exe).
    ImGui::SameLine();
    std::wstring sendText = SendOriginal() ? Tr(L"as written") : NativeName(SendLang());
    if (Translating() && g_translator) sendText += L" · " + g_translator->Name();
    const std::string send = UI(TrF(L"Send: {1}", {sendText}));
    ImGui::SetNextItemWidth(chipWidth(send));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.94f, 0.75f, 0.35f, 1.f));
    const bool open = ImGui::BeginCombo("##send", send.c_str());
    ImGui::PopStyleColor();
    if (open) {
        for (size_t i = 0; i <= g_cfg.writeLangs.size(); ++i) {
            const std::string n = i == g_cfg.writeLangs.size() ? UI(Tr(L"as written")) : U8(NativeName(g_cfg.writeLangs[i]));
            if (ImGui::Selectable(n.c_str(), static_cast<int>(i) == g_sendIdx)) {
                g_sendIdx = static_cast<int>(i);
                ++g_inputGen;
                g_preview.clear();
                g_inputChanged = GetTickCount64();
                g_focusInput = true;
            }
        }
        ImGui::Separator();
        for (const LangInfo& l : Languages()) {  // more languages: the chosen one becomes the first
            if (!l.latinScript || std::find(g_cfg.writeLangs.begin(), g_cfg.writeLangs.end(), l.code) != g_cfg.writeLangs.end())
                continue;
            if (ImGui::Selectable(U8(l.native).c_str(), false)) {
                g_cfg.writeLangs.insert(g_cfg.writeLangs.begin(), l.code);
                g_cfg.SaveAll();
                g_sendIdx = 0;
                ++g_inputGen;
                g_preview.clear();
                g_inputChanged = GetTickCount64();
            }
        }
        ImGui::EndCombo();
    }
    if (g_cfg.voiceEnabled) {  // send right after speaking (off by default)
        ImGui::SameLine();
        if (ImGui::Checkbox(UI(Tr(L"auto") + L"##autosend").c_str(), &g_cfg.voiceAutoSend))
            g_cfg.SaveBool(L"Voice", L"AutoSend", g_cfg.voiceAutoSend);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", UI(Tr(L"Send right after speaking (releasing the key counts as your Enter)")).c_str());
    }
    const size_t n = CodePointCount(W(g_input));
    const std::string counter = std::to_string(n) + "/" + std::to_string(g_cfg.maxLength);
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImGui::CalcTextSize(counter.c_str()).x);
    ImGui::TextDisabled("%s", counter.c_str());
}

// ---- "Events: Chat": is it there, and installing it the way the Nexus library would.
std::atomic<bool> g_chatEventsPresent{false};
std::atomic<bool> g_chatEventsInstalling{false};
std::thread g_chatEventsThread;

std::wstring AddonsDir() {  // the folder our DLL lies in: <GW2>\addons\ .
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(g_self, path, MAX_PATH);
    std::wstring p = path;
    return p.substr(0, p.find_last_of(L"\\/") + 1);
}

// A Nexus add-on in this process that says it is "Events: Chat" (its definition is only read). Every 3 s at most.
bool ChatEventsPresent() {
    static ULONGLONG next = 0;
    if (GetTickCount64() < next) return g_chatEventsPresent;
    next = GetTickCount64() + 3000;
    bool found = false;
    HMODULE mods[1024];
    DWORD needed = 0;
    if (K32EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &needed)) {
        const DWORD n = std::min<DWORD>(needed / sizeof(HMODULE), 1024);
        using GetDef = AddonDefinition_t* (*)();
        for (DWORD i = 0; i < n && !found; ++i) {
            if (mods[i] == g_self) continue;
            if (auto get = reinterpret_cast<GetDef>(GetProcAddress(mods[i], "GetAddonDef"))) {
                const AddonDefinition_t* def = get();
                found = def && def->Signature == bridge::kChatEventsSignature;
            }
        }
    }
    g_chatEventsPresent = found;
    return found;
}

void InstallChatEventsNow() {  // on your click: newest release, checksum checked, into the add-ons folder
    if (g_chatEventsInstalling.exchange(true)) return;
    if (g_chatEventsThread.joinable()) g_chatEventsThread.join();
    g_chatEventsThread = std::thread([dir = AddonsDir()] {
        const ExtrasInstallResult r = InstallChatEvents(dir, [] { return g_disabled.load(); });
        g_chatEventsInstalling = false;
        std::lock_guard<std::mutex> lock(g_mu);
        SetStatus(r.code == ExtrasInstallCode::Ok ? Tr(L"Events: Chat installed – Nexus loads it (restart GW2 if no chat arrives)")
                  : r.code == ExtrasInstallCode::NoAnswer ? TrF(L"GitHub did not answer ({1}) – please try again later", {r.detail})
                                                          : TrF(L"Loading {1} failed: {2}", {L"Events: Chat", r.detail}),
                  12000);
    });
}

// ---- the header's icons, drawn like the exe's: microphone, menu (three lines), close
enum class Icon { Mic, Menu, Close };
bool IconButton(const char* id, Icon kind, bool active, bool* rightClick) {
    const float s = ImGui::GetFrameHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(id, ImVec2(s, s));
    const bool hovered = ImGui::IsItemHovered();
    if (rightClick) *rightClick = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 col = active ? IM_COL32(235, 85, 85, 255)
                             : ImGui::GetColorU32(hovered ? ImGuiCol_Text : ImGuiCol_TextDisabled);
    const ImVec2 c(p.x + s * 0.5f, p.y + s * 0.5f);
    const float u = s / 18.0f;
    switch (kind) {
        case Icon::Mic:  // a capsule on a stand
            dl->AddRectFilled(ImVec2(c.x - 2.5f * u, c.y - 6 * u), ImVec2(c.x + 2.5f * u, c.y + 1.5f * u), col, 2.5f * u);
            dl->PathArcTo(ImVec2(c.x, c.y - 1 * u), 4.5f * u, 0.0f, 3.14159f, 10);
            dl->PathStroke(col, false, 1.4f * u);
            dl->AddLine(ImVec2(c.x, c.y + 3.5f * u), ImVec2(c.x, c.y + 6 * u), col, 1.4f * u);
            dl->AddLine(ImVec2(c.x - 3 * u, c.y + 6 * u), ImVec2(c.x + 3 * u, c.y + 6 * u), col, 1.4f * u);
            break;
        case Icon::Menu:  // ≡
            for (int i = -1; i <= 1; ++i)
                dl->AddLine(ImVec2(c.x - 5.5f * u, c.y + i * 4 * u), ImVec2(c.x + 5.5f * u, c.y + i * 4 * u), col, 1.6f * u);
            break;
        case Icon::Close:  // ×
            dl->AddLine(ImVec2(c.x - 4.5f * u, c.y - 4.5f * u), ImVec2(c.x + 4.5f * u, c.y + 4.5f * u), col, 1.6f * u);
            dl->AddLine(ImVec2(c.x - 4.5f * u, c.y + 4.5f * u), ImVec2(c.x + 4.5f * u, c.y - 4.5f * u), col, 1.6f * u);
            break;
    }
    return pressed;
}

// Cut to the width with "…" at the end.
std::string Fit(const std::string& s, float width) {
    if (ImGui::CalcTextSize(s.c_str()).x <= width) return s;
    std::wstring w = W(s);
    while (!w.empty() && ImGui::CalcTextSize((U8(w) + "...").c_str()).x > width) w.pop_back();
    return U8(w) + "...";
}

// The status line at the top left (like the exe): a message, else what is going on.
std::wstring StatusText() {  // g_mu held
    if (!g_status.empty()) return g_status;
    if (g_recording) return Tr(L"Hearing you … release the key when you are done");
    if (!g_chatEventsLive && !g_chatEventsPresent) return Tr(L"Every channel needs Events: Chat – ≡ → Options installs it");
    if (const std::wstring q = MyMemoryQuotaText(); !q.empty()) return q;
    std::wstring route = SendOriginal() ? Tr(L"as written") : ToUpperAscii(PrimaryLang(SendLang()));
    if (Translating() && g_translator) route += L" · " + g_translator->Name();
    return TrF(L"Send: {1}", {route});
}

bool g_micLocked = false;  // right-click on the microphone: on until the next right-click
bool g_compact = false;    // menu ≡: only the newest lines of the chat

void RenderOptionsContent();

void RenderOptionsWindow() {
    if (!g_showOptions) return;
    ImGui::SetNextWindowSize(ImVec2(520, 640), ImGuiCond_FirstUseEver);
    if (ImGui::Begin((UI(Tr(L"GW2 Keyboard – Options")) + "###kbopts").c_str(), &g_showOptions)) RenderOptionsContent();
    ImGui::End();
}

void RenderWindow() {
    if (!ImGui::GetCurrentContext()) return;  // failsafe: no context, no drawing
    ImGuiIO& io = ImGui::GetIO();
    if (io.DisplaySize.x <= 0 || io.DisplaySize.y <= 0) return;
    g_keyboardFree = !io.WantCaptureKeyboard;
    g_fps = static_cast<int>(io.Framerate);
    {
        // MumbleLink (every 250 ms): shown only on a map – never at character select or on a loading screen (like the
        // exe waiting for GW2); your character's name; the GW2 chat line opening (focus behaviour).
        static ULONGLONG next = 0;
        static MumbleLink mumble;
        static bool chatWasOpen = false;
        if (GetTickCount64() > next) {
            next = GetTickCount64() + 250;
            const MumbleState ms = mumble.Read();
            std::lock_guard<std::mutex> lock(g_mu);
            g_inMap = ms.live && ms.inMap;
            if (ms.live) g_ownName = ms.identity.name;
            const bool chatOpen = ms.live && ms.TextboxHasFocus();
            if (chatOpen && !chatWasOpen && g_cfg.focusOnGameChat && g_show) g_focusInput = true;
            chatWasOpen = chatOpen;
        }
    }
    ChatEventsPresent();
    RenderOptionsWindow();
    bool inMap;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        inMap = g_inMap;
    }
    if (!g_show || !inMap) return;
    ImGui::SetNextWindowSize(ImVec2(520, 360), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(std::clamp(g_cfg.opacity, 60, 255) / 255.0f);
    if (!ImGui::Begin("GW2 Keyboard###kbmain", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    ImGui::SetWindowFontScale(std::clamp(g_cfg.fontPercent, 70, 200) / 100.0f);
    std::unique_lock<std::mutex> lock(g_mu);
    if (g_statusUntil && GetTickCount64() > g_statusUntil) {
        g_status.clear();
        g_statusUntil = 0;
    }
    const ImVec4 kAccent(0.94f, 0.75f, 0.35f, 1.f), kPreview(0.59f, 0.80f, 1.f, 1.f);
    // ---- header: status at the left; microphone, menu, close at the right
    const float icon = ImGui::GetFrameHeight();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const int icons = g_cfg.voiceEnabled ? 3 : 2;
    const float statusW = ImGui::GetContentRegionAvail().x - icons * (icon + spacing);
    const std::string status = UI(StatusText());
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(g_status.empty() ? ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled) : kAccent, "%s",
                       Fit(status, statusW).c_str());
    if (ImGui::IsItemHovered() && Fit(status, statusW) != status) ImGui::SetTooltip("%s", status.c_str());
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - icons * (icon + spacing) + spacing);
    if (g_cfg.voiceEnabled) {
        // Hold with the left button to speak; a right-click: on until a pause (or the next right-click).
        bool right = false;
        IconButton("##mic", Icon::Mic, g_recording, &right);
        static bool held = false;
        if (ImGui::IsItemActivated()) {
            held = true;
            lock.unlock();
            TalkDown();
            lock.lock();
        } else if (held && !ImGui::IsItemActive()) {
            held = false;
            if (!g_micLocked && !g_cfg.voiceToggle) TalkUp();
        }
        if (right) {
            const bool stop = g_recording;
            g_micLocked = !stop;
            lock.unlock();
            if (stop) TalkUp();
            else TalkLocked();
            lock.lock();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", UI(Tr(L"Hold to speak (or the push-to-talk key in Nexus) · right-click: on / off")).c_str());
        ImGui::SameLine();
    }
    if (IconButton("##menu", Icon::Menu, false, nullptr)) ImGui::OpenPopup("kbmenu");
    if (ImGui::BeginPopup("kbmenu")) {
        if (ImGui::MenuItem(UI(Tr(L"Options …")).c_str())) g_showOptions = true;
        ImGui::MenuItem(UI(Tr(L"Compact view")).c_str(), nullptr, &g_compact);
        if (!g_chatEventsPresent && ImGui::MenuItem(UI(Tr(L"Install Events: Chat (every channel) …")).c_str()))
            g_showOptions = true;
        ImGui::Separator();
        if (ImGui::MenuItem(UI(Tr(L"Hide (Ctrl+Alt+K shows it again)")).c_str())) g_show = false;
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (IconButton("##close", Icon::Close, false, nullptr)) g_show = false;
    if (!g_recording && g_micLocked) g_micLocked = false;  // the recording ended (pause, maximum length)

    // ---- the chat (the newest at the bottom): name white – a click answers with a whisper –, text in the channel colour
    const float line = ImGui::GetTextLineHeightWithSpacing();
    const float lowerH = line * 3.2f + ImGui::GetFrameHeightWithSpacing() * 3 + 10;
    ImGui::BeginChild("chat", ImVec2(0, g_compact ? line * 3 + 4 : -lowerH), false);
    const size_t first = g_compact && g_lines.size() > 3 ? g_lines.size() - 3 : 0;
    for (size_t i = first; i < g_lines.size(); ++i) {
        const ChatLine& l = g_lines[i];
        ImGui::PushID(static_cast<int>(l.id));
        if (!l.speaker.empty()) {
            const std::string name = UI((l.outgoing ? L"> " : L"") + l.speaker + L":");
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            if (ImGui::SmallButton(name.c_str()) && !l.own) {
                g_channel = Channel::Whisper;
                g_whisperTo = l.speaker;
                g_focusInput = true;
            }
            ImGui::PopStyleColor();
            ImGui::SameLine(0, 2);
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

    // ---- the preview (like the exe): what arrives in the GW2 chat, the back-translation small under it
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
    ImGui::BeginChild("preview", ImVec2(0, line * 3.2f), false);
    const bool previewCurrent = g_previewGen == g_inputGen && !g_preview.empty();
    if (g_recording) {
        ImGui::TextColored(kPreview, "%s", g_voiceLive.empty() ? "..." : U8(g_voiceLive).c_str());
    } else if (!g_input[0]) {
        ImGui::TextDisabled("%s", UI(Tr(L"Write in your language – here you see what arrives in the GW2 chat.")).c_str());
    } else if (!Translating()) {
        ImGui::PushStyleColor(ImGuiCol_Text, kPreview);
        ImGui::TextWrapped("%s", U8(ComposePrefix() + SanitizeChatText(W(g_input))).c_str());
        ImGui::PopStyleColor();
    } else if (previewCurrent) {
        ImGui::PushStyleColor(ImGuiCol_Text, kPreview);
        ImGui::TextWrapped("%s", U8(ComposePrefix() + g_preview).c_str());
        ImGui::PopStyleColor();
        if (!g_back.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("%s", U8(g_back).c_str());
            ImGui::PopStyleColor();
        }
    } else {
        ImGui::TextDisabled("%s", UI(Tr(L"translating …")).c_str());
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    // ---- the word bar (like the exe): three cells across the line, what Tab takes in orange
    {
        const float w = ImGui::GetContentRegionAvail().x;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = ImGui::GetFrameHeight();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const bool have = g_cfg.suggestions && g_help.text == W(g_input);
        for (int i = 0; i < 3; ++i) {
            const ImVec2 a(p.x + w * i / 3.0f, p.y), b(p.x + w * (i + 1) / 3.0f, p.y + h);
            if (i) dl->AddLine(ImVec2(a.x, a.y + h * 0.25f), ImVec2(a.x, b.y - h * 0.25f), ImGui::GetColorU32(ImGuiCol_Separator));
            if (!have || i >= static_cast<int>(g_help.bar.words.size())) continue;
            const std::string word = U8(g_help.bar.words[i]);
            ImGui::SetCursorScreenPos(a);
            ImGui::PushID(i);
            if (ImGui::InvisibleButton("##word", ImVec2(b.x - a.x, h))) TakeWord(g_help.bar.words[i]);
            const bool hot = ImGui::IsItemHovered();
            ImGui::PopID();
            if (hot) dl->AddRectFilled(a, b, ImGui::GetColorU32(ImGuiCol_FrameBgHovered));
            const ImVec2 size = ImGui::CalcTextSize(word.c_str());
            const bool pick = i == g_help.bar.autoIndex;
            dl->AddText(ImVec2((a.x + b.x - size.x) * 0.5f, a.y + (h - size.y) * 0.5f),
                        pick ? ImGui::GetColorU32(kAccent) : ImGui::GetColorU32(ImGuiCol_Text), word.c_str());
        }
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h + ImGui::GetStyle().ItemSpacing.y));
    }

    // ---- the input line
    if (g_focusInput && !g_sending) {
        ImGui::SetKeyboardFocusHere();
        g_focusInput = false;
    }
    ImGui::SetNextItemWidth(-1);
    const ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCompletion |
                                      ImGuiInputTextFlags_CallbackHistory | ImGuiInputTextFlags_CallbackAlways;
    const bool enter = ImGui::InputText("##input", g_input, sizeof(g_input), flags, InputCallback);
    const ImVec2 inputMin = ImGui::GetItemRectMin();
    if (const std::wstring rest = GhostRest(); !rest.empty() && ImGui::IsItemActive()) {  // what Space / Tab writes
        const ImVec2 typed = ImGui::CalcTextSize(g_input);
        const ImGuiStyle& st = ImGui::GetStyle();
        ImGui::GetWindowDrawList()->AddText(ImVec2(inputMin.x + st.FramePadding.x + typed.x, inputMin.y + st.FramePadding.y),
                                            ImGui::GetColorU32(ImGuiCol_TextDisabled), UI(rest).c_str());
    }

    // ---- the footer (like the exe): channel | write | send · translator | auto | counter
    RenderFooter();
    // Changes of the text: a pause translates it; the typing help follows every key.
    const std::string now = g_input;
    if (now != g_lastInput) {
        g_lastInput = now;
        g_inputChanged = GetTickCount64();
        ++g_inputGen;
        g_back.clear();
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
    if (g_autoSendPending && !g_edit.active && g_input[0]) {  // the dictation is in the line now
        g_autoSendPending = false;
        StartSend();
    }
    lock.unlock();
    ImGui::End();
}

// ---- voice model download (its own thread: minutes, never on the worker or the game's threads)
std::thread g_download;
std::atomic<bool> g_downloadCancel{false};
std::atomic<int> g_downloadPct{-1};  // -1: none running

void StartModelDownload(const VoiceModelInfo& m) {
    if (g_downloadPct >= 0) return;
    if (g_download.joinable()) g_download.join();
    g_downloadCancel = false;
    g_downloadPct = 0;
    std::wstring dir;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        dir = g_cfg.VoiceModelDir();
    }
    g_download = std::thread([m, dir] {
        EnsureDir(dir);
        std::wstring error;
        const bool ok = DownloadVoiceModel(
            dir, m,
            [](uint64_t done, uint64_t total) {
                if (total) g_downloadPct = static_cast<int>(done * 100 / total);
                return true;
            },
            &g_downloadCancel, &error);
        g_downloadPct = -1;
        std::lock_guard<std::mutex> lock(g_mu);
        SetStatus(ok ? Tr(L"Voice model installed") : error, 10000);
        if (ok) g_voice.Send("reload");
    });
}

// A language combo over Languages(): `value` "" = `emptyLabel`.
bool LanguageCombo(const char* id, std::wstring& value, const std::wstring& emptyLabel) {
    std::string current = value.empty() ? U8(emptyLabel) : U8(value);
    for (const LangInfo& l : Languages())
        if (PrimaryLang(l.code) == PrimaryLang(value) && !value.empty()) current = U8(std::wstring(l.native) + L" (" + l.code + L")");
    bool changed = false;
    if (ImGui::BeginCombo(id, current.c_str())) {
        if (!emptyLabel.empty() && ImGui::Selectable(U8(emptyLabel).c_str(), value.empty())) {
            value.clear();
            changed = true;
        }
        for (const LangInfo& l : Languages()) {
            const std::string n = U8(std::wstring(l.native) + L" (" + l.code + L")");
            if (ImGui::Selectable(n.c_str(), value == l.code)) {
                value = l.code;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

// Our options window: the exe's settings pages as tabs (as far as they make sense inside Nexus). "Save" applies.
void RenderOptionsContent() {
    static Config edit;
    static bool loaded = false;
    static char keyBuf[256] = {}, regionBuf[64] = {}, urlBuf[256] = {}, mailBuf[128] = {};
    static std::vector<AudioDevice> mics;
    static bool forgetAsked = false;
    if (!loaded) {
        std::lock_guard<std::mutex> lock(g_mu);
        edit = g_cfg;
        loaded = true;
        const std::string key = U8(edit.engine == Engine::DeepL ? edit.deeplKey : edit.engine == Engine::Google ? edit.googleKey
                                   : edit.engine == Engine::Microsoft ? edit.msKey : edit.libreKey);
        strncpy_s(keyBuf, key.c_str(), _TRUNCATE);
        strncpy_s(regionBuf, U8(edit.msRegion).c_str(), _TRUNCATE);
        strncpy_s(urlBuf, U8(edit.libreUrl).c_str(), _TRUNCATE);
        strncpy_s(mailBuf, U8(edit.basicEmail).c_str(), _TRUNCATE);
        mics = AudioCapture::Devices();
        forgetAsked = false;
    }
    const float wide = 280;
    auto combo = [&](const char* id, const std::vector<std::string>& items, int* sel) {
        bool changed = false;
        ImGui::SetNextItemWidth(wide);
        if (ImGui::BeginCombo(id, items[std::clamp(*sel, 0, static_cast<int>(items.size()) - 1)].c_str())) {
            for (int i = 0; i < static_cast<int>(items.size()); ++i)
                if (ImGui::Selectable(items[i].c_str(), i == *sel)) {
                    *sel = i;
                    changed = true;
                }
            ImGui::EndCombo();
        }
        return changed;
    };
    if (ImGui::BeginTabBar("##tabs")) {
        // ---- General
        if (ImGui::BeginTabItem(UI(Tr(L"General")).c_str())) {
            ImGui::SetNextItemWidth(wide);
            LanguageCombo(UI(Tr(L"Language of this window") + L"##ui").c_str(), edit.uiLangCode, Tr(L"like Windows"));
            int pt = std::clamp(edit.fontPercent, 70, 200);
            ImGui::SetNextItemWidth(wide);
            if (ImGui::SliderInt(UI(Tr(L"Text size") + L" (%)##size").c_str(), &pt, 70, 200)) edit.fontPercent = pt;
            int transparency = 100 - std::clamp(edit.opacity, 60, 255) * 100 / 255;
            ImGui::SetNextItemWidth(wide);
            if (ImGui::SliderInt(UI(Tr(L"Transparency") + L" (%)##alpha").c_str(), &transparency, 0, 75))
                edit.opacity = (100 - transparency) * 255 / 100;
            ImGui::Checkbox(UI(Tr(L"Show the window")).c_str(), &g_show);
            ImGui::TextDisabled("%s", UI(Tr(L"Keys (show / hide: Ctrl+Alt+K, push to talk, focus the input): Nexus → Keybinds → GW2 Keyboard.")).c_str());
            ImGui::Separator();
            ImGui::TextUnformatted(UI(Tr(L"The chat")).c_str());
            if (g_chatEventsPresent || g_chatEventsLive) {
                ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.f), "%s", UI(Tr(L"Events: Chat is loaded – every channel arrives exactly.")).c_str());
            } else {
                ImGui::TextWrapped("%s", UI(Tr(L"The chat (map, whispers, guild, party, squad) comes from Events: Chat by Vonsh, an "
                                               L"add-on in the Nexus library. Without it no chat arrives here.")).c_str());
                if (g_chatEventsInstalling) ImGui::TextDisabled("%s", UI(Tr(L"Loading Events: Chat …")).c_str());
                else if (ImGui::Button(UI(Tr(L"Install Events: Chat")).c_str())) InstallChatEventsNow();
            }
            ImGui::EndTabItem();
        }
        // ---- Writing
        if (ImGui::BeginTabItem(UI(Tr(L"Writing")).c_str())) {
            ImGui::SetNextItemWidth(wide);
            LanguageCombo(UI(Tr(L"I write in") + L"##writein").c_str(), edit.writeIn, Tr(L"like Windows"));
            ImGui::Checkbox(UI(Tr(L"Spell checking (Windows, offline)")).c_str(), &edit.spellEnabled);
            int ac = static_cast<int>(edit.autoCorrect);
            if (combo(UI(Tr(L"Autocorrection") + L"##ac").c_str(),
                      {UI(Tr(L"Off")), UI(Tr(L"Safe")), UI(Tr(L"Like a phone keyboard"))}, &ac))
                edit.autoCorrect = static_cast<AutoCorrectMode>(ac);
            ImGui::Checkbox(UI(Tr(L"Word suggestions while typing")).c_str(), &edit.suggestions);
            int learn = !edit.learnWords ? 2 : edit.learnLater ? 1 : 0;
            if (combo("##learn", {UI(Tr(L"Learn from what I send – while playing")), UI(Tr(L"Learn from what I send – later")),
                                  UI(Tr(L"Do not learn"))},
                      &learn)) {
                edit.learnWords = learn != 2;
                edit.learnLater = learn == 1;
            }
            if (!forgetAsked) {
                if (ImGui::Button(UI(Tr(L"Delete everything learned…")).c_str())) forgetAsked = true;
            } else {
                ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.35f, 1.f), "%s", UI(Tr(L"Really delete everything learned?")).c_str());
                ImGui::SameLine();
                if (ImGui::Button(UI(Tr(L"Yes")).c_str())) {
                    g_worker.Post([] { if (g_spell) g_spell->ForgetAll(); });
                    forgetAsked = false;
                }
                ImGui::SameLine();
                if (ImGui::Button(UI(Tr(L"No")).c_str())) forgetAsked = false;
            }
            ImGui::Checkbox(UI(Tr(L"Show the back-translation of my message")).c_str(), &edit.backTranslate);
            int enterDoes = edit.copyOnly ? 1 : 0;
            if (combo(UI(Tr(L"Enter does") + L"##enter").c_str(),
                      {UI(Tr(L"Send into the GW2 chat")), UI(Tr(L"Only copy (not a single key reaches the game)"))}, &enterDoes))
                edit.copyOnly = enterDoes == 1;
            ImGui::TextUnformatted(UI(Tr(L"Focus behaviour")).c_str());
            ImGui::Checkbox(UI(Tr(L"back here after sending")).c_str(), &edit.returnFocus);
            ImGui::SameLine();
            ImGui::Checkbox(UI(Tr(L"here when the GW2 chat opens")).c_str(), &edit.focusOnGameChat);
            ImGui::EndTabItem();
        }
        // ---- Voice
        if (ImGui::BeginTabItem(UI(Tr(L"Voice")).c_str())) {
            ImGui::Checkbox(UI(Tr(L"Voice input")).c_str(), &edit.voiceEnabled);
            const VoiceCatalog catalog = LoadVoiceCatalog(edit.dataDir);
            const VoiceModelInfo* chosen = catalog.Find(U8(edit.voiceModel));
            if (!chosen) chosen = catalog.Recommended();
            ImGui::SetNextItemWidth(wide);
            if (ImGui::BeginCombo(UI(Tr(L"Model") + L"##model").c_str(), chosen ? chosen->name.c_str() : "-")) {
                for (const VoiceModelInfo& m : catalog.models) {
                    const std::string label = m.name + "  (" + std::to_string(m.bytes / (1024 * 1024)) + " MB)" +
                                              (VoiceModelInstalled(edit.VoiceModelDir(), m) ? "  [OK]" : "");
                    if (ImGui::Selectable(label.c_str(), chosen && chosen->id == m.id)) edit.voiceModel = W(m.id);
                }
                ImGui::EndCombo();
            }
            if (chosen && !VoiceModelInstalled(edit.VoiceModelDir(), *chosen)) {
                const int pct = g_downloadPct;
                if (pct >= 0) ImGui::ProgressBar(pct / 100.f, ImVec2(wide, 0));
                else if (ImGui::Button(UI(Tr(L"Download the model")).c_str())) StartModelDownload(*chosen);
            }
            std::vector<std::string> langs{UI(Tr(L"Like I write")), UI(Tr(L"Detect it"))};
            std::vector<std::wstring> codes{L"", L"auto"};
            for (const LangInfo& l : Languages()) {
                langs.push_back(U8(l.native));
                codes.push_back(l.code);
            }
            int li = 0;
            for (size_t i = 0; i < codes.size(); ++i)
                if (codes[i] == edit.voiceLang) li = static_cast<int>(i);
            if (combo(UI(Tr(L"Language") + L"##vlang").c_str(), langs, &li)) edit.voiceLang = codes[li];
            ImGui::Checkbox(UI(Tr(L"Translate into English (whisper)")).c_str(), &edit.voiceTranslate);
            ImGui::Checkbox(UI(Tr(L"Live text while speaking (more CPU)")).c_str(), &edit.voiceLive);
            ImGui::SetNextItemWidth(wide);
            ImGui::SliderInt(UI(Tr(L"Longest recording") + L" (s)##max").c_str(), &edit.voiceMaxSec, 5, 60);
            std::vector<std::string> threads{UI(Tr(L"Automatic"))};
            for (int i = 1; i <= 8; ++i) threads.push_back(std::to_string(i));
            int ti = std::clamp(edit.voiceThreads, 0, 8);
            if (combo(UI(Tr(L"CPU threads") + L"##thr").c_str(), threads, &ti)) edit.voiceThreads = ti;
            ImGui::TextDisabled("%s", UI(Tr(L"Automatic: the least busy cores – the ones GW2 does not use.")).c_str());
            ImGui::Checkbox(UI(Tr(L"Send right after speaking (releasing the key counts as your Enter)")).c_str(), &edit.voiceAutoSend);
            ImGui::TextDisabled("%s", UI(Tr(L"Runs on this PC – no sound leaves it, nothing is recorded to disk.")).c_str());
            ImGui::EndTabItem();
        }
        // ---- Microphone
        if (ImGui::BeginTabItem(UI(Tr(L"Microphone")).c_str())) {
            int mode = edit.voiceToggle ? 1 : 0;
            if (combo(UI(Tr(L"Starting") + L"##mode").c_str(),
                      {UI(Tr(L"Hold the button or key while you speak")), UI(Tr(L"Press once – a pause ends it"))}, &mode))
                edit.voiceToggle = mode == 1;
            std::vector<std::string> names{UI(Tr(L"Windows default"))};
            int mi = 0;
            for (size_t i = 0; i < mics.size(); ++i) {
                names.push_back(U8(mics[i].name));
                if (mics[i].id == edit.voiceDevice) mi = static_cast<int>(i) + 1;
            }
            if (combo(UI(Tr(L"Microphone") + L"##dev").c_str(), names, &mi))
                edit.voiceDevice = mi == 0 ? std::wstring() : mics[mi - 1].id;
            int boost = std::clamp(edit.voiceBoost, 0, 3);
            if (combo(UI(Tr(L"Microphone boost") + L"##boost").c_str(), {UI(Tr(L"Off")), "+6 dB", "+12 dB", UI(Tr(L"Automatic"))}, &boost))
                edit.voiceBoost = boost;
            ImGui::SetNextItemWidth(wide);
            ImGui::SliderInt(UI(Tr(L"Pause to end") + L" (ms)##pause").c_str(), &edit.voicePauseMs, 400, 3000);
            ImGui::TextDisabled("%s", UI(Tr(L"Right-click on the microphone: on until a pause.")).c_str());
            ImGui::EndTabItem();
        }
        // ---- Translator
        if (ImGui::BeginTabItem(UI(Tr(L"Translator")).c_str())) {
            ImGui::Checkbox(UI(Tr(L"Translate my text")).c_str(), &edit.translateWriting);
            static const Engine kEngines[] = {Engine::Auto, Engine::Basic, Engine::DeepL, Engine::Google, Engine::Microsoft, Engine::Libre};
            static const wchar_t* kNames[] = {L"Automatic", L"MyMemory (free)", L"DeepL", L"Google", L"Microsoft", L"LibreTranslate"};
            int ei = 0;
            for (int i = 0; i < 6; ++i)
                if (kEngines[i] == edit.engine) ei = i;
            std::vector<std::string> engines;
            for (const wchar_t* n : kNames) engines.push_back(UI(Tr(n)));
            if (combo(UI(Tr(L"Translator") + L"##engine").c_str(), engines, &ei)) {
                edit.engine = kEngines[ei];
                const std::wstring k = edit.engine == Engine::DeepL ? edit.deeplKey : edit.engine == Engine::Google ? edit.googleKey
                                       : edit.engine == Engine::Microsoft ? edit.msKey : edit.libreKey;
                strncpy_s(keyBuf, U8(k).c_str(), _TRUNCATE);
            }
            if (edit.engine == Engine::DeepL || edit.engine == Engine::Google || edit.engine == Engine::Microsoft ||
                edit.engine == Engine::Libre) {
                ImGui::SetNextItemWidth(wide);
                ImGui::InputText(UI(Tr(L"API key") + L"##key").c_str(), keyBuf, sizeof(keyBuf), ImGuiInputTextFlags_Password);
            }
            if (edit.engine == Engine::Microsoft) {
                ImGui::SetNextItemWidth(wide);
                ImGui::InputText(UI(Tr(L"Region") + L"##region").c_str(), regionBuf, sizeof(regionBuf));
            }
            if (edit.engine == Engine::Libre) {
                ImGui::SetNextItemWidth(wide);
                ImGui::InputText(UI(Tr(L"Server") + L"##url").c_str(), urlBuf, sizeof(urlBuf));
            }
            if (edit.engine == Engine::Auto || edit.engine == Engine::Basic) {
                ImGui::SetNextItemWidth(wide);
                ImGui::InputText(UI(Tr(L"E-mail for MyMemory (more free text per day)") + L"##mail").c_str(), mailBuf, sizeof(mailBuf));
            }
            ImGui::SetNextItemWidth(wide);
            std::wstring chatLang = edit.writeLangs.empty() ? std::wstring(L"EN-GB") : edit.writeLangs.front();
            if (LanguageCombo(UI(Tr(L"The chat gets") + L"##sendlang").c_str(), chatLang, L"")) {
                edit.writeLangs.erase(std::remove(edit.writeLangs.begin(), edit.writeLangs.end(), chatLang), edit.writeLangs.end());
                edit.writeLangs.insert(edit.writeLangs.begin(), chatLang);
            }
            ImGui::EndTabItem();
        }
        // ---- Technical
        if (ImGui::BeginTabItem(UI(Tr(L"Technical")).c_str())) {
            ImGui::SetNextItemWidth(120);
            ImGui::InputInt(UI(Tr(L"Key hold when sending (ms)") + L"##hold").c_str(), &edit.send.keyHoldMs);
            ImGui::SetNextItemWidth(120);
            ImGui::InputInt(UI(Tr(L"Pause between keys (ms)") + L"##step").c_str(), &edit.send.stepDelayMs);
            edit.send.keyHoldMs = std::clamp(edit.send.keyHoldMs, 10, 500);
            edit.send.stepDelayMs = std::clamp(edit.send.stepDelayMs, 10, 1000);
            ImGui::Separator();
            std::lock_guard<std::mutex> lock(g_mu);
            const int fps = g_fps;
            const int hold = fps > 0 && g_cfg.send.keyHoldMs == 30 ? std::clamp(1000 / fps + 10, 15, 1010) : g_cfg.send.keyHoldMs;
            ImGui::Text("GW2 Keyboard for Nexus %s", GCT_VERSION_STR);
            ImGui::Text("%s", UI(TrF(L"Game: {1} fps → keys held {2} ms", {std::to_wstring(fps), std::to_wstring(hold)})).c_str());
            ImGui::Text("%s", UI(TrF(L"Translator: {1}", {g_translator ? g_translator->Name() : std::wstring(L"-")})).c_str());
            if (const std::wstring q = MyMemoryQuotaText(); !q.empty()) ImGui::Text("%s", UI(q).c_str());
            ImGui::Text("Events: Chat: %s", g_chatEventsPresent || g_chatEventsLive ? "[OK]" : "[--]");
            ImGui::Text("%s", UI(TrF(L"Voice: {1}", {!g_cfg.voiceEnabled ? Tr(L"off") : g_voiceReady ? Tr(L"ready")
                                                       : g_voice.Running() ? Tr(L"starting") : Tr(L"The voice module is missing")})).c_str());
            ImGui::Text("%s", UI(TrF(L"Chat lines: {1}", {std::to_wstring(g_lines.size())})).c_str());
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::Separator();
    if (ImGui::Button(UI(Tr(L"Save")).c_str())) {
        const std::wstring key = W(keyBuf);
        if (edit.engine == Engine::DeepL) edit.deeplKey = key;
        else if (edit.engine == Engine::Google) edit.googleKey = key;
        else if (edit.engine == Engine::Microsoft) edit.msKey = key;
        else if (edit.engine == Engine::Libre) edit.libreKey = key;
        edit.msRegion = W(regionBuf);
        edit.libreUrl = W(urlBuf);
        edit.basicEmail = W(mailBuf);
        edit.uiLang = EffectiveUiLang(edit.uiLangCode);
        bool typingChanged, voiceChanged;
        {
            std::lock_guard<std::mutex> lock(g_mu);
            typingChanged = edit.writeIn != g_cfg.writeIn || edit.spellEnabled != g_cfg.spellEnabled;
            voiceChanged = edit.voiceEnabled != g_cfg.voiceEnabled;
            edit.myMemoryDay = g_cfg.myMemoryDay;  // counted meanwhile
            edit.myMemoryUsed = g_cfg.myMemoryUsed;
            g_cfg = edit;
            g_cfg.SaveAll();
            SetUiLang(g_cfg.uiLang);
            g_translator = MakeTranslator(g_cfg);
            g_sendIdx = g_cfg.translateWriting ? 0 : static_cast<int>(g_cfg.writeLangs.size());
            SetStatus(Tr(L"Saved"), 3000);
        }
        if (typingChanged) g_worker.Post([] {  // spelling and learned words follow the language you write in
            FreeWorkerState();
            InitSpelling();
        });
        else g_worker.Post([learn = edit.learnWords] { if (g_spell) g_spell->SetLearnChoices(learn); });
        if (voiceChanged && edit.voiceEnabled && !g_voice.Running() &&
            GetFileAttributesW(HelperExe().c_str()) != INVALID_FILE_ATTRIBUTES)
            g_voice.Start(HelperExe(), edit.dataDir);
        else if (g_voice.Running())
            g_voice.Send("reload");
    }
    ImGui::SameLine();
    if (ImGui::Button(UI(Tr(L"Undo changes")).c_str())) loaded = false;
}

// Nexus → Addons → GW2 Keyboard: only the button that opens / closes our own options window.
void RenderNexusOptions() {
    if (!ImGui::GetCurrentContext()) return;
    if (ImGui::Button(U8(g_showOptions ? Tr(L"Close the GW2 Keyboard options") : Tr(L"Open the GW2 Keyboard options")).c_str()))
        g_showOptions = !g_showOptions;
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
        GuardedRender(RenderNexusOptions);
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
    bool voice = false;
    std::wstring dataDir;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        voice = g_cfg.voiceEnabled;
        dataDir = g_cfg.dataDir;
        std::string fixes;
        if (ReadFileBytes(dataDir + L"\\voice-fixes.txt", fixes)) g_voiceFixes.Parse(FromUtf8(fixes));
    }
    (void)voice;  // the voice helper starts on the first press of the microphone or the key, not now
    if (api->GUI_Register) {
        api->GUI_Register(RT_Render, SafeRender);
        api->GUI_Register(RT_OptionsRender, SafeOptions);
    }
    if (api->InputBinds_RegisterWithString) {
        api->InputBinds_RegisterWithString(kToggleBind, OnToggle, "CTRL+ALT+K");
        api->InputBinds_RegisterWithString(kFocusBind, OnFocusKey, "(null)");
        api->InputBinds_RegisterWithString(kTalkBind, OnTalkKey, "(null)");
    }
    if (api->Events_Subscribe) {
        api->Events_Subscribe(bridge::kChatEventsEvent, OnChatEvents);
    }
    Log(LOGL_INFO, "loaded");
}

void AddonUnload() {
    if (g_api) {
        if (g_api->Events_Unsubscribe) {
            g_api->Events_Unsubscribe(bridge::kChatEventsEvent, OnChatEvents);
        }
        if (g_api->InputBinds_Deregister) {
            g_api->InputBinds_Deregister(kToggleBind);
            g_api->InputBinds_Deregister(kFocusBind);
            g_api->InputBinds_Deregister(kTalkBind);
        }
        if (g_api->GUI_Deregister) {
            g_api->GUI_Deregister(SafeRender);
            g_api->GUI_Deregister(SafeOptions);
        }
    }
    g_downloadCancel = true;
    if (g_download.joinable()) g_download.join();
    if (g_chatEventsThread.joinable()) g_chatEventsThread.join();
    g_voice.Stop();  // the helper ends (it would also end by itself when its input closes)
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
                        "on your Enter. The chat comes from \"Events: Chat\" (Nexus library).";
    g_def.Load = AddonLoad;
    g_def.Unload = AddonUnload;
    g_def.Flags = AF_None;
    g_def.Provider = UP_None;
    return &g_def;
}
