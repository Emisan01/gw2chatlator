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
#include "unofficial_extras/Definitions.h"
#include "version.h"
#include "win/deepl_translator.hpp"
#include "win/els.hpp"
#include "win/files.hpp"
#include "win/gw2_sender.hpp"
#include "win/mumble_link.hpp"
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

// Voice (stage 4): the helper process gw2kb_voice.exe runs the recognizer outside the game.
bool g_voiceReady = false;       // the helper said "ready"
bool g_recording = false;        // a recording runs
std::wstring g_voiceLive;        // the live text while speaking (only drawn, never written into the input)
std::wstring g_lastDictated;     // the last recognized text as it came (learning misheard words on send)
std::wstring g_dictLang;         // its language
VoiceFixBook g_voiceFixes;       // learned misheard words (<data>\voice-fixes.txt), per language
bool g_autoSendPending = false;  // "send after speaking": the dictation arrived, Enter follows by itself

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
            SetStatus(g_voice.Running() ? Tr(L"Voice input is starting – a moment") : Tr(L"The voice module is missing"));
            return;
        }
    }
    g_voice.Send("begin");
}
void TalkUp() { g_voice.Send("end"); }

void OnTalkKey(const char*, bool release) {
    if (g_disabled) return;
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
    // ---- the translation of what you type (while you speak: the live text)
    const bool previewCurrent = g_previewGen == g_inputGen && !g_preview.empty();
    if (g_recording)
        ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.f, 1.f), "%s", g_voiceLive.empty() ? "…" : U8(g_voiceLive).c_str());
    else if (Translating())
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
    // ---- microphone (hold), channel, send language, input
    if (g_cfg.voiceEnabled) {
        ImGui::PushStyleColor(ImGuiCol_Button, g_recording ? ImVec4(0.75f, 0.2f, 0.2f, 1.f)
                                                           : ImGui::GetStyleColorVec4(ImGuiCol_Button));
        ImGui::Button("Mic");
        ImGui::PopStyleColor();
        static bool micHeld = false;
        if (ImGui::IsItemActivated()) {
            micHeld = true;
            g_mu.unlock();
            TalkDown();
            g_mu.lock();
        } else if (micHeld && !ImGui::IsItemActive()) {
            micHeld = false;
            TalkUp();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", U8(Tr(L"Hold to speak (or the push-to-talk key in Nexus)")).c_str());
        ImGui::SameLine();
        if (ImGui::Checkbox("##autosend", &g_cfg.voiceAutoSend)) g_cfg.SaveBool(L"Voice", L"AutoSend", g_cfg.voiceAutoSend);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", U8(Tr(L"Send right after speaking (releasing the key counts as your Enter)")).c_str());
        ImGui::SameLine();
    }
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
    if (g_autoSendPending && !g_edit.active && g_input[0]) {  // the dictation is in the line now
        g_autoSendPending = false;
        StartSend();
    }
    // ---- status
    ImGui::TextDisabled("%s", g_status.empty() ? (g_chatEventsLive ? "" : U8(Tr(L"Waiting for chat lines (Events: Chat or Unofficial Extras)")).c_str())
                                               : U8(g_status).c_str());
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

// Nexus → Addons → GW2 Keyboard: the essentials of the exe's settings. Changes apply on "Save".
void RenderOptions() {
    if (!ImGui::GetCurrentContext()) return;
    static Config edit;
    static bool loaded = false;
    static char keyBuf[256] = {}, regionBuf[64] = {}, urlBuf[256] = {}, mailBuf[128] = {};
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
    }
    ImGui::TextWrapped("%s", U8(TrF(L"GW2 Keyboard for Nexus {1} – the chat in your language, what you write in the chat's "
                                    L"language. The chat comes from Events: Chat (every channel) or Unofficial Extras "
                                    L"(party and squad). Nothing is sent without your Enter; nothing of other players is "
                                    L"stored.",
                                    {GCT_VERSION_WSTR})).c_str());
    ImGui::Checkbox(U8(Tr(L"Show the window")).c_str(), &g_show);
    ImGui::Separator();

    // ---- languages
    ImGui::TextUnformatted(U8(Tr(L"Languages")).c_str());
    ImGui::SetNextItemWidth(260);
    LanguageCombo(U8(Tr(L"I write in") + L"##writein").c_str(), edit.writeIn, Tr(L"like Windows"));
    std::wstring chatLang = edit.writeLangs.empty() ? std::wstring(L"EN-GB") : edit.writeLangs.front();
    ImGui::SetNextItemWidth(260);
    if (LanguageCombo(U8(Tr(L"The chat gets") + L"##sendlang").c_str(), chatLang, L"")) {
        edit.writeLangs.erase(std::remove(edit.writeLangs.begin(), edit.writeLangs.end(), chatLang), edit.writeLangs.end());
        edit.writeLangs.insert(edit.writeLangs.begin(), chatLang);
    }
    ImGui::SetNextItemWidth(260);
    LanguageCombo(U8(Tr(L"Language of this window") + L"##ui").c_str(), edit.uiLangCode, Tr(L"like Windows"));

    // ---- translator
    ImGui::Separator();
    static const Engine kEngines[] = {Engine::Auto, Engine::Basic, Engine::DeepL, Engine::Google, Engine::Microsoft, Engine::Libre};
    static const wchar_t* kEngineNames[] = {L"Automatic", L"MyMemory (free)", L"DeepL", L"Google", L"Microsoft", L"LibreTranslate"};
    int engineIdx = 0;
    for (int i = 0; i < 6; ++i)
        if (kEngines[i] == edit.engine) engineIdx = i;
    ImGui::SetNextItemWidth(260);
    if (ImGui::BeginCombo(U8(Tr(L"Translator") + L"##engine").c_str(), U8(Tr(kEngineNames[engineIdx])).c_str())) {
        for (int i = 0; i < 6; ++i)
            if (ImGui::Selectable(U8(Tr(kEngineNames[i])).c_str(), i == engineIdx)) {
                edit.engine = kEngines[i];
                const std::wstring k = edit.engine == Engine::DeepL ? edit.deeplKey : edit.engine == Engine::Google ? edit.googleKey
                                       : edit.engine == Engine::Microsoft ? edit.msKey : edit.libreKey;
                strncpy_s(keyBuf, U8(k).c_str(), _TRUNCATE);
            }
        ImGui::EndCombo();
    }
    if (edit.engine == Engine::DeepL || edit.engine == Engine::Google || edit.engine == Engine::Microsoft ||
        edit.engine == Engine::Libre) {
        ImGui::SetNextItemWidth(260);
        ImGui::InputText(U8(Tr(L"API key") + L"##key").c_str(), keyBuf, sizeof(keyBuf), ImGuiInputTextFlags_Password);
    }
    if (edit.engine == Engine::Microsoft) {
        ImGui::SetNextItemWidth(260);
        ImGui::InputText(U8(Tr(L"Region") + L"##region").c_str(), regionBuf, sizeof(regionBuf));
    }
    if (edit.engine == Engine::Libre) {
        ImGui::SetNextItemWidth(260);
        ImGui::InputText(U8(Tr(L"Server") + L"##url").c_str(), urlBuf, sizeof(urlBuf));
    }
    if (edit.engine == Engine::Auto || edit.engine == Engine::Basic) {
        ImGui::SetNextItemWidth(260);
        ImGui::InputText(U8(Tr(L"E-mail for MyMemory (more free text per day)") + L"##mail").c_str(), mailBuf, sizeof(mailBuf));
    }

    // ---- writing
    ImGui::Separator();
    ImGui::Checkbox(U8(Tr(L"Word suggestions while typing")).c_str(), &edit.suggestions);
    int ac = static_cast<int>(edit.autoCorrect);
    const std::string acNames[] = {U8(Tr(L"Off")), U8(Tr(L"Safe")), U8(Tr(L"Like a phone keyboard"))};
    ImGui::SetNextItemWidth(260);
    if (ImGui::BeginCombo(U8(Tr(L"Autocorrection") + L"##ac").c_str(), acNames[ac].c_str())) {
        for (int i = 0; i < 3; ++i)
            if (ImGui::Selectable(acNames[i].c_str(), i == ac)) edit.autoCorrect = static_cast<AutoCorrectMode>(i);
        ImGui::EndCombo();
    }
    ImGui::Checkbox(U8(Tr(L"Learn my words")).c_str(), &edit.learnWords);
    ImGui::Checkbox(U8(Tr(L"back here after sending")).c_str(), &edit.returnFocus);

    // ---- voice
    ImGui::Separator();
    ImGui::Checkbox(U8(Tr(L"Voice input")).c_str(), &edit.voiceEnabled);
    if (edit.voiceEnabled) {
        const VoiceCatalog catalog = LoadVoiceCatalog(edit.dataDir);
        const VoiceModelInfo* chosen = catalog.Find(U8(edit.voiceModel));
        if (!chosen) chosen = catalog.Recommended();
        ImGui::SetNextItemWidth(260);
        if (ImGui::BeginCombo(U8(Tr(L"Model") + L"##model").c_str(), chosen ? chosen->name.c_str() : "-")) {
            for (const VoiceModelInfo& m : catalog.models) {
                const std::string label = m.name + (VoiceModelInstalled(edit.VoiceModelDir(), m) ? "  [OK]" : "") +
                                          "  (" + std::to_string(m.bytes / (1024 * 1024)) + " MB)";
                if (ImGui::Selectable(label.c_str(), chosen && chosen->id == m.id)) edit.voiceModel = W(m.id);
            }
            ImGui::EndCombo();
        }
        if (chosen && !VoiceModelInstalled(edit.VoiceModelDir(), *chosen)) {
            const int pct = g_downloadPct;
            if (pct >= 0) ImGui::ProgressBar(pct / 100.f, ImVec2(260, 0));
            else if (ImGui::Button(U8(Tr(L"Download the model")).c_str())) StartModelDownload(*chosen);
        }
        ImGui::Checkbox(U8(Tr(L"Translate into English (whisper)")).c_str(), &edit.voiceTranslate);
        ImGui::SetNextItemWidth(120);
        ImGui::SliderInt(U8(Tr(L"Threads (0 = automatic: the cores GW2 does not use)") + L"##thr").c_str(), &edit.voiceThreads, 0, 8);
        int boost = edit.voiceBoost;
        const std::string boostNames[] = {U8(Tr(L"Off")), "+6 dB", "+12 dB", U8(Tr(L"Automatic"))};
        ImGui::SetNextItemWidth(260);
        if (ImGui::BeginCombo(U8(Tr(L"Microphone boost") + L"##boost").c_str(), boostNames[std::clamp(boost, 0, 3)].c_str())) {
            for (int i = 0; i < 4; ++i)
                if (ImGui::Selectable(boostNames[i].c_str(), i == boost)) edit.voiceBoost = i;
            ImGui::EndCombo();
        }
        ImGui::Checkbox(U8(Tr(L"Send right after speaking (releasing the key counts as your Enter)")).c_str(), &edit.voiceAutoSend);
        ImGui::TextDisabled("%s", U8(Tr(L"Push to talk: set the key in Nexus → Keybinds → GW2 Keyboard.")).c_str());
    }

    ImGui::Separator();
    if (ImGui::Button(U8(Tr(L"Save")).c_str())) {
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
            typingChanged = edit.writeIn != g_cfg.writeIn;
            voiceChanged = edit.voiceEnabled != g_cfg.voiceEnabled;
            g_cfg = edit;
            g_cfg.SaveAll();
            SetUiLang(g_cfg.uiLang);
            g_translator = MakeTranslator(g_cfg);
            g_sendIdx = 0;
            SetStatus(Tr(L"Saved"), 3000);
        }
        if (typingChanged) g_worker.Post([] {  // spelling and learned words follow the language you write in
            FreeWorkerState();
            InitSpelling();
        });
        if (voiceChanged && edit.voiceEnabled && !g_voice.Running() &&
            GetFileAttributesW(HelperExe().c_str()) != INVALID_FILE_ATTRIBUTES)
            g_voice.Start(HelperExe(), edit.dataDir);
        else if (g_voice.Running())
            g_voice.Send("reload");
    }
    ImGui::SameLine();
    if (ImGui::Button(U8(Tr(L"Undo changes")).c_str())) loaded = false;
    ImGui::TextDisabled("%s", U8(g_chatEventsLive ? Tr(L"Events: Chat delivers.")
                                                  : Tr(L"Waiting for chat lines (Events: Chat or Unofficial Extras)")).c_str());
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
    bool voice = false;
    std::wstring dataDir;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        voice = g_cfg.voiceEnabled;
        dataDir = g_cfg.dataDir;
        std::string fixes;
        if (ReadFileBytes(dataDir + L"\\voice-fixes.txt", fixes)) g_voiceFixes.Parse(FromUtf8(fixes));
    }
    if (voice && GetFileAttributesW(HelperExe().c_str()) != INVALID_FILE_ATTRIBUTES) g_voice.Start(HelperExe(), dataDir);
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
            g_api->InputBinds_Deregister(kTalkBind);
        }
        if (g_api->GUI_Deregister) {
            g_api->GUI_Deregister(SafeRender);
            g_api->GUI_Deregister(SafeOptions);
        }
    }
    g_downloadCancel = true;
    if (g_download.joinable()) g_download.join();
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
                        "on your Enter. Needs \"Events: Chat\" (or Unofficial Extras) for the chat.";
    g_def.Load = AddonLoad;
    g_def.Unload = AddonUnload;
    g_def.Flags = AF_None;
    g_def.Provider = UP_None;
    return &g_def;
}
