// kb_main.cpp — "GW2 Keyboard" as a Nexus add-on, stage 1 (docs/NEXUS_KEYBOARD_PLAN.md).
//
// An ImGui window inside the game: the chat from Vonsh's "Events: Chat" (every channel) or Unofficial Extras
// (party/squad), foreign lines translated into your language; an input line whose translation is shown as you type;
// Enter copies the translation to the clipboard (sending into the chat comes in stage 2).
//
// Inside the game process (plan, "Rules"): the render callback only draws and copies; translations run on our own
// worker thread; every callback is guarded (try/catch + SEH) – after an error the add-on switches itself off for the
// session and says so in the Nexus log. Nothing of other players is stored.

#include <windows.h>
#include <objbase.h>

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
#include "core/chat_line.hpp"
#include "core/langs.hpp"
#include "core/languages.hpp"
#include "core/protect.hpp"
#include "core/slang.hpp"
#include "core/text.hpp"
#include "core/translator.hpp"
#include "imgui/imgui.h"
#include "nexus/Nexus.h"
#include "unofficial_extras/Definitions.h"
#include "version.h"
#include "win/deepl_translator.hpp"
#include "win/els.hpp"
#include "win/online_translators.hpp"

using namespace gct;

namespace {

constexpr const char* kName = "GW2 Keyboard";
constexpr const char* kToggleBind = "KB_GW2KEYBOARD_TOGGLE";
constexpr size_t kMaxLines = 200;

AddonDefinition_t g_def = {};
AddonAPI_t* g_api = nullptr;
HMODULE g_self = nullptr;
std::atomic<bool> g_disabled{false};  // set after an error: the add-on stays quiet for the rest of the session
std::atomic<bool> g_chatEventsLive{false};
bool g_show = true;

void Log(ELogLevel level, const char* text) {
    if (g_api && g_api->Log) g_api->Log(level, kName, text);
}

// ---------------------------------------------------------------------------------------------------------------
// One worker thread: translations (and later spelling, voice) never run on the game's threads.
class Worker {
public:
    void Start() {
        stop_ = false;
        thread_ = std::thread([this] { Run(); });
    }
    void Stop() {
        {
            std::lock_guard<std::mutex> lock(mu_);
            stop_ = true;
            jobs_.clear();
        }
        cv_.notify_all();
        if (thread_.joinable()) thread_.join();
    }
    void Post(std::function<void()> job) {
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (jobs_.size() > 64) jobs_.pop_front();  // a flood (WvW): the oldest wait no longer
            jobs_.push_back(std::move(job));
        }
        cv_.notify_one();
    }

private:
    void Run() {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);  // language detection (ELS) and translators
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lock(mu_);
                cv_.wait(lock, [this] { return stop_ || !jobs_.empty(); });
                if (stop_) break;
                job = std::move(jobs_.front());
                jobs_.pop_front();
            }
            try {
                job();
            } catch (...) {
                Log(LOGL_WARNING, "a translation failed");
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

// ---------------------------------------------------------------------------------------------------------------
// State shared between the game thread (events, drawing) and the worker. All behind g_mu.
struct ChatLine {
    uint64_t id = 0;
    Channel channel = Channel::Unknown;
    bool outgoing = false;
    std::wstring speaker, text, translated;
};

std::mutex g_mu;
Config g_cfg;
std::shared_ptr<Translator> g_translator;
std::deque<ChatLine> g_lines;
uint64_t g_nextId = 1;
bool g_scrollDown = false;
// The input line and its translation.
char g_input[600] = {};
std::string g_lastInput;
ULONGLONG g_inputChanged = 0;
uint64_t g_inputGen = 0;
std::wstring g_preview;  // the translation of the input (empty: none yet)
std::wstring g_status;

std::wstring TargetLang() {  // the language you read and write in
    if (!g_cfg.writeIn.empty()) return PrimaryLang(g_cfg.writeIn);
    wchar_t locale[LOCALE_NAME_MAX_LENGTH] = {};
    GetUserDefaultUILanguage();
    GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH);
    return PrimaryLang(locale);
}

std::wstring SendLang() {  // the chat's language
    return g_cfg.writeLangs.empty() ? std::wstring(L"EN-GB") : g_cfg.writeLangs.front();
}

std::shared_ptr<Translator> MakeTranslator(const Config& c) {
    if (!c.deeplKey.empty()) return MakeDeepLTranslator(c.deeplKey);
    if (!c.googleKey.empty()) return MakeGoogleTranslator(c.googleKey);
    if (!c.msKey.empty()) return MakeMicrosoftTranslator(c.msKey, c.msRegion);
    if (!c.libreUrl.empty()) return MakeLibreTranslator(c.libreUrl, c.libreKey);
    return MakeMyMemoryTranslator(c.basicEmail);
}

// Worker: a chat line into your language, when its language is sure and not yours.
void TranslateLine(uint64_t id, std::wstring text) {
    std::shared_ptr<Translator> tr;
    std::wstring target;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        tr = g_translator;
        target = TargetLang();
    }
    if (!tr) return;
    ProtectedText p = ProtectForTranslation(text, nullptr, &BuiltinKeepWords());
    if (!HasTranslatableText(p.segments)) return;
    const std::wstring detected = SureLanguage(text, DetectLanguage(text));
    if (detected.empty() || PrimaryLang(detected) == target) return;
    const TranslateResult r = tr->Translate(p.segments, SourceCode(detected), target);
    if (!r.ok || r.text.empty()) return;
    std::lock_guard<std::mutex> lock(g_mu);
    for (ChatLine& l : g_lines)
        if (l.id == id) l.translated = SanitizeChatText(r.text);
}

// Worker: the input into the chat's language.
void TranslateInput(uint64_t gen, std::wstring text) {
    std::shared_ptr<Translator> tr;
    std::wstring target, typing;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        tr = g_translator;
        target = SendLang();
        typing = TargetLang();
    }
    if (!tr) return;
    std::wstring result = text;
    if (PrimaryLang(target) != typing) {
        ProtectedText p = ProtectForTranslation(text, nullptr, &BuiltinKeepWords());
        if (HasTranslatableText(p.segments)) {
            std::wstring source = DetectLanguage(text);
            if (source.empty()) source = typing;
            const TranslateResult r = tr->Translate(p.segments, source, target);
            if (r.ok) result = r.text;
        }
    }
    std::lock_guard<std::mutex> lock(g_mu);
    if (gen == g_inputGen) g_preview = SanitizeChatText(result);
}

// Game thread: a line arrives – copy it, queue its translation.
void AddLine(Channel ch, bool outgoing, const char* speaker, const char* text) {
    if (!text || !*text) return;
    ChatLine l;
    l.channel = ch;
    l.outgoing = outgoing;
    l.speaker = speaker ? SanitizeChatText(FromUtf8(speaker)) : std::wstring();
    l.text = SanitizeChatText(FromUtf8(text));
    if (l.text.empty()) return;
    uint64_t id;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        l.id = id = g_nextId++;
        g_lines.push_back(l);
        while (g_lines.size() > kMaxLines) g_lines.pop_front();
        g_scrollDown = true;
    }
    if (!outgoing) g_worker.Post([id, t = l.text] { TranslateLine(id, t); });
}

// ---------------------------------------------------------------------------------------------------------------
// Events (game thread): copy, nothing else.
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

// ---------------------------------------------------------------------------------------------------------------
// Drawing (game thread).
ImVec4 ColorOf(Channel ch) {
    for (const ChannelColor& cc : g_cfg.palette)
        if (cc.channel == ch) return ImVec4(cc.rgb.r / 255.f, cc.rgb.g / 255.f, cc.rgb.b / 255.f, 1.f);
    return ImVec4(0.9f, 0.9f, 0.92f, 1.f);
}

void CopyToClipboard(const std::wstring& text) {
    if (!OpenClipboard(nullptr)) return;
    EmptyClipboard();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        if (void* p = GlobalLock(h)) {
            std::memcpy(p, text.c_str(), bytes);
            GlobalUnlock(h);
            if (!SetClipboardData(CF_UNICODETEXT, h)) GlobalFree(h);
        } else {
            GlobalFree(h);
        }
    }
    CloseClipboard();
}

void RenderWindow() {
    if (!ImGui::GetCurrentContext()) return;  // failsafe: no context, no drawing
    const ImGuiIO& io = ImGui::GetIO();
    if (io.DisplaySize.x <= 0 || io.DisplaySize.y <= 0) return;
    if (!g_show) return;
    ImGui::SetNextWindowSize(ImVec2(460, 300), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("GW2 Keyboard", &g_show)) {
        ImGui::End();
        return;
    }
    std::lock_guard<std::mutex> lock(g_mu);
    // The chat.
    const float inputH = ImGui::GetFrameHeightWithSpacing() * 2 + 6;
    ImGui::BeginChild("chat", ImVec2(0, -inputH), false);
    for (const ChatLine& l : g_lines) {
        const std::wstring& shown = l.translated.empty() ? l.text : l.translated;
        const std::string name = ToUtf8(l.outgoing ? L"→ " + l.speaker : l.speaker);
        if (!name.empty()) {
            ImGui::TextUnformatted((name + ":").c_str());
            ImGui::SameLine();
        }
        ImGui::PushStyleColor(ImGuiCol_Text, ColorOf(l.channel));
        ImGui::TextWrapped("%s", ToUtf8(shown).c_str());
        ImGui::PopStyleColor();
        if (!l.translated.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", ToUtf8(l.text).c_str());
    }
    if (g_scrollDown) {
        ImGui::SetScrollHereY(1.0f);
        g_scrollDown = false;
    }
    ImGui::EndChild();
    // The translation of what you type, then the input line.
    ImGui::TextDisabled("%s", g_preview.empty() ? " " : ToUtf8(g_preview).c_str());
    ImGui::SetNextItemWidth(-1);
    const bool enter = ImGui::InputText("##input", g_input, sizeof(g_input), ImGuiInputTextFlags_EnterReturnsTrue);
    const std::string now = g_input;
    if (now != g_lastInput) {
        g_lastInput = now;
        g_inputChanged = GetTickCount64();
        ++g_inputGen;
        g_preview.clear();
    }
    if (g_inputChanged && GetTickCount64() - g_inputChanged > 400 && !now.empty()) {  // a pause: translate
        g_inputChanged = 0;
        g_worker.Post([gen = g_inputGen, t = SanitizeChatText(FromUtf8(now))] { TranslateInput(gen, t); });
    }
    if (enter && !now.empty()) {
        // Stage 1: into the clipboard (stage 2 sends it into the GW2 chat, only on your Enter).
        CopyToClipboard(g_preview.empty() ? SanitizeChatText(FromUtf8(now)) : g_preview);
        g_status = L"Copied – paste it into the GW2 chat with Ctrl+V";
        g_input[0] = 0;
        g_lastInput.clear();
        g_preview.clear();
        ImGui::SetKeyboardFocusHere(-1);
    }
    if (!g_status.empty()) ImGui::TextDisabled("%s", ToUtf8(g_status).c_str());
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
        g_translator = MakeTranslator(g_cfg);
    }
    g_worker.Start();
    if (api->GUI_Register) {
        api->GUI_Register(RT_Render, SafeRender);
        api->GUI_Register(RT_OptionsRender, SafeOptions);
    }
    if (api->InputBinds_RegisterWithString) api->InputBinds_RegisterWithString(kToggleBind, OnToggle, "CTRL+ALT+K");
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
        if (g_api->InputBinds_Deregister) g_api->InputBinds_Deregister(kToggleBind);
        if (g_api->GUI_Deregister) {
            g_api->GUI_Deregister(SafeRender);
            g_api->GUI_Deregister(SafeOptions);
        }
    }
    g_worker.Stop();
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
    g_def.Description = "Typing help for the GW2 chat: the chat in your language, what you write translated. Needs "
                        "\"Events: Chat\" (or Unofficial Extras) for the chat. Sends only on your Enter.";
    g_def.Load = AddonLoad;
    g_def.Unload = AddonUnload;
    g_def.Flags = AF_None;
    g_def.Provider = UP_None;
    return &g_def;
}
