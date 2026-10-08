// main_window.cpp
#include "main_window.hpp"

#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cwchar>
#include <memory>
#include <thread>

#include "app/region_picker.hpp"
#include "app/modal_scope.hpp"
#include "app/settings_dialog.hpp"
#include "core/chat_geometry.hpp"
#include "core/deepl_protocol.hpp"
#include "core/rapid_models.hpp"
#include "version.h"
#include "core/gw2_install.hpp"
#include "core/gw2_text.hpp"
#include "core/housekeeping.hpp"
#include "core/i18n.hpp"
#include "core/hotkey.hpp"
#include "core/langs.hpp"
#include "core/languages.hpp"
#include "core/protect.hpp"
#include "core/slang.hpp"
#include "core/text.hpp"
#include "win/deepl_translator.hpp"
#include "win/els.hpp"
#include "win/files.hpp"
#include "win/folder_cleanup.hpp"
#include "win/gw2_api.hpp"
#include "win/gw2_locate.hpp"
#include "win/gw2_sender.hpp"
#include "win/screen_capture.hpp"
#include "win/tesseract_ocr.hpp"

#ifndef MOD_NOREPEAT
#define MOD_NOREPEAT 0x4000
#endif
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

namespace gct {

struct TranslatedMsg {
    enum class Kind { Forward, Back, Romanize };
    Kind kind = Kind::Forward;
    uint64_t gen = 0;
    TranslateResult result;
};

struct NamesMsg {
    std::string lang;
    NameFetchResult result;
};

struct GrammarMsg {
    std::wstring text;
    LtResult result;
};

struct IncomingMsg {
    std::vector<uint64_t> ids;
    std::vector<std::wstring> texts;
    std::wstring lang;
    std::vector<TranslateResult> results;
    ULONGLONG started = 0;  // for the technical page: how long translating takes
};

namespace {

constexpr wchar_t kClassName[] = L"GW2ChatTranslatorWindow";
constexpr wchar_t kTitle[] = L"GW2 Chat Translator";

constexpr UINT WM_APP_TRANSLATED = WM_APP + 1;
constexpr UINT WM_APP_NAMES = WM_APP + 2;
constexpr UINT WM_APP_SNAPSHOT = WM_APP + 3;
constexpr UINT WM_APP_INCOMING = WM_APP + 4;
constexpr UINT WM_APP_GRAMMAR = WM_APP + 5;
constexpr UINT WM_APP_TRAY = WM_APP + 6;
constexpr UINT WM_APP_FIRSTRUN = WM_APP + 7;
constexpr UINT WM_APP_MYMEMORY_NOTICE = WM_APP + 8;
constexpr UINT WM_APP_CHATFOUND = WM_APP + 9;   // lParam: RECT* (owned), empty = no chat seen yet
constexpr UINT_PTR kTimerDebounce = 1;
constexpr UINT_PTR kTimerStatus = 2;
constexpr UINT_PTR kTimerGame = 3;
constexpr UINT_PTR kTimerCaptures = 4;
constexpr UINT_PTR kTimerGrammar = 5;
constexpr UINT_PTR kTimerConfirm = 9;  // double scan: read again to confirm new lines
constexpr UINT_PTR kTimerOnce = 11;    // "translate once" ends
constexpr UINT kOnceMs = 10000;         // at most; it ends as soon as the pictures are read (slow OCR: Tesseract)
constexpr UINT kConfirmDelayMs = 200;  // second look at new lines (not the same frame)
constexpr ULONGLONG kDetectEveryMs = 1500;  // no chat area yet: look for the GW2 chat this often
constexpr UINT kCaptureMinutes = 15;  // diagnostic pictures switch themselves off
constexpr int kHotkeyId = 1;
constexpr size_t kBatchMax = 12;             // incoming lines per translation request
constexpr size_t kBatchChars = 2500;
constexpr ULONGLONG kEchoWindowMs = 180000;  // own lines coming back through OCR
constexpr ULONGLONG kErrorPauseMs = 30000;
constexpr ULONGLONG kQuotaPauseMs = 20 * 60000;  // free contingent used up
constexpr int kParallelJobs = 4;                 // incoming translation requests at once (not for an LLM)
constexpr size_t kQueueMax = 40;                  // incoming lines waiting for translation

// Menu command ranges
constexpr UINT kCmdLangBase = 1000;   // + index into Languages()
constexpr UINT kCmdMoreBase = 2000;   // "Weitere Sprachen" + index into Languages()
constexpr UINT kCmdFavBase = 3000;    // + index into writeLangs_
constexpr UINT kCmdOriginal = 3999;
constexpr UINT kCmdPartnerBase = 4000;

// Saved position: keep the whole window inside the work area of the monitor
// it is (mostly) on. No position yet: bottom-left, above the GW2 chat panel.
void PlaceWindow(int& x, int& y, int w, int h) {
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    const bool autoPos = (x == Config::kAutoPos || y == Config::kAutoPos);
    const RECT want{x, y, x + w, y + h};
    HMONITOR mon = autoPos ? MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY)
                           : MonitorFromRect(&want, MONITOR_DEFAULTTONEAREST);
    GetMonitorInfoW(mon, &mi);
    const RECT& wa = mi.rcWork;
    if (autoPos) {
        x = wa.left + (wa.right - wa.left) / 64;
        y = wa.bottom - h - (wa.bottom - wa.top) * 38 / 100;
    }
    x = std::clamp(x, static_cast<int>(wa.left), std::max(static_cast<int>(wa.left), static_cast<int>(wa.right) - w));
    y = std::clamp(y, static_cast<int>(wa.top), std::max(static_cast<int>(wa.top), static_cast<int>(wa.bottom) - h));
}

bool HasTranslatableText(const std::vector<Segment>& segments) {
    for (const Segment& s : segments)
        if (!s.keep && std::any_of(s.text.begin(), s.text.end(), IsWordChar)) return true;
    return false;
}

bool IsRtlLanguage(const std::wstring& primary) {
    return primary == L"AR" || primary == L"HE" || primary == L"FA" || primary == L"UR" || primary == L"PS" ||
           primary == L"YI";
}

// "/p ", "/w Name, " ... -> channel. Unknown = the channel active in GW2.
Channel ChannelFromPrefix(const std::wstring& prefix, std::wstring* whisperTo) {
    const std::wstring p = Trim(prefix);
    if (p.size() < 2 || p[0] != L'/') return Channel::Unknown;
    const size_t end = p.find(L' ');
    const std::wstring cmd = ToLowerAscii(p.substr(1, end == std::wstring::npos ? std::wstring::npos : end - 1));
    if (cmd == L"s" || cmd == L"say") return Channel::Say;
    if (cmd == L"m" || cmd == L"map") return Channel::Map;
    if (cmd == L"p" || cmd == L"party") return Channel::Party;
    if (cmd == L"d" || cmd == L"squad") return Channel::Squad;
    if (cmd == L"t" || cmd == L"team") return Channel::Team;
    if (cmd == L"g" || cmd == L"guild" || (cmd.size() == 2 && cmd[0] == L'g' && cmd[1] >= L'1' && cmd[1] <= L'5'))
        return Channel::Guild;
    if (cmd == L"r" || cmd == L"reply") return Channel::Whisper;
    if (cmd == L"w" || cmd == L"whisper" || cmd == L"tell") {
        if (whisperTo && end != std::wstring::npos) {
            std::wstring name = Trim(p.substr(end + 1));
            if (!name.empty() && name.back() == L',') name.pop_back();
            *whisperTo = Trim(name);
        }
        return Channel::Whisper;
    }
    return Channel::Unknown;
}

std::wstring HitsText(const std::vector<GlossaryMatch>& hits) {
    std::wstring s;
    for (size_t i = 0; i < hits.size() && i < 2; ++i) {
        if (!s.empty()) s += L", ";
        s += hits[i].source + L" \u2192 " + hits[i].target;
    }
    if (hits.size() > 2) s += L" \u2026";
    return s.empty() ? s : TrF(L"Game names: {1}", {s});
}

COLORREF ToColorRef(Rgb c) { return RGB(c.r, c.g, c.b); }

COLORREF ChannelColorRef(const std::vector<ChannelColor>& palette, Channel ch, COLORREF fallback) {
    for (const ChannelColor& c : palette)
        if (c.channel == ch) return ToColorRef(c.rgb);
    return fallback;
}

int TextWidth(HDC dc, const std::wstring& s, HFONT font) {
    SelectObject(dc, font);
    SIZE sz{};
    GetTextExtentPoint32W(dc, s.c_str(), static_cast<int>(s.size()), &sz);
    return sz.cx;
}

void DrawLine(HDC dc, const std::wstring& s, RECT r, COLORREF color, HFONT font, UINT flags) {
    SelectObject(dc, font);
    SetTextColor(dc, color);
    DrawTextW(dc, s.c_str(), static_cast<int>(s.size()), &r, flags | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
}

void DrawCaret(HDC dc, int cx, int cy, int half, COLORREF color) {
    HBRUSH b = CreateSolidBrush(color);
    HPEN p = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
    POINT pts[3] = {{cx - half, cy - half / 2}, {cx + half, cy - half / 2}, {cx, cy + half / 2 + 1}};
    Polygon(dc, pts, 3);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(b);
    DeleteObject(p);
}

// A small rounded "chip" with a drop-down caret. `x` is the left edge, or
// the right edge when `fromRight`. Returns its rectangle.
RECT DrawChip(HDC dc, const Theme& t, int x, int top, int bottom, const std::wstring& text, COLORREF color,
              bool fromRight) {
    const int textW = TextWidth(dc, text, t.fontUi);
    const int w = textW + t.S(8) + t.S(18);
    RECT r{fromRight ? x - w : x, top, fromRight ? x : x + w, bottom};
    HBRUSH bg = CreateSolidBrush(Theme::kInputBg);
    HPEN pen = CreatePen(PS_SOLID, 1, Theme::kInputBg);
    HGDIOBJ ob = SelectObject(dc, bg), op = SelectObject(dc, pen);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, t.S(8), t.S(8));
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(bg);
    DeleteObject(pen);
    RECT tr{r.left + t.S(8), r.top, r.right - t.S(16), r.bottom};
    DrawLine(dc, text, tr, color, t.fontUi, DT_LEFT | DT_END_ELLIPSIS);
    DrawCaret(dc, r.right - t.S(10), (r.top + r.bottom) / 2, t.S(3), Theme::kMuted);
    return r;
}

std::wstring LangMenuLabel(const LangInfo& l) { return std::wstring(l.native) + L"\t" + l.code; }

// Default tab names ("Whisper", "Party" ...) follow the UI language; names
// you typed yourself stay as they are.
std::wstring TabLabel(const ChatTab& t) { return t.person.empty() ? Tr(t.name.c_str()) : t.person; }

// Menus open mirrored for a right-to-left UI language.
UINT MenuFlags(UINT flags) { return flags | (UiRtl() ? TPM_LAYOUTRTL : 0); }

}  // namespace

// ===========================================================================
// Setup
// ===========================================================================
int MainWindow::Run(HINSTANCE inst, const std::wstring& cmdLine) {
    inst_ = inst;
    cfg_.Load(DataDir());
    SetUiLang(cfg_.uiLang);
    waitForGame_ = HasSwitch(cmdLine, kWaitForGw2Switch);
    const bool markChat = HasSwitch(cmdLine, L"--mark-chat"), coverChat = HasSwitch(cmdLine, L"--cover-chat");
    CleanUpFiles();
    // The installed copy is always in the start menu, so it can never get lost (autostart and the desktop shortcut
    // are free choices). Autostart belongs to it too: an entry left pointing elsewhere (an old download folder) moves.
    if (RunningInstalledCopy() && cfg_.startMenu) EnsureStartMenuShortcut(CurrentExePath());
    if (RunningInstalledCopy() && !AutostartTarget().empty() &&  // also when the old exe is gone
        CompareStringOrdinal(AutostartTarget().c_str(), -1, CurrentExePath().c_str(), -1, TRUE) != CSTR_EQUAL)
        SetAutostart(true, CurrentExePath());
    LoadCorrections();
    LoadMyWords();

    HDC screen = GetDC(nullptr);
    const int dpi = GetDeviceCaps(screen, LOGPIXELSY);
    ReleaseDC(nullptr, screen);
    theme_.Create(dpi, cfg_.fontPercent, cfg_.fontFace);
    InitServices();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = Proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);
    ChatLogView::Register(inst);
    PreviewView::Register(inst);
    SuggestionBar::Register(inst);

    int w = theme_.S(cfg_.w), h = theme_.S(cfg_.h), x = cfg_.x, y = cfg_.y;
    PlaceWindow(x, y, w, h);
    CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kClassName, kTitle, WS_POPUP | WS_CLIPCHILDREN, x,
                    y, w, h, nullptr, nullptr, inst, this);
    if (!hwnd_) return 1;
    // Started on a monitor whose scale differs from the primary one?
    using DpiForWindowFn = UINT(WINAPI*)(HWND);
    if (auto fn = reinterpret_cast<DpiForWindowFn>(
            reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow")))) {
        const int wdpi = static_cast<int>(fn(hwnd_));
        if (wdpi > 0 && wdpi != theme_.dpi) {
            RECT r;
            GetWindowRect(hwnd_, &r);
            r.right = r.left + MulDiv(r.right - r.left, wdpi, theme_.dpi);
            r.bottom = r.top + MulDiv(r.bottom - r.top, wdpi, theme_.dpi);
            ApplyDpi(wdpi, &r);
        }
    }
    SetLayeredWindowAttributes(hwnd_, 0, static_cast<BYTE>(cfg_.opacity), LWA_ALPHA);
    // Capture exclusion is switched on only while the window covers the chat
    // area (PollGame), so screenshots normally show it.

    if (auto hk = ParseHotkey(cfg_.hotkey))
        hotkeyOk_ = RegisterHotKey(hwnd_, kHotkeyId, hk->mods | MOD_NOREPEAT, hk->vk) != FALSE;

    if (engine_ == Engine::Basic)
        SetStatus(Tr(L"Translator: MyMemory (free) \u00b7 better: Google, Microsoft, DeepL or an AI model (\u2261 \u2192 "
                     L"Settings \u2192 Translator)"),
                  Tone::Muted, 9000);
    else
        SetStatus(TrF(L"Translator: {1}", {translator_->Name()}), Tone::Ok, 5000);
    if (cfg_.spellEnabled && !spell_.Ready())
        SetStatus(TrF(L"No spell checking installed for {1} (Windows language packs)", {kbdLocale_}), Tone::Warn, 8000);

    EnsureDir(cfg_.CacheDir());
    RefreshGlossary();
    UpdateSpellWords();
    StartReader();
    SetTimer(hwnd_, kTimerGame, 300, nullptr);
    UpdateHint();
    UpdatePreview();

    AddTrayIcon();
    if (!waitForGame_) {
        ShowWindow(hwnd_, SW_SHOW);
        SetForegroundWindow(hwnd_);
        SetFocus(input_.Hwnd());
    }
    if (markChat || coverChat) PostMessageW(hwnd_, WM_APP_FIRSTRUN, markChat ? 1 : 0, coverChat ? 1 : 0);
    else if (!cfg_.setupDone && !waitForGame_) PostMessageW(hwnd_, WM_APP_FIRSTRUN, 2, 0);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    reader_.Stop();
    spell_.SaveLearned();
    theme_.Destroy();
    return static_cast<int>(m.wParam);
}

// Diagnostic pictures, leftovers of interrupted writes, old caches: keep the
// folders small without asking.
void MainWindow::CleanUpFiles() {
    CleanFolder(cfg_.dataDir, {0, 0, 86400}, IsStaleTempFile);
    CleanFolder(cfg_.CacheDir(), {0, 0, 86400}, IsStaleTempFile);
    CleanFolder(cfg_.LearnedDir(), {0, 0, 86400}, IsStaleTempFile);
    CleanFolder(cfg_.CaptureDir(), {60, 150ull << 20, 3 * 86400}, IsCaptureFile);
    if (cfg_.saveCaptures) {  // diagnostics never stay on across restarts
        cfg_.saveCaptures = false;
        cfg_.SaveBool(L"Reader", L"SaveCaptures", false);
    }
}

void MainWindow::InitServices() {
    wchar_t locale[LOCALE_NAME_MAX_LENGTH] = {};
    GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH);
    kbdLocale_ = InputBox::KeyboardLocale();
    if (kbdLocale_.empty()) kbdLocale_ = locale;
    kbdRtl_ = IsRtlLanguage(PrimaryLang(kbdLocale_));

    const LangInfo* read = FindLanguage(cfg_.readLang.empty() ? std::wstring(locale) : cfg_.readLang);
    readLang_ = read ? read->code : L"EN-GB";
    for (const std::wstring& code : cfg_.writeLangs)
        if (const LangInfo* l = FindLanguage(code)) writeLangs_.push_back(l->code);
    if (writeLangs_.empty()) writeLangs_ = {L"EN-GB"};
    writeIdx_ = 0;
    if (const LangInfo* cl = FindLanguage(cfg_.chatLang); cl && cl->latinScript) chatLang_ = cl->code;

    for (const ChatTab& t : cfg_.tabs) {
        TabState st;
        st.send = SoleSendChannel(t.channels);
        tabState_.push_back(st);
        nextTabId_ = std::max(nextTabId_, t.id + 1);
    }
    tab_ = std::min(static_cast<size_t>(std::max(cfg_.activeTab, 0)), cfg_.tabs.size() - 1);

    llm_.reset();
    if (!cfg_.llmModel.empty()) {
        LlmSettings s;
        s.url = cfg_.llmUrl;
        s.model = cfg_.llmModel;
        s.apiKey = cfg_.llmKey;
        s.timeoutMs = cfg_.llmTimeoutSec * 1000;
        llm_ = MakeLlmTranslator(s);
    }
    ChooseEngine(false);

    if (cfg_.spellEnabled) {
        // Spell check, learned words and word-bar starters all follow the language you type in.
        const std::wstring typing = TypingLocale();
        std::vector<std::wstring> tags = SpellTagCandidates(PrimaryLang(typing), typing);
        if (cfg_.writeIn.empty())
            for (const std::wstring& t : SpellTagCandidates(PrimaryLang(locale), locale)) tags.push_back(t);
        spell_.Init(tags, cfg_.UserWordsPath());
    }
    spell_.UseLearnedLanguage(PrimaryLang(TypingLocale()), cfg_.LearnedDir());
}

void MainWindow::ChooseEngine(bool announce) {
    const bool haveDeepL = !cfg_.deeplKey.empty(), haveLlm = llm_ != nullptr;
    const bool haveGoogle = !cfg_.googleKey.empty(), haveMs = !cfg_.msKey.empty(), haveLibre = !cfg_.libreUrl.empty();
    Engine e = cfg_.engine;
    std::wstring note;
    if (e == Engine::Auto)
        e = haveDeepL    ? Engine::DeepL
            : haveGoogle ? Engine::Google
            : haveMs     ? Engine::Microsoft
            : haveLibre  ? Engine::Libre
            : haveLlm    ? Engine::Llm
                         : Engine::Basic;
    if ((e == Engine::Google && !haveGoogle) || (e == Engine::Microsoft && !haveMs) || (e == Engine::Libre && !haveLibre)) {
        e = Engine::Basic;
        note = Tr(L"No API key set – using basic (MyMemory)");
    }
    if (e == Engine::DeepL && !haveDeepL) {
        e = Engine::Basic;
        note = Tr(L"No DeepL key set \u2013 using basic (MyMemory)");
    }
    if (e == Engine::Llm && !haveLlm) {
        e = Engine::Basic;
        note = Tr(L"No LLM model set \u2013 using basic (MyMemory)");
    }
    engine_ = e;
    if (e == Engine::DeepL) translator_ = MakeDeepLTranslator(cfg_.deeplKey);
    else if (e == Engine::Google) translator_ = MakeGoogleTranslator(cfg_.googleKey);
    else if (e == Engine::Microsoft) translator_ = MakeMicrosoftTranslator(cfg_.msKey, cfg_.msRegion);
    else if (e == Engine::Libre) translator_ = MakeLibreTranslator(cfg_.libreUrl, cfg_.libreKey);
    else if (e == Engine::Llm) translator_ = llm_;
    else translator_ = MakeMyMemoryTranslator(cfg_.basicEmail);
    inPauseUntil_ = 0;
    // MyMemory works fully, but people should know once who receives their texts.
    if (e == Engine::Basic && !cfg_.myMemoryNoticeShown && cfg_.setupDone)
        PostMessageW(hwnd_, WM_APP_MYMEMORY_NOTICE, 0, 0);
    if (announce) {
        if (note.empty()) SetStatus(TrF(L"Translator: {1}", {translator_->Name()}), Tone::Ok, 4000);
        else SetStatus(note, Tone::Warn, 6000);
    }
}

void MainWindow::CreateChildren() {
    ChatLogView::Callbacks lcb;
    lcb.onReply = [this](const std::wstring& speaker) {
        whisperTarget_ = speaker;
        // A whisper tab if there is one, otherwise this tab writes the whisper.
        size_t target = tab_;
        if (SoleSendChannel(cfg_.tabs[tab_].channels) != Channel::Whisper)
            for (size_t i = 0; i < cfg_.tabs.size(); ++i)
                if (SoleSendChannel(cfg_.tabs[i].channels) == Channel::Whisper) target = i;
        SwitchTab(target);
        SetSendChannel(Channel::Whisper);
        SetStatus(TrF(L"Reply to {1} \u2013 the name comes from text recognition, please check it", {speaker}),
                  Tone::Muted, 6000);
        ShowOverlay();
    };
    lcb.onUseChannel = [this](Channel ch) {
        SetSendChannel(ch);
        ShowOverlay();
    };
    lcb.onCalibrate = [this](Channel ch, Rgb rgb) {
        cfg_.SaveColor(ch, rgb);
        log_.SetPalette(cfg_.palette);
        SetStatus(TrF(L"Colour #{1} now belongs to \u201c{2}\u201d", {RgbToHex(rgb), ChannelLabel(ch)}), Tone::Ok, 5000);
    };
    lcb.onResetColors = [this] {
        cfg_.ResetColors();
        log_.SetPalette(cfg_.palette);
        SetStatus(Tr(L"Channel colours reset to the GW2 defaults"), Tone::Ok, 4000);
    };
    lcb.onHintClick = [this] { PickRegion(); };
    lcb.onSpeakerClick = [this](const std::wstring& speaker) { OpenWhisperTab(speaker); };
    lcb.onRetranslate = [this](uint64_t id, const std::wstring& text) { Retranslate(id, text); };
    lcb.onCorrect = [this](const ChatEntry& e) { CorrectEntry(e); };
    log_.Create(hwnd_, inst_, &theme_, std::move(lcb));
    log_.SetPalette(cfg_.palette);
    ApplyTabFilter();

    preview_.Create(hwnd_, inst_, &theme_, [this] { SetFocus(input_.Hwnd()); });
    words_.Create(hwnd_, inst_, &theme_, [this](size_t i) {
        input_.AcceptSuggestion(i);
        SetFocus(input_.Hwnd());
    });
    words_.SetForget([this](const std::wstring& w) { return spell_.IsLearned(w); },
                     [this](const std::wstring& w) {
                         spell_.Forget(w);
                         input_.RefreshSuggestions();
                         OnWordForgotten(w);
                         SetFocus(input_.Hwnd());
                     });

    words_.SetNames([this](const std::wstring& w) { return spell_.NameFor(w); },
                    [this](const std::wstring& w, bool add) {
                        if (add) spell_.AddName(w);
                        else spell_.RemoveName(w);
                        input_.RefreshSuggestions();
                        SetFocus(input_.Hwnd());
                    });

    InputBox::Callbacks cb;
    cb.onEnter = [this](bool original) { OnEnter(original); };
    cb.onEscape = [this] { ReturnToGame(); };
    cb.onCycleLang = [this] { CycleWrite(); };
    cb.onRomanize = [this] { Romanize(); };
    cb.onSwitchTab = [this] { SwitchTab((tab_ + 1) % cfg_.tabs.size()); };
    cb.onAutoCorrected = [this](const std::wstring& from, const std::wstring& to) {
        SetStatus(TrF(L"Corrected: {1} \u2192 {2}  \u00b7  Backspace: undo", {from, to}), Tone::Muted, 4000);
    };
    cb.onCorrectionUndone = [this](const std::wstring& original) {
        SetStatus(TrF(L"Kept \u201c{1}\u201d \u2013 learned", {original}), Tone::Muted, 3000);
    };
    cb.onForgotten = [this](const std::wstring& w) { OnWordForgotten(w); };
    cb.onExplain = [this](const std::wstring& w) { ExplainWord(w); };
    cb.onSuggestions = [this](const WordSuggestions& s) { words_.Set(s); };
    cb.onKeyboardLanguage = [this](const std::wstring& locale) { OnKeyboardLanguage(locale); };
    spell_.SetLearnChoices(cfg_.learnWords);
    input_.Create(hwnd_, inst_, &theme_, &spell_, cfg_.autoCorrect, cfg_.suggestions, std::move(cb));
    input_.SetRtl(kbdRtl_);
    Layout();
}

void MainWindow::StartReader() {
    if (!ReadingWanted()) return;
    reader_.SetSaveCaptures(cfg_.saveCaptures);
    reader_.Start(hwnd_, WM_APP_SNAPSHOT, MakeReaderOptions());
}

// Everything the text recognition needs, from the settings (reader, previews, the comparison).
ReaderOptions MainWindow::MakeReaderOptions() const {
    ReaderOptions o;
    o.intervalMs = cfg_.readerIntervalMs;
    o.ocrChoice = static_cast<int>(cfg_.ocr);
    o.tesseractPath = cfg_.tesseractPath;
    o.tesseractLangs = cfg_.tesseractLangs;
    o.readChinese = cfg_.readChinese;
    o.ocrLanguage = cfg_.ocrLanguage;
    o.scale = cfg_.ocrScale;
    o.windowCapture = WindowCaptureAllowed();
    o.freeText = cfg_.freeArea;
    o.secondLook = cfg_.secondLook;
    // Dictionaries for the second look: the languages you read and write in (the OCR language is added).
    o.wordLangs = cfg_.writeLangs;
    o.wordLangs.insert(o.wordLangs.begin(), readLang_);
    o.wordLangs.push_back(cfg_.chatLang);
    for (const auto& e : myWords_.Entries()) o.knownWords.push_back(e.first);
    // RapidOCR: the models in <data>\rapid for the languages you read and write.
    o.rapidDir = cfg_.RapidDir();
    o.glyphDir = cfg_.dataDir + L"\\glyphs";  // the glyph reader learns the chat font here
    o.rapidGroups = RapidGroupsFor(o.wordLangs);
    for (const auto& e : ocrFixes_.Entries())
        if (!e.second.empty()) o.ocrFixes.push_back(e);
    o.captureDir = cfg_.CaptureDir();
    return o;
}

// ===========================================================================
// Layout & painting
// ===========================================================================
namespace {
struct Metrics {
    int pad, head, foot, gap, inputH;
};
Metrics MetricsFor(const Theme& t) {
    return {t.S(8), t.S(32), t.S(30), t.S(6), t.textLineHeight * 2 + t.S(12)};
}
}  // namespace

void MainWindow::Layout() {
    if (!input_.Hwnd()) return;
    RECT rc;
    GetClientRect(hwnd_, &rc);
    const Metrics m = MetricsFor(theme_);
    if (collapsed_) {
        ShowWindow(log_.Hwnd(), SW_HIDE);
        ShowWindow(preview_.Hwnd(), SW_HIDE);
        ShowWindow(words_.Hwnd(), SW_HIDE);
        ShowWindow(input_.Hwnd(), SW_HIDE);
        return;
    }
    const int w = std::max(10, static_cast<int>(rc.right) - 2 * m.pad);
    // Like the GW2 chat: the log takes the space; the preview appears while you type.
    const int previewH = previewVisible_ ? preview_.PreferredHeight() : 0;
    const bool bar = cfg_.suggestions;
    const int barH = bar ? words_.PreferredHeight() : 0;
    const int inputY = rc.bottom - m.foot - m.inputH;
    const int barY = inputY - barH;
    const int previewY = barY - (previewVisible_ ? m.gap + previewH : 0);
    const int logH = std::max(theme_.S(40), previewY - m.gap - m.head);

    MoveWindow(log_.Hwnd(), m.pad, m.head, w, logH, TRUE);
    ShowWindow(log_.Hwnd(), SW_SHOW);
    MoveWindow(preview_.Hwnd(), m.pad, previewY, w, std::max(1, previewH), TRUE);
    ShowWindow(preview_.Hwnd(), previewVisible_ ? SW_SHOWNA : SW_HIDE);
    MoveWindow(words_.Hwnd(), m.pad, barY, w, std::max(1, barH), TRUE);
    ShowWindow(words_.Hwnd(), bar ? SW_SHOWNA : SW_HIDE);
    MoveWindow(input_.Hwnd(), m.pad, inputY, w, m.inputH, TRUE);
    ShowWindow(input_.Hwnd(), SW_SHOW);
    input_.ApplyPadding();
}

void MainWindow::ToggleCollapse() {
    collapsed_ = !collapsed_;
    RECT r{};
    GetWindowRect(hwnd_, &r);
    const Metrics m = MetricsFor(theme_);
    if (collapsed_) {
        expandedHeight_ = r.bottom - r.top;
        const int newH = m.head;
        SetWindowPos(hwnd_, nullptr, r.left, r.bottom - newH, r.right - r.left, newH,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    } else {
        const int targetH = expandedHeight_ > m.head ? expandedHeight_ : theme_.S(320);
        SetWindowPos(hwnd_, nullptr, r.left, r.bottom - targetH, r.right - r.left, targetH,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
    Layout();
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void MainWindow::ApplyDpi(int dpi, const RECT* suggested) {
    theme_.Destroy();
    theme_.Create(dpi, cfg_.fontPercent, cfg_.fontFace);
    input_.ApplyTheme();
    log_.ThemeChanged();
    if (suggested)
        SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                     suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
    Layout();
    InvalidateRect(hwnd_, nullptr, FALSE);
    InvalidateRect(preview_.Hwnd(), nullptr, FALSE);
    InvalidateRect(words_.Hwnd(), nullptr, FALSE);
}

void MainWindow::InvalidateChrome() {
    if (!hwnd_) return;
    RECT rc;
    GetClientRect(hwnd_, &rc);
    const Metrics m = MetricsFor(theme_);
    RECT head{0, 0, rc.right, m.head};
    RECT foot{0, rc.bottom - m.foot, rc.right, rc.bottom};
    InvalidateRect(hwnd_, &head, FALSE);
    InvalidateRect(hwnd_, &foot, FALSE);
}

std::wstring MainWindow::ChannelChipText() const {
    const std::wstring text = SanitizeChatText(input_.Hwnd() ? input_.Text() : L"");
    if (!text.empty() && text[0] == L'/') return Tr(L"Command");
    const Channel c = SendChannel();
    if (c == Channel::Whisper) return whisperTarget_.empty() ? Tr(L"Reply (/r)") : TrF(L"To {1}", {whisperTarget_});
    return c == Channel::Unknown ? Tr(L"Active channel") : ChannelLabel(c);
}

std::wstring MainWindow::WriteChipText() const {
    if (WriteOriginal()) return Tr(L"Send: original (only corrected)");
    const LangInfo* l = FindLanguage(WriteLang());
    return TrF(L"Send: {1}", {l ? std::wstring(l->native) : WriteLang()});
}

std::wstring MainWindow::TypeChipText() const {
    const LangInfo* l = FindLanguage(TypingLocale());
    if (!l) l = FindLanguage(PrimaryLang(TypingLocale()));
    return TrF(L"Write: {1}", {l ? std::wstring(l->native) : TypingLocale()});
}

// The language you type in: only this text is corrected and suggested (and learned); what is sent is translated from it.
void MainWindow::ShowTypeLangMenu() {
    const ModalScope modal;
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, Tr(L"I write in:").c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (cfg_.writeIn.empty() ? MF_CHECKED : 0), kCmdLangBase - 1,
                Tr(L"Keyboard language (switches with it)").c_str());
    const auto& langs = Languages();
    for (size_t i = 0; i < langs.size(); ++i) {
        UINT flags = MF_STRING;
        if (!cfg_.writeIn.empty() && FindLanguage(cfg_.writeIn) == &langs[i]) flags |= MF_CHECKED;
        if (i > 0 && i % 20 == 0) flags |= MF_MENUBARBREAK;
        AppendMenuW(menu, flags, kCmdLangBase + i, LangMenuLabel(langs[i]).c_str());
    }
    POINT pt{typeRect_.left, typeRect_.top};
    ClientToScreen(hwnd_, &pt);
    const UINT cmd = static_cast<UINT>(TrackPopupMenu(menu, MenuFlags(TPM_RETURNCMD | TPM_NONOTIFY | TPM_BOTTOMALIGN),
                                                      pt.x, pt.y, 0, hwnd_, nullptr));
    DestroyMenu(menu);
    if (cmd == kCmdLangBase - 1 || (cmd >= kCmdLangBase && cmd - kCmdLangBase < langs.size())) {
        cfg_.writeIn = cmd == kCmdLangBase - 1 ? std::wstring() : std::wstring(langs[cmd - kCmdLangBase].code);
        cfg_.SaveValue(L"Spelling", L"WriteIn", cfg_.writeIn);
        ApplyTypingLanguage();
        InvalidateChrome();
    }
    SetFocus(input_.Hwnd());
}

void MainWindow::ApplyTypingLanguage() {
    const std::wstring typing = TypingLocale();
    if (cfg_.spellEnabled) {
        spell_.SwitchLanguage(SpellTagCandidates(PrimaryLang(typing), typing));
        input_.RecheckSpelling();
    }
    spell_.UseLearnedLanguage(PrimaryLang(typing), cfg_.LearnedDir());
    input_.RefreshSuggestions();
}

void MainWindow::Paint() {
    PAINTSTRUCT ps;
    HDC wdc = BeginPaint(hwnd_, &ps);
    RECT rc;
    GetClientRect(hwnd_, &rc);
    HDC dc = CreateCompatibleDC(wdc);
    HBITMAP bmp = CreateCompatibleBitmap(wdc, std::max(1L, rc.right), std::max(1L, rc.bottom));
    HGDIOBJ oldBmp = SelectObject(dc, bmp);
    HGDIOBJ oldFont = SelectObject(dc, theme_.fontUi);
    FillRect(dc, &rc, theme_.bg);
    SetBkMode(dc, TRANSPARENT);
    const Metrics m = MetricsFor(theme_);
    const Theme& t = theme_;

    // ---- header: right side first (close, collapse, menu, reading language), tabs in the rest
    closeRect_ = {rc.right - t.S(30), 0, rc.right, m.head};
    DrawLine(dc, L"\u00d7", closeRect_, Theme::kMuted, t.fontText, DT_CENTER);
    collapseRect_ = {closeRect_.left - t.S(28), 0, closeRect_.left, m.head};
    DrawLine(dc, collapsed_ ? L"\u25bc" : L"\u25b2", collapseRect_, collapsed_ ? Theme::kAccent : Theme::kMuted, t.fontSmall, DT_CENTER);
    menuRect_ = {collapseRect_.left - t.S(28), 0, collapseRect_.left, m.head};
    DrawLine(dc, L"\u2261", menuRect_, Theme::kMuted, t.fontText, DT_CENTER);
    // Reading on/off: a small dot, quiet grey while reading, black when off.
    readDotRect_ = {menuRect_.left - t.S(20), 0, menuRect_.left, m.head};
    {
        const int d = t.S(9);
        const int cx = (readDotRect_.left + readDotRect_.right) / 2, cy = m.head / 2;
        HBRUSH fill = CreateSolidBrush(cfg_.readerEnabled ? RGB(150, 155, 165) : RGB(0, 0, 0));
        HPEN ring = CreatePen(PS_SOLID, std::max(1, t.S(1)), RGB(120, 125, 135));
        HGDIOBJ oldB = SelectObject(dc, fill), oldP = SelectObject(dc, ring);
        Ellipse(dc, cx - d / 2, cy - d / 2, cx + d / 2 + 1, cy + d / 2 + 1);
        SelectObject(dc, oldB);
        SelectObject(dc, oldP);
        DeleteObject(fill);
        DeleteObject(ring);
    }
    const LangInfo* rl = FindLanguage(readLang_);
    const std::wstring readName = rl ? rl->native : readLang_;
    // Short form when space is tight (several tabs or a narrow window).
    const bool roomy = rc.right >= t.S(470) && cfg_.tabs.size() <= 3;
    readRect_ = DrawChip(dc, t, readDotRect_.left - t.S(2), t.S(6), m.head - t.S(6),
                         roomy ? TrF(L"Read: {1}", {readName}) : readName, Theme::kAccent, true);

    const int badgeD = t.S(15), gap = t.S(16);
    const int tabsLeft = m.pad + t.S(2), tabsRight = static_cast<int>(readRect_.left) - t.S(10);
    const size_t n = cfg_.tabs.size();
    std::vector<int> natural(n);
    int total = 0;
    for (size_t i = 0; i < n; ++i) {
        natural[i] = TextWidth(dc, TabLabel(cfg_.tabs[i]), i == tab_ ? t.fontUiBold : t.fontUi) +
                     (tabState_[i].unread > 0 ? badgeD + t.S(5) : 0);
        total += natural[i] + (i + 1 < n ? gap : 0);
    }
    // Not enough room: every tab gets an equal share, names end in "...".
    const int share = n ? std::max(t.S(24), (tabsRight - tabsLeft - gap * static_cast<int>(n - 1)) / static_cast<int>(n))
                        : 0;
    tabRects_.assign(n, RECT{});
    int x = tabsLeft;
    for (size_t i = 0; i < n; ++i) {
        const bool active = i == tab_;
        const int unread = tabState_[i].unread;
        const int w = total > tabsRight - tabsLeft ? std::min(natural[i], share) : natural[i];
        const int textW = std::max(t.S(8), w - (unread > 0 ? badgeD + t.S(5) : 0));
        HFONT font = active ? t.fontUiBold : t.fontUi;
        RECT r{x, 0, x + textW, m.head};
        DrawLine(dc, TabLabel(cfg_.tabs[i]), r, active ? Theme::kText : Theme::kMuted, font, DT_LEFT | DT_END_ELLIPSIS);
        int right = x + textW;
        if (unread > 0) {
            const std::wstring num = unread > 9 ? L"9+" : std::to_wstring(unread);
            // Purple for whisper tabs (like GW2's whisper colour), gold otherwise.
            const COLORREF bc =
                SoleSendChannel(cfg_.tabs[i].channels) == Channel::Whisper ? Theme::kBadge : Theme::kAccent;
            const int bx = right + t.S(5), by = (m.head - badgeD) / 2;
            HBRUSH b = CreateSolidBrush(bc);
            HPEN p = CreatePen(PS_SOLID, 1, bc);
            HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
            Ellipse(dc, bx, by, bx + badgeD, by + badgeD);
            SelectObject(dc, ob);
            SelectObject(dc, op);
            DeleteObject(b);
            DeleteObject(p);
            RECT br{bx, by, bx + badgeD, by + badgeD};
            DrawLine(dc, num, br, Theme::kBg, t.fontSmall, DT_CENTER);
            right = bx + badgeD;
        }
        if (active) {
            RECT bar{x, m.head - t.S(4), x + textW, m.head - t.S(2)};
            HBRUSH b = CreateSolidBrush(Theme::kAccent);
            FillRect(dc, &bar, b);
            DeleteObject(b);
        }
        tabRects_[i] = {x - t.S(6), 0, right + t.S(6), m.head};
        x = right + gap;
    }

    // ---- footer: channel | send-as | counter | status (hidden when collapsed)
    if (collapsed_) {
        channelRect_ = {};
        writeRect_ = {};
        typeRect_ = {};
    } else {
        const int fy0 = rc.bottom - m.foot + t.S(4), fy1 = rc.bottom - t.S(4);
        const Channel chipChannel = SendChannel();
        channelRect_ = DrawChip(dc, t, m.pad, fy0, fy1, ChannelChipText(),
                                ChannelColorRef(cfg_.palette, chipChannel, Theme::kText), false);
        // You write in one language (only that is corrected and suggested), the chat gets another.
        typeRect_ = DrawChip(dc, t, channelRect_.right + t.S(6), fy0, fy1, TypeChipText(), Theme::kText, false);
        writeRect_ = DrawChip(dc, t, typeRect_.right + t.S(6), fy0, fy1, WriteChipText(),
                              WriteOriginal() ? Theme::kMuted : Theme::kAccent, false);
        chatRect_ = {};

        std::wstring counter;
        COLORREF counterColor = Theme::kMuted;
        if (PreviewIsCurrent() && parts_.size() > 1) {
            counter = TrF(L"{1} parts", {std::to_wstring(parts_.size())});
            counterColor = Theme::kWarn;
        } else {
            const std::wstring line = PreviewIsCurrent() && !parts_.empty()
                                          ? parts_[std::min(partIdx_, parts_.size() - 1)]
                                          : ComposePrefix() + SanitizeChatText(input_.Text());
            const size_t charCount = CodePointCount(line);
            counter = std::to_wstring(charCount) + L"/" + std::to_wstring(cfg_.maxLength);
            if (charCount > static_cast<size_t>(cfg_.maxLength)) counterColor = Theme::kWarn;
        }
        const LONG chipsRight = IsRectEmpty(&chatRect_) ? writeRect_.right : chatRect_.right;
        RECT fr{chipsRight + t.S(10), rc.bottom - m.foot, rc.right - m.pad, rc.bottom};
        DrawLine(dc, counter, fr, counterColor, t.fontUi, DT_LEFT);
        fr.left += TextWidth(dc, counter, t.fontUi) + t.S(10);

        std::wstring text = status_;
        COLORREF color = Theme::kMuted;
        switch (tone_) {
            case Tone::Ok: color = Theme::kOk; break;
            case Tone::Warn: color = Theme::kWarn; break;
            case Tone::Error: color = Theme::kError; break;
            default: break;
        }
        if (text.empty()) {
            if (!hotkeyOk_ && !Trim(cfg_.hotkey).empty()) {  // no hotkey chosen: nothing to warn about
                text = TrF(L"Hotkey \u201c{1}\u201d is taken \u2013 change it in the settings", {cfg_.hotkey});
                color = Theme::kWarn;
            } else if (readingActive_) {
                text = Tr(L"\u25cf reading the chat");
                color = Theme::kOk;
                const std::wstring quota = MyMemoryQuotaText();  // small and quiet, only with MyMemory
                if (!quota.empty()) text += L"  \u00b7  " + quota;
            } else if (!gw2_) {
                text = Tr(L"GW2 not found");
            } else {
                text = cfg_.copyOnly ? Tr(L"Copy only \u00b7 Esc: back to the game") : Tr(L"Esc: back to the game");
            }
        }
        DrawLine(dc, text, fr, color, t.fontUi, DT_RIGHT | DT_END_ELLIPSIS | (IsRtlText(text) ? DT_RTLREADING : 0));
    }

    BitBlt(wdc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldFont);
    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(dc);
    EndPaint(hwnd_, &ps);
}

bool MainWindow::IsClickable(POINT pt) const {
    for (const RECT* r : {&readRect_, &readDotRect_, &menuRect_, &collapseRect_, &closeRect_, &channelRect_, &writeRect_,
                          &chatRect_, &typeRect_})
        if (PtInRect(r, pt)) return true;
    for (const RECT& r : tabRects_)
        if (PtInRect(&r, pt)) return true;
    return false;
}

LRESULT MainWindow::HitTest(LPARAM lp) const {
    POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    ScreenToClient(hwnd_, &pt);
    RECT rc;
    GetClientRect(hwnd_, &rc);
    if (IsClickable(pt)) return HTCLIENT;
    const int b = theme_.S(6);
    const bool left = pt.x < b, right = pt.x >= rc.right - b;
    if (collapsed_) {
        if (left) return HTLEFT;
        if (right) return HTRIGHT;
        return HTCAPTION;
    }
    const bool top = pt.y < b, bottom = pt.y >= rc.bottom - b;
    if (top && left) return HTTOPLEFT;
    if (top && right) return HTTOPRIGHT;
    if (bottom && left) return HTBOTTOMLEFT;
    if (bottom && right) return HTBOTTOMRIGHT;
    if (left) return HTLEFT;
    if (right) return HTRIGHT;
    if (top) return HTTOP;
    if (bottom) return HTBOTTOM;
    const Metrics m = MetricsFor(theme_);
    if (pt.y < m.head || pt.y >= rc.bottom - m.foot) return HTCAPTION;  // drag by header/footer
    return HTCLIENT;
}

void MainWindow::OnClick(POINT pt) {
    if (PtInRect(&closeRect_, pt)) PostMessageW(hwnd_, WM_CLOSE, 0, 0);
    else if (PtInRect(&collapseRect_, pt)) ToggleCollapse();
    else if (PtInRect(&menuRect_, pt)) ShowMainMenu();
    else if (PtInRect(&readDotRect_, pt)) ToggleReading();
    else if (PtInRect(&readRect_, pt)) ShowReadMenu();
    else if (PtInRect(&channelRect_, pt)) ShowChannelMenu();
    else if (PtInRect(&chatRect_, pt)) ShowChatLangMenu();
    else if (PtInRect(&typeRect_, pt)) ShowTypeLangMenu();
    else if (PtInRect(&writeRect_, pt)) ShowWriteMenu();
    else
        for (size_t i = 0; i < tabRects_.size(); ++i)
            if (PtInRect(&tabRects_[i], pt)) SwitchTab(i);
}

// ===========================================================================
// Status
// ===========================================================================
void MainWindow::SetStatus(const std::wstring& text, Tone tone, UINT autoClearMs) {
    status_ = text;
    tone_ = tone;
    if (!hwnd_) return;
    KillTimer(hwnd_, kTimerStatus);
    if (autoClearMs) SetTimer(hwnd_, kTimerStatus, autoClearMs, nullptr);
    InvalidateChrome();
}

// Background work must not hide a warning or error that matters more.
bool MainWindow::BackgroundNoticeAllowed() const {
    return status_.empty() || tone_ == Tone::Muted || tone_ == Tone::Ok;
}

// ===========================================================================
// Languages, tabs, menus
// ===========================================================================
bool MainWindow::WriteOriginal() const { return writeIdx_ >= writeLangs_.size(); }

std::wstring MainWindow::WriteLang() const { return WriteOriginal() ? std::wstring() : writeLangs_[writeIdx_]; }

// GW2 cannot show every script. Writing in Arabic, Chinese ... means: you see
// your message in that language, but the chat gets it in the chat language.
bool MainWindow::WriteNeedsChatLang() const {
    // "Write" and "Send" are two choices now: what is chosen under Send is what the chat gets. (Before, sending in a
    // script GW2 cannot show went into a separate chat language.)
    return false;
}

std::wstring MainWindow::SendLang() const {
    if (WriteOriginal()) return std::wstring();
    return WriteNeedsChatLang() ? chatLang_ : WriteLang();
}

void MainWindow::ShowChatLangMenu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, Tr(L"Send into the chat in:").c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    const auto& langs = Languages();
    std::vector<size_t> idx;
    for (size_t i = 0; i < langs.size(); ++i) {
        if (!langs[i].latinScript) continue;
        idx.push_back(i);
        UINT flags = MF_STRING | (chatLang_ == langs[i].code ? MF_CHECKED : 0);
        if (idx.size() > 1 && (idx.size() - 1) % 20 == 0) flags |= MF_MENUBARBREAK;
        AppendMenuW(menu, flags, kCmdLangBase + i, LangMenuLabel(langs[i]).c_str());
    }
    POINT pt{chatRect_.left, chatRect_.top};
    ClientToScreen(hwnd_, &pt);
    const UINT cmd = static_cast<UINT>(TrackPopupMenu(menu, MenuFlags(TPM_RETURNCMD | TPM_NONOTIFY | TPM_BOTTOMALIGN),
                                                      pt.x, pt.y, 0, hwnd_, nullptr));
    DestroyMenu(menu);
    if (cmd >= kCmdLangBase && cmd - kCmdLangBase < langs.size()) {
        chatLang_ = langs[cmd - kCmdLangBase].code;
        cfg_.chatLang = chatLang_;
        cfg_.SaveValue(L"Translate", L"ChatLang", chatLang_);
        SetWriteIndex(writeIdx_);  // translate again
    }
    SetFocus(input_.Hwnd());
}

void MainWindow::CycleWrite() { SetWriteIndex((writeIdx_ + 1) % (writeLangs_.size() + 1)); }

void MainWindow::SetWriteIndex(size_t idx) {
    writeIdx_ = std::min(idx, writeLangs_.size());
    if (tone_ == Tone::Error) SetStatus(L"", Tone::Muted);  // e.g. an error of the old language
    ++inputGen_;
    previewOk_ = false;
    sendPending_ = false;
    parts_.clear();
    partIdx_ = 0;
    lastHits_.clear();
    RefreshGlossary();
    UpdatePreview();
    InvalidateChrome();
    StartTranslation();
}

void MainWindow::SetReadLang(const std::wstring& code) {
    if (code == readLang_) return;
    readLang_ = code;
    cfg_.SaveValue(L"Translate", L"ReadLang", code);
    SetStatus(TrF(L"New chat lines are translated into {1}", {LanguageLabel(code)}), Tone::Ok, 4000);
    InvalidateChrome();
    if (PreviewIsCurrent()) {
        backText_.clear();
        StartBackTranslation();
        UpdatePreview();
    }
}

void MainWindow::SwitchTab(size_t idx) {
    if (idx >= cfg_.tabs.size()) return;
    tabState_[idx].unread = 0;
    if (idx != tab_) {
        tab_ = idx;
        cfg_.activeTab = static_cast<int>(idx);
        cfg_.SaveValue(L"Tabs", L"Active", std::to_wstring(idx + 1));
        ApplyTabFilter();
        // The preview shows the prefix of the new tab's channel.
        if (partIdx_ == 0 && PreviewIsCurrent()) {
            const ChatSplit split = SplitChatCommand(SanitizeChatText(input_.Text()));
            if (split.prefix.empty()) SetPreviewBody(ComposePrefix(), previewBody_);
        }
        UpdateHint();
    }
    InvalidateChrome();
    UpdatePreview();
}

void MainWindow::ApplyTabFilter() {
    const ChatTab& t = cfg_.tabs[tab_];
    log_.SetFilter(t.channels, t.id, t.person);
    if (!t.person.empty()) {  // a player's whisper tab writes to that player
        whisperTarget_ = t.person;
        tabState_[tab_].send = Channel::Whisper;
    }
}

// Click on a name: that player's own whisper tab (opened or reused; at most
// kMaxPersonTabs, the oldest one makes room), ready to write to them.
void MainWindow::OpenWhisperTab(const std::wstring& name) {
    const std::wstring who = Trim(name);
    if (who.empty()) return;
    for (size_t i = 0; i < cfg_.tabs.size(); ++i)
        if (CaseFold(cfg_.tabs[i].person) == CaseFold(who)) {
            SwitchTab(i);
            ApplyTabFilter();
            SetSendChannel(Channel::Whisper);
            ShowOverlay();
            return;
        }
    size_t persons = 0, oldest = cfg_.tabs.size();
    for (size_t i = 0; i < cfg_.tabs.size(); ++i)
        if (!cfg_.tabs[i].person.empty()) {
            ++persons;
            if (oldest == cfg_.tabs.size()) oldest = i;
        }
    if (persons >= kMaxPersonTabs && oldest < cfg_.tabs.size()) {
        cfg_.tabs[oldest].name = who;
        cfg_.tabs[oldest].person = who;
        // Move it to the end: tabs are kept in the order they were opened.
        cfg_.tabs.push_back(cfg_.tabs[oldest]);
        tabState_.push_back(tabState_[oldest]);
        cfg_.tabs.erase(cfg_.tabs.begin() + static_cast<std::ptrdiff_t>(oldest));
        tabState_.erase(tabState_.begin() + static_cast<std::ptrdiff_t>(oldest));
        tab_ = cfg_.tabs.size() - 1;
        tabState_[tab_].unread = 0;
        OnTabsChanged();
    } else {
        ChatTab t;
        t.name = who;
        t.person = who;
        t.channels = ChannelBit(Channel::Whisper);
        AddTab(t);
    }
    SetSendChannel(Channel::Whisper);
    SetStatus(TrF(L"Whisper tab for {1} – the name comes from text recognition, please check it", {who}),
              Tone::Muted, 6000);
    ShowOverlay();
    SetFocus(input_.Hwnd());
}

// Click on a line that stayed untranslated: translate it now, before anything else.
// Measured (ocr_bench, real captures): Windows OCR ~90 ms and best at normal/large text; Tesseract 1.5-3.5 s,
// better only with very small letters (1080p with a small chat font).
std::wstring MainWindow::ReadingAdvice() const {
    RECT r = GameClientRect();
    std::wstring what = L"GW2";
    if (IsRectEmpty(&r)) {
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTOPRIMARY), &mi);
        r = mi.rcMonitor;
        what = Tr(L"Screen");
    }
    const std::wstring size = std::to_wstring(r.right - r.left) + L" × " + std::to_wstring(r.bottom - r.top);
    if (r.bottom - r.top > 1200)
        return TrF(L"{1}: {2} → Windows OCR fits (about 0.1 s per picture). Tesseract only helps with very small "
                   L"text.",
                   {what, size});
    return TrF(L"{1}: {2} → small letters possible. RapidOCR reads small text clearly better – or use a larger "
               L"chat font in GW2.",
               {what, size});
}

std::wstring MainWindow::TypingLocale() const {
    if (cfg_.writeIn.empty()) return kbdLocale_;
    // "DE" -> "de-DE", "EN-GB" -> "en-GB", "EN" -> "en-US": a tag Windows' spell checker knows.
    const std::wstring code = ToUpperAscii(cfg_.writeIn);
    const size_t dash = code.find(L'-');
    std::wstring primary = ToLowerAscii(code.substr(0, dash));
    if (dash != std::wstring::npos) return primary + L"-" + code.substr(dash + 1);
    return primary + L"-" + (primary == L"en" ? std::wstring(L"US") : ToUpperAscii(primary));
}

// One look at the chat (or the screen area): what is there now and foreign is translated, then reading stops
// again. Lines already in the log do not come twice (the stream remembers them).
void MainWindow::TranslateOnce() {
    if (cfg_.readerEnabled || once_) return;
    once_ = true;
    onceFound_ = 0;
    onceShots_ = 0;
    streamPrimed_ = true;  // everything visible is wanted, not only the last lines
    RestartReader();
    SetTimer(hwnd_, kTimerOnce, kOnceMs, nullptr);
    SetStatus(Tr(L"Translating what is visible now …"), Tone::Muted, 4000);
}

void MainWindow::EndTranslateOnce() {
    KillTimer(hwnd_, kTimerOnce);
    if (!once_) return;
    once_ = false;
    RestartReader();
    if (onceFound_ == 0)
        SetStatus(cfg_.freeArea || (cfg_.regionSet && mumbleState_.inMap)
                      ? Tr(L"Nothing new to translate")
                      : Tr(L"Nothing read – is the chat on screen (on a map, chat area set)?"),
                  Tone::Muted, 4000);
}

void MainWindow::ToggleReading() {
    EndTranslateOnce();
    cfg_.readerEnabled = !cfg_.readerEnabled;
    cfg_.SaveBool(L"Reader", L"Enabled", cfg_.readerEnabled);
    RestartReader();
    SetStatus(cfg_.readerEnabled ? Tr(L"Reading the chat: on") : Tr(L"Reading the chat: off"), Tone::Muted, 2500);
    InvalidateChrome();
}

// The words of a chat line for the completions (4+ letters, no numbers); never learned, only offered.
void MainWindow::NoteChatWords(const std::wstring& text) {
    std::wstring word;
    auto flush = [&] {
        bool digit = false;
        for (wchar_t c : word) digit = digit || (c >= L'0' && c <= L'9');
        if (word.size() >= 4 && !digit) {
            const std::wstring k = CaseFold(word);
            chatWords_.erase(std::remove_if(chatWords_.begin(), chatWords_.end(),
                                            [&](const std::wstring& x) { return CaseFold(x) == k; }),
                             chatWords_.end());
            chatWords_.push_front(word);
        }
        word.clear();
    };
    for (wchar_t c : text) {
        if (IsWordChar(c)) word += c;
        else flush();
    }
    flush();
    while (chatWords_.size() > 80) chatWords_.pop_back();
    spell_.SetContext({chatWords_.begin(), chatWords_.end()});
}

// The word help's benefit, measured: key presses for this message vs. the letters it has (what you would have
// typed without help). Pasted or programmatically set texts are not counted. Today and in total, in the ini.
void MainWindow::CountTyping() {
    if (input_.Pasted() || input_.KeyPresses() <= 0) return;
    const ChatSplit split = SplitChatCommand(SanitizeChatText(input_.Text()));
    const int chars = static_cast<int>((split.prefix.empty() ? split.body.empty() ? input_.Text() : split.body : split.body).size());
    if (chars <= 0) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    const int day = st.wYear * 10000 + st.wMonth * 100 + st.wDay;
    if (cfg_.typingDay != day) {
        cfg_.typingDay = day;
        cfg_.dayKeys = cfg_.dayChars = 0;
    }
    cfg_.dayKeys += input_.KeyPresses();
    cfg_.dayChars += chars;
    cfg_.totalKeys += input_.KeyPresses();
    cfg_.totalChars += chars;
    ++cfg_.totalMessages;
    cfg_.SaveValue(L"Typing", L"Day", std::to_wstring(cfg_.typingDay));
    cfg_.SaveValue(L"Typing", L"DayKeys", std::to_wstring(cfg_.dayKeys));
    cfg_.SaveValue(L"Typing", L"DayChars", std::to_wstring(cfg_.dayChars));
    cfg_.SaveValue(L"Typing", L"TotalKeys", std::to_wstring(cfg_.totalKeys));
    cfg_.SaveValue(L"Typing", L"TotalChars", std::to_wstring(cfg_.totalChars));
    cfg_.SaveValue(L"Typing", L"TotalMessages", std::to_wstring(cfg_.totalMessages));
}

void MainWindow::Retranslate(uint64_t id, const std::wstring& text) {
    if (!translator_ || Trim(text).empty()) return;
    log_.Update(id, [](ChatEntry& e) {
        e.state = ChatEntry::State::Pending;
        e.note.clear();
    });
    inQueue_.push_back({id, text});  // the back is translated first
    inPauseUntil_ = 0;
    PumpIncoming();
}

Channel MainWindow::SendChannel() const { return tabState_.empty() ? Channel::Unknown : tabState_[tab_].send; }

void MainWindow::SetSendChannel(Channel c) {
    if (partIdx_ > 0) {
        SetStatus(Tr(L"Send the remaining parts first \u2013 or change the text"), Tone::Warn, 4000);
        return;
    }
    tabState_[tab_].send = c;
    if (PreviewIsCurrent()) {
        const ChatSplit split = SplitChatCommand(SanitizeChatText(input_.Text()));
        if (split.prefix.empty()) SetPreviewBody(ComposePrefix(), previewBody_);
    }
    UpdatePreview();
    InvalidateChrome();
}

void MainWindow::OnTabsChanged() {
    cfg_.activeTab = static_cast<int>(tab_);
    cfg_.SaveTabs();
    ApplyTabFilter();
    UpdateHint();
    InvalidateChrome();
    UpdatePreview();
}

void MainWindow::AddTab(const ChatTab& preset) {
    if (cfg_.tabs.size() >= 8) {
        SetStatus(Tr(L"At most 8 tabs"), Tone::Warn, 3000);
        return;
    }
    ChatTab t = preset;
    t.id = nextTabId_++;
    cfg_.tabs.push_back(t);
    TabState st;
    st.send = SoleSendChannel(t.channels);
    tabState_.push_back(st);
    tab_ = cfg_.tabs.size() - 1;
    OnTabsChanged();
}

void MainWindow::RemoveTab(size_t idx) {
    if (cfg_.tabs.size() <= 1 || idx >= cfg_.tabs.size()) return;
    cfg_.tabs.erase(cfg_.tabs.begin() + static_cast<std::ptrdiff_t>(idx));
    tabState_.erase(tabState_.begin() + static_cast<std::ptrdiff_t>(idx));
    if (tab_ > idx || tab_ >= cfg_.tabs.size()) tab_ = tab_ > 0 ? tab_ - 1 : 0;
    OnTabsChanged();
}

void MainWindow::MoveTab(size_t idx, int delta) {
    const long to = static_cast<long>(idx) + delta;
    if (idx >= cfg_.tabs.size() || to < 0 || to >= static_cast<long>(cfg_.tabs.size())) return;
    std::swap(cfg_.tabs[idx], cfg_.tabs[static_cast<size_t>(to)]);
    std::swap(tabState_[idx], tabState_[static_cast<size_t>(to)]);
    if (tab_ == idx) tab_ = static_cast<size_t>(to);
    else if (tab_ == static_cast<size_t>(to)) tab_ = idx;
    OnTabsChanged();
}

void MainWindow::ResetTabs() {
    cfg_.tabs = DefaultTabs();
    tabState_.clear();
    for (ChatTab& t : cfg_.tabs) {
        t.id = nextTabId_++;
        TabState st;
        st.send = SoleSendChannel(t.channels);
        tabState_.push_back(st);
    }
    tab_ = 0;
    OnTabsChanged();
}

// Right click on a tab: which channels it shows, new tab, order, close.
void MainWindow::ShowTabMenu(size_t idx, POINT screen) {
    if (idx >= cfg_.tabs.size()) return;
    enum : UINT { kChannelBase = 100, kPresetBase = 200, kLeft = 300, kRight, kClose, kReset };
    const ChatTab& tab = cfg_.tabs[idx];
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, TrF(L"Tab \u201c{1}\u201d shows:", {TabLabel(tab)}).c_str());
    const auto& channels = TabChannels();
    for (size_t i = 0; i < channels.size(); ++i)
        AppendMenuW(menu, MF_STRING | (TabShows(tab, channels[i]) ? MF_CHECKED : 0), kChannelBase + i,
                    TabChannelLabel(channels[i]).c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    HMENU presets = CreatePopupMenu();
    const std::vector<ChatTab> presetList = TabPresets();
    for (size_t i = 0; i < presetList.size(); ++i)
        AppendMenuW(presets, MF_STRING, kPresetBase + i, presetList[i].name.c_str());
    AppendMenuW(menu, MF_POPUP | (cfg_.tabs.size() >= 8 ? MF_GRAYED : 0), reinterpret_cast<UINT_PTR>(presets),
                Tr(L"New tab").c_str());
    AppendMenuW(menu, MF_STRING | (idx == 0 ? MF_GRAYED : 0), kLeft, Tr(L"Move left").c_str());
    AppendMenuW(menu, MF_STRING | (idx + 1 >= cfg_.tabs.size() ? MF_GRAYED : 0), kRight, Tr(L"Move right").c_str());
    AppendMenuW(menu, MF_STRING | (cfg_.tabs.size() <= 1 ? MF_GRAYED : 0), kClose, Tr(L"Close tab").c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kReset, Tr(L"Reset tabs").c_str());
    const UINT cmd = static_cast<UINT>(TrackPopupMenu(menu, MenuFlags(TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY),
                                                      screen.x, screen.y, 0, hwnd_, nullptr));
    DestroyMenu(menu);

    if (cmd >= kChannelBase && cmd - kChannelBase < channels.size()) {
        ChatTab& t = cfg_.tabs[idx];
        const ChannelMask bit = ChannelBit(channels[cmd - kChannelBase]);
        if ((t.channels & ~bit) == 0) {
            SetStatus(Tr(L"A tab needs at least one channel"), Tone::Warn, 3000);
            return;
        }
        const Channel before = SoleSendChannel(t.channels);
        t.channels ^= bit;
        // The chip follows the tab unless you picked something else yourself.
        if (tabState_[idx].send == before) tabState_[idx].send = SoleSendChannel(t.channels);
        OnTabsChanged();
    } else if (cmd >= kPresetBase && cmd - kPresetBase < presetList.size()) {
        AddTab(presetList[cmd - kPresetBase]);
    } else if (cmd == kLeft || cmd == kRight) {
        MoveTab(idx, cmd == kLeft ? -1 : 1);
    } else if (cmd == kClose) {
        RemoveTab(idx);
    } else if (cmd == kReset) {
        ResetTabs();
    }
}

void MainWindow::ShowReadMenu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, Tr(L"Translate the chat into:").c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    const auto& langs = Languages();
    for (size_t i = 0; i < langs.size(); ++i) {
        UINT flags = MF_STRING | (readLang_ == langs[i].code ? MF_CHECKED : 0);
        if (i > 0 && i % 20 == 0) flags |= MF_MENUBARBREAK;
        AppendMenuW(menu, flags, kCmdLangBase + i, LangMenuLabel(langs[i]).c_str());
    }
    POINT pt{readRect_.left, readRect_.bottom};
    ClientToScreen(hwnd_, &pt);
    const UINT cmd =
        static_cast<UINT>(TrackPopupMenu(menu, MenuFlags(TPM_RETURNCMD | TPM_NONOTIFY), pt.x, pt.y, 0, hwnd_, nullptr));
    DestroyMenu(menu);
    if (cmd >= kCmdLangBase && cmd - kCmdLangBase < langs.size()) SetReadLang(langs[cmd - kCmdLangBase].code);
}

void MainWindow::ShowWriteMenu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, Tr(L"Send as:").c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    for (size_t i = 0; i < writeLangs_.size(); ++i) {
        const LangInfo* l = FindLanguage(writeLangs_[i]);
        const std::wstring label = l ? LangMenuLabel(*l) : writeLangs_[i];
        AppendMenuW(menu, MF_STRING | (i == writeIdx_ ? MF_CHECKED : 0), kCmdFavBase + i, label.c_str());
    }
    AppendMenuW(menu, MF_STRING | (WriteOriginal() ? MF_CHECKED : 0), kCmdOriginal,
                Tr(L"Original (only corrected)\tCtrl+Enter").c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    HMENU more = CreatePopupMenu();
    const auto& langs = Languages();
    for (size_t i = 0; i < langs.size(); ++i) {
        UINT flags = MF_STRING;
        if (i > 0 && i % 20 == 0) flags |= MF_MENUBARBREAK;
        AppendMenuW(more, flags, kCmdMoreBase + i, LangMenuLabel(langs[i]).c_str());
    }
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(more), Tr(L"More languages").c_str());
    POINT pt{writeRect_.left, writeRect_.top};
    ClientToScreen(hwnd_, &pt);
    const UINT cmd = static_cast<UINT>(TrackPopupMenu(menu, MenuFlags(TPM_RETURNCMD | TPM_NONOTIFY | TPM_BOTTOMALIGN),
                                                      pt.x, pt.y, 0, hwnd_, nullptr));
    DestroyMenu(menu);

    if (cmd == kCmdOriginal) {
        SetWriteIndex(writeLangs_.size());
    } else if (cmd >= kCmdFavBase && cmd - kCmdFavBase < writeLangs_.size()) {
        SetWriteIndex(cmd - kCmdFavBase);
    } else if (cmd >= kCmdMoreBase && cmd - kCmdMoreBase < langs.size()) {
        const std::wstring code = langs[cmd - kCmdMoreBase].code;
        auto it = std::find(writeLangs_.begin(), writeLangs_.end(), code);
        if (it == writeLangs_.end()) {
            writeLangs_.push_back(code);
            std::wstring joined;
            for (const std::wstring& c : writeLangs_) joined += (joined.empty() ? L"" : L",") + c;
            cfg_.SaveValue(L"Translate", L"WriteLangs", joined);
            it = writeLangs_.end() - 1;
        }
        SetWriteIndex(static_cast<size_t>(it - writeLangs_.begin()));
    }
    SetFocus(input_.Hwnd());
}

void MainWindow::ShowChannelMenu() {
    if (partIdx_ > 0) {
        SetStatus(Tr(L"Send the remaining parts first \u2013 or change the text"), Tone::Warn, 4000);
        return;
    }
    enum : UINT { kActive = 1, kReply = 2, kChannelBase = 10 };
    static const Channel kChannels[] = {Channel::Say,  Channel::Map,  Channel::Party, Channel::Squad,
                                        Channel::Team, Channel::Guild};
    const Channel cur = SendChannel();
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | (cur == Channel::Unknown ? MF_CHECKED : 0), kActive,
                Tr(L"Active channel (as chosen in GW2)").c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    for (size_t i = 0; i < std::size(kChannels); ++i) {
        const std::wstring label = ChannelLabel(kChannels[i]) + L"\t" + ChannelCommand(kChannels[i]);
        AppendMenuW(menu, MF_STRING | (cur == kChannels[i] ? MF_CHECKED : 0), kChannelBase + i, label.c_str());
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    const bool whisper = cur == Channel::Whisper;
    AppendMenuW(menu, MF_STRING | (whisper && whisperTarget_.empty() ? MF_CHECKED : 0), kReply,
                Tr(L"Whisper: reply to the last one\t/r").c_str());
    for (size_t i = 0; i < whisperers_.size(); ++i)
        AppendMenuW(menu, MF_STRING | (whisper && whisperTarget_ == whisperers_[i] ? MF_CHECKED : 0),
                    kCmdPartnerBase + i, TrF(L"Whisper to {1}", {whisperers_[i]}).c_str());
    POINT pt{channelRect_.left, channelRect_.top};
    ClientToScreen(hwnd_, &pt);
    const UINT cmd = static_cast<UINT>(TrackPopupMenu(menu, MenuFlags(TPM_RETURNCMD | TPM_NONOTIFY | TPM_BOTTOMALIGN),
                                                      pt.x, pt.y, 0, hwnd_, nullptr));
    DestroyMenu(menu);
    if (cmd == kActive) {
        SetSendChannel(Channel::Unknown);
    } else if (cmd >= kChannelBase && cmd - kChannelBase < std::size(kChannels)) {
        SetSendChannel(kChannels[cmd - kChannelBase]);
    } else if (cmd == kReply) {
        whisperTarget_.clear();
        SetSendChannel(Channel::Whisper);
    } else if (cmd >= kCmdPartnerBase && cmd - kCmdPartnerBase < whisperers_.size()) {
        whisperTarget_ = whisperers_[cmd - kCmdPartnerBase];
        SetSendChannel(Channel::Whisper);
    }
    SetFocus(input_.Hwnd());
}

void MainWindow::ShowMainMenu() {
    const ModalScope modal;
    enum : UINT {
        kSetup = 1, kSettings, kRegion, kReader, kCover, kDock, kQuit, kGw2Chat, kFreeArea, kOnce, kOnlyTr,
        kUiLangBase = 100,  // + index into UiLanguages()
    };
    auto check = [](bool on) { return static_cast<UINT>(on ? MF_CHECKED : MF_UNCHECKED); };
    auto add = [](HMENU m, UINT flags, UINT id, const std::wstring& text) { AppendMenuW(m, flags, id, text.c_str()); };
    // Short and flat: the everyday actions. Everything else is in Settings.
    HMENU menu = CreatePopupMenu();
    add(menu, MF_STRING | check(cfg_.readerEnabled), kReader, Tr(L"Automatic translation (permanent)"));
    add(menu, MF_STRING | (cfg_.readerEnabled || once_ ? MF_GRAYED : 0), kOnce, Tr(L"Translate once now"));
    add(menu, MF_STRING | check(cfg_.onlyTranslations), kOnlyTr, Tr(L"Show only translations"));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    add(menu, MF_STRING | (cfg_.freeArea ? MF_UNCHECKED : MF_CHECKED), kGw2Chat, Tr(L"Read the GW2 chat"));
    add(menu, MF_STRING | check(cfg_.freeArea), kFreeArea, Tr(L"Translate a screen area (any text) …"));
    add(menu, MF_STRING, kRegion, Tr(L"Set the chat area …"));
    add(menu, MF_STRING | (cfg_.regionSet ? 0 : MF_GRAYED), kCover, Tr(L"Lay over the GW2 chat (replaces it)"));
    add(menu, MF_STRING | check(cfg_.dock), kDock, Tr(L"Dock to GW2 (moves with it)"));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    add(menu, MF_STRING, kSettings, Tr(L"Settings …"));
    add(menu, MF_STRING, kSetup, Tr(L"Setup (install, mark the chat) …"));
    HMENU ui = CreatePopupMenu();
    for (size_t i = 0; i < UiLanguages().size(); ++i)
        add(ui, MF_STRING | check(UiLanguages()[i].lang == cfg_.uiLang), kUiLangBase + static_cast<UINT>(i),
            UiLanguages()[i].native);
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(ui), L"Language / Sprache / \u0627\u0644\u0644\u063a\u0629");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    add(menu, MF_STRING, kQuit, Tr(L"Quit"));

    POINT pt{menuRect_.left, menuRect_.bottom};
    ClientToScreen(hwnd_, &pt);
    const UINT cmd =
        static_cast<UINT>(TrackPopupMenu(menu, MenuFlags(TPM_RETURNCMD | TPM_NONOTIFY), pt.x, pt.y, 0, hwnd_, nullptr));
    DestroyMenu(menu);

    switch (cmd) {
        case kSetup: RunSetup(); break;
        case kSettings: OpenSettings(SettingsPage::General); break;
        case kRegion:
            SetFreeArea(false);
            PickRegion();
            break;
        case kGw2Chat: SetFreeArea(false); break;
        case kFreeArea: PickFreeArea(); break;
        case kReader: ToggleReading(); break;
        case kOnce: TranslateOnce(); break;
        case kOnlyTr:
            cfg_.onlyTranslations = !cfg_.onlyTranslations;
            cfg_.SaveBool(L"Reader", L"OnlyTranslations", cfg_.onlyTranslations);
            SetStatus(cfg_.onlyTranslations ? Tr(L"From now on only translated lines appear")
                                            : Tr(L"From now on every chat line appears"),
                      Tone::Muted, 3500);
            break;
        case kDock: SetDock(!cfg_.dock); break;
        case kCover: CoverChat(); break;
        case kQuit: PostMessageW(hwnd_, WM_CLOSE, 0, 0); break;
        default:
            if (cmd >= kUiLangBase && cmd < kUiLangBase + UiLanguages().size())
                SetUiLanguage(UiLanguages()[cmd - kUiLangBase].lang);
            break;
    }
}

void MainWindow::SetUiLanguage(UiLang lang) {
    cfg_.uiLang = lang;
    cfg_.uiLangCode = UiLangCode(lang);
    cfg_.SaveValue(L"General", L"UiLanguage", cfg_.uiLangCode);
    SetUiLang(lang);
    // Default tab names follow the language as long as you did not rename them.
    UpdateHint();
    UpdatePreview();
    InvalidateRect(hwnd_, nullptr, FALSE);
    InvalidateRect(log_.Hwnd(), nullptr, FALSE);
    log_.ThemeChanged();
    UpdateTrayTip();
    SetStatus(Tr(L"Language changed"), Tone::Ok, 3000);
}

void MainWindow::SetSaveCaptures(bool on) {
    cfg_.saveCaptures = on;
    cfg_.SaveBool(L"Reader", L"SaveCaptures", on);
    reader_.SetSaveCaptures(on);
    KillTimer(hwnd_, kTimerCaptures);
    if (on) {
        SetTimer(hwnd_, kTimerCaptures, kCaptureMinutes * 60000, nullptr);
        reader_.Rescan();
        SetStatus(TrF(L"Pictures go to {1} (off again after 15 minutes)", {cfg_.CaptureDir()}), Tone::Ok, 8000);
    }
}

void MainWindow::RestartReader() {
    reader_.Stop();
    readingActive_ = false;
    readerError_.clear();
    readerReported_ = false;
    if (ReadingWanted()) StartReader();
    UpdateHint();
    InvalidateChrome();
}


bool MainWindow::Understood(const std::wstring& lang) const {
    if (lang.empty()) return false;
    const std::wstring p = PrimaryLang(lang);
    for (const std::wstring& u : cfg_.understoodLangs)
        if (PrimaryLang(u) == p) return true;
    return false;
}

// What MyMemory gets is counted per day (its free contingent: 5,000
// characters, 50,000 with an e-mail address), shown small in the footer.
void MainWindow::CountMyMemory(size_t chars) {
    if (engine_ != Engine::Basic || chars == 0) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t day[16];
    swprintf(day, 16, L"%04u%02u%02u", st.wYear, st.wMonth, st.wDay);
    if (cfg_.myMemoryDay != day) {
        cfg_.myMemoryDay = day;
        cfg_.myMemoryUsed = 0;
        cfg_.SaveValue(L"Basic", L"UsedDay", day);
    }
    cfg_.myMemoryUsed += static_cast<int>(chars);
    cfg_.SaveValue(L"Basic", L"UsedChars", std::to_wstring(cfg_.myMemoryUsed));
    InvalidateChrome();
}

// "MyMemory 3.2k / 5k" — only while MyMemory is the translator.
std::wstring MainWindow::MyMemoryQuotaText() const {
    if (engine_ != Engine::Basic) return {};
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t day[16];
    swprintf(day, 16, L"%04u%02u%02u", st.wYear, st.wMonth, st.wDay);
    const int used = cfg_.myMemoryDay == day ? cfg_.myMemoryUsed : 0;
    const int limit = Trim(cfg_.basicEmail).empty() ? 5000 : 50000;
    auto k = [](int v) {
        wchar_t b[16];
        if (v < 1000) swprintf(b, 16, L"%d", v);
        else swprintf(b, 16, L"%.1fk", v / 1000.0);
        return std::wstring(b);
    };
    return TrF(L"MyMemory {1} / {2} today", {k(std::min(used, limit)), k(limit)});
}

// Window capture (WGC) only where Windows lets us switch its yellow frame off
// (Windows 11), unless the player chose otherwise.
bool MainWindow::WindowCaptureAllowed() const {
    return cfg_.captureMode == 1 || (cfg_.captureMode == 0 && ScreenCapture::BorderlessWindowCapture());
}

void MainWindow::ShowMyMemoryNotice() {
    if (cfg_.myMemoryNoticeShown || engine_ != Engine::Basic) return;
    cfg_.myMemoryNoticeShown = true;
    cfg_.SaveBool(L"Basic", L"NoticeShown", true);
    const ModalScope modal;
    const int answer = MessageBoxW(
        hwnd_,
        Tr(L"Translating with MyMemory (free).\n\n"
           L"What goes there: only the lines being translated. MyMemory may keep them in its public translation "
           L"memory.\n"
           L"What stays here: pictures of the screen, your learned words, your settings.\n\n"
           L"Keep MyMemory? “No” opens the translator settings (Google, Microsoft, DeepL, an AI model – or a "
           L"local one, then nothing leaves this PC).")
            .c_str(),
        Tr(L"Privacy notice").c_str(),
        MB_YESNO | MB_ICONINFORMATION | (UiRtl() ? MB_RTLREADING | MB_RIGHT : 0));
    if (answer == IDNO) OpenSettings(SettingsPage::Translator);
}

std::wstring MainWindow::CorrectionsPath() const { return cfg_.dataDir + L"\\corrections.txt"; }

void MainWindow::LoadCorrections() {
    std::string data;
    if (ReadFileBytes(CorrectionsPath(), data)) corrections_.Parse(data);
}

void MainWindow::SaveCorrections() { WriteFileAtomic(CorrectionsPath(), corrections_.Serialize()); }

void MainWindow::LoadMyWords() {
    std::string data;
    if (ReadFileBytes(cfg_.dataDir + L"\\my-words.txt", data)) myWords_.Parse(FromUtf8(data));
    data.clear();
    if (ReadFileBytes(cfg_.dataDir + L"\\ocr-fixes.txt", data)) ocrFixes_.Parse(FromUtf8(data));
}

void MainWindow::SaveOcrFixes() {
    WriteFileAtomic(cfg_.dataDir + L"\\ocr-fixes.txt",
                    ToUtf8(L"# GW2 Chat Translator: learned recognition errors, \"as read = correct\" per line\r\n" +
                           ocrFixes_.Serialize()));
}

void MainWindow::SaveMyWords() {
    WriteFileAtomic(cfg_.dataDir + L"\\my-words.txt",
                    ToUtf8(L"# GW2 Chat Translator: your words, \"word = meaning\" per line (replaced before translating)\r\n" +
                           myWords_.Serialize()));
}

void MainWindow::ExplainWord(const std::wstring& word) {
    std::wstring meaning = myWords_.MeaningOf(word);
    if (!AskWordMeaning(hwnd_, inst_, word, &meaning)) return;
    myWords_.Set(word, meaning);
    spell_.AddUserWord(word);  // never underlined or autocorrected again
    SaveMyWords();
    ++inputGen_;  // the preview is translated again with the meaning
    StartTranslation();
    SetStatus(meaning.empty() ? TrF(L"“{1}” is now a correct word", {word})
                              : TrF(L"Got it: “{1}” means “{2}” – used before translating", {word, meaning}),
              Tone::Ok, 5000);
}

std::wstring MainWindow::CorrectionsInfo() const {
    return TrF(L"{1} lines, {2} phrases", {std::to_wstring(corrections_.Lines()), std::to_wstring(corrections_.Phrases())});
}

void MainWindow::CorrectEntry(const ChatEntry& e) {
    if (e.original.empty()) return;
    std::wstring text = e.main;
    if (!AskCorrection(hwnd_, inst_, e.original, &text) || text == e.main) return;
    // Incoming lines are translated into your reading language, your messages into the chat language.
    const bool outgoing = e.kind == ChatEntry::Kind::Outgoing;
    const std::wstring lang = outgoing ? SendLang() : readLang_;
    corrections_.Add(e.original, lang, e.main, text);
    SaveCorrections();
    if (!outgoing) cache_.Put(e.original, lang, text);
    log_.Update(e.id, [&](ChatEntry& x) { x.main = text; });
    SetStatus(Tr(L"Remembered – this text gets your translation from now on"), Tone::Ok, 4000);
}

void MainWindow::OnWordForgotten(const std::wstring& word) {
    SetStatus(TrF(L"Forgot “{1}” – it will no longer be suggested", {word}), Tone::Muted, 4000);
}

void MainWindow::OnKeyboardLanguage(const std::wstring& locale) {
    if (locale.empty() || locale == kbdLocale_) return;
    kbdLocale_ = locale;
    const std::wstring primary = PrimaryLang(locale);
    kbdRtl_ = IsRtlLanguage(primary);
    if (SanitizeChatText(input_.Text()).empty()) input_.SetRtl(kbdRtl_);
    if (!cfg_.writeIn.empty()) return;  // a fixed writing language: the keyboard layout does not change it
    if (cfg_.spellEnabled) {
        const bool ok = spell_.SwitchLanguage(SpellTagCandidates(primary, locale));
        input_.RecheckSpelling();
        if (ok) SetStatus(TrF(L"Spelling: {1}", {spell_.Tag()}), Tone::Muted, 2500);
        else
            SetStatus(TrF(L"No spell checking installed for {1}", {LanguageLabel(primary)}), Tone::Muted, 4000);
    }
    spell_.UseLearnedLanguage(primary, cfg_.LearnedDir());
    RefreshGlossary();
}

// ===========================================================================
// Glossary (official names from the GW2 API, cached on disk)
// ===========================================================================
static std::wstring NameCachePath(const Config& cfg, const std::string& lang) {
    return cfg.CacheDir() + L"\\gw2names_" + FromUtf8(lang) + L".tsv";
}

void MainWindow::RefreshGlossary() {
    glossary_ = Glossary();
    if (!cfg_.glossaryEnabled) return;
    // Source: the language you type in (keyboard), else your reading language.
    std::string src = Gw2ApiLang(PrimaryLang(kbdLocale_));
    if (src.empty()) src = Gw2ApiLang(readLang_);
    if (src.empty()) return;
    const bool haveSrc = EnsureNames(src);
    if (WriteOriginal()) return;
    const std::string tgt = Gw2ApiLang(SendLang());
    if (tgt.empty() || tgt == src) return;
    const bool haveTgt = EnsureNames(tgt);
    if (haveSrc && haveTgt) glossary_ = Glossary::Build(names_.at(src), names_.at(tgt));
}

bool MainWindow::EnsureNames(const std::string& lang) {
    if (names_.count(lang)) return true;
    const std::wstring path = NameCachePath(cfg_, lang);
    const double age = FileAgeDays(path);
    if (age >= 0) {
        std::string data;
        if (ReadFileBytes(path, data)) {
            NameTable t = ParseNameTable(data);
            if (!t.empty()) names_[lang] = std::move(t);
        }
    }
    if (age < 0 || age > cfg_.glossaryRefreshDays || !names_.count(lang)) StartNameFetch(lang);
    return names_.count(lang) > 0;
}

void MainWindow::StartNameFetch(const std::string& lang) {
    if (fetching_.count(lang) || fetchFailed_.count(lang)) return;
    fetching_.insert(lang);
    if (!names_.count(lang) && BackgroundNoticeAllowed()) {
        SetStatus(TrF(L"Loading official GW2 names ({1})\u2026", {FromUtf8(lang)}), Tone::Muted);
        loadingNames_ = true;
    }
    std::thread([hwnd = hwnd_, lang] {
        auto msg = std::make_unique<NamesMsg>();
        msg->lang = lang;
        msg->result = FetchGw2Names(lang);
        if (PostMessageW(hwnd, WM_APP_NAMES, 0, reinterpret_cast<LPARAM>(msg.get()))) msg.release();
    }).detach();
}

void MainWindow::OnNamesFetched(NamesMsg* raw) {
    std::unique_ptr<NamesMsg> m(raw);
    fetching_.erase(m->lang);
    if (!m->result.ok) {
        fetchFailed_.insert(m->lang);
        if (!BackgroundNoticeAllowed()) return;
        if (names_.count(m->lang))
            SetStatus(Tr(L"GW2 API not reachable \u2013 using the saved names"), Tone::Muted, 5000);
        else
            SetStatus(TrF(L"GW2 names not loaded: {1}", {m->result.error}), Tone::Warn, 8000);
        return;
    }
    WriteFileAtomic(NameCachePath(cfg_, m->lang), SerializeNameTable(m->result.names));
    names_[m->lang] = std::move(m->result.names);

    const bool hadGlossary = !glossary_.Empty();
    RefreshGlossary();
    UpdateSpellWords();
    if (!BackgroundNoticeAllowed()) return;
    if (!hadGlossary && !glossary_.Empty())
        SetStatus(TrF(L"GW2 glossary ready: {1} official names", {std::to_wstring(glossary_.Size())}), Tone::Ok, 5000);
    else if (fetching_.empty() && loadingNames_)
        SetStatus(L"", Tone::Muted);
    if (fetching_.empty()) loadingNames_ = false;
}

void MainWindow::UpdateSpellWords() {
    // Names in every loaded language: players mix "Löwenstein" and "Lion's Arch".
    WordSet words;
    for (const auto& kv : names_) CollectNameWords(kv.second, words);
    spell_.SetGameWords(std::move(words));
}

// ===========================================================================
// Outgoing: translate, preview, send
// ===========================================================================
std::wstring MainWindow::ComposePrefix() const {
    const Channel c = SendChannel();
    if (c == Channel::Whisper) return whisperTarget_.empty() ? L"/r " : L"/w " + whisperTarget_ + L", ";
    if (const wchar_t* cmd = ChannelCommand(c)) return std::wstring(cmd) + L" ";
    return L"";
}

bool MainWindow::PreviewIsCurrent() const { return previewOk_ && previewGen_ == inputGen_; }

void MainWindow::ResetCompose() {
    previewOk_ = false;
    sendPending_ = false;
    parts_.clear();
    partIdx_ = 0;
    backText_.clear();
}

void MainWindow::OnInputChanged() {
    input_.OnTextChanged();
    ++inputGen_;
    ResetCompose();
    KillTimer(hwnd_, kTimerDebounce);
    const std::wstring text = SanitizeChatText(input_.Text());
    input_.SetRtl(text.empty() ? kbdRtl_ : IsRtlText(text));
    if (previewVisible_ == text.empty()) {  // show / hide the preview area
        previewVisible_ = !text.empty();
        Layout();
    }
    if (text.empty()) {
        previewBody_.clear();
        previewPrefix_.clear();
        lastHits_.clear();
        if (tone_ != Tone::Ok) SetStatus(L"", Tone::Muted);
        UpdatePreview();
        InvalidateChrome();
        return;
    }
    UpdatePreview();  // grey out the stale preview
    InvalidateChrome();
    SetTimer(hwnd_, kTimerDebounce, WriteOriginal() ? 120 : static_cast<UINT>(cfg_.debounceMs), nullptr);
}

void MainWindow::SetPreviewBody(const std::wstring& prefix, const std::wstring& body) {
    previewPrefix_ = prefix;
    previewBody_ = body;
    previewOk_ = true;
    previewGen_ = inputGen_;
    partIdx_ = 0;
    if (!body.empty()) parts_ = SplitForChat(body, prefix, static_cast<size_t>(cfg_.maxLength));
    else if (!Trim(prefix).empty()) parts_ = {Trim(prefix)};  // "/wave"
    else parts_.clear();
    UpdatePreview();
    InvalidateChrome();
}

void MainWindow::StartTranslation() {
    KillTimer(hwnd_, kTimerDebounce);
    const std::wstring text = SanitizeChatText(input_.Text());
    if (text.empty()) return;
    const ChatSplit split = SplitChatCommand(text);
    const std::wstring prefix = split.prefix.empty() ? ComposePrefix() : split.prefix;
    const std::wstring body = split.prefix.empty() ? text : split.body;

    auto finish = [&](const std::wstring& out) {
        SetPreviewBody(prefix, out);
        if (sendPending_) {
            sendPending_ = false;
            SendNextPart();
        }
    };
    if (WriteOriginal() || body.empty()) {  // "nur Korrektur", or "/wave"
        lastHits_.clear();
        finish(body);
        return;
    }
    if (inflightGen_ == inputGen_) return;  // already on its way

    // Your words ("finds") go to the translator as what they mean ("finde es").
    // Spoken German ("habs") goes to the translator written out ("hab es"); a German chat gets it as written anyway.
    std::wstring toTranslate = myWords_.Expand(body);
    if (PrimaryLang(TypingLocale()) == L"de") toTranslate = ExpandGermanContractions(toTranslate);
    ProtectedText p = ProtectForTranslation(toTranslate, glossary_.Empty() ? nullptr : &glossary_,
                                            &spell_.KeepWords(), &speakers_);
    lastHits_ = p.glossaryHits;
    if (!HasTranslatableText(p.segments)) {  // only names, codes, keep-words
        finish(JoinSegments(p.segments));
        return;
    }

    // You corrected this message once: your translation, no translator needed.
    if (std::wstring fixed; corrections_.Lookup(body, SendLang(), &fixed)) {
        finish(fixed);
        return;
    }
    // MyMemory needs a source language; DeepL and LLMs detect it better themselves.
    std::wstring source;
    if (engine_ == Engine::Basic) {
        source = DetectLanguage(body);
        if (source.empty()) source = PrimaryLang(TypingLocale());
    }
    inflightGen_ = inputGen_;
    CountMyMemory(CodePointCount(JoinSegments(p.segments)));
    UpdatePreview();
    std::thread([hwnd = hwnd_, gen = inputGen_, translator = translator_, segments = std::move(p.segments), source,
                 target = SendLang()] {
        auto msg = std::make_unique<TranslatedMsg>();
        msg->kind = TranslatedMsg::Kind::Forward;
        msg->gen = gen;
        msg->result = translator->Translate(segments, source, target);
        if (PostMessageW(hwnd, WM_APP_TRANSLATED, 0, reinterpret_cast<LPARAM>(msg.get()))) msg.release();
    }).detach();
}

void MainWindow::OnTranslated(TranslatedMsg* raw) {
    std::unique_ptr<TranslatedMsg> m(raw);
    switch (m->kind) {
        case TranslatedMsg::Kind::Forward: {
            if (inflightGen_ == m->gen) inflightGen_ = 0;
            if (m->gen != inputGen_) return;  // text or language changed meanwhile
            if (!m->result.ok) {
                sendPending_ = false;
                previewOk_ = false;
                UpdatePreview();
                SetStatus(m->result.error, Tone::Error);
                return;
            }
            const std::wstring text = SanitizeChatText(input_.Text());
            const ChatSplit split = SplitChatCommand(text);
            if (tone_ == Tone::Error) SetStatus(L"", Tone::Muted);
            SetPreviewBody(split.prefix.empty() ? ComposePrefix() : split.prefix,
                           corrections_.Apply(SanitizeChatText(m->result.text), SendLang()));
            StartBackTranslation();
            if (sendPending_) {
                sendPending_ = false;
                SendNextPart();
            }
            return;
        }
        case TranslatedMsg::Kind::Back:
            if (m->gen != inputGen_ || !PreviewIsCurrent()) return;
            backText_ = m->result.ok ? SanitizeChatText(m->result.text) : L"";
            backGen_ = m->gen;
            UpdatePreview();
            return;
        case TranslatedMsg::Kind::Romanize:
            if (m->gen != inputGen_ || !PreviewIsCurrent()) return;
            if (!m->result.ok) {
                SetStatus(m->result.error, Tone::Warn, 7000);
                return;
            }
            SetPreviewBody(previewPrefix_, SanitizeChatText(m->result.text));
            SetStatus(Tr(L"In Latin letters \u2013 Enter sends it"), Tone::Ok, 4000);
            return;
    }
}

void MainWindow::StartBackTranslation() {
    if ((!cfg_.backTranslate && !WriteNeedsChatLang()) || !PreviewIsCurrent() || WriteOriginal() ||
        previewBody_.empty())
        return;
    // Writing in a language GW2 cannot show: the "back" text is your message in that
    // language (shown big); otherwise it is the check in your reading language.
    const std::wstring backTarget = WriteNeedsChatLang() ? WriteLang() : readLang_;
    if (PrimaryLang(SendLang()) == PrimaryLang(backTarget)) return;  // you can read it anyway
    ProtectedText p = ProtectForTranslation(previewBody_, nullptr, &spell_.KeepWords(), &speakers_);
    if (!HasTranslatableText(p.segments)) return;
    CountMyMemory(CodePointCount(previewBody_));
    std::thread([hwnd = hwnd_, gen = inputGen_, translator = translator_, segments = std::move(p.segments),
                 source = SourceCode(SendLang()), target = backTarget] {
        auto msg = std::make_unique<TranslatedMsg>();
        msg->kind = TranslatedMsg::Kind::Back;
        msg->gen = gen;
        msg->result = translator->Translate(segments, source, target);
        if (PostMessageW(hwnd, WM_APP_TRANSLATED, 0, reinterpret_cast<LPARAM>(msg.get()))) msg.release();
    }).detach();
}

// Ctrl+U: the same message in Latin letters, for scripts the GW2 chat font
// may not show (Arabizi, Pinyin, Romaji ...). Windows can do Cyrillic and
// Devanagari offline; everything else needs the optional LLM.
void MainWindow::Romanize() {
    if (!PreviewIsCurrent() || previewBody_.empty()) {
        SetStatus(Tr(L"Type first, then Ctrl+U for Latin letters"), Tone::Muted, 3000);
        return;
    }
    const std::wstring script = UnsupportedScript(previewBody_);
    if (script.empty()) {
        SetStatus(Tr(L"Already in Latin letters"), Tone::Muted, 2500);
        return;
    }
    const std::wstring t = TransliterateToLatin(previewBody_);
    if (t != previewBody_ && UnsupportedScript(t).empty()) {
        SetPreviewBody(previewPrefix_, t);
        SetStatus(Tr(L"In Latin letters \u2013 Enter sends it"), Tone::Ok, 4000);
        return;
    }
    if (!llm_) {
        SetStatus(TrF(L"Latin letters for {1} need the optional LLM (\u2261 \u2192 Settings \u2192 Translator)", {script}),
                  Tone::Warn, 7000);
        return;
    }
    SetStatus(Tr(L"Writing it in Latin letters (LLM) \u2026"), Tone::Muted);
    std::thread([hwnd = hwnd_, gen = inputGen_, llm = llm_, text = previewBody_] {
        auto msg = std::make_unique<TranslatedMsg>();
        msg->kind = TranslatedMsg::Kind::Romanize;
        msg->gen = gen;
        msg->result = llm->Romanize(text);
        if (PostMessageW(hwnd, WM_APP_TRANSLATED, 0, reinterpret_cast<LPARAM>(msg.get()))) msg.release();
    }).detach();
}

void MainWindow::UpdatePreview() {
    PreviewView::Content c;
    c.placeholder = Tr(L"Write in your language \u2013 here you see what arrives in the GW2 chat.") + L"  " +
                    (cfg_.copyOnly ? Tr(L"Enter copies") : Tr(L"Enter sends")) + L" \u00b7 " +
                    Tr(L"Ctrl+Enter the original \u00b7 Ctrl+L language \u00b7 Ctrl+Tab next tab");
    const std::wstring text = SanitizeChatText(input_.Hwnd() ? input_.Text() : L"");
    if (text.empty()) {
        preview_.Set(std::move(c));
        return;
    }
    if (PreviewIsCurrent() && !parts_.empty()) {
        const size_t idx = std::min(partIdx_, parts_.size() - 1);
        c.text = parts_[idx];
        c.current = true;
        if (backGen_ == inputGen_) c.back = backText_;
        if (WriteNeedsChatLang()) {
            // Big: the message in your writing language; small: what the chat gets.
            const LangInfo* cl = FindLanguage(chatLang_);
            c.back = TrF(L"Into the chat ({1}): {2}", {cl ? std::wstring(cl->native) : chatLang_, parts_[idx]});
            c.backPrefix = L"";
            c.text = backGen_ == inputGen_ && !backText_.empty() ? backText_ : Tr(L"translating …");
        }
        const std::wstring script = UnsupportedScript(previewBody_);
        if (parts_.size() > 1) {
            c.note = TrF(L"Part {1}/{2} \u2013 every Enter sends one part",
                         {std::to_wstring(idx + 1), std::to_wstring(parts_.size())});
        }
        if (!script.empty()) {
            c.note = (c.note.empty() ? L"" : c.note + L"  \u00b7  ") +
                     TrF(L"GW2 probably cannot show {1} \u2013 Ctrl+U: Latin letters", {script});
            c.warn = true;
        } else if (c.note.empty()) {
            c.note = HitsText(lastHits_);
        }
    } else {
        c.text = previewBody_.empty() ? ComposePrefix() + text : previewPrefix_ + previewBody_;
        c.current = false;
        if (inflightGen_ == inputGen_) c.note = Tr(L"translating \u2026");
    }
    preview_.Set(std::move(c));
}

void MainWindow::OnEnter(bool sendOriginal) {
    const std::wstring text = SanitizeChatText(input_.Text());
    if (text.empty()) return;
    if (sendOriginal && partIdx_ == 0) {
        const ChatSplit split = SplitChatCommand(text);
        lastHits_.clear();
        backText_.clear();
        SetPreviewBody(split.prefix.empty() ? ComposePrefix() : split.prefix, split.prefix.empty() ? text : split.body);
        SendNextPart();
        return;
    }
    if (PreviewIsCurrent()) {
        SendNextPart();
        return;
    }
    sendPending_ = true;
    StartTranslation();
    if (sendPending_ && inflightGen_ == inputGen_) SetStatus(Tr(L"translating and sending \u2026"), Tone::Muted);
}

void MainWindow::SendNextPart() {
    if (parts_.empty() || partIdx_ >= parts_.size()) return;
    const std::wstring line = parts_[partIdx_];
    const size_t n = CodePointCount(line);
    if (n > static_cast<size_t>(cfg_.maxLength)) {
        SetStatus(TrF(L"Too long for the GW2 chat ({1}/{2}) \u2013 please shorten it",
                      {std::to_wstring(n), std::to_wstring(cfg_.maxLength)}),
                  Tone::Warn);
        return;
    }
    std::wstring original;
    if (partIdx_ == 0) {
        const std::wstring text = SanitizeChatText(input_.Text());
        const ChatSplit split = SplitChatCommand(text);
        original = split.prefix.empty() ? text : split.body;
    }
    if (!DoSend(line, original)) return;
    if (partIdx_ == 0) {
        CountTyping();
        input_.AddHistory(input_.Text());  // Up brings it back
    }
    if (partIdx_ == 0 && cfg_.learnWords) {
        // Learn what you write in your own language (the original), like a phone keyboard.
        const std::wstring typed = SanitizeChatText(input_.Text());
        const ChatSplit split = SplitChatCommand(typed);
        spell_.Learn(split.prefix.empty() ? typed : split.body);
    }
    ++partIdx_;
    if (partIdx_ >= parts_.size()) {
        input_.Clear();
        OnInputChanged();  // multi-line EDITs send no EN_CHANGE for WM_SETTEXT
        return;
    }
    UpdatePreview();
    InvalidateChrome();
    if (cfg_.copyOnly) {  // you paste it in GW2, then come back for the next part
        SetStatus(TrF(L"Part {1}/{2} copied \u2013 paste it in GW2, then Enter here for the next one",
                      {std::to_wstring(partIdx_), std::to_wstring(parts_.size())}),
                  Tone::Ok);
        return;
    }
    // More to come: back to our window so the next Enter (your key press) sends the next part.
    SetStatus(TrF(L"Part {1}/{2} sent \u2013 Enter sends the next one",
                  {std::to_wstring(partIdx_), std::to_wstring(parts_.size())}),
              Tone::Ok);
    if (Front(hwnd_)) SetFocus(input_.Hwnd());
}

bool MainWindow::Front(HWND h) { return BringToFront(h, !cfg_.copyOnly); }

bool MainWindow::DoSend(const std::wstring& line, const std::wstring& original) {
    SendOutcome out;
    if (cfg_.copyOnly) {
        // Copy only: not a single key goes to the game; you paste it yourself.
        out.ok = CopyTextToClipboard(hwnd_, line);
        if (!out.ok) {
            SetStatus(Tr(L"The clipboard is busy \u2013 press Enter again"), Tone::Error);
            return false;
        }
    } else {
        SetStatus(Tr(L"sending \u2026"), Tone::Muted);
        UpdateWindow(hwnd_);  // sending blocks for a moment; show the status first
        out = SendToGw2Chat(hwnd_, line, cfg_.send, mumbleState_.live ? &mumble_ : nullptr);
        if (!out.ok) {
            SetStatus(out.error, Tone::Error);
            return false;
        }
    }

    const ChatSplit split = SplitChatCommand(line);
    const std::wstring body = split.prefix.empty() ? line : split.body;

    ChatEntry e;
    e.kind = ChatEntry::Kind::Outgoing;
    std::wstring whisperTo;
    e.channel = ChannelFromPrefix(split.prefix, &whisperTo);
    e.speaker = whisperTo;
    e.main = body.empty() ? line : body;
    if (!original.empty() && original != e.main) e.original = original;
    e.splitSend = parts_.size() > 1;
    e.tabId = cfg_.tabs[tab_].id;  // stays visible in the tab it was written in
    const uint64_t id = log_.Add(std::move(e));
    recentSent_.push_back({NormalizeForCompare(body), GetTickCount64(), id, false});
    while (recentSent_.size() > 40) recentSent_.pop_front();

    if (cfg_.copyOnly) {
        SetStatus(Tr(L"Copied \u2013 in GW2: Enter \u00b7 Ctrl+V \u00b7 Enter"), Tone::Ok, 8000);
        if (gw2_ && IsWindow(gw2_)) Front(gw2_);  // window switch only, no keys
        return true;
    }
    if (out.clipboardRestored)
        SetStatus(Tr(L"Sent."), Tone::Ok, 3000);
    else
        SetStatus(Tr(L"Sent \u2013 GW2 did not confirm, so your old clipboard was not put back (to be safe)"), Tone::Warn,
                  8000);
    if (cfg_.returnFocus && Front(hwnd_)) SetFocus(input_.Hwnd());
    return true;
}

// ===========================================================================
// Incoming: read, filter, translate
// ===========================================================================
void MainWindow::OnSnapshot(ReaderSnapshot* raw) {
    std::unique_ptr<ReaderSnapshot> s(raw);
    // Taken while our own window may have covered the chat (moved, region
    // picker, capture exclusion just switched on): never read ourselves.
    if (s->captureTick && s->captureTick < ignoreSnapshotsBefore_) return;
    // One of our dialogs, menus or message boxes may be in the picture.
    if (ModalScope::MayShowOurWindow(s->captureTick ? s->captureTick : GetTickCount64())) return;
    if (!s->error.empty()) {
        if (readerError_ != s->error) {
            readerError_ = s->error;
            SetStatus(TrF(L"Reading the chat: {1}", {s->error}), Tone::Warn, 10000);
            UpdateHint();
        }
        return;
    }
    if (!readerError_.empty()) {
        readerError_.clear();
        UpdateHint();
    }
    lastCaptureMethod_ = s->method;
    if (!readerReported_ && BackgroundNoticeAllowed()) {
        readerReported_ = true;
        SetStatus(TrF(L"Chat found: {1} lines \u00b7 {2} \u00b7 {3} \u00b7 {4} ms",
                      {std::to_wstring(s->lines.size()), s->engine + L" " + s->language, s->method,
                       std::to_wstring(s->milliseconds)}),
                  Tone::Ok, 6000);
        lastOcrEngine_ = s->engine + L" (" + s->language + L")";
    }
    // Only text that looks like chat (no symbol noise), and only lines read the
    // same way twice in a row (double scan): a window dragged over the chat, a
    // scrolling chat or half-drawn text never produce output.
    std::vector<ChatMessage> msgs;
    std::vector<ChatMessage> built =
        cfg_.freeArea ? BuildFreeTextMessages(s->lines) : BuildMessages(s->lines, cfg_.palette);
    // Free area: the whole area once, then only the lowest lines (where new text appears). Typing below made the
    // reading of old text further up flicker, and every slightly different reading came out as a new fragment.
    if (cfg_.freeArea && streamPrimed_ && !once_) built = KeepActiveBottom(std::move(built), s->lines, kFreeActiveLines);
    for (ChatMessage& m : built)
        if (cfg_.freeArea ? LooksLikeFreeText(m.text) : LooksLikeChatText(m.text)) msgs.push_back(std::move(m));
    std::vector<ChatMessage> fresh = stream_.Feed(msgs, true);
    // For the technical page.
    if (!stats_.since) stats_.since = GetTickCount64();
    ++stats_.pictures;
    stats_.lastMs = s->milliseconds;
    stats_.avgMs = stats_.avgMs == 0 ? s->milliseconds : stats_.avgMs * 0.9 + s->milliseconds * 0.1;
    stats_.lastLines = s->lines.size();
    stats_.messages += built.size();
    stats_.dropped += built.size() - msgs.size();
    stats_.confirmed += fresh.size();
    stats_.secondLooks += static_cast<uint64_t>(s->secondLooks);
    stats_.secondFixes += static_cast<uint64_t>(s->secondFixes);
    stats_.glyphRows += static_cast<uint64_t>(s->glyphRows);
    stats_.glyphLetters = s->glyphLetters;
    if (!s->newFixes.empty()) {  // learned: from now on fixed at once, also after a restart
        for (const auto& [wrong, right] : s->newFixes) ocrFixes_.Set(wrong, right);
        SaveOcrFixes();
    }
    if (stream_.HasPending()) SetTimer(hwnd_, kTimerConfirm, kConfirmDelayMs, nullptr);
    // The first picture shows the whole chat history: only the last few lines
    // are worth translating, the rest is old (all lines are remembered, so
    // they do not come again later).
    if (!streamPrimed_ && !fresh.empty()) {
        streamPrimed_ = true;
        if (fresh.size() > kStartLines && !cfg_.freeArea)  // a free area: all of its text is wanted
            fresh.erase(fresh.begin(), fresh.end() - kStartLines);
    }
    onceFound_ += fresh.size();
    for (const ChatMessage& m : fresh) HandleIncoming(m);
    PumpIncoming();
    UpdateHint();
    // "Translate once": done after the first picture when nothing waits for its confirmation, else after the second
    // (the double scan) – however long the text recognition needs.
    if (once_ && (++onceShots_ >= 2 || !stream_.HasPending())) EndTranslateOnce();
}

bool MainWindow::NeedsTranslation(const std::wstring& text, std::wstring* detected) const {
    size_t letters = 0;
    for (wchar_t c : text)
        if (IsWordChar(c) && !(c >= L'0' && c <= L'9')) ++letters;
    if (letters < 3) return false;  // "gg", "ty", emotes
    const ProtectedText p = ProtectForTranslation(text, nullptr, &spell_.KeepWords());
    if (!HasTranslatableText(p.segments)) return false;  // only LFG/WvW/chat codes
    // Only when the language is clear: unsure lines ("ok np", names, slang) are not sent anywhere by themselves.
    *detected = SureLanguage(text, DetectLanguage(text));
    return !detected->empty() && *detected != PrimaryLang(readLang_);
}

MainWindow::Own MainWindow::ClassifyOwn(const ChatMessage& m) {
    const ULONGLONG now = GetTickCount64();
    // Prefer a sent line that has not come back yet ("inc", "inc" sent twice).
    for (int pass = 0; pass < 2; ++pass) {
        for (Sent& s : recentSent_) {
            if (s.echoed != (pass == 1) || now - s.tick >= kEchoWindowMs) continue;
            if (DiceSimilarity(m.text, s.normalized) < 0.8) continue;
            if (!s.echoed) {
                s.echoed = true;
                // "Active channel" sends: the colour in the game chat tells where it went.
                log_.Update(s.entryId, [&](ChatEntry& e) {
                    if (e.channel == Channel::Unknown && m.channel != Channel::System) e.channel = m.channel;
                    if (e.channel == Channel::Whisper && e.speaker.empty() && m.outgoingWhisper) e.speaker = m.speaker;
                });
            }
            return Own::SentHere;
        }
    }
    if (m.outgoingWhisper) return Own::TypedInGame;  // "To Name: ..." is always yours
    const std::wstring& own = mumbleState_.identity.name;
    if (!own.empty() && CaseFold(Trim(m.speaker)) == CaseFold(own)) return Own::TypedInGame;
    return Own::No;
}

void MainWindow::HandleIncoming(const ChatMessage& m) {
    const bool system =
        !m.freeText && (m.channel == Channel::System || (m.channel == Channel::Unknown && m.speaker.empty()));
    if (system && !cfg_.showSystemLines) return;
    if (log_.ShowsTranslation(m.text)) {  // our own window, read back: never translate it again
        OnSelfRead();
        return;
    }
    if (m.freeText) {
        for (FreeParagraph& fp : recentFree_) {
            if (!SameFreeParagraph(fp.text, m.text)) continue;
            // The same paragraph again: only a longer version (it grew) replaces it; a slightly different
            // reading of the same text is ignored, so the translator is not asked again and again.
            if (m.text.size() < fp.text.size() + 3) return;
            fp.text = m.text;
            inQueue_.erase(std::remove_if(inQueue_.begin(), inQueue_.end(),
                                          [&](const PendingLine& pl) { return pl.entryId == fp.id; }),
                           inQueue_.end());
            std::wstring detected;
            const bool foreign = NeedsTranslation(m.text, &detected) && !Understood(detected);
            log_.Update(fp.id, [&](ChatEntry& e) {
                e.main = m.text;
                e.original.clear();
                e.note.clear();
                e.state = ChatEntry::State::Plain;
                e.lang = detected;
            });
            if (foreign) Retranslate(fp.id, m.text);
            return;
        }
    }
    switch (m.freeText ? Own::No : ClassifyOwn(m)) {
        case Own::SentHere:  // already in the log as "Du: ..."
            return;
        case Own::TypedInGame: {  // typed in GW2 itself: show it for context, untranslated
            if (HideUntranslated()) return;
            ChatEntry e;
            e.kind = ChatEntry::Kind::Outgoing;
            e.channel = m.channel == Channel::System ? Channel::Unknown : m.channel;
            if (m.outgoingWhisper) e.speaker = m.speaker;
            e.main = m.text;
            log_.Add(std::move(e));
            return;
        }
        case Own::No:
            break;
    }

    if (!system && !m.speaker.empty()) speakers_.Add(m.speaker);
    if (!system) NoteChatWords(m.speaker);  // completions: the names of the people in the chat

    ChatEntry e;
    e.kind = system ? ChatEntry::Kind::System : ChatEntry::Kind::Incoming;
    e.channel = m.channel;
    e.whisperOut = m.outgoingWhisper;
    e.speaker = m.speaker;
    e.main = m.text;
    e.hasColor = true;
    e.color = m.color;

    bool pending = false;
    std::wstring detected;
    // Translated without a click: another language than yours (and not one you
    // understand), in a channel meant for you (whisper, party, squad, guild by
    // default). Everything else: a click on the line translates it.
    const bool foreign = !system && NeedsTranslation(m.text, &detected) && !Understood(detected);
    const bool automatic = m.freeText || (cfg_.autoTranslate & ChannelBit(m.channel)) != 0;
    // "Show only translations": what is not foreign (your languages, unsure lines, system lines) does not appear.
    if (!system && !foreign) NoteChatWords(m.text);  // your language: words you may answer with (foreign: its translation)
    if (!foreign && HideUntranslated()) return;
    if (foreign) {
        std::wstring cached;
        if (corrections_.Lookup(m.text, readLang_, &cached)) {  // you corrected this text once
            e.state = ChatEntry::State::Translated;
            e.main = cached;
            e.original = m.text;
        } else if (cache_.Get(m.text, readLang_, cached)) {
            if (NormalizeForCompare(cached) != NormalizeForCompare(m.text)) {
                e.state = ChatEntry::State::Translated;
                e.main = cached;
                NoteChatWords(cached);
                e.original = m.text;
            }
        } else if (!automatic) {
            e.note = Tr(L"click to translate");
        } else if (GetTickCount64() + 60000 < inPauseUntil_) {
            e.note = Tr(L"not translated \u2013 translator paused");  // long pause (contingent): don't pile up
        } else {
            e.state = ChatEntry::State::Pending;
            pending = true;
        }
    }
    e.lang = detected;
    const uint64_t id = log_.Add(std::move(e));
    if (m.freeText) {
        recentFree_.push_back({id, m.text});
        while (recentFree_.size() > 40) recentFree_.pop_front();
    }
    if (pending) {
        inQueue_.push_back({id, m.text});
        while (inQueue_.size() > kQueueMax) {  // a busy chat outruns the translator: skip the oldest
            log_.Update(inQueue_.front().entryId, [](ChatEntry& x) {
                x.state = ChatEntry::State::Plain;
                x.note = Tr(L"skipped \u2013 too much at once");
            });
            inQueue_.pop_front();
        }
    }

    if (m.channel == Channel::Whisper && !m.outgoingWhisper && !m.speaker.empty()) {
        whisperers_.erase(std::remove(whisperers_.begin(), whisperers_.end(), m.speaker), whisperers_.end());
        whisperers_.push_front(m.speaker);
        while (whisperers_.size() > 8) whisperers_.pop_back();
    }
    // Unread counters on the other tabs that show this line (not for system lines).
    if (!system) {
        bool changed = false;
        for (size_t i = 0; i < cfg_.tabs.size(); ++i) {
            if (i == tab_ || !TabShows(cfg_.tabs[i], m.channel)) continue;
            if (!cfg_.tabs[i].person.empty() && CaseFold(cfg_.tabs[i].person) != CaseFold(Trim(m.speaker))) continue;
            ++tabState_[i].unread;
            changed = true;
        }
        if (changed) InvalidateChrome();
    }
}

// Newest first, several requests at once: a busy chat must not make the
// latest line wait behind a backlog. The log keeps its order (new lines at the
// bottom); only the translations of older lines may arrive later.
void MainWindow::PumpIncoming() {
    if (!translator_) return;
    if (GetTickCount64() < inPauseUntil_) return;  // after an error; the game timer retries
    // An LLM works on one prompt at a time (a local one shares the graphics
    // card with the game); MyMemory answers one line per request, so lines go
    // out one by one in parallel; DeepL takes a batch per request.
    const bool llm = engine_ == Engine::Llm;
    const int maxJobs = llm ? 1 : kParallelJobs;
    const size_t perJob = engine_ == Engine::Basic ? 1 : kBatchMax;
    while (inFlight_ < maxJobs && !inQueue_.empty()) {
        auto msg = std::make_unique<IncomingMsg>();
        msg->lang = readLang_;
        msg->started = GetTickCount64();
        std::vector<std::vector<Segment>> items;
        size_t chars = 0;
        while (!inQueue_.empty() && items.size() < perJob && chars < kBatchChars) {
            PendingLine pl = std::move(inQueue_.back());  // the newest line first
            inQueue_.pop_back();
            chars += pl.text.size();
            CountMyMemory(CodePointCount(pl.text));
            items.push_back(
                ProtectForTranslation(ExpandGermanContractions(myWords_.Expand(pl.text)), nullptr, &spell_.KeepWords(),
                                      &speakers_).segments);
            msg->ids.push_back(pl.entryId);
            msg->texts.push_back(std::move(pl.text));
        }
        ++inFlight_;
        // With an LLM, chat lines read from the screen get their OCR errors repaired too.
        std::shared_ptr<LlmTranslator> ocrLlm = (llm && cfg_.llmFixOcr) ? llm_ : nullptr;
        std::thread([hwnd = hwnd_, translator = translator_, ocrLlm, items = std::move(items),
                     m = std::move(msg)]() mutable {
            m->results =
                ocrLlm ? ocrLlm->TranslateOcrBatch(items, m->lang) : translator->TranslateBatch(items, L"", m->lang);
            if (PostMessageW(hwnd, WM_APP_INCOMING, 0, reinterpret_cast<LPARAM>(m.get()))) m.release();
        }).detach();
    }
}

void MainWindow::OnIncomingTranslated(IncomingMsg* raw) {
    std::unique_ptr<IncomingMsg> m(raw);
    if (inFlight_ > 0) --inFlight_;
    if (m->started) {
        const double ms = static_cast<double>(GetTickCount64() - m->started);
        stats_.avgTranslateMs = stats_.avgTranslateMs == 0 ? ms : stats_.avgTranslateMs * 0.8 + ms * 0.2;
    }
    for (const TranslateResult& r : m->results) (r.ok ? stats_.translated : stats_.failed)++;
    size_t ok = 0;
    bool quota = false;
    std::wstring firstError;
    for (size_t i = 0; i < m->ids.size(); ++i) {
        TranslateResult r;
        if (i < m->results.size()) r = m->results[i];
        else r.error = Tr(L"no answer");
        quota = quota || r.quotaExceeded;
        const std::wstring& original = m->texts[i];
        if (r.ok) {
            ++ok;
            const std::wstring t = corrections_.Apply(SanitizeChatText(r.text), m->lang);
            cache_.Put(original, m->lang, t);
            const std::wstring det = ToUpperAscii(PrimaryLang(r.detectedSource));
            const bool same = NormalizeForCompare(t) == NormalizeForCompare(original) ||
                              (!det.empty() && det == PrimaryLang(m->lang));
            log_.Update(m->ids[i], [&](ChatEntry& e) {
                if (!det.empty()) e.lang = det;
                if (same) {
                    e.state = ChatEntry::State::Plain;
                    e.main = original;
                    e.original.clear();
                } else {
                    e.state = ChatEntry::State::Translated;
                    e.main = t;
                    e.original = original;
                    NoteChatWords(t);  // in your language: the words you may answer with
                }
            });
        } else {
            if (firstError.empty()) firstError = r.error;
            log_.Update(m->ids[i], [&](ChatEntry& e) {
                e.state = ChatEntry::State::Failed;
                e.note = Tr(L"not translated");
            });
        }
    }
    if (ok == 0 && !firstError.empty()) {
        inPauseUntil_ = GetTickCount64() + (quota ? kQuotaPauseMs : kErrorPauseMs);
        SetStatus((quota ? TrF(L"Chat translation paused (20 min): {1}", {firstError})
                         : TrF(L"Chat translation paused (30 s): {1}", {firstError})),
                  Tone::Warn, quota ? 30000 : 12000);
        if (quota) {  // the waiting lines will not get a translation in time
            for (const PendingLine& pl : inQueue_)
                log_.Update(pl.entryId, [](ChatEntry& x) {
                    x.state = ChatEntry::State::Plain;
                    x.note = Tr(L"not translated \u2013 quota used up");
                });
            inQueue_.clear();
        }
    }
    PumpIncoming();
}

void MainWindow::UpdateHint() {
    if (SoleSendChannel(cfg_.tabs[tab_].channels) == Channel::Whisper) {
        log_.SetEmptyHint(Tr(L"Whispers appear here, translated.\nRight-click a message \u2192 Reply. Ctrl+Tab "
                             L"switches the tab."),
                          false);
        return;
    }
    if (!cfg_.readerEnabled)
        log_.SetEmptyHint(Tr(L"Automatic translation is off (menu \u2261: switch it on, or \u201cTranslate once now\u201d)."), false);
    else if (!readerError_.empty())
        log_.SetEmptyHint(TrF(L"Text recognition not available:\n{1}", {readerError_}), false);
    else if (cfg_.freeArea)
        log_.SetEmptyHint(overlapsChat_ ? Tr(L"This window covers the chat area – please move it beside the chat.")
                                        : Tr(L"Translating everything in the screen area you marked …\n(Back to "
                                             L"the GW2 chat: menu ≡ → Read the GW2 chat)"),
                          false);
    else if (!cfg_.regionSet)
        log_.SetEmptyHint(
            !gw2_ ? Tr(L"Start Guild Wars 2 – the chat is found by itself.\n(Or click here to draw the frame yourself.)")
                  : Tr(L"Open your GW2 chat (Enter) – it is found by itself and this window lies over it.\n"
                       L"Timestamps must be on (GW2 options → Chat). Or click here to draw the frame yourself."),
            true);
    else if (!gw2_)
        log_.SetEmptyHint(Tr(L"Waiting for Guild Wars 2 \u2026"), false);
    else if (!wasInMap_)
        log_.SetEmptyHint(Tr(L"Reading pauses outside a map (character selection, loading screen)."), false);
    else if (overlapsChat_)
        log_.SetEmptyHint(Tr(L"This window covers the chat area \u2013 please move it beside the chat."), false);
    else
        log_.SetEmptyHint(Tr(L"Reading the chat \u2026 new messages appear here in your language.\n(Adjust the "
                             L"area: menu \u2261 \u2192 Reading the chat \u2192 Set the chat area)"),
                          false);
}

// ===========================================================================
// Game tracking & visibility
// ===========================================================================
RECT MainWindow::GameClientRect() const {
    RECT client{};
    if (!gw2_ || !GetClientRect(gw2_, &client)) return {};
    POINT origin{0, 0};
    ClientToScreen(gw2_, &origin);
    OffsetRect(&client, origin.x, origin.y);
    return client;
}

RECT MainWindow::ChatArea() const {
    if (cfg_.freeArea) {
        const RECT screen{GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
                          GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
                          GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN)};
        RECT clipped{};
        IntersectRect(&clipped, &cfg_.freeRect, &screen);
        return clipped;
    }
    const RECT client = GameClientRect();
    if (IsRectEmpty(&client)) return {};
    RECT area{client.left + cfg_.regionLeft, client.bottom - cfg_.regionFromBottom - cfg_.regionHeight,
              client.left + cfg_.regionLeft + cfg_.regionWidth, client.bottom - cfg_.regionFromBottom};
    // Read a bit below the frame, at most to the game window's edge: the
    // newest chat line is never cut off. The input line and the number row
    // that follow are filtered out by the parser.
    area.bottom = std::min(client.bottom, area.bottom + std::max(60L, (area.bottom - area.top) / 3));
    RECT clipped{};
    IntersectRect(&clipped, &area, &client);
    return clipped;
}

void MainWindow::PollGame() {
    mumbleState_ = mumble_.Read();
    if (gw2_ && !IsWindow(gw2_)) gw2_ = nullptr;
    const ULONGLONG now = GetTickCount64();
    const bool hadGame = gw2_ != nullptr;
    if (!gw2_ && now - lastFind_ > 2000) {
        lastFind_ = now;
        gw2_ = FindGw2Window(mumbleState_.live ? mumbleState_.processId : 0);
        if (gw2_) UpdateHint();
    }
    // Started with Windows: appear with the game, disappear when it closes.
    if (waitForGame_ && hadGame != (gw2_ != nullptr)) {
        UpdateTrayTip();
        if (gw2_ && !userHidden_) {
            ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
            autoHidden_ = false;
        } else if (!gw2_) {
            ShowWindow(hwnd_, SW_HIDE);
        }
    }
    if (picking_) return;

    // First start with the game running: lie where the GW2 chat usually is
    // (bottom left), so its own hint "open your GW2 chat" sits right there.
    if (!cfg_.regionSet && !cfg_.dockSet && !placedForSetup_ && gw2_ && !IsIconic(gw2_)) {
        placedForSetup_ = true;
        const RECT c = GameClientRect();
        if (!IsRectEmpty(&c)) {
            const int cw = c.right - c.left, ch = c.bottom - c.top;
            const int w = std::max(theme_.S(420), cw * 28 / 100), h = std::max(theme_.S(300), ch * 30 / 100);
            SetWindowPos(hwnd_, nullptr, c.left + cw / 100, c.bottom - h - ch * 6 / 100, w, h,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }

    // Docked: keep our place relative to the game's bottom-left corner.
    if (cfg_.dock && cfg_.dockSet && gw2_ && !IsIconic(gw2_) && !moving_) {
        RECT want = DockTarget();
        if (collapsed_ && !IsRectEmpty(&want)) want.top = want.bottom - MetricsFor(theme_).head;  // the bar sits low
        RECT cur{};
        GetWindowRect(hwnd_, &cur);
        if (!IsRectEmpty(&want) && !EqualRect(&want, &cur))
            SetWindowPos(hwnd_, nullptr, want.left, want.top, want.right - want.left, want.bottom - want.top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
    }

    HWND fg = GetForegroundWindow();
    DWORD fgPid = 0;
    if (fg) GetWindowThreadProcessId(fg, &fgPid);
    const bool ours = fgPid == GetCurrentProcessId();
    const bool gameFront = gw2_ && fg == gw2_;

    // Optional focus transfer: when in-game chat box is opened, bring translator to front.
    const bool textboxFocus = mumbleState_.TextboxHasFocus();
    if (cfg_.focusOnGameChat && textboxFocus && !lastTextboxFocus_ && gw2_ && (gameFront || ours)) {
        ShowOverlay();
    }
    lastTextboxFocus_ = textboxFocus;

    // Behave like part of the game: visible while GW2 or this window is in front.
    if (cfg_.followGame && gw2_ && !userHidden_ && !cfg_.freeArea) {
        if (gameFront || ours) {
            if (autoHidden_) {
                autoHidden_ = false;
                ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
            }
        } else if (IsWindowVisible(hwnd_)) {
            autoHidden_ = true;
            ShowWindow(hwnd_, SW_HIDE);
        }
    }

    // Read only while the chat is really on screen (not behind another app).
    RECT area{};
    // Only on a map: the character selection and loading screens show other
    // text where the chat is (character name, level, map progress ...).
    const bool inMap = mumbleState_.inMap;
    // Free screen area: any text, also without GW2.
    const bool canRead = ModalScope::Active() ? false  // one of our dialogs / menus is open
                         : cfg_.freeArea      ? ReadingWanted() && !moving_ && !picking_
                                              : ReadingWanted() && cfg_.regionSet && gw2_ && !IsIconic(gw2_) &&
                                               (gameFront || ours) && !moving_ && inMap;
    if (inMap != wasInMap_) {
        wasInMap_ = inMap;
        UpdateHint();
    }
    if (canRead) area = ChatArea();
    // No chat area yet: look for the GW2 chat by itself now and then.
    const bool searching = cfg_.readerEnabled && !cfg_.freeArea && !cfg_.regionSet && gw2_ && !IsIconic(gw2_) && (gameFront || ours) &&
                           inMap && !moving_ && !picking_;
    const RECT watch = !IsRectEmpty(&area) ? area : searching ? ChatSearchArea() : RECT{};
    bool overlap = false;
    if (!IsRectEmpty(&watch) && IsWindowVisible(hwnd_)) {
        RECT me{}, both{};
        GetWindowRect(hwnd_, &me);
        overlap = IntersectRect(&both, &me, &watch) != FALSE;
    }
    if (searching && !detecting_ && now - lastDetect_ > kDetectEveryMs && (excludedFromCapture_ || !overlap))
        StartChatDetection();
    // Covering the chat: hide this window from captures (Windows 10 2004+).
    // Otherwise lift it again, so screenshots and recordings show the window.
    if (overlap != excludedFromCapture_ && !(overlap && affinityUnsupported_)) {
        if (SetWindowDisplayAffinity(hwnd_, overlap ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE)) {
            excludedFromCapture_ = overlap;
            if (overlap) ignoreSnapshotsBefore_ = GetTickCount64() + 150;  // until the compositor applied it
        } else if (overlap) {
            affinityUnsupported_ = true;
        }
    }
    // WGC captures the GW2 DirectX backbuffer directly beneath overlays.
    const bool blocked = overlap && !excludedFromCapture_ && lastCaptureMethod_ != L"WGC";
    if (blocked) area = {};  // would read our own window
    reader_.SetTarget(cfg_.freeArea ? nullptr : gw2_);  // free area: the screen as you see it
    reader_.SetArea(area);
    const bool active = !IsRectEmpty(&area) && reader_.Running();
    if (active != readingActive_ || blocked != overlapsChat_) {
        readingActive_ = active;
        overlapsChat_ = blocked;
        UpdateHint();
        InvalidateChrome();
    }
    PumpIncoming();
}

void MainWindow::PickRegion() {
    if (!gw2_) gw2_ = FindGw2Window(mumbleState_.live ? mumbleState_.processId : 0);
    picking_ = true;
    reader_.SetArea({});  // the dimmed picker must not be read as chat
    const bool wasVisible = IsWindowVisible(hwnd_) != FALSE;
    ShowWindow(hwnd_, SW_HIDE);
    if (gw2_) Front(gw2_);

    // A still picture of the game (or the monitor) to snap the frame to the
    // text lines and to preview what will be read.
    RECT stillRect = GameClientRect();
    if (IsRectEmpty(&stillRect)) {
        POINT cursor;
        GetCursorPos(&cursor);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY), &mi);
        stillRect = mi.rcMonitor;
    }
    auto still = std::make_shared<Image>();
    {
        ScreenCapture cap;
        cap.SetUseWindowCapture(WindowCaptureAllowed());
        cap.SetTarget(gw2_);
        for (int i = 0; i < 12 && still->Empty(); ++i) {
            if (!cap.Grab(stillRect, *still)) Sleep(40);  // the first frame may take a moment
        }
    }
    PickAnalyzer analyze;
    PickPreview preview;
    if (!still->Empty()) {
        analyze = [still, stillRect](const RECT& rough) {
            const SnapResult snap =
                SnapChatArea(*still, {rough.left - stillRect.left, rough.top - stillRect.top, rough.right - rough.left,
                                      rough.bottom - rough.top});
            PickCheck c;
            c.snapped = {stillRect.left + snap.area.x, stillRect.top + snap.area.y,
                         stillRect.left + snap.area.x + snap.area.w, stillRect.top + snap.area.y + snap.area.h};
            const AreaQuality q = RateArea(snap);
            c.quality = q == AreaQuality::Good ? 2 : q == AreaQuality::Small ? 1 : 0;
            const std::wstring lines = std::to_wstring(snap.grid.rows.size()),
                               px = std::to_wstring(snap.grid.textHeight);
            if (q == AreaQuality::Good)
                c.summary = TrF(L"[OK] {1} lines, text {2} px – well readable", {lines, px});
            else if (q == AreaQuality::Small)
                c.summary = TrF(L"[!] {1} lines, text only {2} px – enlarged {3}x. A larger chat font in GW2 reads "
                                L"better (our window covers the chat anyway).",
                                {lines, px, std::to_wstring(snap.scale)});
            else
                c.summary = Tr(L"[--] No chat lines found in this frame.");
            return c;
        };
        ReaderOptions o;
        o.ocrChoice = static_cast<int>(cfg_.ocr);
        o.tesseractPath = cfg_.tesseractPath;
        o.tesseractLangs = cfg_.tesseractLangs;
        o.readChinese = cfg_.readChinese;
        o.ocrLanguage = cfg_.ocrLanguage;
        preview = [still, stillRect, o, palette = cfg_.palette](const RECT& r) {
            ChatOcr ocr;
            std::wstring err;
            std::vector<OcrLine> lines;
            const Image crop = Crop(*still, {r.left - stillRect.left, r.top - stillRect.top, r.right - r.left, r.bottom - r.top});
            if (!ocr.Init(o, &err) || !ocr.Read(crop, o.scale, lines, nullptr, &err)) return err;
            std::wstring text;
            int shown = 0;
            for (const ChatMessage& m : BuildMessages(lines, palette)) {
                std::wstring line = (m.speaker.empty() ? L"" : m.speaker + L": ") + m.text;
                if (line.size() > 70) line = line.substr(0, 68) + L"…";
                text += (text.empty() ? L"" : L"\n") + line;
                if (++shown == 3) break;
            }
            return text;
        };
    }
    RECT r{};
    const bool ok = PickScreenRegion(inst_, theme_,
                                     Tr(L"Draw a frame around the text lines of the GW2 chat\n(without the input "
                                        L"line and the tabs).  Esc cancels."),
                                     &r, analyze, preview);
    picking_ = false;
    ignoreSnapshotsBefore_ = GetTickCount64() + 150;
    if (wasVisible) ShowOverlay();
    if (!ok) {
        SetStatus(Tr(L"Chat area not changed"), Tone::Muted, 3000);
        return;
    }
    UseChatArea(r);
    SetStatus(gw2_ ? Tr(L"Chat area saved – reading …")
                   : Tr(L"Chat area saved (GW2 not found – relative to the screen)"),
              gw2_ ? Tone::Ok : Tone::Warn, 6000);
}

// Free screen area: frame any text on the screen (a website, a document,
// another game). No chat rules, no snapping, read also without GW2.
// One picture of the chat (or the free area) read by every text recognition that is available, with the time
// each took – so you can see yourself which one reads your chat best. The text stays in the settings window.
void MainWindow::CompareRecognition(HWND notify, UINT message) {
    const RECT area = ChatArea();
    auto still = std::make_shared<Image>();
    if (!IsRectEmpty(&area)) {
        ScreenCapture cap;
        cap.SetUseWindowCapture(WindowCaptureAllowed());
        cap.SetTarget(cfg_.freeArea ? nullptr : gw2_);
        for (int i = 0; i < 12 && still->Empty(); ++i)
            if (!cap.Grab(area, *still)) Sleep(40);
    }
    if (still->Empty()) {
        auto* text = new std::wstring(Tr(L"No picture: open the GW2 chat (or set a screen area) and try again."));
        if (!PostMessageW(notify, message, 0, reinterpret_cast<LPARAM>(text))) delete text;
        return;
    }
    const ReaderOptions base = MakeReaderOptions();
    std::thread([notify, message, still, base, palette = cfg_.palette, freeText = cfg_.freeArea] {
        std::wstring out = TrF(L"Picture: {1} × {2} px", {std::to_wstring(still->width), std::to_wstring(still->height)}) +
                           L"\r\n\r\n";
        const struct {
            int choice;
            const wchar_t* name;
        } engines[] = {{2, L"Windows OCR"}, {3, L"RapidOCR"}, {1, L"Tesseract"}};
        for (const auto& e : engines) {
            ReaderOptions o = base;
            o.ocrChoice = e.choice;
            o.secondLook = false;  // the recognition itself, without corrections
            ChatOcr ocr;
            std::wstring err;
            std::vector<OcrLine> lines;
            const ULONGLONG t0 = GetTickCount64();
            // Not installed: the recognition falls back to another one – that is "not available" here.
            const bool ok = ocr.Init(o, &err) && ocr.Read(*still, o.scale, lines, nullptr, &err) && ocr.EngineName() == e.name;
            const ULONGLONG ms = GetTickCount64() - t0;
            out += L"== " + std::wstring(e.name);
            if (!ok) {
                out += L": " + Tr(L"not available") + L"\r\n\r\n";
                continue;
            }
            out += L"  (" + std::to_wstring(ms) + L" ms) ==\r\n";
            const std::vector<ChatMessage> msgs = freeText ? BuildFreeTextMessages(lines) : BuildMessages(lines, palette);
            for (const ChatMessage& m : msgs) out += L"  " + (m.speaker.empty() ? L"" : m.speaker + L": ") + m.text + L"\r\n";
            out += L"\r\n";
        }
        out += Tr(L"Which one reads your chat best? Choose it under “Reading the chat” → Text recognition.");
        auto* text = new std::wstring(std::move(out));
        if (!PostMessageW(notify, message, 0, reinterpret_cast<LPARAM>(text))) delete text;
    }).detach();
}

void MainWindow::PickFreeArea() {
    picking_ = true;
    reader_.SetArea({});
    const bool wasVisible = IsWindowVisible(hwnd_) != FALSE;
    ShowWindow(hwnd_, SW_HIDE);

    POINT cursor;
    GetCursorPos(&cursor);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY), &mi);
    const RECT stillRect = mi.rcMonitor;
    Sleep(120);  // our window is gone from the screen
    auto still = std::make_shared<Image>();
    {
        ScreenCapture cap;
        cap.SetUseWindowCapture(false);
        for (int i = 0; i < 12 && still->Empty(); ++i)
            if (!cap.Grab(stillRect, *still)) Sleep(40);
    }
    PickAnalyzer analyze;
    PickPreview preview;
    if (!still->Empty()) {
        analyze = [](const RECT& rough) {
            PickCheck c;
            c.snapped = rough;
            c.quality = 2;
            c.summary = Tr(L"Everything in this frame is translated (no chat rules).");
            return c;
        };
        ReaderOptions o;
        o.ocrChoice = static_cast<int>(cfg_.ocr);
        o.tesseractPath = cfg_.tesseractPath;
        o.tesseractLangs = cfg_.tesseractLangs;
        o.readChinese = cfg_.readChinese;
        o.ocrLanguage = cfg_.ocrLanguage;
        o.freeText = true;
        preview = [still, stillRect, o](const RECT& r) {
            ChatOcr ocr;
            std::wstring err;
            std::vector<OcrLine> lines;
            const Image crop =
                Crop(*still, {r.left - stillRect.left, r.top - stillRect.top, r.right - r.left, r.bottom - r.top});
            if (!ocr.Init(o, &err) || !ocr.Read(crop, o.scale, lines, nullptr, &err)) return err;
            std::wstring text;
            int shown = 0;
            for (const ChatMessage& m : BuildFreeTextMessages(lines)) {
                std::wstring line = m.text;
                if (line.size() > 70) line = line.substr(0, 68) + L"…";
                text += (text.empty() ? L"" : L"\n") + line;
                if (++shown == 3) break;
            }
            return text;
        };
    }
    RECT r{};
    const bool ok = PickScreenRegion(inst_, theme_,
                                     Tr(L"Draw a frame around the text you want translated (any window).  Esc "
                                        L"cancels."),
                                     &r, analyze, preview);
    picking_ = false;
    ignoreSnapshotsBefore_ = GetTickCount64() + 150;
    if (wasVisible) ShowOverlay();
    if (!ok) {
        SetStatus(Tr(L"Screen area not changed"), Tone::Muted, 3000);
        return;
    }
    cfg_.freeRect = r;
    SetFreeArea(true);
    SetStatus(Tr(L"Screen area saved – translating everything in it …"), Tone::Ok, 6000);
}

void MainWindow::SetFreeArea(bool on) {
    if (on && !cfg_.FreeSet()) on = false;
    if (on == cfg_.freeArea && !on) return;
    cfg_.freeArea = on;
    cfg_.SaveFreeArea();
    streamPrimed_ = false;
    readerReported_ = false;
    recentFree_.clear();
    if (!cfg_.readerEnabled) {
        cfg_.readerEnabled = true;
        cfg_.SaveValue(L"Reader", L"Enabled", L"1");
    }
    RestartReader();  // the text recognition works differently for free text
    UpdateHint();
}

// A chat area (screen pixels), drawn or found: stored relative to the
// bottom-left corner of the game (the chat sits there), so it survives moving
// the window or another resolution; reading starts.
void MainWindow::UseChatArea(const RECT& r) {
    RECT ref = GameClientRect();
    if (IsRectEmpty(&ref)) {
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST), &mi);
        ref = mi.rcMonitor;
    }
    cfg_.regionLeft = r.left - ref.left;
    cfg_.regionFromBottom = ref.bottom - r.bottom;
    cfg_.regionWidth = r.right - r.left;
    cfg_.regionHeight = r.bottom - r.top;
    cfg_.regionSet = true;
    cfg_.SaveRegion();
    // ChatStream keeps its memory: lines already shown must not come again.
    readerReported_ = false;
    reader_.Rescan();
    if (!cfg_.readerEnabled) {
        cfg_.readerEnabled = true;
        cfg_.SaveValue(L"Reader", L"Enabled", L"1");
        StartReader();
    }
    UpdateHint();
}

// ===========================================================================
// Finding the GW2 chat by itself: while no chat area is set, the game's
// bottom-left corner is read now and then; the chat shows as several lines
// that start with a timestamp, aligned on the left. Found: the area is set
// and this window lies over the chat. Drawing the frame stays possible.
// ===========================================================================
RECT MainWindow::ChatSearchArea() const {
    const RECT c = GameClientRect();
    if (IsRectEmpty(&c)) return {};
    const LONG w = c.right - c.left, h = c.bottom - c.top;
    return {c.left, c.top + h * 35 / 100, c.left + w * 55 / 100, c.bottom};
}

void MainWindow::StartChatDetection() {
    const RECT search = ChatSearchArea();
    if (IsRectEmpty(&search) || detecting_) return;
    detecting_ = true;
    lastDetect_ = GetTickCount64();
    ReaderOptions o;
    o.ocrChoice = static_cast<int>(cfg_.ocr);
    o.tesseractPath = cfg_.tesseractPath;
    o.tesseractLangs = cfg_.tesseractLangs;
    o.readChinese = cfg_.readChinese;
    o.ocrLanguage = cfg_.ocrLanguage;
    std::thread([hwnd = hwnd_, gw2 = gw2_, search, o, windowCapture = WindowCaptureAllowed()] {
        auto found = std::make_unique<RECT>();
        *found = {};
        Image img;
        {
            ScreenCapture cap;
            cap.SetUseWindowCapture(windowCapture);
            cap.SetTarget(gw2);
            for (int i = 0; i < 12 && img.Empty(); ++i)
                if (!cap.Grab(search, img)) Sleep(40);
        }
        ChatOcr ocr;
        std::wstring err;
        std::vector<OcrLine> lines;
        ChatBlock b;
        if (!img.Empty() && ocr.Init(o, &err) && ocr.Read(img, 0, lines, nullptr, &err) && LocateChatLines(lines, &b)) {
            // Snap to the text lines (whole lines, no tab bar, no input line).
            const int pad = 4;
            const SnapResult snap = SnapChatArea(img, {std::max(0, b.left - pad), std::max(0, b.top - pad),
                                                       b.right - b.left + 2 * pad, b.bottom - b.top + 2 * pad});
            *found = {search.left + snap.area.x, search.top + snap.area.y, search.left + snap.area.x + snap.area.w,
                      search.top + snap.area.y + snap.area.h};
        }
        if (PostMessageW(hwnd, WM_APP_CHATFOUND, 0, reinterpret_cast<LPARAM>(found.get()))) found.release();
    }).detach();
}

void MainWindow::OnChatFound(RECT* raw) {
    std::unique_ptr<RECT> r(raw);
    detecting_ = false;
    if (cfg_.regionSet || IsRectEmpty(r.get())) return;  // set meanwhile, or not open yet: try again later
    UseChatArea(*r);
    CoverChat();
    SetStatus(Tr(L"GW2 chat found \u2013 this window now lies over it and translates it."), Tone::Ok, 9000);
}

// ===========================================================================
// Docking: the window keeps its place relative to the bottom-left corner of
// the game (where the GW2 chat lives) and follows the game window. It stays a
// separate top-level window — no owner/parent link into the game's windows,
// which would tie our input handling to the game's.
// ===========================================================================
RECT MainWindow::DockTarget() const {
    const RECT client = GameClientRect();
    if (IsRectEmpty(&client)) return {};
    // Relative to the game, even beside a windowed game; but always fully on
    // the monitor it lands on.
    RECT r{client.left + cfg_.dockLeft, client.bottom - cfg_.dockFromBottom - cfg_.dockHeight,
           client.left + cfg_.dockLeft + cfg_.dockWidth, client.bottom - cfg_.dockFromBottom};
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST), &mi);
    const RECT& wa = mi.rcWork;
    const int w = std::min(static_cast<int>(r.right - r.left), static_cast<int>(wa.right - wa.left));
    const int h = std::min(static_cast<int>(r.bottom - r.top), static_cast<int>(wa.bottom - wa.top));
    const int x = std::clamp(static_cast<int>(r.left), static_cast<int>(wa.left), static_cast<int>(wa.right) - w);
    const int y = std::clamp(static_cast<int>(r.top), static_cast<int>(wa.top), static_cast<int>(wa.bottom) - h);
    return {x, y, x + w, y + h};
}

void MainWindow::UpdateDockFromWindow() {
    const RECT client = GameClientRect();
    RECT me{};
    if (IsRectEmpty(&client) || !GetWindowRect(hwnd_, &me)) return;
    cfg_.dockLeft = me.left - client.left;
    cfg_.dockFromBottom = client.bottom - me.bottom;
    cfg_.dockWidth = me.right - me.left;
    cfg_.dockHeight = me.bottom - me.top;
    cfg_.dockSet = true;
    cfg_.SaveDock();
}

void MainWindow::SetDock(bool on) {
    if (on && !gw2_) gw2_ = FindGw2Window(mumbleState_.live ? mumbleState_.processId : 0);
    if (on && !gw2_) {
        SetStatus(Tr(L"GW2 not found \u2013 docking works once the game runs"), Tone::Warn, 5000);
        return;
    }
    cfg_.dock = on;
    if (on) {
        UpdateDockFromWindow();  // where it is now, relative to the game
        SetStatus(Tr(L"Docked \u2013 the window moves with GW2"), Tone::Ok, 4000);
    } else {
        cfg_.SaveDock();
        cfg_.SaveWindowRect(hwnd_, theme_.scale);
        SetStatus(Tr(L"Undocked"), Tone::Muted, 2500);
    }
}

// Lay the window over the GW2 chat panel. The reader keeps reading the real
// chat underneath: this window is kept out of the capture while it covers it.
void MainWindow::CoverChat() {
    if (!gw2_) gw2_ = FindGw2Window(mumbleState_.live ? mumbleState_.processId : 0);
    const RECT area = ChatArea();
    const RECT client = GameClientRect();
    if (!cfg_.regionSet || IsRectEmpty(&area) || IsRectEmpty(&client)) {
        SetStatus(Tr(L"Set the chat area first (GW2 must be running)"), Tone::Warn, 5000);
        return;
    }
    // The GW2 panel: its tabs above the text lines, the input line below.
    RECT r{area.left - theme_.S(6), area.top - theme_.S(34), area.right + theme_.S(6), area.bottom + theme_.S(40)};
    MINMAXINFO mmi{};
    SendMessageW(hwnd_, WM_GETMINMAXINFO, 0, reinterpret_cast<LPARAM>(&mmi));
    if (r.right - r.left < mmi.ptMinTrackSize.x) r.right = r.left + mmi.ptMinTrackSize.x;
    if (r.bottom - r.top < mmi.ptMinTrackSize.y) r.top = r.bottom - mmi.ptMinTrackSize.y;  // grow upwards
    OffsetRect(&r, std::max(0L, client.left - r.left), std::max(0L, client.top - r.top));
    OffsetRect(&r, std::min(0L, client.right - r.right), std::min(0L, client.bottom - r.bottom));
    ignoreSnapshotsBefore_ = GetTickCount64() + 400;
    SetWindowPos(hwnd_, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE);
    cfg_.dock = true;
    UpdateDockFromWindow();
    PollGame();  // switch the capture exclusion on right away
    if (affinityUnsupported_)
        SetStatus(Tr(L"This Windows cannot keep the window out of the capture \u2013 please put it beside the chat "
                     L"(works from Windows 10 version 2004)"),
                  Tone::Warn, 12000);
    else
        SetStatus(Tr(L"Lies over the GW2 chat and keeps reading it underneath. Keep the GW2 chat open."), Tone::Ok,
                  9000);
}

void MainWindow::OnSelfRead() {
    const ULONGLONG now = GetTickCount64();
    if (now - selfReadTick_ > 10000) selfReadHits_ = 0;
    selfReadTick_ = now;
    if (++selfReadHits_ < 2 || !excludedFromCapture_) return;  // one look-alike line can be chance
    affinityUnsupported_ = true;
    excludedFromCapture_ = false;
    SetWindowDisplayAffinity(hwnd_, WDA_NONE);
    ignoreSnapshotsBefore_ = now + 150;
    SetStatus(Tr(L"Windows captures this window too \u2013 reading pauses while it covers the chat"), Tone::Warn, 12000);
    PollGame();
}

void MainWindow::ShowOverlay() {
    userHidden_ = false;
    autoHidden_ = false;
    if (collapsed_) ToggleCollapse();
    ShowWindow(hwnd_, SW_SHOW);
    Front(hwnd_);
    SetFocus(input_.Hwnd());
}

void MainWindow::HideOverlay() {
    userHidden_ = true;
    ShowWindow(hwnd_, SW_HIDE);
}

// Esc: hand the keyboard back to the game; the window stays as the chat view.
void MainWindow::ReturnToGame() {
    if (gw2_ && IsWindow(gw2_)) Front(gw2_);
    else HideOverlay();
}

// ===========================================================================
// Settings, setup, tray, grammar check
// ===========================================================================
std::wstring MainWindow::ConnectionStatus() {
    std::wstring s;
    auto line = [&](const std::wstring& t) { s += t + L"\r\n"; };
    line(gw2_ ? L"[OK] " + Tr(L"GW2 window found") : L"[--] " + Tr(L"GW2 window not found (start the game)"));
    if (mumbleState_.live)
        line(L"[OK] " + TrF(L"MumbleLink: {1} (map {2})",
                 {mumbleState_.identity.name.empty() ? L"?" : mumbleState_.identity.name,
                  std::to_wstring(mumbleState_.identity.mapId)}));
    else
        line(L"[--] " + Tr(L"MumbleLink: no data (GW2 not running or in the character select)"));
    const std::wstring dir = cfg_.gw2Dir.empty() ? FindGw2Dir() : cfg_.gw2Dir;
    if (dir.empty()) {
        line(L"[?]  " + Tr(L"GW2 folder unknown"));
    } else {
        const AddonEnvironment env = ScanAddons(dir);
        line(TrF(L"GW2 folder: {1}", {dir}));
        line(std::wstring(env.nexus ? L"[OK] " : L"[--] ") + L"Nexus" + (env.nexus ? L"" : L" " + Tr(L"not installed")));
        line(std::wstring(env.arcdps ? L"[OK] " : L"[--] ") + L"arcdps" +
             (env.arcdps ? L"" : L" " + Tr(L"not installed")));
        line(std::wstring(env.unofficialExtras ? L"[OK] " : L"[--] ") + L"arcdps unofficial extras" +
             (env.unofficialExtras ? L"" : L" " + Tr(L"not installed")));
        if (env.unofficialExtras)
            line(Tr(L"   (could deliver squad/party chat as exact text later – optional add-on, not used now)"));
    }
    TesseractInfo tess;
    if (FindTesseract(cfg_.tesseractPath, &tess)) line(L"[OK] Tesseract: " + tess.exe);
    else line(L"[--] " + Tr(L"Tesseract not installed (Windows text recognition is used)"));
    if (!lastOcrEngine_.empty()) line(TrF(L"Reading with: {1}", {lastOcrEngine_}));
    line(TrF(L"Translator: {1}", {translator_ ? translator_->Name() : std::wstring(L"-")}));
    line(TrF(L"Settings: {1}", {cfg_.iniPath}));
    return s;
}

// The technical page: every parameter (named like in the ini) and live
// numbers. No chat text in here, so it can be shared as a diagnosis.
std::wstring MainWindow::TechnicalStatus() {
    std::wstring s;
    auto line = [&](const std::wstring& l) { s += l + L"\r\n"; };
    auto num = [](double v, int digits = 0) {
        wchar_t b[32];
        swprintf(b, 32, digits ? L"%.1f" : L"%.0f", v);
        return std::wstring(b);
    };
    line(L"GW2 Chat Translator " GCT_VERSION_WSTR);
    line(TrF(L"Picture: {1} · text recognition: {2} · translator: {3}",
             {lastCaptureMethod_.empty() ? std::wstring(L"-") : lastCaptureMethod_,
              lastOcrEngine_.empty() ? std::wstring(L"-") : lastOcrEngine_,
              translator_ ? translator_->Name() : std::wstring(L"-")}));
    line(L"");
    // Safety: what the tool does and does not do, and where texts go right now.
    auto host = [](const std::wstring& url) {
        const size_t scheme = url.find(L"://");
        const size_t start = scheme == std::wstring::npos ? 0 : scheme + 3;
        return url.substr(start, url.find_first_of(L"/?", start) - start);
    };
    std::wstring dest;
    switch (engine_) {
        case Engine::Basic: dest = L"api.mymemory.translated.net"; break;
        case Engine::DeepL: dest = IsDeepLFreeKey(cfg_.deeplKey) ? L"api-free.deepl.com" : L"api.deepl.com"; break;
        case Engine::Google: dest = L"translation.googleapis.com"; break;
        case Engine::Microsoft: dest = L"api.cognitive.microsofttranslator.com"; break;
        case Engine::Libre: dest = host(cfg_.libreUrl); break;
        case Engine::Llm: dest = host(cfg_.llmUrl.empty() ? L"http://localhost:11434" : cfg_.llmUrl); break;
        default: break;
    }
    const bool local = engine_ == Engine::Llm ? IsLocalLlmUrl(cfg_.llmUrl) : engine_ == Engine::Libre && IsLocalLlmUrl(cfg_.libreUrl);
    line(Tr(L"Safety"));
    line(L"  [OK] " + Tr(L"Nothing runs inside the game: no DLL, no hook, no memory reading, no handle to the game process."));
    line(L"  [OK] " + (cfg_.copyOnly ? Tr(L"Sending is off: Enter only copies, not a single key reaches the game.")
                                     : Tr(L"A line goes to the game only when you press Enter, never by itself.")));
    line(L"  [OK] " + Tr(L"Pictures of the screen never leave this PC; incoming chat is only shown, never obeyed."));
    line(L"  " + std::wstring(local ? L"[OK] " : L"[i]  ") +
         (local ? TrF(L"Texts to translate stay in your network ({1}).", {dest})
                : TrF(L"Texts to translate go to: {1}", {dest.empty() ? std::wstring(L"-") : dest})));
    if (cfg_.languageTool)
        line(L"  [i]  " + TrF(L"Grammar check sends what you type to: {1}", {host(cfg_.languageToolUrl.empty()
                                                                                      ? L"https://api.languagetool.org"
                                                                                      : cfg_.languageToolUrl)}));
    const bool anyKey = !cfg_.deeplKey.empty() || !cfg_.googleKey.empty() || !cfg_.msKey.empty() ||
                        !cfg_.libreKey.empty() || !cfg_.llmKey.empty();
    if (anyKey) line(L"  [OK] " + Tr(L"API keys are stored encrypted for your Windows account (DPAPI)."));
    line(L"  [OK] " + TrF(L"Learned words and corrections stay on this PC ({1}).", {CorrectionsInfo()}));
    line(L"");
    line(Tr(L"Live (since reading started)"));
    const double minutes = stats_.since ? (GetTickCount64() - stats_.since) / 60000.0 : 0;
    line(TrF(L"  Pictures read: {1} ({2} per minute) · last {3} ms, average {4} ms · lines in the last one: {5}",
             {std::to_wstring(stats_.pictures), num(minutes > 0 ? stats_.pictures / minutes : 0, 1),
              std::to_wstring(stats_.lastMs), num(stats_.avgMs), std::to_wstring(stats_.lastLines)}));
    const double dropped = stats_.messages ? 100.0 * stats_.dropped / stats_.messages : 0;
    line(TrF(L"  Messages found: {1} · dropped as noise: {2} ({3} %) · new after the double scan: {4}",
             {std::to_wstring(stats_.messages), std::to_wstring(stats_.dropped), num(dropped, 1),
              std::to_wstring(stats_.confirmed)}));
    line(TrF(L"  Translated: {1} · failed: {2} · average {3} ms · waiting: {4} · on their way: {5}",
             {std::to_wstring(stats_.translated), std::to_wstring(stats_.failed), num(stats_.avgTranslateMs),
              std::to_wstring(inQueue_.size()), std::to_wstring(inFlight_)}));
    if (const std::wstring q = MyMemoryQuotaText(); !q.empty()) line(L"  " + q);
    line(TrF(L"  Second look: {1} unknown words read again, {2} corrected",
             {std::to_wstring(stats_.secondLooks), std::to_wstring(stats_.secondFixes)}));
    line(TrF(L"  Glyph reader: knows {1} letters of the chat font, read {2} rows itself",
             {std::to_wstring(stats_.glyphLetters), std::to_wstring(stats_.glyphRows)}));
    line(TrF(L"  Learned words: {1}", {std::to_wstring(spell_.Model().Size())}));
    line(L"");
    line(Tr(L"Parameters (as in the settings file)"));
    std::wstring understood;
    for (const std::wstring& l : cfg_.understoodLangs) understood += (understood.empty() ? L"" : L",") + l;
    line(L"  [Reader] Enabled=" + std::to_wstring(cfg_.readerEnabled) + L"  IntervalMs=" +
         std::to_wstring(cfg_.readerIntervalMs) + L"  OcrEngine=" + OcrKey(cfg_.ocr) + L"  OcrZoom=" +
         std::to_wstring(cfg_.ocrScale) + L"  SecondLook=" + std::to_wstring(cfg_.secondLook) + L"  Capture=" +
         (cfg_.captureMode == 1 ? L"window" : cfg_.captureMode == 2 ? L"screen" : L"auto") + L"  ConfirmMs=" +
         std::to_wstring(kConfirmDelayMs) + L"  ShowSystemLines=" + std::to_wstring(cfg_.showSystemLines));
    line(L"  [Reader] Region=" + std::to_wstring(cfg_.regionLeft) + L"," + std::to_wstring(cfg_.regionFromBottom) +
         L" " + std::to_wstring(cfg_.regionWidth) + L"x" + std::to_wstring(cfg_.regionHeight) + L"  RegionSet=" +
         std::to_wstring(cfg_.regionSet) + L"  WindowCapture=" + std::to_wstring(WindowCaptureAllowed()) +
         L"  FreeArea=" + std::to_wstring(cfg_.freeArea));
    line(L"  [Translate] Engine=" + std::wstring(EngineKey(cfg_.engine)) + L"  ReadLang=" +
         (cfg_.readLang.empty() ? std::wstring(L"(Windows)") : cfg_.readLang) + L"  AutoChannels=" +
         SerializeChannels(cfg_.autoTranslate) + L"  Understood=" + understood + L"  BackTranslate=" +
         std::to_wstring(cfg_.backTranslate) + L"  DebounceMs=" + std::to_wstring(cfg_.debounceMs));
    line(L"  [Chat] SendMode=" + std::wstring(cfg_.copyOnly ? L"copy" : L"send") + L"  StepDelayMs=" +
         std::to_wstring(cfg_.send.stepDelayMs) + L"  KeyHoldMs=" + std::to_wstring(cfg_.send.keyHoldMs) +
         L"  RestoreDelayMs=" + std::to_wstring(cfg_.send.restoreDelayMs) + L"  MaxLength=" +
         std::to_wstring(cfg_.maxLength));
    line(L"  [Spelling] Enabled=" + std::to_wstring(cfg_.spellEnabled) + L"  AutoCorrect=" +
         std::to_wstring(static_cast<int>(cfg_.autoCorrect)) + L"  Suggestions=" + std::to_wstring(cfg_.suggestions) +
         L"  Learn=" + std::to_wstring(cfg_.learnWords) + L"  LanguageTool=" + std::to_wstring(cfg_.languageTool));
    line(L"  [Window] FontPercent=" + std::to_wstring(cfg_.fontPercent) + L"  Opacity=" + std::to_wstring(cfg_.opacity) +
         L"  Dock=" + std::to_wstring(cfg_.dock) + L"  FollowGame=" + std::to_wstring(cfg_.followGame));
    line(L"");
    line(TrF(L"Settings file: {1}", {cfg_.iniPath}));
    return s;
}

void MainWindow::OpenSettings(SettingsPage page) {
    Config edited = cfg_;
    DialogContext ctx;
    ctx.connectionStatus = [this] { return ConnectionStatus(); };
    ctx.technicalStatus = [this] { return TechnicalStatus(); };
    ctx.isGw2Running = [this] { return gw2_ != nullptr || mumbleState_.live; };
    ctx.preview = [this](int opacity, int fontPercent, const std::wstring& face) {
        SetLayeredWindowAttributes(hwnd_, 0, static_cast<BYTE>(opacity), LWA_ALPHA);
        if (fontPercent != theme_.textPercent || face != theme_.fontFace) {
            const int keepSize = cfg_.fontPercent;
            const std::wstring keepFace = cfg_.fontFace;
            cfg_.fontPercent = fontPercent;
            cfg_.fontFace = face;
            ApplyDpi(theme_.dpi, nullptr);
            cfg_.fontPercent = keepSize;
            cfg_.fontFace = keepFace;
        }
    };
    ctx.forgetLearned = [this] {
        spell_.ForgetAll();
        input_.RefreshSuggestions();
    };
    ctx.learnFromFile = [this](const std::wstring& path) {
        std::string data;
        if (!ReadFileBytes(path, data)) return Tr(L"The file could not be read.");
        std::wstring text;
        if (data.size() >= 2 && static_cast<unsigned char>(data[0]) == 0xFF && static_cast<unsigned char>(data[1]) == 0xFE)
            text.assign(reinterpret_cast<const wchar_t*>(data.data() + 2), (data.size() - 2) / sizeof(wchar_t));  // UTF-16
        else
            text = FromUtf8(data);
        const SpellService::ProfileResult r = spell_.LearnFromText(text);
        input_.RefreshSuggestions();
        const LangInfo* l = FindLanguage(TypingLocale());
        if (!l) l = FindLanguage(PrimaryLang(TypingLocale()));
        return TrF(L"Learned {1} sentences, {2} new words and {3} of your typical typos ({4}).",
                   {std::to_wstring(r.sentences), std::to_wstring(r.newWords), std::to_wstring(r.typos),
                    l ? std::wstring(l->native) : TypingLocale()});
    };
    ctx.correctionsInfo = [this] { return CorrectionsInfo(); };
    ctx.myWordsText = [this] { return myWords_.Serialize(); };
    ctx.ocrFixesText = [this] { return ocrFixes_.Serialize(); };
    ctx.setOcrFixes = [this](const std::wstring& text) {
        ocrFixes_.Parse(text);
        SaveOcrFixes();
        RestartReader();  // the reader works with the new list
    };
    ctx.readingAdvice = [this] { return ReadingAdvice(); };
    ctx.compareOcr = [this](HWND notify, UINT message) { CompareRecognition(notify, message); };
    ctx.setMyWords = [this](const std::wstring& text) {
        myWords_.Parse(text);
        for (const auto& e : myWords_.Entries()) spell_.AddUserWord(e.first);
        SaveMyWords();
    };
    ctx.clearCorrections = [this] {
        corrections_.Clear();
        SaveCorrections();
    };
    ctx.exportCorrections = [this](const std::wstring& path) {
        return WriteFileAtomic(path, corrections_.Serialize()) ? CorrectionsInfo() + L" – " + Tr(L"exported")
                                                               : Tr(L"Could not write the file.");
    };
    ctx.importCorrections = [this](const std::wstring& path) {
        std::string data;
        if (!ReadFileBytes(path, data)) return Tr(L"Could not read the file.");
        const size_t added = corrections_.Merge(data);
        SaveCorrections();
        return TrF(L"{1} new entries added", {std::to_wstring(added)}) + L" – " + CorrectionsInfo();
    };
    const DialogResult r = ShowSettingsDialog(hwnd_, inst_, edited, ctx, page);
    if (r.saved) ApplySettings(edited);
    HandleDialogAction(r);
}

void MainWindow::RunSetup() {
    Config edited = cfg_;
    DialogContext ctx;
    ctx.connectionStatus = [this] { return ConnectionStatus(); };
    ctx.technicalStatus = [this] { return TechnicalStatus(); };
    ctx.isGw2Running = [this] { return gw2_ != nullptr || mumbleState_.live; };
    const DialogResult r = ShowSetupWizard(hwnd_, inst_, edited, ctx);
    if (r.saved) ApplySettings(edited);
    HandleDialogAction(r);
}

void MainWindow::HandleDialogAction(const DialogResult& r) {
    switch (r.action) {
        case DialogResult::Action::PickRegion:
            PickRegion();
            break;
        case DialogResult::Action::CoverChat:
            if (!cfg_.regionSet) PickRegion();
            if (cfg_.regionSet) CoverChat();
            break;
        case DialogResult::Action::RunSetup:
            RunSetup();
            break;
        case DialogResult::Action::RestartInto: {
            restartCommand_ = L"\"" + r.restartExe + L"\" --restarted";
            if (r.markChatAfterRestart) restartCommand_ += L" --mark-chat";
            if (r.coverAfterRestart) restartCommand_ += L" --cover-chat";
            PostMessageW(hwnd_, WM_CLOSE, 0, 0);
            break;
        }
        default:
            break;
    }
}

// Takes over what the settings dialog changed and restarts only what needs it.
void MainWindow::ApplySettings(const Config& next) {
    const Config prev = cfg_;
    // Things the window manages itself stay as they are.
    Config c = next;
    c.tabs = prev.tabs;
    c.activeTab = prev.activeTab;
    c.x = prev.x, c.y = prev.y, c.w = prev.w, c.h = prev.h;
    cfg_ = c;
    spell_.SetLearnChoices(cfg_.learnWords);

    if (prev.uiLang != cfg_.uiLang) SetUiLanguage(cfg_.uiLang);
    if (prev.fontPercent != cfg_.fontPercent || theme_.textPercent != cfg_.fontPercent || prev.fontFace != cfg_.fontFace ||
        theme_.fontFace != cfg_.fontFace)
        ApplyDpi(theme_.dpi, nullptr);
    if (prev.opacity != cfg_.opacity) SetLayeredWindowAttributes(hwnd_, 0, static_cast<BYTE>(cfg_.opacity), LWA_ALPHA);
    if (prev.hotkey != cfg_.hotkey) {
        if (hotkeyOk_) UnregisterHotKey(hwnd_, kHotkeyId);
        hotkeyOk_ = false;
        if (auto hk = ParseHotkey(cfg_.hotkey))
            hotkeyOk_ = RegisterHotKey(hwnd_, kHotkeyId, hk->mods | MOD_NOREPEAT, hk->vk) != FALSE;
    }
    if (prev.readLang != cfg_.readLang) {
        wchar_t locale[LOCALE_NAME_MAX_LENGTH] = {};
        GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH);
        const LangInfo* read = FindLanguage(cfg_.readLang.empty() ? std::wstring(locale) : cfg_.readLang);
        readLang_ = read ? read->code : L"EN-GB";
    }
    if (prev.writeLangs != cfg_.writeLangs) {
        writeLangs_.clear();
        for (const std::wstring& code : cfg_.writeLangs)
            if (const LangInfo* l = FindLanguage(code)) writeLangs_.push_back(l->code);
        if (writeLangs_.empty()) writeLangs_ = {L"EN-GB"};
        writeIdx_ = 0;
    }
    if (prev.writeIn != cfg_.writeIn) ApplyTypingLanguage();  // spelling, learned words, word bar follow
    const bool engineChanged = prev.engine != cfg_.engine || prev.deeplKey != cfg_.deeplKey ||
                               prev.basicEmail != cfg_.basicEmail || prev.llmUrl != cfg_.llmUrl ||
                               prev.llmModel != cfg_.llmModel || prev.llmKey != cfg_.llmKey ||
                               prev.googleKey != cfg_.googleKey || prev.msKey != cfg_.msKey ||
                               prev.msRegion != cfg_.msRegion || prev.libreUrl != cfg_.libreUrl ||
                               prev.libreKey != cfg_.libreKey;
    if (engineChanged) {
        llm_.reset();
        if (!cfg_.llmModel.empty()) {
            LlmSettings ls;
            ls.url = cfg_.llmUrl;
            ls.model = cfg_.llmModel;
            ls.apiKey = cfg_.llmKey;
            ls.timeoutMs = cfg_.llmTimeoutSec * 1000;
            llm_ = MakeLlmTranslator(ls);
        }
        ChooseEngine(true);
        ++inputGen_;
        ResetCompose();
        StartTranslation();
    }
    input_.SetAutoCorrect(cfg_.autoCorrect);
    if (prev.suggestions != cfg_.suggestions) {
        input_.SetSuggestions(cfg_.suggestions);
        Layout();
    }
    if (prev.saveCaptures != cfg_.saveCaptures) SetSaveCaptures(cfg_.saveCaptures);
    if (prev.readerEnabled != cfg_.readerEnabled || prev.ocr != cfg_.ocr || prev.tesseractPath != cfg_.tesseractPath ||
        prev.readChinese != cfg_.readChinese || prev.readerIntervalMs != cfg_.readerIntervalMs ||
        prev.captureMode != cfg_.captureMode || prev.ocrScale != cfg_.ocrScale || prev.secondLook != cfg_.secondLook ||
        prev.readLang != cfg_.readLang || prev.writeLangs != cfg_.writeLangs)
        RestartReader();
    if (prev.dock != cfg_.dock) SetDock(cfg_.dock);
    if (!cfg_.followGame && autoHidden_) {
        autoHidden_ = false;
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    }
    UpdateHint();
    UpdatePreview();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::AddTrayIcon() {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd_;
    nid.uID = 1;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_APP_TRAY;
    nid.hIcon = LoadIconW(inst_, MAKEINTRESOURCEW(1));
    if (!nid.hIcon) nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    const std::wstring tip = waitForGame_ ? Tr(L"GW2 Chat Translator – waits for GW2") : std::wstring(kTitle);
    wcsncpy_s(nid.szTip, tip.c_str(), _TRUNCATE);
    trayOk_ = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
}

void MainWindow::UpdateTrayTip() {
    if (!trayOk_) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd_;
    nid.uID = 1;
    nid.uFlags = NIF_TIP;
    const std::wstring tip = gw2_ ? std::wstring(kTitle) : Tr(L"GW2 Chat Translator – waits for GW2");
    wcsncpy_s(nid.szTip, tip.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void MainWindow::RemoveTrayIcon() {
    if (!trayOk_) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd_;
    nid.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    trayOk_ = false;
}

void MainWindow::ShowTrayMenu() {
    enum : UINT { kShow = 1, kSettings, kQuit };
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kShow, Tr(L"Show").c_str());
    AppendMenuW(menu, MF_STRING, kSettings, Tr(L"Settings …").c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kQuit, Tr(L"Quit").c_str());
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd_);  // so the menu closes when clicking elsewhere
    const UINT cmd = static_cast<UINT>(
        TrackPopupMenu(menu, MenuFlags(TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON), pt.x, pt.y, 0, hwnd_, nullptr));
    DestroyMenu(menu);
    if (cmd == kShow) ShowOverlay();
    else if (cmd == kSettings) OpenSettings(SettingsPage::General);
    else if (cmd == kQuit) PostMessageW(hwnd_, WM_CLOSE, 0, 0);
}

// Optional LanguageTool check of what you typed (in your language), a moment
// after you stop typing. Results only mark words; nothing is changed by itself.
void MainWindow::StartGrammarCheck() {
    KillTimer(hwnd_, kTimerGrammar);
    if (!cfg_.languageTool || grammarInFlight_) return;
    const std::wstring text = input_.Text();
    const std::wstring clean = SanitizeChatText(text);
    if (CodePointCount(clean) < 8) return;
    if (!grammarLimiter_.Allow(GetTickCount64())) return;  // public server: max. 20 per minute
    grammarInFlight_ = true;
    std::thread([hwnd = hwnd_, url = cfg_.languageToolUrl, text, lang = TypingLocale()] {
        auto msg = std::make_unique<GrammarMsg>();
        msg->text = text;
        msg->result = CheckWithLanguageTool(url, text, lang, L"");
        if (PostMessageW(hwnd, WM_APP_GRAMMAR, 0, reinterpret_cast<LPARAM>(msg.get()))) msg.release();
    }).detach();
}

void MainWindow::OnGrammar(GrammarMsg* raw) {
    std::unique_ptr<GrammarMsg> m(raw);
    grammarInFlight_ = false;
    if (!m->result.ok) {
        if (BackgroundNoticeAllowed()) SetStatus(m->result.error, Tone::Muted, 5000);
        return;
    }
    std::vector<SpellIssue> issues;
    for (const LtMatch& x : m->result.matches) {
        if (x.Spelling()) continue;  // the Windows checker and our word model handle spelling
        SpellIssue is;
        is.span = x.span;
        is.grammar = true;
        is.suggestions = x.replacements;
        is.message = x.message;
        issues.push_back(std::move(is));
    }
    input_.SetGrammarIssues(m->text, std::move(issues));
}

// ===========================================================================
// Window procedure
// ===========================================================================
LRESULT CALLBACK MainWindow::Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    MainWindow* self;
    if (msg == WM_NCCREATE) {
        self = static_cast<MainWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = h;
    } else {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    }
    return self ? self->Handle(msg, wp, lp) : DefWindowProcW(h, msg, wp, lp);
}

LRESULT MainWindow::Handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:
            CreateChildren();
            return 0;
        case WM_SIZE:
            Layout();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case WM_GETMINMAXINFO: {
            const Metrics m = MetricsFor(theme_);
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
            if (collapsed_) {
                mmi->ptMinTrackSize = {theme_.S(240), m.head};
                mmi->ptMaxTrackSize.y = m.head;
            } else {
                const int previewH = theme_.textLineHeight * 2 + theme_.smallLineHeight * 2 + theme_.S(14);
                mmi->ptMinTrackSize = {theme_.S(360), m.head + theme_.S(40) + previewH + m.inputH + 2 * m.gap + m.foot};
            }
            return 0;
        }
        case WM_NCHITTEST:
            return HitTest(lp);
        case WM_DPICHANGED:  // per-monitor DPI aware: rescale ourselves
            ApplyDpi(HIWORD(wp), reinterpret_cast<const RECT*>(lp));
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            Paint();
            return 0;
        case WM_CTLCOLOREDIT:
            if (reinterpret_cast<HWND>(lp) == input_.Hwnd()) {
                HDC dc = reinterpret_cast<HDC>(wp);
                SetTextColor(dc, Theme::kText);
                SetBkColor(dc, Theme::kInputBg);
                return reinterpret_cast<LRESULT>(theme_.inputBg);
            }
            break;
        case WM_COMMAND:
            if (HIWORD(wp) == EN_CHANGE && reinterpret_cast<HWND>(lp) == input_.Hwnd()) OnInputChanged();
            return 0;
        case WM_TIMER:
            if (wp == kTimerDebounce) {
                StartTranslation();
                if (cfg_.languageTool) SetTimer(hwnd_, kTimerGrammar, 900, nullptr);
            } else if (wp == kTimerGame) PollGame();
            else if (wp == kTimerGrammar) StartGrammarCheck();
            else if (wp == kTimerOnce) EndTranslateOnce();
            else if (wp == kTimerConfirm) {  // double scan: the second look at new lines
                KillTimer(hwnd_, kTimerConfirm);
                reader_.Rescan();
            }
            else if (wp == kTimerCaptures) {
                KillTimer(hwnd_, kTimerCaptures);
                if (cfg_.saveCaptures) {
                    SetSaveCaptures(false);
                    SetStatus(Tr(L"Diagnostic pictures switched off again"), Tone::Muted, 5000);
                }
            }
            else if (wp == kTimerStatus) {
                KillTimer(hwnd_, kTimerStatus);
                status_.clear();
                tone_ = Tone::Muted;
                InvalidateChrome();
            }
            return 0;
        case WM_APP_TRANSLATED:
            OnTranslated(reinterpret_cast<TranslatedMsg*>(lp));
            return 0;
        case WM_APP_NAMES:
            OnNamesFetched(reinterpret_cast<NamesMsg*>(lp));
            return 0;
        case WM_APP_SNAPSHOT:
            OnSnapshot(reinterpret_cast<ReaderSnapshot*>(lp));
            return 0;
        case WM_APP_INCOMING:
            OnIncomingTranslated(reinterpret_cast<IncomingMsg*>(lp));
            return 0;
        case WM_APP_GRAMMAR:
            OnGrammar(reinterpret_cast<GrammarMsg*>(lp));
            return 0;
        case WM_APP_TRAY:
            if (LOWORD(lp) == WM_LBUTTONUP || LOWORD(lp) == WM_LBUTTONDBLCLK) {
                if (IsWindowVisible(hwnd_) && GetForegroundWindow() == hwnd_) HideOverlay();
                else ShowOverlay();
            } else if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_CONTEXTMENU) {
                ShowTrayMenu();
            }
            return 0;
        case WM_APP_CHATFOUND:
            OnChatFound(reinterpret_cast<RECT*>(lp));
            return 0;
        case WM_APP_MYMEMORY_NOTICE:
            ShowMyMemoryNotice();
            return 0;
        case WM_APP_FIRSTRUN:
            if (wp == 2) {
                RunSetup();
            } else {
                if (wp == 1 || !cfg_.regionSet) PickRegion();
                if (lp == 1 && cfg_.regionSet) CoverChat();
            }
            return 0;
        case WM_LBUTTONUP:
            OnClick({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
            return 0;
        case WM_CONTEXTMENU:
        case WM_NCRBUTTONUP: {  // right click on a tab or the header: tab settings
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            if (msg == WM_CONTEXTMENU && reinterpret_cast<HWND>(wp) != hwnd_) break;
            if (pt.x == -1 && pt.y == -1) {
                pt = {tabRects_.empty() ? 0 : tabRects_[tab_].left, MetricsFor(theme_).head};
                ClientToScreen(hwnd_, &pt);
            }
            POINT client = pt;
            ScreenToClient(hwnd_, &client);
            if (client.y < 0 || client.y >= MetricsFor(theme_).head) break;
            size_t idx = tab_;
            for (size_t i = 0; i < tabRects_.size(); ++i)
                if (PtInRect(&tabRects_[i], client)) idx = i;
            ShowTabMenu(idx, pt);
            return 0;
        }
        case WM_SETCURSOR: {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd_, &pt);
            if (LOWORD(lp) == HTCLIENT && IsClickable(pt)) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            break;
        }
        case WM_HOTKEY:
            if (wp == kHotkeyId) {
                if (IsWindowVisible(hwnd_) && GetForegroundWindow() == hwnd_) HideOverlay();
                else ShowOverlay();
            }
            return 0;
        case WM_ACTIVATE:
            if (LOWORD(wp) != WA_INACTIVE) SetFocus(input_.Hwnd());
            return 0;
        case WM_ENTERSIZEMOVE:  // pause reading while the window may slide over the chat
            moving_ = true;
            reader_.SetArea({});
            return 0;
        case WM_EXITSIZEMOVE:
            moving_ = false;
            ignoreSnapshotsBefore_ = GetTickCount64();
            cfg_.SaveWindowRect(hwnd_, theme_.scale);
            if (cfg_.dock) UpdateDockFromWindow();  // docked: the new place is relative to the game
            PollGame();
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd_);
            return 0;
        case WM_DESTROY:
            reader_.Stop();
            RemoveTrayIcon();
            cfg_.SaveWindowRect(hwnd_, theme_.scale);
            if (hotkeyOk_) UnregisterHotKey(hwnd_, kHotkeyId);
            PostQuitMessage(0);
            return 0;
        case WM_NCDESTROY:
            SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
            break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

}  // namespace gct
