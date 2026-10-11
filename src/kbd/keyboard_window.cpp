// keyboard_window.cpp — GW2 Keyboard (see keyboard_window.hpp).
#include "keyboard_window.hpp"

#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <memory>
#include <thread>

#include "app/modal_scope.hpp"
#include "core/chat_line.hpp"
#include "core/chat_stream.hpp"
#include "core/gw2_text.hpp"
#include "core/hotkey.hpp"
#include "core/housekeeping.hpp"
#include "core/i18n.hpp"
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

#ifndef MOD_NOREPEAT
#define MOD_NOREPEAT 0x4000
#endif

namespace gct {

struct KbdTranslatedMsg {
    enum class Kind { Forward, Back, Romanize };
    Kind kind = Kind::Forward;
    uint64_t gen = 0;
    TranslateResult result;
};

struct KbdNamesMsg {
    std::string lang;
    NameFetchResult result;
};

struct KbdGrammarMsg {
    std::wstring text;
    LtResult result;
};

namespace {

constexpr wchar_t kClassName[] = L"GW2KeyboardWindow";
constexpr wchar_t kTitle[] = L"GW2 Keyboard";

constexpr UINT WM_APP_TRANSLATED = WM_APP + 1;
constexpr UINT WM_APP_NAMES = WM_APP + 2;
constexpr UINT WM_APP_GRAMMAR = WM_APP + 3;
constexpr UINT WM_APP_TRAY = WM_APP + 4;
constexpr UINT WM_APP_MYMEMORY_NOTICE = WM_APP + 5;
constexpr UINT_PTR kTimerDebounce = 1;
constexpr UINT_PTR kTimerStatus = 2;
constexpr UINT_PTR kTimerGame = 3;
constexpr UINT_PTR kTimerGrammar = 4;
constexpr int kHotkeyId = 1;

constexpr UINT kCmdLangBase = 1000;  // + index into Languages()
constexpr UINT kCmdMoreBase = 2000;  // "More languages" + index into Languages()
constexpr UINT kCmdFavBase = 3000;   // + index into sendLangs_
constexpr UINT kCmdOriginal = 3999;
constexpr UINT kCmdPartnerBase = 4000;

struct Metrics {
    int pad, head, foot, gap, inputH;
};
Metrics MetricsFor(const Theme& t) { return {t.S(8), t.S(32), t.S(30), t.S(6), t.textLineHeight * 2 + t.S(12)}; }

// Saved position inside the work area of its monitor; none yet: bottom-left, above the GW2 chat panel.
void PlaceWindow(int& x, int& y, int w, int h) {
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    const bool autoPos = (x == Config::kAutoPos || y == Config::kAutoPos);
    const RECT want{x, y, x + w, y + h};
    HMONITOR mon = autoPos ? MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY) : MonitorFromRect(&want, MONITOR_DEFAULTTONEAREST);
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

std::wstring HitsText(const std::vector<GlossaryMatch>& hits) {
    std::wstring s;
    for (size_t i = 0; i < hits.size() && i < 2; ++i) {
        if (!s.empty()) s += L", ";
        s += hits[i].source + L" → " + hits[i].target;
    }
    if (hits.size() > 2) s += L" …";
    return s.empty() ? s : TrF(L"Game names: {1}", {s});
}

COLORREF ChannelColorRef(const std::vector<ChannelColor>& palette, Channel ch, COLORREF fallback) {
    for (const ChannelColor& c : palette)
        if (c.channel == ch) return RGB(c.rgb.r, c.rgb.g, c.rgb.b);
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

// A small rounded chip with a drop-down caret; `x` is its left edge.
RECT DrawChip(HDC dc, const Theme& t, int x, int top, int bottom, const std::wstring& text, COLORREF color) {
    const int w = TextWidth(dc, text, t.fontUi) + t.S(8) + t.S(18);
    RECT r{x, top, x + w, bottom};
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

UINT MenuFlags(UINT flags) { return flags | (UiRtl() ? TPM_LAYOUTRTL : 0); }

std::wstring NameCachePath(const Config& cfg, const std::string& lang) {
    return cfg.CacheDir() + L"\\gw2names_" + FromUtf8(lang) + L".tsv";
}

}  // namespace

// ===========================================================================
// Start
// ===========================================================================
int KeyboardWindow::Run(HINSTANCE inst, const std::wstring& cmdLine) {
    inst_ = inst;
    cfg_.Load(DataDir());  // the same data as the full tool: settings, learned words, corrections
    SetUiLang(cfg_.uiLang);
    waitForGame_ = cmdLine.find(L"--wait-for-gw2") != std::wstring::npos;
    {
        std::string data;
        if (ReadFileBytes(cfg_.dataDir + L"\\corrections.txt", data)) corrections_.Parse(data);
        data.clear();
        if (ReadFileBytes(cfg_.dataDir + L"\\my-words.txt", data)) myWords_.Parse(FromUtf8(data));
    }

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
    PreviewView::Register(inst);
    SuggestionBar::Register(inst);

    // Just preview, word bar and input: the height follows from them.
    const Metrics m = MetricsFor(theme_);
    int w = theme_.S(cfg_.w), h = m.head + theme_.textLineHeight * 2 + theme_.smallLineHeight * 2 + theme_.S(14) + m.gap +
                                 theme_.S(34) + m.inputH + m.foot;
    int x = cfg_.x, y = cfg_.y;
    PlaceWindow(x, y, w, h);
    CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kClassName, kTitle, WS_POPUP | WS_CLIPCHILDREN, x,
                    y, w, h, nullptr, nullptr, inst, this);
    if (!hwnd_) return 1;
    using DpiForWindowFn = UINT(WINAPI*)(HWND);
    if (auto fn = reinterpret_cast<DpiForWindowFn>(
            reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow")))) {
        const int wdpi = static_cast<int>(fn(hwnd_));
        if (wdpi > 0 && wdpi != theme_.dpi) ApplyDpi(wdpi, nullptr);
    }
    SetLayeredWindowAttributes(hwnd_, 0, static_cast<BYTE>(cfg_.opacity), LWA_ALPHA);
    if (auto hk = ParseHotkey(cfg_.hotkey))
        hotkeyOk_ = RegisterHotKey(hwnd_, kHotkeyId, hk->mods | MOD_NOREPEAT, hk->vk) != FALSE;

    if (!waitForGame_) WarmUp();  // started with Windows: only the window and the tray now
    SetTimer(hwnd_, kTimerGame, 300, nullptr);
    UpdatePreview();
    AddTrayIcon();
    if (!waitForGame_) {
        ShowWindow(hwnd_, SW_SHOW);
        SetForegroundWindow(hwnd_);
        SetFocus(input_.Hwnd());
        SetStatus(TrF(L"Translator: {1}", {translator_->Name()}), Tone::Ok, 5000);
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    spell_.SaveLearned();
    theme_.Destroy();
    return static_cast<int>(msg.wParam);
}

void KeyboardWindow::InitServices() {
    wchar_t locale[LOCALE_NAME_MAX_LENGTH] = {};
    GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH);
    kbdLocale_ = InputBox::KeyboardLocale();
    if (kbdLocale_.empty()) kbdLocale_ = locale;
    kbdRtl_ = IsRtlLanguage(PrimaryLang(kbdLocale_));
    for (const std::wstring& code : cfg_.writeLangs)
        if (const LangInfo* l = FindLanguage(code)) sendLangs_.push_back(l->code);
    if (sendLangs_.empty()) sendLangs_ = {L"EN-GB"};
    sendIdx_ = 0;
    if (!cfg_.llmModel.empty()) {
        LlmSettings s;
        s.url = cfg_.llmUrl;
        s.model = cfg_.llmModel;
        s.apiKey = cfg_.llmKey;
        s.timeoutMs = cfg_.llmTimeoutSec * 1000;
        llm_ = MakeLlmTranslator(s);
    }
    ChooseEngine(false);
}

void KeyboardWindow::WarmUp() {
    if (warmedUp_) return;
    warmedUp_ = true;
    CleanFolder(cfg_.dataDir, {0, 0, 86400}, IsStaleTempFile);
    CleanFolder(cfg_.LearnedDir(), {0, 0, 86400}, IsStaleTempFile);
    if (cfg_.spellEnabled) {
        wchar_t locale[LOCALE_NAME_MAX_LENGTH] = {};
        GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH);
        const std::wstring typing = TypingLocale();
        std::vector<std::wstring> tags = SpellTagCandidates(PrimaryLang(typing), typing);
        if (cfg_.writeIn.empty())
            for (const std::wstring& t : SpellTagCandidates(PrimaryLang(locale), locale)) tags.push_back(t);
        spell_.Init(tags, cfg_.UserWordsPath());
    }
    spell_.UseLearnedLanguage(PrimaryLang(TypingLocale()), cfg_.LearnedDir());
    if (cfg_.spellEnabled && !spell_.Ready())
        SetStatus(TrF(L"No spell checking installed for {1} (Windows language packs)", {kbdLocale_}), Tone::Warn, 8000);
    EnsureDir(cfg_.CacheDir());
    RefreshGlossary();
    UpdateSpellWords();
    if (hwnd_) {
        input_.RecheckSpelling();
        input_.RefreshSuggestions();
    }
}

std::shared_ptr<Translator> KeyboardWindow::MakeEngine(Engine wanted, Engine* resolved, std::wstring* noteOut) {
    const bool haveDeepL = !cfg_.deeplKey.empty(), haveLlm = llm_ != nullptr;
    const bool haveGoogle = !cfg_.googleKey.empty(), haveMs = !cfg_.msKey.empty(), haveLibre = !cfg_.libreUrl.empty();
    Engine e = wanted;
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
        note = Tr(L"No DeepL key set – using basic (MyMemory)");
    }
    if (e == Engine::Llm && !haveLlm) {
        e = Engine::Basic;
        note = Tr(L"No LLM model set – using basic (MyMemory)");
    }
    *resolved = e;
    if (noteOut) *noteOut = note;
    if (e == Engine::DeepL) return MakeDeepLTranslator(cfg_.deeplKey);
    if (e == Engine::Google) return MakeGoogleTranslator(cfg_.googleKey);
    if (e == Engine::Microsoft) return MakeMicrosoftTranslator(cfg_.msKey, cfg_.msRegion);
    if (e == Engine::Libre) return MakeLibreTranslator(cfg_.libreUrl, cfg_.libreKey);
    if (e == Engine::Llm) return llm_;
    return MakeMyMemoryTranslator(cfg_.basicEmail);
}

void KeyboardWindow::ChooseEngine(bool announce) {
    std::wstring note;
    translator_ = MakeEngine(cfg_.engine, &engine_, &note);
    if (engine_ == Engine::Basic && !cfg_.myMemoryNoticeShown && cfg_.setupDone && hwnd_)
        PostMessageW(hwnd_, WM_APP_MYMEMORY_NOTICE, 0, 0);
    if (announce) {
        if (note.empty()) SetStatus(TrF(L"Translator: {1}", {translator_->Name()}), Tone::Ok, 4000);
        else SetStatus(note, Tone::Warn, 6000);
    }
}

void KeyboardWindow::CreateChildren() {
    preview_.Create(hwnd_, inst_, &theme_, [this] {
        if (grammarOffer_) {  // the offer in the preview: the grammar check (LanguageTool) on
            cfg_.languageTool = true;
            cfg_.SaveBool(L"Spelling", L"LanguageTool", true);
            grammarOffer_ = false;
            SetStatus(Tr(L"Grammar check: on (Settings → Writing)"), Tone::Ok, 4000);
            SetTimer(hwnd_, kTimerGrammar, 300, nullptr);
            UpdatePreview();
        }
        SetFocus(input_.Hwnd());
    });
    words_.Create(hwnd_, inst_, &theme_, [this](size_t i) {
        input_.AcceptSuggestion(i);
        SetFocus(input_.Hwnd());
    });
    words_.SetForget([this](const std::wstring& w) { return spell_.IsLearned(w); },
                     [this](const std::wstring& w) {
                         spell_.Forget(w);
                         input_.RefreshSuggestions();
                         SetStatus(TrF(L"Forgot “{1}” – it will no longer be suggested", {w}), Tone::Muted, 4000);
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
    cb.onCycleLang = [this] { CycleSend(); };
    cb.onRomanize = [this] { Romanize(); };
    cb.onSwitchTab = [] {};
    cb.onAutoCorrected = [this](const std::wstring& from, const std::wstring& to) {
        SetStatus(TrF(L"Corrected: {1} → {2}  ·  Backspace: undo", {from, to}), Tone::Muted, 4000);
    };
    cb.onCorrectionUndone = [this](const std::wstring& original) {
        SetStatus(TrF(L"Kept “{1}” – learned", {original}), Tone::Muted, 3000);
    };
    cb.onForgotten = [this](const std::wstring& w) {
        SetStatus(TrF(L"Forgot “{1}” – it will no longer be suggested", {w}), Tone::Muted, 4000);
    };
    cb.onExplain = [this](const std::wstring& w) { ExplainWord(w); };
    cb.onSuggestions = [this](const WordSuggestions& s) { words_.Set(s); };
    cb.onKeyboardLanguage = [this](const std::wstring& locale) { OnKeyboardLanguage(locale); };
    spell_.SetLearnChoices(cfg_.learnWords);
    input_.Create(hwnd_, inst_, &theme_, &spell_, cfg_.autoCorrect, cfg_.suggestions, std::move(cb));
    input_.SetRtl(kbdRtl_);
    Layout();
}

// ===========================================================================
// Window
// ===========================================================================
void KeyboardWindow::Layout() {
    if (!input_.Hwnd()) return;
    RECT rc;
    GetClientRect(hwnd_, &rc);
    const Metrics m = MetricsFor(theme_);
    const int w = std::max(10, static_cast<int>(rc.right) - 2 * m.pad);
    const bool bar = cfg_.suggestions;
    const int barH = bar ? words_.PreferredHeight() : 0;
    const int inputY = rc.bottom - m.foot - m.inputH;
    const int barY = inputY - barH;
    const int previewH = std::max(1, barY - m.gap - m.head);  // the preview takes the rest
    MoveWindow(preview_.Hwnd(), m.pad, m.head, w, previewH, TRUE);
    ShowWindow(preview_.Hwnd(), SW_SHOWNA);
    MoveWindow(words_.Hwnd(), m.pad, barY, w, std::max(1, barH), TRUE);
    ShowWindow(words_.Hwnd(), bar ? SW_SHOWNA : SW_HIDE);
    MoveWindow(input_.Hwnd(), m.pad, inputY, w, m.inputH, TRUE);
    ShowWindow(input_.Hwnd(), SW_SHOW);
    input_.ApplyPadding();
}

void KeyboardWindow::ApplyDpi(int dpi, const RECT* suggested) {
    theme_.Destroy();
    theme_.Create(dpi, cfg_.fontPercent, cfg_.fontFace);
    input_.ApplyTheme();
    if (suggested)
        SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                     suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
    Layout();
    InvalidateRect(hwnd_, nullptr, FALSE);
    InvalidateRect(preview_.Hwnd(), nullptr, FALSE);
    InvalidateRect(words_.Hwnd(), nullptr, FALSE);
}

void KeyboardWindow::InvalidateChrome() {
    if (!hwnd_) return;
    RECT rc;
    GetClientRect(hwnd_, &rc);
    const Metrics m = MetricsFor(theme_);
    RECT head{0, 0, rc.right, m.head};
    RECT foot{0, rc.bottom - m.foot, rc.right, rc.bottom};
    InvalidateRect(hwnd_, &head, FALSE);
    InvalidateRect(hwnd_, &foot, FALSE);
}

void KeyboardWindow::Paint() {
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

    // ---- header: title, menu, close
    closeRect_ = {rc.right - t.S(30), 0, rc.right, m.head};
    DrawLine(dc, L"×", closeRect_, Theme::kMuted, t.fontText, DT_CENTER);
    menuRect_ = {closeRect_.left - t.S(28), 0, closeRect_.left, m.head};
    DrawLine(dc, L"≡", menuRect_, Theme::kMuted, t.fontText, DT_CENTER);
    RECT title{m.pad + t.S(2), 0, menuRect_.left - t.S(8), m.head};
    DrawLine(dc, kTitle, title, Theme::kText, t.fontUiBold, DT_LEFT | DT_END_ELLIPSIS);

    // ---- footer: channel | write | send | counter | status
    const int fy0 = rc.bottom - m.foot + t.S(4), fy1 = rc.bottom - t.S(4);
    channelRect_ = DrawChip(dc, t, m.pad, fy0, fy1, ChannelChipText(), ChannelColorRef(cfg_.palette, channel_, Theme::kText));
    typeRect_ = DrawChip(dc, t, channelRect_.right + t.S(6), fy0, fy1, TypeChipText(), Theme::kText);
    sendRect_ = DrawChip(dc, t, typeRect_.right + t.S(6), fy0, fy1, SendChipText(),
                         SendOriginal() ? Theme::kMuted : Theme::kAccent);
    std::wstring counter;
    COLORREF counterColor = Theme::kMuted;
    if (PreviewIsCurrent() && parts_.size() > 1) {
        counter = TrF(L"{1} parts", {std::to_wstring(parts_.size())});
        counterColor = Theme::kWarn;
    } else {
        const std::wstring line = PreviewIsCurrent() && !parts_.empty() ? parts_[std::min(partIdx_, parts_.size() - 1)]
                                                                        : ComposePrefix() + SanitizeChatText(input_.Text());
        const size_t n = CodePointCount(line);
        counter = std::to_wstring(n) + L"/" + std::to_wstring(cfg_.maxLength);
        if (n > static_cast<size_t>(cfg_.maxLength)) counterColor = Theme::kWarn;
    }
    RECT fr{sendRect_.right + t.S(10), rc.bottom - m.foot, rc.right - m.pad, rc.bottom};
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
        if (!hotkeyOk_ && !Trim(cfg_.hotkey).empty()) {
            text = TrF(L"Hotkey “{1}” is taken – change it in the settings", {cfg_.hotkey});
            color = Theme::kWarn;
        } else if (!gw2_) {
            text = Tr(L"GW2 not found");
        } else {
            text = cfg_.copyOnly ? Tr(L"Copy only · Esc: back to the game") : Tr(L"Esc: back to the game");
            if (const std::wstring q = MyMemoryQuotaText(); !q.empty()) text += L"  ·  " + q;
        }
    }
    DrawLine(dc, text, fr, color, t.fontUi, DT_RIGHT | DT_END_ELLIPSIS | (IsRtlText(text) ? DT_RTLREADING : 0));

    BitBlt(wdc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldFont);
    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(dc);
    EndPaint(hwnd_, &ps);
}

bool KeyboardWindow::IsClickable(POINT pt) const {
    for (const RECT* r : {&menuRect_, &closeRect_, &channelRect_, &typeRect_, &sendRect_})
        if (PtInRect(r, pt)) return true;
    return false;
}

LRESULT KeyboardWindow::HitTest(LPARAM lp) const {
    POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    ScreenToClient(hwnd_, &pt);
    RECT rc;
    GetClientRect(hwnd_, &rc);
    if (IsClickable(pt)) return HTCLIENT;
    const int b = theme_.S(6);
    const bool left = pt.x < b, right = pt.x >= rc.right - b, top = pt.y < b, bottom = pt.y >= rc.bottom - b;
    if (top && left) return HTTOPLEFT;
    if (top && right) return HTTOPRIGHT;
    if (bottom && left) return HTBOTTOMLEFT;
    if (bottom && right) return HTBOTTOMRIGHT;
    if (left) return HTLEFT;
    if (right) return HTRIGHT;
    if (top) return HTTOP;
    if (bottom) return HTBOTTOM;
    const Metrics m = MetricsFor(theme_);
    if (pt.y < m.head || pt.y >= rc.bottom - m.foot) return HTCAPTION;  // drag by header / footer
    return HTCLIENT;
}

void KeyboardWindow::OnClick(POINT pt) {
    if (PtInRect(&closeRect_, pt)) PostMessageW(hwnd_, WM_CLOSE, 0, 0);
    else if (PtInRect(&menuRect_, pt)) ShowMainMenu();
    else if (PtInRect(&channelRect_, pt)) ShowChannelMenu();
    else if (PtInRect(&typeRect_, pt)) ShowTypeLangMenu();
    else if (PtInRect(&sendRect_, pt)) ShowSendMenu();
}

void KeyboardWindow::SetStatus(const std::wstring& text, Tone tone, UINT autoClearMs) {
    status_ = text;
    tone_ = tone;
    if (!hwnd_) return;
    KillTimer(hwnd_, kTimerStatus);
    if (autoClearMs) SetTimer(hwnd_, kTimerStatus, autoClearMs, nullptr);
    InvalidateChrome();
}

bool KeyboardWindow::BackgroundNoticeAllowed() const {
    return status_.empty() || tone_ == Tone::Muted || tone_ == Tone::Ok;
}

void KeyboardWindow::ShowWindowNow() {
    WarmUp();
    userHidden_ = false;
    autoHidden_ = false;
    ShowWindow(hwnd_, SW_SHOW);
    Front(hwnd_);
    SetFocus(input_.Hwnd());
}

void KeyboardWindow::HideWindowNow() {
    userHidden_ = true;
    ShowWindow(hwnd_, SW_HIDE);
}

// Esc: hand the keyboard back to the game.
void KeyboardWindow::ReturnToGame() {
    if (gw2_ && IsWindow(gw2_)) Front(gw2_);
    else HideWindowNow();
}

bool KeyboardWindow::Front(HWND h) { return BringToFront(h, !cfg_.copyOnly); }

// ===========================================================================
// Languages, channel, menus
// ===========================================================================
std::wstring KeyboardWindow::TypingLocale() const {
    if (cfg_.writeIn.empty()) return kbdLocale_;
    const std::wstring code = ToUpperAscii(cfg_.writeIn);
    const size_t dash = code.find(L'-');
    const std::wstring primary = ToLowerAscii(code.substr(0, dash));
    if (dash != std::wstring::npos) return primary + L"-" + code.substr(dash + 1);
    return primary + L"-" + (primary == L"en" ? std::wstring(L"US") : ToUpperAscii(primary));
}

bool KeyboardWindow::SendOriginal() const { return sendIdx_ >= sendLangs_.size(); }

std::wstring KeyboardWindow::SendLang() const { return SendOriginal() ? std::wstring() : sendLangs_[sendIdx_]; }

// Translation only when the chat gets another language than the one you write in; otherwise only the typing help.
bool KeyboardWindow::Translating() const {
    return !SendOriginal() && PrimaryLang(SendLang()) != PrimaryLang(TypingLocale());
}

void KeyboardWindow::CycleSend() { SetSendIndex((sendIdx_ + 1) % (sendLangs_.size() + 1)); }

void KeyboardWindow::SetSendIndex(size_t idx) {
    sendIdx_ = std::min(idx, sendLangs_.size());
    if (tone_ == Tone::Error) SetStatus(L"", Tone::Muted);
    ++inputGen_;
    ResetCompose();
    lastHits_.clear();
    RefreshGlossary();
    UpdatePreview();
    InvalidateChrome();
    StartTranslation();
}

void KeyboardWindow::ApplyTypingLanguage() {
    const std::wstring typing = TypingLocale();
    if (cfg_.spellEnabled) {
        spell_.SwitchLanguage(SpellTagCandidates(PrimaryLang(typing), typing));
        input_.RecheckSpelling();
    }
    spell_.UseLearnedLanguage(PrimaryLang(typing), cfg_.LearnedDir());
    input_.RefreshSuggestions();
    backText_.clear();
    ++inputGen_;
    RefreshGlossary();
    StartTranslation();
    InvalidateChrome();
}

void KeyboardWindow::OnKeyboardLanguage(const std::wstring& locale) {
    if (locale.empty() || locale == kbdLocale_) return;
    kbdLocale_ = locale;
    kbdRtl_ = IsRtlLanguage(PrimaryLang(locale));
    if (SanitizeChatText(input_.Text()).empty()) input_.SetRtl(kbdRtl_);
    if (!cfg_.writeIn.empty()) return;  // a fixed writing language: the keyboard layout does not change it
    ApplyTypingLanguage();
}

std::wstring KeyboardWindow::ChannelChipText() const {
    const std::wstring text = SanitizeChatText(input_.Hwnd() ? input_.Text() : L"");
    if (!text.empty() && text[0] == L'/') return Tr(L"Command");
    if (channel_ == Channel::Whisper) return whisperTarget_.empty() ? Tr(L"Reply (/r)") : TrF(L"To {1}", {whisperTarget_});
    return channel_ == Channel::Unknown ? Tr(L"Active channel") : ChannelLabel(channel_);
}

std::wstring KeyboardWindow::TypeChipText() const {
    const LangInfo* l = FindLanguage(TypingLocale());
    if (!l) l = FindLanguage(PrimaryLang(TypingLocale()));
    return TrF(L"Write: {1}", {l ? std::wstring(l->native) : TypingLocale()});
}

std::wstring KeyboardWindow::SendChipText() const {
    if (SendOriginal()) return Tr(L"Send: original (only corrected)");
    const LangInfo* l = FindLanguage(SendLang());
    return TrF(L"Send: {1}", {l ? std::wstring(l->native) : SendLang()});
}

void KeyboardWindow::ShowChannelMenu() {
    if (partIdx_ > 0) {
        SetStatus(Tr(L"Send the remaining parts first – or change the text"), Tone::Warn, 4000);
        return;
    }
    const ModalScope modal;
    enum : UINT { kActive = 1, kReply = 2, kChannelBase = 10 };
    static const Channel kChannels[] = {Channel::Say, Channel::Map, Channel::Party, Channel::Squad, Channel::Team,
                                        Channel::Guild};
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | (channel_ == Channel::Unknown ? MF_CHECKED : 0), kActive,
                Tr(L"Active channel (as chosen in GW2)").c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    for (size_t i = 0; i < std::size(kChannels); ++i) {
        const std::wstring label = ChannelLabel(kChannels[i]) + L"\t" + ChannelCommand(kChannels[i]);
        AppendMenuW(menu, MF_STRING | (channel_ == kChannels[i] ? MF_CHECKED : 0), kChannelBase + i, label.c_str());
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    const bool whisper = channel_ == Channel::Whisper;
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
    if (cmd == kActive) channel_ = Channel::Unknown;
    else if (cmd >= kChannelBase && cmd - kChannelBase < std::size(kChannels)) channel_ = kChannels[cmd - kChannelBase];
    else if (cmd == kReply) {
        whisperTarget_.clear();
        channel_ = Channel::Whisper;
    } else if (cmd >= kCmdPartnerBase && cmd - kCmdPartnerBase < whisperers_.size()) {
        whisperTarget_ = whisperers_[cmd - kCmdPartnerBase];
        channel_ = Channel::Whisper;
    }
    if (cmd) {
        if (PreviewIsCurrent()) {
            const ChatSplit split = SplitChatCommand(SanitizeChatText(input_.Text()));
            if (split.prefix.empty()) SetPreviewBody(ComposePrefix(), previewBody_);
        }
        UpdatePreview();
        InvalidateChrome();
    }
    SetFocus(input_.Hwnd());
}

// The language you type in: only this text is corrected, suggested and learned.
void KeyboardWindow::ShowTypeLangMenu() {
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
    }
    SetFocus(input_.Hwnd());
}

// What goes into the chat: only languages GW2 can show (Latin script), or the original.
void KeyboardWindow::ShowSendMenu() {
    const ModalScope modal;
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, Tr(L"Send as:").c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    for (size_t i = 0; i < sendLangs_.size(); ++i) {
        const LangInfo* l = FindLanguage(sendLangs_[i]);
        if (l && !l->latinScript && i != sendIdx_) continue;
        AppendMenuW(menu, MF_STRING | (i == sendIdx_ ? MF_CHECKED : 0), kCmdFavBase + i,
                    (l ? LangMenuLabel(*l) : sendLangs_[i]).c_str());
    }
    AppendMenuW(menu, MF_STRING | (SendOriginal() ? MF_CHECKED : 0), kCmdOriginal,
                Tr(L"Original (only corrected)\tCtrl+Enter").c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    HMENU more = CreatePopupMenu();
    const auto& langs = Languages();
    for (size_t i = 0, shown = 0; i < langs.size(); ++i) {
        if (!langs[i].latinScript) continue;
        UINT flags = MF_STRING;
        if (shown > 0 && shown % 20 == 0) flags |= MF_MENUBARBREAK;
        ++shown;
        AppendMenuW(more, flags, kCmdMoreBase + i, LangMenuLabel(langs[i]).c_str());
    }
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(more), Tr(L"More languages").c_str());
    POINT pt{sendRect_.left, sendRect_.top};
    ClientToScreen(hwnd_, &pt);
    const UINT cmd = static_cast<UINT>(TrackPopupMenu(menu, MenuFlags(TPM_RETURNCMD | TPM_NONOTIFY | TPM_BOTTOMALIGN),
                                                      pt.x, pt.y, 0, hwnd_, nullptr));
    DestroyMenu(menu);
    if (cmd == kCmdOriginal) {
        SetSendIndex(sendLangs_.size());
    } else if (cmd >= kCmdFavBase && cmd - kCmdFavBase < sendLangs_.size()) {
        SetSendIndex(cmd - kCmdFavBase);
    } else if (cmd >= kCmdMoreBase && cmd - kCmdMoreBase < langs.size()) {
        const std::wstring code = langs[cmd - kCmdMoreBase].code;
        auto it = std::find(sendLangs_.begin(), sendLangs_.end(), code);
        if (it == sendLangs_.end()) {
            sendLangs_.push_back(code);
            std::wstring joined;
            for (const std::wstring& c : sendLangs_) joined += (joined.empty() ? L"" : L",") + c;
            cfg_.SaveValue(L"Translate", L"WriteLangs", joined);
            it = sendLangs_.end() - 1;
        }
        SetSendIndex(static_cast<size_t>(it - sendLangs_.begin()));
    }
    SetFocus(input_.Hwnd());
}

void KeyboardWindow::ShowMainMenu() {
    const ModalScope modal;
    enum : UINT { kSettings = 1, kDock, kQuit, kChat, kUiLangBase = 100 };
    auto check = [](bool on) { return static_cast<UINT>(on ? MF_CHECKED : MF_UNCHECKED); };
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kChat, Tr(L"Activate chat translation (reads the GW2 chat) …").c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | check(cfg_.dock), kDock, Tr(L"Dock to GW2 (moves with it)").c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kSettings, Tr(L"Settings …").c_str());
    HMENU ui = CreatePopupMenu();
    for (size_t i = 0; i < UiLanguages().size(); ++i)
        AppendMenuW(ui, MF_STRING | check(UiLanguages()[i].lang == cfg_.uiLang), kUiLangBase + static_cast<UINT>(i),
                    UiLanguages()[i].native);
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(ui), L"Language / Sprache / اللغة");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kQuit, Tr(L"Quit").c_str());
    POINT pt{menuRect_.left, menuRect_.bottom};
    ClientToScreen(hwnd_, &pt);
    const UINT cmd =
        static_cast<UINT>(TrackPopupMenu(menu, MenuFlags(TPM_RETURNCMD | TPM_NONOTIFY), pt.x, pt.y, 0, hwnd_, nullptr));
    DestroyMenu(menu);
    if (cmd == kSettings) OpenSettings(SettingsPage::Writing);
    else if (cmd == kDock) SetDock(!cfg_.dock);
    else if (cmd == kChat) ActivateChatTranslation();
    else if (cmd == kQuit) PostMessageW(hwnd_, WM_CLOSE, 0, 0);
    else if (cmd >= kUiLangBase && cmd < kUiLangBase + UiLanguages().size()) SetUiLanguage(UiLanguages()[cmd - kUiLangBase].lang);
}

void KeyboardWindow::ActivateChatTranslation() {
    // Next to this program (the download) or the installed copy.
    std::wstring exe = CurrentExePath();
    exe = exe.substr(0, exe.find_last_of(L"\\/") + 1) + L"GW2ChatTranslator.exe";
    if (GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) exe = FindInstalledExe();
    if (exe.empty() || GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        SetStatus(Tr(L"The chat translation is part of GW2 Chat Translator – not found next to this program"),
                  Tone::Warn, 8000);
        return;
    }
    spell_.SaveLearned();
    cfg_.SaveBool(L"Basic", L"KeyboardOnly", false);  // the full tool starts with reading on
    cfg_.SaveWindowRect(hwnd_, theme_.scale);
    if (reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", exe.c_str(), L"--restarted", nullptr, SW_SHOWNORMAL)) <= 32) {
        SetStatus(Tr(L"The chat translation is part of GW2 Chat Translator – not found next to this program"),
                  Tone::Warn, 8000);
        return;
    }
    PostMessageW(hwnd_, WM_CLOSE, 0, 0);
}

void KeyboardWindow::SetUiLanguage(UiLang lang) {
    cfg_.uiLang = lang;
    cfg_.uiLangCode = UiLangCode(lang);
    cfg_.SaveValue(L"General", L"UiLanguage", cfg_.uiLangCode);
    SetUiLang(lang);
    UpdatePreview();
    InvalidateRect(hwnd_, nullptr, FALSE);
    UpdateTrayTip();
    SetStatus(Tr(L"Language changed"), Tone::Ok, 3000);
}

// ===========================================================================
// Glossary and your words
// ===========================================================================
void KeyboardWindow::RefreshGlossary() {
    glossary_ = Glossary();
    if (!cfg_.glossaryEnabled || !warmedUp_) return;
    const std::string src = Gw2ApiLang(PrimaryLang(TypingLocale()));
    if (src.empty()) return;
    const bool haveSrc = EnsureNames(src);
    if (SendOriginal()) return;
    const std::string tgt = Gw2ApiLang(SendLang());
    if (tgt.empty() || tgt == src) return;
    const bool haveTgt = EnsureNames(tgt);
    if (haveSrc && haveTgt) glossary_ = Glossary::Build(names_.at(src), names_.at(tgt));
}

bool KeyboardWindow::EnsureNames(const std::string& lang) {
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

void KeyboardWindow::StartNameFetch(const std::string& lang) {
    if (fetching_.count(lang) || fetchFailed_.count(lang)) return;
    fetching_.insert(lang);
    std::thread([hwnd = hwnd_, lang] {
        auto msg = std::make_unique<KbdNamesMsg>();
        msg->lang = lang;
        msg->result = FetchGw2Names(lang);
        if (PostMessageW(hwnd, WM_APP_NAMES, 0, reinterpret_cast<LPARAM>(msg.get()))) msg.release();
    }).detach();
}

void KeyboardWindow::OnNamesFetched(KbdNamesMsg* raw) {
    std::unique_ptr<KbdNamesMsg> m(raw);
    fetching_.erase(m->lang);
    if (!m->result.ok) {
        fetchFailed_.insert(m->lang);
        return;
    }
    WriteFileAtomic(NameCachePath(cfg_, m->lang), SerializeNameTable(m->result.names));
    names_[m->lang] = std::move(m->result.names);
    RefreshGlossary();
    UpdateSpellWords();
}

void KeyboardWindow::UpdateSpellWords() {
    WordSet words;  // official names in every loaded language: never marked as spelling errors
    for (const auto& kv : names_) CollectNameWords(kv.second, words);
    spell_.SetGameWords(std::move(words));
}

void KeyboardWindow::ExplainWord(const std::wstring& word) {
    std::wstring meaning = myWords_.MeaningOf(word);
    if (!AskWordMeaning(hwnd_, inst_, word, &meaning)) return;
    myWords_.Set(word, meaning);
    spell_.AddUserWord(word);
    WriteFileAtomic(cfg_.dataDir + L"\\my-words.txt",
                    ToUtf8(L"# GW2 Chat Translator: your words, \"word = meaning\" per line (replaced before translating)\r\n" +
                           myWords_.Serialize()));
    ++inputGen_;
    StartTranslation();
    SetStatus(meaning.empty() ? TrF(L"“{1}” is now a correct word", {word})
                              : TrF(L"Got it: “{1}” means “{2}” – used before translating", {word, meaning}),
              Tone::Ok, 5000);
}

// ===========================================================================
// Writing, translating, sending
// ===========================================================================
std::wstring KeyboardWindow::ComposePrefix() const {
    if (channel_ == Channel::Whisper) return whisperTarget_.empty() ? L"/r " : L"/w " + whisperTarget_ + L", ";
    if (const wchar_t* cmd = ChannelCommand(channel_)) return std::wstring(cmd) + L" ";
    return L"";
}

bool KeyboardWindow::PreviewIsCurrent() const { return previewOk_ && previewGen_ == inputGen_; }

void KeyboardWindow::ResetCompose() {
    previewOk_ = false;
    sendPending_ = false;
    parts_.clear();
    partIdx_ = 0;
    backText_.clear();
}

void KeyboardWindow::OnInputChanged() {
    input_.OnTextChanged();
    ++inputGen_;
    ResetCompose();
    KillTimer(hwnd_, kTimerDebounce);
    const std::wstring text = SanitizeChatText(input_.Text());
    input_.SetRtl(text.empty() ? kbdRtl_ : IsRtlText(text));
    if (text.empty()) {
        previewBody_.clear();
        previewPrefix_.clear();
        lastHits_.clear();
        if (tone_ != Tone::Ok) SetStatus(L"", Tone::Muted);
    }
    UpdatePreview();
    InvalidateChrome();
    if (!text.empty()) SetTimer(hwnd_, kTimerDebounce, Translating() ? static_cast<UINT>(cfg_.debounceMs) : 120, nullptr);
}

void KeyboardWindow::SetPreviewBody(const std::wstring& prefix, const std::wstring& body) {
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

void KeyboardWindow::StartTranslation() {
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
    // Same language in and out (or "only corrected", "/wave"): nothing to translate, only the typing help runs.
    if (!Translating() || body.empty()) {
        lastHits_.clear();
        finish(body);
        return;
    }
    if (inflightGen_ == inputGen_) return;  // already on its way
    // Your words ("finds") go to the translator as what they mean; spoken German ("habs") written out.
    std::wstring toTranslate = myWords_.Expand(body);
    if (PrimaryLang(TypingLocale()) == L"de") toTranslate = ExpandGermanContractions(toTranslate);
    ProtectedText p = ProtectForTranslation(toTranslate, glossary_.Empty() ? nullptr : &glossary_, &spell_.KeepWords());
    lastHits_ = p.glossaryHits;
    if (!HasTranslatableText(p.segments)) {  // only names, codes, keep-words
        finish(JoinSegments(p.segments));
        return;
    }
    if (std::wstring fixed; corrections_.Lookup(body, SendLang(), &fixed)) {  // you corrected this message once
        finish(fixed);
        return;
    }
    std::wstring source;  // MyMemory needs a source language; the others detect it better themselves
    if (engine_ == Engine::Basic) {
        source = DetectLanguage(body);
        if (source.empty()) source = PrimaryLang(TypingLocale());
    }
    if (engine_ == Engine::Llm) {  // one request at a time: newer text waits
        if (llmBusy_) {
            llmAgain_ = true;
            return;
        }
        llmBusy_ = true;
    }
    inflightGen_ = inputGen_;
    CountMyMemory(CodePointCount(JoinSegments(p.segments)));
    UpdatePreview();
    std::thread([hwnd = hwnd_, gen = inputGen_, translator = translator_, segments = std::move(p.segments), source,
                 target = SendLang()] {
        auto msg = std::make_unique<KbdTranslatedMsg>();
        msg->kind = KbdTranslatedMsg::Kind::Forward;
        msg->gen = gen;
        msg->result = translator->Translate(segments, source, target);
        if (PostMessageW(hwnd, WM_APP_TRANSLATED, 0, reinterpret_cast<LPARAM>(msg.get()))) msg.release();
    }).detach();
}

void KeyboardWindow::OnTranslated(KbdTranslatedMsg* raw) {
    std::unique_ptr<KbdTranslatedMsg> m(raw);
    switch (m->kind) {
        case KbdTranslatedMsg::Kind::Forward: {
            if (inflightGen_ == m->gen) inflightGen_ = 0;
            llmBusy_ = false;
            if (llmAgain_) {
                llmAgain_ = false;
                if (m->gen != inputGen_) {
                    StartTranslation();
                    return;
                }
            }
            if (m->gen != inputGen_) return;
            if (!m->result.ok) {
                sendPending_ = false;
                previewOk_ = false;
                UpdatePreview();
                SetStatus(m->result.error, Tone::Error);
                return;
            }
            const ChatSplit split = SplitChatCommand(SanitizeChatText(input_.Text()));
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
        case KbdTranslatedMsg::Kind::Back:
            if (m->gen != inputGen_ || !PreviewIsCurrent()) return;
            backText_ = m->result.ok ? SanitizeChatText(m->result.text) : L"";
            backGen_ = m->gen;
            UpdatePreview();
            return;
        case KbdTranslatedMsg::Kind::Romanize:
            if (m->gen != inputGen_ || !PreviewIsCurrent()) return;
            if (!m->result.ok) {
                SetStatus(m->result.error, Tone::Warn, 7000);
                return;
            }
            SetPreviewBody(previewPrefix_, SanitizeChatText(m->result.text));
            SetStatus(Tr(L"In Latin letters – Enter sends it"), Tone::Ok, 4000);
            return;
    }
}

// The check of what goes into the chat, in the language you write in.
void KeyboardWindow::StartBackTranslation() {
    if (!cfg_.backTranslate || !PreviewIsCurrent() || !Translating() || previewBody_.empty()) return;
    ProtectedText p = ProtectForTranslation(previewBody_, nullptr, &spell_.KeepWords());
    if (!HasTranslatableText(p.segments)) return;
    CountMyMemory(CodePointCount(previewBody_));
    std::thread([hwnd = hwnd_, gen = inputGen_, translator = translator_, segments = std::move(p.segments),
                 source = SourceCode(SendLang()), target = PrimaryLang(TypingLocale())] {
        auto msg = std::make_unique<KbdTranslatedMsg>();
        msg->kind = KbdTranslatedMsg::Kind::Back;
        msg->gen = gen;
        msg->result = translator->Translate(segments, source, target);
        if (PostMessageW(hwnd, WM_APP_TRANSLATED, 0, reinterpret_cast<LPARAM>(msg.get()))) msg.release();
    }).detach();
}

// Ctrl+U: the message in Latin letters (Windows for Cyrillic / Devanagari, the optional LLM for the rest).
void KeyboardWindow::Romanize() {
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
        SetStatus(Tr(L"In Latin letters – Enter sends it"), Tone::Ok, 4000);
        return;
    }
    if (!llm_) {
        SetStatus(TrF(L"Latin letters for {1} need the optional LLM (≡ → Settings → Translator)", {script}),
                  Tone::Warn, 7000);
        return;
    }
    SetStatus(Tr(L"Writing it in Latin letters (LLM) …"), Tone::Muted);
    std::thread([hwnd = hwnd_, gen = inputGen_, llm = llm_, text = previewBody_] {
        auto msg = std::make_unique<KbdTranslatedMsg>();
        msg->kind = KbdTranslatedMsg::Kind::Romanize;
        msg->gen = gen;
        msg->result = llm->Romanize(text);
        if (PostMessageW(hwnd, WM_APP_TRANSLATED, 0, reinterpret_cast<LPARAM>(msg.get()))) msg.release();
    }).detach();
}

void KeyboardWindow::UpdatePreview() {
    PreviewView::Content c;
    c.placeholder = Tr(L"Write in your language – here you see what arrives in the GW2 chat.") + L"  " +
                    (cfg_.copyOnly ? Tr(L"Enter copies") : Tr(L"Enter sends"));
    const std::wstring text = SanitizeChatText(input_.Hwnd() ? input_.Text() : L"");
    if (text.empty()) {
        preview_.Set(std::move(c));
        return;
    }
    grammarOffer_ = false;
    if (PreviewIsCurrent() && !parts_.empty()) {
        const size_t idx = std::min(partIdx_, parts_.size() - 1);
        c.text = parts_[idx];
        c.current = true;
        if (backGen_ == inputGen_) c.back = backText_;
        const std::wstring script = UnsupportedScript(previewBody_);
        if (parts_.size() > 1)
            c.note = TrF(L"Part {1}/{2} – every Enter sends one part",
                         {std::to_wstring(idx + 1), std::to_wstring(parts_.size())});
        if (!script.empty()) {
            c.note = (c.note.empty() ? L"" : c.note + L"  ·  ") +
                     TrF(L"GW2 probably cannot show {1} – Ctrl+U: Latin letters", {script});
            c.warn = true;
        } else if (c.note.empty()) {
            c.note = HitsText(lastHits_);
        }
        // Translating and the grammar check is off: offered once the sentence stands – in the warning colour when the
        // back-translation says something quite different (a click on the preview switches it on).
        if (c.note.empty() && !cfg_.languageTool && Translating()) {
            const std::wstring typed = Trim(SplitChatCommand(text).body);
            const bool sentence = !typed.empty() && (typed.back() == L'.' || typed.back() == L'!' || typed.back() == L'?');
            const bool differs = backGen_ == inputGen_ && !backText_.empty() && !typed.empty() &&
                                 DiceSimilarity(NormalizeForCompare(backText_), NormalizeForCompare(typed)) < 0.5;
            if (sentence || differs) {
                c.note = differs ? Tr(L"The back-translation differs a lot – click: switch on the grammar check")
                                 : Tr(L"Click: switch on the grammar check (sentence structure)");
                c.warn = differs;
                grammarOffer_ = true;
            }
        }
    } else {
        c.text = previewBody_.empty() ? ComposePrefix() + text : previewPrefix_ + previewBody_;
        c.current = false;
        if (inflightGen_ == inputGen_) c.note = Tr(L"translating …");
    }
    preview_.Set(std::move(c));
}

void KeyboardWindow::OnEnter(bool sendOriginal) {
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
    if (sendPending_ && inflightGen_ == inputGen_) SetStatus(Tr(L"translating and sending …"), Tone::Muted);
}

void KeyboardWindow::SendNextPart() {
    if (parts_.empty() || partIdx_ >= parts_.size()) return;
    const std::wstring line = parts_[partIdx_];
    const size_t n = CodePointCount(line);
    if (n > static_cast<size_t>(cfg_.maxLength)) {
        SetStatus(TrF(L"Too long for the GW2 chat ({1}/{2}) – please shorten it",
                      {std::to_wstring(n), std::to_wstring(cfg_.maxLength)}),
                  Tone::Warn);
        return;
    }
    if (!DoSend(line)) return;
    if (partIdx_ == 0) {
        CountTyping();
        input_.AddHistory(input_.Text());  // Up brings it back
        // A whisper you typed yourself: the name goes into the channel menu.
        const ChatSplit split = SplitChatCommand(SanitizeChatText(input_.Text()));
        if (split.prefix.rfind(L"/w ", 0) == 0) {
            std::wstring name = Trim(split.prefix.substr(3));
            if (!name.empty() && name.back() == L',') name.pop_back();
            name = Trim(name);
            if (!name.empty()) {
                whisperers_.erase(std::remove(whisperers_.begin(), whisperers_.end(), name), whisperers_.end());
                whisperers_.push_front(name);
                while (whisperers_.size() > 8) whisperers_.pop_back();
            }
        }
        if (cfg_.learnWords) spell_.Learn(split.prefix.empty() ? SanitizeChatText(input_.Text()) : split.body);
    }
    ++partIdx_;
    if (partIdx_ >= parts_.size()) {
        input_.Clear();
        OnInputChanged();  // multi-line EDITs send no EN_CHANGE for WM_SETTEXT
        return;
    }
    UpdatePreview();
    InvalidateChrome();
    if (cfg_.copyOnly) {
        SetStatus(TrF(L"Part {1}/{2} copied – paste it in GW2, then Enter here for the next one",
                      {std::to_wstring(partIdx_), std::to_wstring(parts_.size())}),
                  Tone::Ok);
        return;
    }
    SetStatus(TrF(L"Part {1}/{2} sent – Enter sends the next one",
                  {std::to_wstring(partIdx_), std::to_wstring(parts_.size())}),
              Tone::Ok);
    if (Front(hwnd_)) SetFocus(input_.Hwnd());
}

bool KeyboardWindow::DoSend(const std::wstring& line) {
    if (cfg_.copyOnly) {  // copy only: not a single key goes to the game
        if (!CopyTextToClipboard(hwnd_, line)) {
            SetStatus(Tr(L"The clipboard is busy – press Enter again"), Tone::Error);
            return false;
        }
        SetStatus(Tr(L"Copied – in GW2: Enter · Ctrl+V · Enter"), Tone::Ok, 8000);
        if (gw2_ && IsWindow(gw2_)) Front(gw2_);
        return true;
    }
    SetStatus(Tr(L"sending …"), Tone::Muted);
    UpdateWindow(hwnd_);
    const SendOutcome out = SendToGw2Chat(hwnd_, line, SendTiming(), mumbleState_.live ? &mumble_ : nullptr);
    if (!out.ok) {
        SetStatus(out.error, Tone::Error);
        return false;
    }
    lastOpenMs_ = out.openMs;
    lastConfirmMs_ = out.confirmMs;
    if (out.confirmedByGame) ++sendsConfirmed_;
    ++sends_;
    if (out.clipboardRestored) SetStatus(Tr(L"Sent."), Tone::Ok, 3000);
    else
        SetStatus(Tr(L"Sent – GW2 did not confirm, so your old clipboard was not put back (to be safe)"), Tone::Warn,
                  8000);
    if (cfg_.returnFocus && Front(hwnd_)) SetFocus(input_.Hwnd());
    return true;
}

// The word help's benefit: key presses for this message vs. its letters (pasted texts are not counted).
void KeyboardWindow::CountTyping() {
    if (input_.Pasted() || input_.KeyPresses() <= 0) return;
    const ChatSplit split = SplitChatCommand(SanitizeChatText(input_.Text()));
    const int chars = static_cast<int>((split.prefix.empty() ? input_.Text() : split.body).size());
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

void KeyboardWindow::CountMyMemory(size_t chars) {
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

std::wstring KeyboardWindow::MyMemoryQuotaText() const {
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

void KeyboardWindow::ShowMyMemoryNotice() {
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
        Tr(L"Privacy notice").c_str(), MB_YESNO | MB_ICONINFORMATION | (UiRtl() ? MB_RTLREADING | MB_RIGHT : 0));
    if (answer == IDNO) OpenSettings(SettingsPage::Translator);
}

// Key timing from the measured frame rate: every action one frame + 10 ms (GW2 reads the keyboard once per frame).
SendOptions KeyboardWindow::SendTiming() const {
    SendOptions so = cfg_.send;
    if (gameFps_ <= 0 || so.keyHoldMs != 30) return so;
    const int action = std::clamp(static_cast<int>(std::ceil(1000.0 / gameFps_)) + 10, 15, 1010);
    so.keyHoldMs = action;
    if (so.stepDelayMs == 80) so.stepDelayMs = action;
    return so;
}

// Optional LanguageTool check (sentence structure only) a moment after you stop typing; it only marks words.
void KeyboardWindow::StartGrammarCheck() {
    KillTimer(hwnd_, kTimerGrammar);
    if (!cfg_.languageTool || grammarInFlight_) return;
    const std::wstring text = input_.Text();
    if (CodePointCount(SanitizeChatText(text)) < 8) return;
    if (!grammarLimiter_.Allow(GetTickCount64())) return;
    grammarInFlight_ = true;
    std::thread([hwnd = hwnd_, url = cfg_.languageToolUrl, text, lang = TypingLocale()] {
        auto msg = std::make_unique<KbdGrammarMsg>();
        msg->text = text;
        msg->result = CheckWithLanguageTool(url, text, lang, L"");
        if (PostMessageW(hwnd, WM_APP_GRAMMAR, 0, reinterpret_cast<LPARAM>(msg.get()))) msg.release();
    }).detach();
}

void KeyboardWindow::OnGrammar(KbdGrammarMsg* raw) {
    std::unique_ptr<KbdGrammarMsg> m(raw);
    grammarInFlight_ = false;
    if (!m->result.ok) {
        if (BackgroundNoticeAllowed()) SetStatus(m->result.error, Tone::Muted, 5000);
        return;
    }
    std::vector<SpellIssue> issues;
    for (const LtMatch& x : m->result.matches) {
        if (x.Spelling()) continue;
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
// Game, docking, tray, settings
// ===========================================================================
RECT KeyboardWindow::GameClientRect() const {
    RECT client{};
    if (!gw2_ || !GetClientRect(gw2_, &client)) return {};
    POINT origin{0, 0};
    ClientToScreen(gw2_, &origin);
    OffsetRect(&client, origin.x, origin.y);
    return client;
}

void KeyboardWindow::PollGame() {
    mumbleState_ = mumble_.Read();
    // Frame rate from MumbleLink's frame counter every second on a map: a drop counts at once, a rise slowly.
    if (mumbleState_.inMap) {
        const ULONGLONG t = GetTickCount64();
        if (fpsSince_ == 0) {
            fpsSince_ = t;
            fpsTick_ = mumbleState_.uiTick;
        } else if (t - fpsSince_ >= 1000) {
            const double fps = static_cast<double>(mumbleState_.uiTick - fpsTick_) * 1000.0 / static_cast<double>(t - fpsSince_);
            if (fps >= 1 && fps <= 1000) gameFps_ = gameFps_ <= 0 || fps < gameFps_ ? fps : 0.7 * gameFps_ + 0.3 * fps;
            fpsSince_ = t;
            fpsTick_ = mumbleState_.uiTick;
        }
    } else {
        fpsSince_ = 0;
    }
    if (gw2_ && !IsWindow(gw2_)) gw2_ = nullptr;
    const ULONGLONG now = GetTickCount64();
    const bool hadGame = gw2_ != nullptr;
    if (!gw2_ && now - lastFind_ > 2000) {
        lastFind_ = now;
        gw2_ = FindGw2Window(mumbleState_.live ? mumbleState_.processId : 0);
    }
    if (hadGame != (gw2_ != nullptr)) InvalidateChrome();
    // Started with Windows: appear in the 3D world (MumbleLink carries a character), go when the game closes.
    const bool ready = gw2_ != nullptr && mumbleState_.live;
    if (waitForGame_ && (hadGame != (gw2_ != nullptr) || ready != gameReady_)) {
        UpdateTrayTip();
        if (ready && !gameReady_ && !userHidden_) {
            WarmUp();
            ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
            autoHidden_ = false;
        } else if (!gw2_) {
            ShowWindow(hwnd_, SW_HIDE);
        }
    }
    gameReady_ = ready;
    // Docked: keep our place relative to the game's bottom-left corner.
    if (cfg_.dock && cfg_.dockSet && gw2_ && !IsIconic(gw2_) && !moving_) {
        const RECT want = DockTarget();
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
    // Optional: the GW2 chat line opens (MumbleLink) -> we take the keyboard.
    const bool textboxFocus = mumbleState_.TextboxHasFocus();
    if (cfg_.focusOnGameChat && textboxFocus && !lastTextboxFocus_ && gw2_ && (gameFront || ours)) ShowWindowNow();
    lastTextboxFocus_ = textboxFocus;
    // Like part of the game: visible while GW2 or this window is in front.
    if (cfg_.followGame && gw2_ && !userHidden_) {
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
}

RECT KeyboardWindow::DockTarget() const {
    const RECT client = GameClientRect();
    if (IsRectEmpty(&client)) return {};
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

void KeyboardWindow::UpdateDockFromWindow() {
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

void KeyboardWindow::SetDock(bool on) {
    if (on && !gw2_) gw2_ = FindGw2Window(mumbleState_.live ? mumbleState_.processId : 0);
    if (on && !gw2_) {
        SetStatus(Tr(L"GW2 not found – docking works once the game runs"), Tone::Warn, 5000);
        return;
    }
    cfg_.dock = on;
    if (on) {
        UpdateDockFromWindow();
        SetStatus(Tr(L"Docked – the window moves with GW2"), Tone::Ok, 4000);
    } else {
        cfg_.SaveDock();
        cfg_.SaveWindowRect(hwnd_, theme_.scale);
        SetStatus(Tr(L"Undocked"), Tone::Muted, 2500);
    }
}

void KeyboardWindow::AddTrayIcon() {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd_;
    nid.uID = 1;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_APP_TRAY;
    nid.hIcon = LoadIconW(inst_, MAKEINTRESOURCEW(1));
    if (!nid.hIcon) nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcsncpy_s(nid.szTip, kTitle, _TRUNCATE);
    trayOk_ = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
}

void KeyboardWindow::UpdateTrayTip() {
    if (!trayOk_) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd_;
    nid.uID = 1;
    nid.uFlags = NIF_TIP;
    wcsncpy_s(nid.szTip, kTitle, _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void KeyboardWindow::RemoveTrayIcon() {
    if (!trayOk_) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd_;
    nid.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    trayOk_ = false;
}

void KeyboardWindow::ShowTrayMenu() {
    enum : UINT { kShow = 1, kSettings, kQuit };
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kShow, Tr(L"Show").c_str());
    AppendMenuW(menu, MF_STRING, kSettings, Tr(L"Settings …").c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kQuit, Tr(L"Quit").c_str());
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd_);
    const UINT cmd = static_cast<UINT>(
        TrackPopupMenu(menu, MenuFlags(TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON), pt.x, pt.y, 0, hwnd_, nullptr));
    DestroyMenu(menu);
    if (cmd == kShow) ShowWindowNow();
    else if (cmd == kSettings) OpenSettings(SettingsPage::Writing);
    else if (cmd == kQuit) PostMessageW(hwnd_, WM_CLOSE, 0, 0);
}

// The same settings dialog as the full tool: every translator (MyMemory, Google, Microsoft, DeepL, own server, AI
// model local or cloud), your words, corrections, the typing profile, sending.
void KeyboardWindow::OpenSettings(SettingsPage page) {
    WarmUp();
    Config edited = cfg_;
    DialogContext ctx;
    ctx.technicalStatus = [this] { return TechnicalStatus(); };
    ctx.connectionStatus = [this] { return TechnicalStatus(); };
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
            text.assign(reinterpret_cast<const wchar_t*>(data.data() + 2), (data.size() - 2) / sizeof(wchar_t));
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
    ctx.correctionsInfo = [this] {
        return TrF(L"{1} lines, {2} phrases", {std::to_wstring(corrections_.Lines()), std::to_wstring(corrections_.Phrases())});
    };
    ctx.clearCorrections = [this] {
        corrections_.Clear();
        WriteFileAtomic(cfg_.dataDir + L"\\corrections.txt", corrections_.Serialize());
    };
    ctx.myWordsText = [this] { return myWords_.Serialize(); };
    ctx.setMyWords = [this](const std::wstring& text) {
        myWords_.Parse(text);
        for (const auto& e : myWords_.Entries()) spell_.AddUserWord(e.first);
        WriteFileAtomic(cfg_.dataDir + L"\\my-words.txt",
                        ToUtf8(L"# GW2 Chat Translator: your words, \"word = meaning\" per line (replaced before translating)\r\n" +
                               myWords_.Serialize()));
    };
    const DialogResult r = ShowSettingsDialog(hwnd_, inst_, edited, ctx, page);
    if (r.saved) ApplySettings(edited);
}

void KeyboardWindow::ApplySettings(const Config& next) {
    const Config prev = cfg_;
    Config c = next;
    c.x = prev.x, c.y = prev.y, c.w = prev.w, c.h = prev.h;
    cfg_ = c;
    spell_.SetLearnChoices(cfg_.learnWords);
    if (prev.uiLang != cfg_.uiLang) SetUiLanguage(cfg_.uiLang);
    if (prev.fontPercent != cfg_.fontPercent || prev.fontFace != cfg_.fontFace) ApplyDpi(theme_.dpi, nullptr);
    if (prev.opacity != cfg_.opacity) SetLayeredWindowAttributes(hwnd_, 0, static_cast<BYTE>(cfg_.opacity), LWA_ALPHA);
    if (prev.hotkey != cfg_.hotkey) {
        if (hotkeyOk_) UnregisterHotKey(hwnd_, kHotkeyId);
        hotkeyOk_ = false;
        if (auto hk = ParseHotkey(cfg_.hotkey))
            hotkeyOk_ = RegisterHotKey(hwnd_, kHotkeyId, hk->mods | MOD_NOREPEAT, hk->vk) != FALSE;
    }
    if (prev.writeLangs != cfg_.writeLangs) {
        sendLangs_.clear();
        for (const std::wstring& code : cfg_.writeLangs)
            if (const LangInfo* l = FindLanguage(code)) sendLangs_.push_back(l->code);
        if (sendLangs_.empty()) sendLangs_ = {L"EN-GB"};
        sendIdx_ = 0;
    }
    if (prev.writeIn != cfg_.writeIn) ApplyTypingLanguage();
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
    }
    input_.SetAutoCorrect(cfg_.autoCorrect);
    if (prev.suggestions != cfg_.suggestions) {
        input_.SetSuggestions(cfg_.suggestions);
        Layout();
    }
    if (prev.dock != cfg_.dock) SetDock(cfg_.dock);
    if (!cfg_.followGame && autoHidden_) {
        autoHidden_ = false;
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    }
    ++inputGen_;
    ResetCompose();
    RefreshGlossary();
    StartTranslation();
    UpdatePreview();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

std::wstring KeyboardWindow::TechnicalStatus() {
    std::wstring s;
    auto line = [&](const std::wstring& t) { s += t + L"\r\n"; };
    line(kTitle);
    line(gw2_ ? L"[OK] " + Tr(L"GW2 window found") : L"[--] " + Tr(L"GW2 window not found (start the game)"));
    line(TrF(L"Translator: {1}", {translator_ ? translator_->Name() : std::wstring(L"-")}));
    const SendOptions so = SendTiming();
    line(L"  " + TrF(L"Game: {1} fps → keys held {2} ms, steps {3} ms apart",
                     {gameFps_ > 0 ? std::to_wstring(static_cast<int>(gameFps_ + 0.5)) : std::wstring(L"?"),
                      std::to_wstring(so.keyHoldMs), std::to_wstring(so.stepDelayMs)}));
    auto ms = [](int v) { return v < 0 ? std::wstring(L"?") : std::to_wstring(v); };
    line(L"  " + TrF(L"Last send: chat line open after {1} ms, message out after {2} ms · confirmed {3} of {4}",
                     {ms(lastOpenMs_), ms(lastConfirmMs_), std::to_wstring(sendsConfirmed_), std::to_wstring(sends_)}));
    if (const std::wstring q = MyMemoryQuotaText(); !q.empty()) line(L"  " + q);
    return s;
}

// ===========================================================================
// Window procedure
// ===========================================================================
LRESULT CALLBACK KeyboardWindow::Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    KeyboardWindow* self;
    if (msg == WM_NCCREATE) {
        self = static_cast<KeyboardWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = h;
    } else {
        self = reinterpret_cast<KeyboardWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    }
    return self ? self->Handle(msg, wp, lp) : DefWindowProcW(h, msg, wp, lp);
}

LRESULT KeyboardWindow::Handle(UINT msg, WPARAM wp, LPARAM lp) {
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
            const int previewH = theme_.textLineHeight * 2 + theme_.smallLineHeight * 2 + theme_.S(14);
            mmi->ptMinTrackSize = {theme_.S(360), m.head + previewH + m.inputH + 2 * m.gap + m.foot};
            return 0;
        }
        case WM_NCHITTEST:
            return HitTest(lp);
        case WM_DPICHANGED:
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
            } else if (wp == kTimerGame) {
                PollGame();
            } else if (wp == kTimerGrammar) {
                StartGrammarCheck();
            } else if (wp == kTimerStatus) {
                KillTimer(hwnd_, kTimerStatus);
                status_.clear();
                tone_ = Tone::Muted;
                InvalidateChrome();
            }
            return 0;
        case WM_APP_TRANSLATED:
            OnTranslated(reinterpret_cast<KbdTranslatedMsg*>(lp));
            return 0;
        case WM_APP_NAMES:
            OnNamesFetched(reinterpret_cast<KbdNamesMsg*>(lp));
            return 0;
        case WM_APP_GRAMMAR:
            OnGrammar(reinterpret_cast<KbdGrammarMsg*>(lp));
            return 0;
        case WM_APP_MYMEMORY_NOTICE:
            ShowMyMemoryNotice();
            return 0;
        case WM_APP_TRAY:
            if (LOWORD(lp) == WM_LBUTTONUP || LOWORD(lp) == WM_LBUTTONDBLCLK) {
                if (IsWindowVisible(hwnd_) && GetForegroundWindow() == hwnd_) HideWindowNow();
                else ShowWindowNow();
            } else if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_CONTEXTMENU) {
                ShowTrayMenu();
            }
            return 0;
        case WM_LBUTTONUP:
            OnClick({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
            return 0;
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
                if (IsWindowVisible(hwnd_) && GetForegroundWindow() == hwnd_) HideWindowNow();
                else ShowWindowNow();
            }
            return 0;
        case WM_ACTIVATE:
            if (LOWORD(wp) != WA_INACTIVE) SetFocus(input_.Hwnd());
            return 0;
        case WM_ENTERSIZEMOVE:
            moving_ = true;
            return 0;
        case WM_EXITSIZEMOVE:
            moving_ = false;
            cfg_.SaveWindowRect(hwnd_, theme_.scale);
            if (cfg_.dock) UpdateDockFromWindow();
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd_);
            return 0;
        case WM_DESTROY:
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
