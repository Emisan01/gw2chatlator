// main_window.cpp
#include "main_window.hpp"

#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cwchar>
#include <thread>

#include "app/region_picker.hpp"
#include "core/gw2_text.hpp"
#include "core/hotkey.hpp"
#include "core/langs.hpp"
#include "core/languages.hpp"
#include "core/protect.hpp"
#include "core/slang.hpp"
#include "core/text.hpp"
#include "win/deepl_translator.hpp"
#include "win/els.hpp"
#include "win/files.hpp"
#include "win/gw2_api.hpp"
#include "win/gw2_sender.hpp"

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

struct IncomingMsg {
    std::vector<uint64_t> ids;
    std::vector<std::wstring> texts;
    std::wstring lang;
    std::vector<TranslateResult> results;
};

namespace {

constexpr wchar_t kClassName[] = L"GW2ChatTranslatorWindow";
constexpr wchar_t kTitle[] = L"GW2 Chat Translator";

constexpr UINT WM_APP_TRANSLATED = WM_APP + 1;
constexpr UINT WM_APP_NAMES = WM_APP + 2;
constexpr UINT WM_APP_SNAPSHOT = WM_APP + 3;
constexpr UINT WM_APP_INCOMING = WM_APP + 4;
constexpr UINT_PTR kTimerDebounce = 1;
constexpr UINT_PTR kTimerStatus = 2;
constexpr UINT_PTR kTimerGame = 3;
constexpr int kHotkeyId = 1;
constexpr size_t kBatchMax = 12;             // incoming lines per translation request
constexpr size_t kBatchChars = 2500;
constexpr ULONGLONG kEchoWindowMs = 180000;  // own lines coming back through OCR
constexpr ULONGLONG kErrorPauseMs = 30000;
constexpr ULONGLONG kQuotaPauseMs = 20 * 60000;  // free contingent used up
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
    return s.empty() ? s : L"Spielnamen: " + s;
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

}  // namespace

// ===========================================================================
// Setup
// ===========================================================================
int MainWindow::Run(HINSTANCE inst) {
    inst_ = inst;
    cfg_.Load(DataDir());

    HDC screen = GetDC(nullptr);
    const int dpi = GetDeviceCaps(screen, LOGPIXELSY);
    ReleaseDC(nullptr, screen);
    theme_.Create(dpi);
    InitServices();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = Proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);
    ChatLogView::Register(inst);
    PreviewView::Register(inst);

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
        SetStatus(L"\u00dcbersetzer: Basis (MyMemory, kostenlos) \u00b7 beste Qualit\u00e4t: DeepL-Key oder lokales LLM in "
                  L"der ini",
                  Tone::Muted, 9000);
    else
        SetStatus(L"\u00dcbersetzer: " + translator_->Name(), Tone::Ok, 5000);
    if (cfg_.spellEnabled && !spell_.Ready())
        SetStatus(L"Rechtschreibpr\u00fcfung f\u00fcr " + kbdLocale_ + L" nicht installiert (Windows-Sprachpakete)",
                  Tone::Warn, 8000);

    EnsureDir(cfg_.CacheDir());
    RefreshGlossary();
    UpdateSpellWords();
    StartReader();
    SetTimer(hwnd_, kTimerGame, 300, nullptr);
    UpdateHint();
    UpdatePreview();

    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
    SetFocus(input_.Hwnd());

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    reader_.Stop();
    theme_.Destroy();
    return static_cast<int>(m.wParam);
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

    for (const ChatTab& t : cfg_.tabs) {
        TabState st;
        st.send = SoleSendChannel(t.channels);
        tabState_.push_back(st);
        nextTabId_ = std::max(nextTabId_, t.id + 1);
    }
    tab_ = std::min(static_cast<size_t>(std::max(cfg_.activeTab, 0)), cfg_.tabs.size() - 1);

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
        std::vector<std::wstring> tags = SpellTagCandidates(PrimaryLang(kbdLocale_), kbdLocale_);
        for (const std::wstring& t : SpellTagCandidates(PrimaryLang(locale), locale)) tags.push_back(t);
        spell_.Init(tags, cfg_.UserWordsPath());
    }
}

void MainWindow::ChooseEngine(bool announce) {
    const bool haveDeepL = !cfg_.deeplKey.empty(), haveLlm = llm_ != nullptr;
    Engine e = cfg_.engine;
    std::wstring note;
    if (e == Engine::Auto) e = haveDeepL ? Engine::DeepL : haveLlm ? Engine::Llm : Engine::Basic;
    if (e == Engine::DeepL && !haveDeepL) {
        e = Engine::Basic;
        note = L"Kein DeepL-Key in der ini \u2013 nutze Basis (MyMemory)";
    }
    if (e == Engine::Llm && !haveLlm) {
        e = Engine::Basic;
        note = L"Kein LLM-Modell in der ini \u2013 nutze Basis (MyMemory)";
    }
    engine_ = e;
    if (e == Engine::DeepL) translator_ = MakeDeepLTranslator(cfg_.deeplKey);
    else if (e == Engine::Llm) translator_ = llm_;
    else translator_ = MakeMyMemoryTranslator(cfg_.basicEmail);
    inPauseUntil_ = 0;
    if (announce) {
        if (note.empty()) SetStatus(L"\u00dcbersetzer: " + translator_->Name(), Tone::Ok, 4000);
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
        SetStatus(L"Antwort an " + speaker + L" \u2013 Name stammt aus der Texterkennung, bitte pr\u00fcfen",
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
        SetStatus(L"Farbe #" + RgbToHex(rgb) + L" geh\u00f6rt jetzt zu \u201e" + ChannelLabel(ch) + L"\u201c",
                  Tone::Ok, 5000);
    };
    lcb.onHintClick = [this] { PickRegion(); };
    log_.Create(hwnd_, inst_, &theme_, std::move(lcb));
    log_.SetPalette(cfg_.palette);
    ApplyTabFilter();

    preview_.Create(hwnd_, inst_, &theme_, [this] { SetFocus(input_.Hwnd()); });

    InputBox::Callbacks cb;
    cb.onEnter = [this](bool original) { OnEnter(original); };
    cb.onEscape = [this] { ReturnToGame(); };
    cb.onCycleLang = [this] { CycleWrite(); };
    cb.onRomanize = [this] { Romanize(); };
    cb.onSwitchTab = [this] { SwitchTab((tab_ + 1) % cfg_.tabs.size()); };
    cb.onAutoCorrected = [this](const std::wstring& from, const std::wstring& to) {
        SetStatus(L"Autokorrektur: " + from + L" \u2192 " + to + L"  \u00b7  Strg+Z: r\u00fcckg\u00e4ngig", Tone::Muted, 4000);
    };
    cb.onKeyboardLanguage = [this](const std::wstring& locale) { OnKeyboardLanguage(locale); };
    input_.Create(hwnd_, inst_, &theme_, &spell_, cfg_.autoCorrect, std::move(cb));
    input_.SetRtl(kbdRtl_);
    Layout();
}

void MainWindow::StartReader() {
    if (!cfg_.readerEnabled) return;
    ReaderOptions o;
    o.intervalMs = cfg_.readerIntervalMs;
    o.ocrLanguage = cfg_.ocrLanguage;
    o.scale = cfg_.ocrScale;
    o.captureDir = cfg_.CaptureDir();
    reader_.SetSaveCaptures(cfg_.saveCaptures);
    reader_.Start(hwnd_, WM_APP_SNAPSHOT, o);
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
    const int w = std::max(10, static_cast<int>(rc.right) - 2 * m.pad);
    // Like the GW2 chat: the log takes the space; the preview appears while you type.
    const int previewH = previewVisible_ ? preview_.PreferredHeight() : 0;
    const int inputY = rc.bottom - m.foot - m.inputH;
    const int previewY = inputY - (previewVisible_ ? m.gap + previewH : 0);
    const int logH = std::max(theme_.S(40), previewY - m.gap - m.head);

    MoveWindow(log_.Hwnd(), m.pad, m.head, w, logH, TRUE);
    MoveWindow(preview_.Hwnd(), m.pad, previewY, w, std::max(1, previewH), TRUE);
    ShowWindow(preview_.Hwnd(), previewVisible_ ? SW_SHOWNA : SW_HIDE);
    MoveWindow(input_.Hwnd(), m.pad, inputY, w, m.inputH, TRUE);
    input_.ApplyPadding();
}

void MainWindow::ApplyDpi(int dpi, const RECT* suggested) {
    theme_.Destroy();
    theme_.Create(dpi);
    input_.ApplyTheme();
    log_.ThemeChanged();
    if (suggested)
        SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                     suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
    Layout();
    InvalidateRect(hwnd_, nullptr, FALSE);
    InvalidateRect(preview_.Hwnd(), nullptr, FALSE);
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
    if (!text.empty() && text[0] == L'/') return L"Befehl";
    const Channel c = SendChannel();
    if (c == Channel::Whisper) return whisperTarget_.empty() ? L"Antwort (/r)" : L"An " + whisperTarget_;
    return c == Channel::Unknown ? L"Aktiver Kanal" : ChannelLabel(c);
}

std::wstring MainWindow::WriteChipText() const {
    if (WriteOriginal()) return L"Original";
    const LangInfo* l = FindLanguage(WriteLang());
    return L"\u2192 " + (l ? std::wstring(l->native) : WriteLang());
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

    // ---- header: right side first (close, menu, reading language), tabs in the rest
    closeRect_ = {rc.right - t.S(30), 0, rc.right, m.head};
    DrawLine(dc, L"\u00d7", closeRect_, Theme::kMuted, t.fontText, DT_CENTER);
    menuRect_ = {closeRect_.left - t.S(28), 0, closeRect_.left, m.head};
    DrawLine(dc, L"\u2261", menuRect_, Theme::kMuted, t.fontText, DT_CENTER);
    const LangInfo* rl = FindLanguage(readLang_);
    const std::wstring readName = rl ? rl->native : readLang_;
    // Short form when space is tight (several tabs or a narrow window).
    const bool roomy = rc.right >= t.S(470) && cfg_.tabs.size() <= 3;
    readRect_ = DrawChip(dc, t, menuRect_.left - t.S(4), t.S(6), m.head - t.S(6),
                         roomy ? L"Lesen: " + readName : readName, Theme::kAccent, true);

    const int badgeD = t.S(15), gap = t.S(16);
    const int tabsLeft = m.pad + t.S(2), tabsRight = static_cast<int>(readRect_.left) - t.S(10);
    const size_t n = cfg_.tabs.size();
    std::vector<int> natural(n);
    int total = 0;
    for (size_t i = 0; i < n; ++i) {
        natural[i] = TextWidth(dc, cfg_.tabs[i].name, i == tab_ ? t.fontUiBold : t.fontUi) +
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
        DrawLine(dc, cfg_.tabs[i].name, r, active ? Theme::kText : Theme::kMuted, font, DT_LEFT | DT_END_ELLIPSIS);
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

    // ---- footer: channel | send-as | counter | status
    const int fy0 = rc.bottom - m.foot + t.S(4), fy1 = rc.bottom - t.S(4);
    const Channel chipChannel = SendChannel();
    channelRect_ = DrawChip(dc, t, m.pad, fy0, fy1, ChannelChipText(),
                            ChannelColorRef(cfg_.palette, chipChannel, Theme::kText), false);
    writeRect_ = DrawChip(dc, t, channelRect_.right + t.S(6), fy0, fy1, WriteChipText(),
                          WriteOriginal() ? Theme::kMuted : Theme::kAccent, false);

    std::wstring counter;
    COLORREF counterColor = Theme::kMuted;
    if (PreviewIsCurrent() && parts_.size() > 1) {
        counter = std::to_wstring(parts_.size()) + L" Teile";
        counterColor = Theme::kWarn;
    } else {
        const std::wstring line = PreviewIsCurrent() && !parts_.empty()
                                      ? parts_[std::min(partIdx_, parts_.size() - 1)]
                                      : ComposePrefix() + SanitizeChatText(input_.Text());
        const size_t n = CodePointCount(line);
        counter = std::to_wstring(n) + L"/" + std::to_wstring(cfg_.maxLength);
        if (n > static_cast<size_t>(cfg_.maxLength)) counterColor = Theme::kWarn;
    }
    RECT fr{writeRect_.right + t.S(10), rc.bottom - m.foot, rc.right - m.pad, rc.bottom};
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
        if (!hotkeyOk_) {
            text = L"Hotkey \u201e" + cfg_.hotkey + L"\u201c belegt \u2013 in der ini \u00e4ndern";
            color = Theme::kWarn;
        } else if (readingActive_) {
            text = L"\u25cf liest den Chat";
            color = Theme::kOk;
        } else if (!gw2_) {
            text = L"GW2 nicht gefunden";
        } else {
            text = cfg_.copyOnly ? L"Nur kopieren \u00b7 Esc: zur\u00fcck ins Spiel" : L"Esc: zur\u00fcck ins Spiel";
        }
    }
    DrawLine(dc, text, fr, color, t.fontUi, DT_RIGHT | DT_END_ELLIPSIS);

    BitBlt(wdc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldFont);
    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(dc);
    EndPaint(hwnd_, &ps);
}

bool MainWindow::IsClickable(POINT pt) const {
    for (const RECT* r : {&readRect_, &menuRect_, &closeRect_, &channelRect_, &writeRect_})
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
    if (IsClickable(pt)) return HTCLIENT;
    const Metrics m = MetricsFor(theme_);
    if (pt.y < m.head || pt.y >= rc.bottom - m.foot) return HTCAPTION;  // drag by header/footer
    return HTCLIENT;
}

void MainWindow::OnClick(POINT pt) {
    if (PtInRect(&closeRect_, pt)) PostMessageW(hwnd_, WM_CLOSE, 0, 0);
    else if (PtInRect(&menuRect_, pt)) ShowMainMenu();
    else if (PtInRect(&readRect_, pt)) ShowReadMenu();
    else if (PtInRect(&channelRect_, pt)) ShowChannelMenu();
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
    SetStatus(L"Neue Chatzeilen werden in " + LanguageLabel(code) + L" \u00fcbersetzt", Tone::Ok, 4000);
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
    log_.SetFilter(t.channels, t.id);
}

Channel MainWindow::SendChannel() const { return tabState_.empty() ? Channel::Unknown : tabState_[tab_].send; }

void MainWindow::SetSendChannel(Channel c) {
    if (partIdx_ > 0) {
        SetStatus(L"Erst die restlichen Teile senden \u2013 oder den Text \u00e4ndern", Tone::Warn, 4000);
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
        SetStatus(L"H\u00f6chstens 8 Tabs", Tone::Warn, 3000);
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
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, (L"Tab \u201e" + tab.name + L"\u201c zeigt:").c_str());
    const auto& channels = TabChannels();
    for (size_t i = 0; i < channels.size(); ++i)
        AppendMenuW(menu, MF_STRING | (TabShows(tab, channels[i]) ? MF_CHECKED : 0), kChannelBase + i,
                    TabChannelLabel(channels[i]));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    HMENU presets = CreatePopupMenu();
    for (size_t i = 0; i < TabPresets().size(); ++i)
        AppendMenuW(presets, MF_STRING, kPresetBase + i, TabPresets()[i].name.c_str());
    AppendMenuW(menu, MF_POPUP | (cfg_.tabs.size() >= 8 ? MF_GRAYED : 0), reinterpret_cast<UINT_PTR>(presets),
                L"Neuer Tab");
    AppendMenuW(menu, MF_STRING | (idx == 0 ? MF_GRAYED : 0), kLeft, L"Nach links");
    AppendMenuW(menu, MF_STRING | (idx + 1 >= cfg_.tabs.size() ? MF_GRAYED : 0), kRight, L"Nach rechts");
    AppendMenuW(menu, MF_STRING | (cfg_.tabs.size() <= 1 ? MF_GRAYED : 0), kClose, L"Tab schlie\u00dfen");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kReset, L"Tabs zur\u00fccksetzen");
    const UINT cmd = static_cast<UINT>(
        TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, screen.x, screen.y, 0, hwnd_, nullptr));
    DestroyMenu(menu);

    if (cmd >= kChannelBase && cmd - kChannelBase < channels.size()) {
        ChatTab& t = cfg_.tabs[idx];
        const ChannelMask bit = ChannelBit(channels[cmd - kChannelBase]);
        if ((t.channels & ~bit) == 0) {
            SetStatus(L"Ein Tab braucht mindestens einen Kanal", Tone::Warn, 3000);
            return;
        }
        const Channel before = SoleSendChannel(t.channels);
        t.channels ^= bit;
        // The chip follows the tab unless you picked something else yourself.
        if (tabState_[idx].send == before) tabState_[idx].send = SoleSendChannel(t.channels);
        OnTabsChanged();
    } else if (cmd >= kPresetBase && cmd - kPresetBase < TabPresets().size()) {
        AddTab(TabPresets()[cmd - kPresetBase]);
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
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"Chat \u00fcbersetzen in:");
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
        static_cast<UINT>(TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd_, nullptr));
    DestroyMenu(menu);
    if (cmd >= kCmdLangBase && cmd - kCmdLangBase < langs.size()) SetReadLang(langs[cmd - kCmdLangBase].code);
}

void MainWindow::ShowWriteMenu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"Senden als:");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    for (size_t i = 0; i < writeLangs_.size(); ++i) {
        const LangInfo* l = FindLanguage(writeLangs_[i]);
        const std::wstring label = l ? LangMenuLabel(*l) : writeLangs_[i];
        AppendMenuW(menu, MF_STRING | (i == writeIdx_ ? MF_CHECKED : 0), kCmdFavBase + i, label.c_str());
    }
    AppendMenuW(menu, MF_STRING | (WriteOriginal() ? MF_CHECKED : 0), kCmdOriginal,
                L"Original (nur Korrektur)\tStrg+Enter");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    HMENU more = CreatePopupMenu();
    const auto& langs = Languages();
    for (size_t i = 0; i < langs.size(); ++i) {
        UINT flags = MF_STRING;
        if (i > 0 && i % 20 == 0) flags |= MF_MENUBARBREAK;
        AppendMenuW(more, flags, kCmdMoreBase + i, LangMenuLabel(langs[i]).c_str());
    }
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(more), L"Weitere Sprachen");
    POINT pt{writeRect_.left, writeRect_.top};
    ClientToScreen(hwnd_, &pt);
    const UINT cmd = static_cast<UINT>(
        TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hwnd_, nullptr));
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
        SetStatus(L"Erst die restlichen Teile senden \u2013 oder den Text \u00e4ndern", Tone::Warn, 4000);
        return;
    }
    enum : UINT { kActive = 1, kReply = 2, kChannelBase = 10 };
    static const Channel kChannels[] = {Channel::Say,  Channel::Map,  Channel::Party, Channel::Squad,
                                        Channel::Team, Channel::Guild};
    const Channel cur = SendChannel();
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | (cur == Channel::Unknown ? MF_CHECKED : 0), kActive,
                L"Aktiver Kanal (wie in GW2 gew\u00e4hlt)");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    for (size_t i = 0; i < std::size(kChannels); ++i) {
        const std::wstring label = std::wstring(ChannelLabel(kChannels[i])) + L"\t" + ChannelCommand(kChannels[i]);
        AppendMenuW(menu, MF_STRING | (cur == kChannels[i] ? MF_CHECKED : 0), kChannelBase + i, label.c_str());
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    const bool whisper = cur == Channel::Whisper;
    AppendMenuW(menu, MF_STRING | (whisper && whisperTarget_.empty() ? MF_CHECKED : 0), kReply,
                L"Fl\u00fcstern: Antwort an den Letzten\t/r");
    for (size_t i = 0; i < whisperers_.size(); ++i)
        AppendMenuW(menu, MF_STRING | (whisper && whisperTarget_ == whisperers_[i] ? MF_CHECKED : 0),
                    kCmdPartnerBase + i, (L"Fl\u00fcstern an " + whisperers_[i]).c_str());
    POINT pt{channelRect_.left, channelRect_.top};
    ClientToScreen(hwnd_, &pt);
    const UINT cmd = static_cast<UINT>(
        TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hwnd_, nullptr));
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
    enum : UINT {
        kRegion = 1, kReader, kBack, kCopyOnly, kFollow, kDock, kCover, kSystem, kCaptures, kResetColors, kOpenIni,
        kOpenDir, kQuit,
        kEngineAuto = 50, kEngineBasic, kEngineDeepL, kEngineLlm
    };
    auto check = [](bool on) { return static_cast<UINT>(on ? MF_CHECKED : MF_UNCHECKED); };
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kRegion, L"Chat-Bereich festlegen \u2026");
    AppendMenuW(menu, MF_STRING | check(cfg_.readerEnabled), kReader, L"Chat dauerhaft \u00fcbersetzen");
    HMENU engines = CreatePopupMenu();
    AppendMenuW(engines, MF_STRING | check(cfg_.engine == Engine::Auto), kEngineAuto, L"Automatisch (bester verf\u00fcgbarer)");
    AppendMenuW(engines, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(engines, MF_STRING | check(cfg_.engine == Engine::Basic), kEngineBasic,
                L"Basis \u2013 MyMemory (kostenlos, ohne Anmeldung)");
    AppendMenuW(engines, MF_STRING | check(cfg_.engine == Engine::DeepL) | (cfg_.deeplKey.empty() ? MF_GRAYED : 0),
                kEngineDeepL, cfg_.deeplKey.empty() ? L"DeepL (Key in der ini fehlt)" : L"DeepL");
    AppendMenuW(engines, MF_STRING | check(cfg_.engine == Engine::Llm) | (llm_ ? 0 : MF_GRAYED), kEngineLlm,
                llm_ ? (L"LLM \u2013 " + cfg_.llmModel).c_str() : L"LLM (Modell in der ini fehlt)");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(engines),
                (L"\u00dcbersetzer: " + translator_->Name()).c_str());
    AppendMenuW(menu, MF_STRING | check(cfg_.backTranslate), kBack, L"R\u00fcck\u00fcbersetzung zeigen");
    AppendMenuW(menu, MF_STRING | check(cfg_.copyOnly), kCopyOnly,
                L"Nur kopieren \u2013 keine Tasten an GW2 (selbst einf\u00fcgen)");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (cfg_.regionSet ? 0 : MF_GRAYED), kCover,
                L"\u00dcber den GW2-Chat legen (ersetzt ihn)");
    AppendMenuW(menu, MF_STRING | check(cfg_.dock), kDock, L"An GW2 andocken (wandert mit)");
    AppendMenuW(menu, MF_STRING | check(cfg_.followGame), kFollow, L"Mit dem Spiel ein-/ausblenden");
    AppendMenuW(menu, MF_STRING | check(cfg_.showSystemLines), kSystem, L"Systemzeilen anzeigen");
    AppendMenuW(menu, MF_STRING | check(cfg_.saveCaptures), kCaptures, L"Diagnose-Aufnahmen speichern");
    AppendMenuW(menu, MF_STRING, kResetColors, L"Kanalfarben zur\u00fccksetzen");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kOpenIni, L"Einstellungen (ini) \u00f6ffnen");
    AppendMenuW(menu, MF_STRING, kOpenDir, L"Datenordner \u00f6ffnen");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kQuit, L"Beenden");

    POINT pt{menuRect_.left, menuRect_.bottom};
    ClientToScreen(hwnd_, &pt);
    const UINT cmd =
        static_cast<UINT>(TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd_, nullptr));
    DestroyMenu(menu);

    auto saveBool = [this](const wchar_t* sec, const wchar_t* key, bool v) { cfg_.SaveValue(sec, key, v ? L"1" : L"0"); };
    switch (cmd) {
        case kRegion:
            PickRegion();
            break;
        case kReader:
            cfg_.readerEnabled = !cfg_.readerEnabled;
            saveBool(L"Reader", L"Enabled", cfg_.readerEnabled);
            if (cfg_.readerEnabled) StartReader();
            else reader_.Stop();
            readingActive_ = false;
            UpdateHint();
            InvalidateChrome();
            break;
        case kBack:
            cfg_.backTranslate = !cfg_.backTranslate;
            saveBool(L"Translate", L"BackTranslate", cfg_.backTranslate);
            backText_.clear();
            StartBackTranslation();
            UpdatePreview();
            break;
        case kCopyOnly:
            cfg_.copyOnly = !cfg_.copyOnly;
            cfg_.SaveValue(L"Chat", L"SendMode", cfg_.copyOnly ? L"copy" : L"send");
            SetStatus(cfg_.copyOnly ? L"Nur kopieren: Enter legt die Zeile in die Zwischenablage, du f\u00fcgst sie in "
                                      L"GW2 selbst ein"
                                    : L"Senden: Enter schickt die Zeile direkt in den GW2-Chat",
                      Tone::Ok, 7000);
            UpdatePreview();
            InvalidateChrome();
            break;
        case kDock:
            SetDock(!cfg_.dock);
            break;
        case kCover:
            CoverChat();
            break;
        case kFollow:
            cfg_.followGame = !cfg_.followGame;
            saveBool(L"Window", L"FollowGame", cfg_.followGame);
            if (!cfg_.followGame && autoHidden_) {
                autoHidden_ = false;
                ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
            }
            break;
        case kSystem:
            cfg_.showSystemLines = !cfg_.showSystemLines;
            saveBool(L"Reader", L"ShowSystem", cfg_.showSystemLines);
            break;
        case kCaptures:
            cfg_.saveCaptures = !cfg_.saveCaptures;
            saveBool(L"Reader", L"SaveCaptures", cfg_.saveCaptures);
            reader_.SetSaveCaptures(cfg_.saveCaptures);
            if (cfg_.saveCaptures) {
                reader_.Rescan();
                SetStatus(L"Aufnahmen landen in " + cfg_.CaptureDir(), Tone::Ok, 8000);
            }
            break;
        case kResetColors:
            cfg_.ResetColors();
            log_.SetPalette(cfg_.palette);
            SetStatus(L"Kanalfarben auf GW2-Standard zur\u00fcckgesetzt", Tone::Ok, 4000);
            break;
        case kOpenIni:
            ShellExecuteW(hwnd_, L"open", L"notepad.exe", (L"\"" + cfg_.iniPath + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
            break;
        case kOpenDir:
            ShellExecuteW(hwnd_, L"open", cfg_.dataDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            break;
        case kQuit:
            PostMessageW(hwnd_, WM_CLOSE, 0, 0);
            break;
        case kEngineAuto: SetEngine(Engine::Auto); break;
        case kEngineBasic: SetEngine(Engine::Basic); break;
        case kEngineDeepL: SetEngine(Engine::DeepL); break;
        case kEngineLlm: SetEngine(Engine::Llm); break;
        default: break;
    }
}

void MainWindow::SetEngine(Engine e) {
    cfg_.engine = e;
    cfg_.SaveValue(L"Translate", L"Engine", EngineKey(e));
    ChooseEngine(true);
    ++inputGen_;
    previewOk_ = false;
    parts_.clear();
    partIdx_ = 0;
    UpdatePreview();
    StartTranslation();
}

void MainWindow::OnKeyboardLanguage(const std::wstring& locale) {
    if (locale.empty() || locale == kbdLocale_) return;
    kbdLocale_ = locale;
    const std::wstring primary = PrimaryLang(locale);
    kbdRtl_ = IsRtlLanguage(primary);
    if (SanitizeChatText(input_.Text()).empty()) input_.SetRtl(kbdRtl_);
    if (cfg_.spellEnabled) {
        const bool ok = spell_.SwitchLanguage(SpellTagCandidates(primary, locale));
        input_.RecheckSpelling();
        if (ok) SetStatus(L"Rechtschreibung: " + spell_.Tag(), Tone::Muted, 2500);
        else
            SetStatus(L"Keine Rechtschreibpr\u00fcfung f\u00fcr " + LanguageLabel(primary) + L" installiert", Tone::Muted,
                      4000);
    }
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
    const std::string tgt = Gw2ApiLang(WriteLang());
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
    if (!names_.count(lang) && BackgroundNoticeAllowed())
        SetStatus(L"Lade offizielle GW2-Namen (" + FromUtf8(lang) + L")\u2026", Tone::Muted);
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
            SetStatus(L"GW2-API nicht erreichbar \u2013 nutze gespeicherte Namen", Tone::Muted, 5000);
        else
            SetStatus(L"GW2-Namen nicht geladen: " + m->result.error, Tone::Warn, 8000);
        return;
    }
    WriteFileAtomic(NameCachePath(cfg_, m->lang), SerializeNameTable(m->result.names));
    names_[m->lang] = std::move(m->result.names);

    const bool hadGlossary = !glossary_.Empty();
    RefreshGlossary();
    UpdateSpellWords();
    if (!BackgroundNoticeAllowed()) return;
    if (!hadGlossary && !glossary_.Empty())
        SetStatus(L"GW2-Glossar bereit: " + std::to_wstring(glossary_.Size()) + L" offizielle Namen", Tone::Ok, 5000);
    else if (fetching_.empty() && status_.rfind(L"Lade offizielle", 0) == 0)
        SetStatus(L"", Tone::Muted);
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

    ProtectedText p = ProtectForTranslation(body, glossary_.Empty() ? nullptr : &glossary_, &BuiltinKeepWords());
    lastHits_ = p.glossaryHits;
    if (!HasTranslatableText(p.segments)) {  // only names, codes, keep-words
        finish(JoinSegments(p.segments));
        return;
    }

    // MyMemory needs a source language; DeepL and LLMs detect it better themselves.
    std::wstring source;
    if (engine_ == Engine::Basic) {
        source = DetectLanguage(body);
        if (source.empty()) source = PrimaryLang(kbdLocale_);
    }
    inflightGen_ = inputGen_;
    UpdatePreview();
    std::thread([hwnd = hwnd_, gen = inputGen_, translator = translator_, segments = std::move(p.segments), source,
                 target = WriteLang()] {
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
            SetPreviewBody(split.prefix.empty() ? ComposePrefix() : split.prefix, SanitizeChatText(m->result.text));
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
            SetStatus(L"Umschrift in lateinischen Buchstaben \u2013 Enter sendet sie", Tone::Ok, 4000);
            return;
    }
}

void MainWindow::StartBackTranslation() {
    if (!cfg_.backTranslate || !PreviewIsCurrent() || WriteOriginal() || previewBody_.empty()) return;
    if (PrimaryLang(WriteLang()) == PrimaryLang(readLang_)) return;  // you can read it anyway
    ProtectedText p = ProtectForTranslation(previewBody_, nullptr, &BuiltinKeepWords());
    if (!HasTranslatableText(p.segments)) return;
    std::thread([hwnd = hwnd_, gen = inputGen_, translator = translator_, segments = std::move(p.segments),
                 source = SourceCode(WriteLang()), target = readLang_] {
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
        SetStatus(L"Erst tippen, dann Strg+U f\u00fcr die Umschrift", Tone::Muted, 3000);
        return;
    }
    const std::wstring script = UnsupportedScript(previewBody_);
    if (script.empty()) {
        SetStatus(L"Schon in lateinischer Schrift", Tone::Muted, 2500);
        return;
    }
    const std::wstring t = TransliterateToLatin(previewBody_);
    if (t != previewBody_ && UnsupportedScript(t).empty()) {
        SetPreviewBody(previewPrefix_, t);
        SetStatus(L"Umschrift in lateinischen Buchstaben \u2013 Enter sendet sie", Tone::Ok, 4000);
        return;
    }
    if (!llm_) {
        SetStatus(L"Umschrift f\u00fcr " + script + L" braucht das optionale LLM ([LLM] in der ini)", Tone::Warn, 7000);
        return;
    }
    SetStatus(L"Umschrift l\u00e4uft (LLM) \u2026", Tone::Muted);
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
    c.placeholder = std::wstring(L"Schreib in deiner Sprache \u2013 hier steht, was im GW2-Chat ankommt.  ") +
                    (cfg_.copyOnly ? L"Enter kopiert" : L"Enter sendet") +
                    L" \u00b7 Strg+Enter das Original \u00b7 Strg+L Sprache \u00b7 Strg+Tab Fl\u00fcstern";
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
        const std::wstring script = UnsupportedScript(previewBody_);
        if (parts_.size() > 1) {
            c.note = L"Teil " + std::to_wstring(idx + 1) + L"/" + std::to_wstring(parts_.size()) +
                     L" \u2013 jedes Enter sendet einen Teil";
        }
        if (!script.empty()) {
            c.note = (c.note.empty() ? L"" : c.note + L"  \u00b7  ") + L"GW2 zeigt " + script +
                     L" vermutlich nicht an \u2013 Strg+U: Umschrift";
            c.warn = true;
        } else if (c.note.empty()) {
            c.note = HitsText(lastHits_);
        }
    } else {
        c.text = previewBody_.empty() ? ComposePrefix() + text : previewPrefix_ + previewBody_;
        c.current = false;
        if (inflightGen_ == inputGen_) c.note = L"\u00fcbersetze \u2026";
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
    if (sendPending_ && inflightGen_ == inputGen_) SetStatus(L"\u00fcbersetze und sende \u2026", Tone::Muted);
}

void MainWindow::SendNextPart() {
    if (parts_.empty() || partIdx_ >= parts_.size()) return;
    const std::wstring line = parts_[partIdx_];
    const size_t n = CodePointCount(line);
    if (n > static_cast<size_t>(cfg_.maxLength)) {
        SetStatus(L"Zu lang f\u00fcr den GW2-Chat (" + std::to_wstring(n) + L"/" + std::to_wstring(cfg_.maxLength) +
                      L") \u2013 bitte k\u00fcrzen",
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
    ++partIdx_;
    if (partIdx_ >= parts_.size()) {
        input_.Clear();
        OnInputChanged();  // multi-line EDITs send no EN_CHANGE for WM_SETTEXT
        return;
    }
    UpdatePreview();
    InvalidateChrome();
    if (cfg_.copyOnly) {  // you paste it in GW2, then come back for the next part
        SetStatus(L"Teil " + std::to_wstring(partIdx_) + L"/" + std::to_wstring(parts_.size()) +
                      L" kopiert \u2013 in GW2 einf\u00fcgen, dann hier Enter f\u00fcr den n\u00e4chsten",
                  Tone::Ok);
        return;
    }
    // More to come: back to our window so the next Enter (your key press) sends the next part.
    SetStatus(L"Teil " + std::to_wstring(partIdx_) + L"/" + std::to_wstring(parts_.size()) +
                  L" gesendet \u2013 Enter sendet den n\u00e4chsten",
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
            SetStatus(L"Zwischenablage gerade blockiert \u2013 nochmal Enter", Tone::Error);
            return false;
        }
    } else {
        SetStatus(L"sende \u2026", Tone::Muted);
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
    e.tabId = cfg_.tabs[tab_].id;  // stays visible in the tab it was written in
    const uint64_t id = log_.Add(std::move(e));
    recentSent_.push_back({NormalizeForCompare(body), GetTickCount64(), id, false});
    while (recentSent_.size() > 40) recentSent_.pop_front();

    if (cfg_.copyOnly) {
        SetStatus(L"Kopiert \u2013 in GW2: Enter \u00b7 Strg+V \u00b7 Enter", Tone::Ok, 8000);
        if (gw2_ && IsWindow(gw2_)) Front(gw2_);  // window switch only, no keys
        return true;
    }
    if (out.clipboardRestored)
        SetStatus(L"Gesendet.", Tone::Ok, 3000);
    else
        SetStatus(L"Gesendet \u2013 GW2 hat nicht best\u00e4tigt, deine alte Zwischenablage wurde zur Sicherheit nicht "
                  L"zur\u00fcckgeschrieben",
                  Tone::Warn, 8000);
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
    if (!s->error.empty()) {
        if (readerError_ != s->error) {
            readerError_ = s->error;
            SetStatus(L"Chat lesen: " + s->error, Tone::Warn, 10000);
            UpdateHint();
        }
        return;
    }
    if (!readerError_.empty()) {
        readerError_.clear();
        UpdateHint();
    }
    if (!readerReported_ && BackgroundNoticeAllowed()) {
        readerReported_ = true;
        SetStatus(L"Chat erkannt: " + std::to_wstring(s->lines.size()) + L" Zeilen \u00b7 " + s->method + L" \u00b7 " +
                      s->language + L" \u00b7 " + std::to_wstring(s->milliseconds) + L" ms",
                  Tone::Ok, 6000);
    }
    const std::vector<ChatMessage> msgs = BuildMessages(s->lines, cfg_.palette);
    for (const ChatMessage& m : stream_.Feed(msgs)) HandleIncoming(m);
    PumpIncoming();
    UpdateHint();
}

bool MainWindow::NeedsTranslation(const std::wstring& text, std::wstring* detected) const {
    size_t letters = 0;
    for (wchar_t c : text)
        if (IsWordChar(c) && !(c >= L'0' && c <= L'9')) ++letters;
    if (letters < 3) return false;  // "gg", "ty", emotes
    const ProtectedText p = ProtectForTranslation(text, nullptr, &BuiltinKeepWords());
    if (!HasTranslatableText(p.segments)) return false;  // only LFG/WvW/chat codes
    *detected = DetectLanguage(text);
    return detected->empty() || *detected != PrimaryLang(readLang_);
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
    const bool system = m.channel == Channel::System || (m.channel == Channel::Unknown && m.speaker.empty());
    if (system && !cfg_.showSystemLines) return;
    if (log_.ShowsTranslation(m.text)) {  // our own window, read back: never translate it again
        OnSelfRead();
        return;
    }
    switch (ClassifyOwn(m)) {
        case Own::SentHere:  // already in the log as "Du: ..."
            return;
        case Own::TypedInGame: {  // typed in GW2 itself: show it for context, untranslated
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
    if (!system && NeedsTranslation(m.text, &detected)) {
        std::wstring cached;
        if (cache_.Get(m.text, readLang_, cached)) {
            if (NormalizeForCompare(cached) != NormalizeForCompare(m.text)) {
                e.state = ChatEntry::State::Translated;
                e.main = cached;
                e.original = m.text;
            }
        } else if (GetTickCount64() + 60000 < inPauseUntil_) {
            e.note = L"nicht \u00fcbersetzt \u2013 \u00dcbersetzer pausiert";  // long pause (contingent): don't pile up
        } else {
            e.state = ChatEntry::State::Pending;
            pending = true;
        }
    }
    e.lang = detected;
    const uint64_t id = log_.Add(std::move(e));
    if (pending) {
        inQueue_.push_back({id, m.text});
        while (inQueue_.size() > kQueueMax) {  // a busy chat outruns the translator: skip the oldest
            log_.Update(inQueue_.front().entryId, [](ChatEntry& x) {
                x.state = ChatEntry::State::Plain;
                x.note = L"\u00fcbersprungen \u2013 zu viel auf einmal";
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
            ++tabState_[i].unread;
            changed = true;
        }
        if (changed) InvalidateChrome();
    }
}

void MainWindow::PumpIncoming() {
    if (inFlight_ || inQueue_.empty() || !translator_) return;
    if (GetTickCount64() < inPauseUntil_) return;  // after an error; the game timer retries
    auto msg = std::make_unique<IncomingMsg>();
    msg->lang = readLang_;
    std::vector<std::vector<Segment>> items;
    size_t chars = 0;
    while (!inQueue_.empty() && items.size() < kBatchMax && chars < kBatchChars) {
        PendingLine pl = std::move(inQueue_.front());
        inQueue_.pop_front();
        chars += pl.text.size();
        items.push_back(ProtectForTranslation(pl.text, nullptr, &BuiltinKeepWords()).segments);
        msg->ids.push_back(pl.entryId);
        msg->texts.push_back(std::move(pl.text));
    }
    inFlight_ = true;
    std::thread([hwnd = hwnd_, translator = translator_, items = std::move(items), m = std::move(msg)]() mutable {
        m->results = translator->TranslateBatch(items, L"", m->lang);
        if (PostMessageW(hwnd, WM_APP_INCOMING, 0, reinterpret_cast<LPARAM>(m.get()))) m.release();
    }).detach();
}

void MainWindow::OnIncomingTranslated(IncomingMsg* raw) {
    std::unique_ptr<IncomingMsg> m(raw);
    inFlight_ = false;
    size_t ok = 0;
    bool quota = false;
    std::wstring firstError;
    for (size_t i = 0; i < m->ids.size(); ++i) {
        TranslateResult r;
        if (i < m->results.size()) r = m->results[i];
        else r.error = L"keine Antwort";
        quota = quota || r.quotaExceeded;
        const std::wstring& original = m->texts[i];
        if (r.ok) {
            ++ok;
            const std::wstring t = SanitizeChatText(r.text);
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
                }
            });
        } else {
            if (firstError.empty()) firstError = r.error;
            log_.Update(m->ids[i], [&](ChatEntry& e) {
                e.state = ChatEntry::State::Failed;
                e.note = L"nicht \u00fcbersetzt";
            });
        }
    }
    if (ok == 0 && !firstError.empty()) {
        inPauseUntil_ = GetTickCount64() + (quota ? kQuotaPauseMs : kErrorPauseMs);
        SetStatus(std::wstring(quota ? L"Chat-\u00dcbersetzung pausiert (20 min): " : L"Chat-\u00dcbersetzung pausiert (30 s): ") +
                      firstError,
                  Tone::Warn, quota ? 30000 : 12000);
        if (quota) {  // the waiting lines will not get a translation in time
            for (const PendingLine& pl : inQueue_)
                log_.Update(pl.entryId, [](ChatEntry& x) {
                    x.state = ChatEntry::State::Plain;
                    x.note = L"nicht \u00fcbersetzt \u2013 Kontingent aufgebraucht";
                });
            inQueue_.clear();
        }
    }
    PumpIncoming();
}

void MainWindow::UpdateHint() {
    if (SoleSendChannel(cfg_.tabs[tab_].channels) == Channel::Whisper) {
        log_.SetEmptyHint(L"Fl\u00fcsternachrichten erscheinen hier \u00fcbersetzt.\nRechtsklick auf eine Nachricht \u2192 "
                          L"Antworten. Strg+Tab wechselt den Tab.",
                          false);
        return;
    }
    if (!cfg_.readerEnabled)
        log_.SetEmptyHint(L"Chat-Lesen ist aus (Men\u00fc \u2261 \u2192 Chat dauerhaft \u00fcbersetzen).", false);
    else if (!readerError_.empty())
        log_.SetEmptyHint(L"Texterkennung nicht verf\u00fcgbar:\n" + readerError_, false);
    else if (!cfg_.regionSet)
        log_.SetEmptyHint(L"Einmalig einrichten: hier klicken und einen Rahmen um den GW2-Chat ziehen.\nDanach erscheint "
                          L"hier der ganze Chat in deiner Sprache.",
                          true);
    else if (!gw2_)
        log_.SetEmptyHint(L"Warte auf Guild Wars 2 \u2026", false);
    else if (overlapsChat_)
        log_.SetEmptyHint(L"Dieses Fenster \u00fcberdeckt den Chat-Bereich \u2013 bitte daneben schieben.", false);
    else
        log_.SetEmptyHint(L"Lese den Chat \u2026 neue Nachrichten erscheinen hier in deiner Sprache.\n(Bereich "
                          L"anpassen: Men\u00fc \u2261 \u2192 Chat-Bereich festlegen)",
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
    const RECT client = GameClientRect();
    if (IsRectEmpty(&client)) return {};
    RECT area{client.left + cfg_.regionLeft, client.bottom - cfg_.regionFromBottom - cfg_.regionHeight,
              client.left + cfg_.regionLeft + cfg_.regionWidth, client.bottom - cfg_.regionFromBottom};
    RECT clipped{};
    IntersectRect(&clipped, &area, &client);
    return clipped;
}

void MainWindow::PollGame() {
    mumbleState_ = mumble_.Read();
    if (gw2_ && !IsWindow(gw2_)) gw2_ = nullptr;
    const ULONGLONG now = GetTickCount64();
    if (!gw2_ && now - lastFind_ > 2000) {
        lastFind_ = now;
        gw2_ = FindGw2Window(mumbleState_.live ? mumbleState_.processId : 0);
        if (gw2_) UpdateHint();
    }
    if (picking_) return;

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

    // Behave like part of the game: visible while GW2 or this window is in front.
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

    // Read only while the chat is really on screen (not behind another app).
    RECT area{};
    const bool canRead =
        cfg_.readerEnabled && cfg_.regionSet && gw2_ && !IsIconic(gw2_) && (gameFront || ours) && !moving_;
    if (canRead) area = ChatArea();
    bool overlap = false;
    if (!IsRectEmpty(&area) && IsWindowVisible(hwnd_)) {
        RECT me{}, both{};
        GetWindowRect(hwnd_, &me);
        overlap = IntersectRect(&both, &me, &area) != FALSE;
    }
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
    const bool blocked = overlap && !excludedFromCapture_;
    if (blocked) area = {};  // would read our own window
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
    RECT r{};
    const bool ok = PickScreenRegion(inst_, theme_,
                                     L"Ziehe einen Rahmen um die Textzeilen des GW2-Chats\n(ohne Eingabezeile und "
                                     L"Reiter).  Esc bricht ab.",
                                     &r);
    picking_ = false;
    ignoreSnapshotsBefore_ = GetTickCount64() + 150;
    if (wasVisible) ShowOverlay();
    if (!ok) {
        SetStatus(L"Chat-Bereich nicht ge\u00e4ndert", Tone::Muted, 3000);
        return;
    }
    // Store relative to the bottom-left corner of the game (the chat sits
    // there), so it survives moving the window or another resolution.
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
    SetStatus(gw2_ ? L"Chat-Bereich gespeichert \u2013 lese \u2026"
                   : L"Chat-Bereich gespeichert (GW2 nicht gefunden \u2013 relativ zum Bildschirm)",
              gw2_ ? Tone::Ok : Tone::Warn, 6000);
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
        SetStatus(L"GW2 nicht gefunden \u2013 andocken geht, sobald das Spiel l\u00e4uft", Tone::Warn, 5000);
        return;
    }
    cfg_.dock = on;
    if (on) {
        UpdateDockFromWindow();  // where it is now, relative to the game
        SetStatus(L"Angedockt \u2013 das Fenster wandert mit GW2 mit", Tone::Ok, 4000);
    } else {
        cfg_.SaveDock();
        cfg_.SaveWindowRect(hwnd_, theme_.scale);
        SetStatus(L"Abgedockt", Tone::Muted, 2500);
    }
}

// Lay the window over the GW2 chat panel. The reader keeps reading the real
// chat underneath: this window is kept out of the capture while it covers it.
void MainWindow::CoverChat() {
    if (!gw2_) gw2_ = FindGw2Window(mumbleState_.live ? mumbleState_.processId : 0);
    const RECT area = ChatArea();
    const RECT client = GameClientRect();
    if (!cfg_.regionSet || IsRectEmpty(&area) || IsRectEmpty(&client)) {
        SetStatus(L"Erst den Chat-Bereich festlegen (GW2 muss laufen)", Tone::Warn, 5000);
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
        SetStatus(L"Dieses Windows kann das Fenster nicht aus der Aufnahme ausblenden \u2013 bitte neben den Chat "
                  L"legen (ab Windows 10 Version 2004 geht es)",
                  Tone::Warn, 12000);
    else
        SetStatus(L"Liegt \u00fcber dem GW2-Chat und liest ihn darunter weiter. Der GW2-Chat muss offen bleiben.",
                  Tone::Ok, 9000);
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
    SetStatus(L"Windows nimmt dieses Fenster mit auf \u2013 Lesen pausiert, solange es den Chat verdeckt", Tone::Warn,
              12000);
    PollGame();
}

void MainWindow::ShowOverlay() {
    userHidden_ = false;
    autoHidden_ = false;
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
            const int previewH = theme_.textLineHeight * 2 + theme_.smallLineHeight * 2 + theme_.S(14);
            mmi->ptMinTrackSize = {theme_.S(360), m.head + theme_.S(40) + previewH + m.inputH + 2 * m.gap + m.foot};
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
            if (wp == kTimerDebounce) StartTranslation();
            else if (wp == kTimerGame) PollGame();
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
