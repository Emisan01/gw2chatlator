// settings_dialog.cpp — settings window and guided setup, built from
// native controls without a resource script.
#include "settings_dialog.hpp"

#include <commctrl.h>
#include <shellapi.h>

#include <algorithm>
#include <memory>
#include <thread>
#include <vector>

#include "core/gw2_install.hpp"
#include "core/i18n.hpp"
#include "core/languages.hpp"
#include "core/text.hpp"
#include "win/deepl_translator.hpp"
#include "win/gw2_locate.hpp"
#include "win/online_translators.hpp"
#include "win/tesseract_ocr.hpp"

namespace gct {
namespace {

constexpr wchar_t kDialogClass[] = L"GW2ChatTranslatorDialog";
constexpr UINT WM_APP_MODELS = WM_APP + 50;
constexpr UINT WM_APP_TEST = WM_APP + 51;
constexpr UINT WM_APP_PULL = WM_APP + 52;
constexpr wchar_t kTesseractUrl[] = L"https://github.com/UB-Mannheim/tesseract/wiki";

enum : int {
    kIdTab = 100,
    // General
    kUiLang, kReadLang, kWriteLangs, kFontSize, kOpacity, kHotkey,
    // Reading
    kReaderOn, kOcrEngine, kTessPath, kTessBrowse, kTessStatus, kTessGet, kChinese, kInterval, kShowSystem, kCaptures,
    kPickRegion, kCover,
    // Writing
    kSpell, kAutoCorrect, kSuggest, kLearn, kForgetAll, kForgetStatus, kLt, kLtUrl, kBackTr, kSendMode, kReturnFocus,
    // Translator
    kEngine, kEngineNote, kLocalModel, kPull, kLocalInfo, kGetOllama, kPullStatus, kDeepL, kEmail, kLlmUrl, kLlmModel, kLlmLoad, kLlmKey, kFixOcr, kTest, kTestStatus,
    // Game & start
    kGw2Dir, kGw2Find, kGw2Browse, kInstall, kInstallStatus, kAutostart, kDock, kFollow, kFocusGameChat, kStatus, kRefresh, kSetup,
    // Wizard
    kBack, kNext, kStepTitle, kStepText,
};

const int kFontSizes[] = {90, 100, 115, 135};
const int kOpacities[] = {178, 204, 230, 255};

std::wstring WindowText(HWND h) {
    const int n = GetWindowTextLengthW(h);
    std::wstring s(static_cast<size_t>(n) + 1, L'\0');
    GetWindowTextW(h, s.data(), n + 1);
    s.resize(static_cast<size_t>(n));
    return s;
}

int DpiOf(HWND h) {
    using Fn = UINT(WINAPI*)(HWND);
    static Fn fn = reinterpret_cast<Fn>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow")));
    if (fn && h) {
        const UINT d = fn(h);
        if (d) return static_cast<int>(d);
    }
    HDC dc = GetDC(nullptr);
    const int d = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(nullptr, dc);
    return d;
}

std::wstring TesseractStatus(const std::wstring& configured) {
    TesseractInfo info;
    if (!FindTesseract(configured, &info)) return Tr(L"Not installed – Windows text recognition is used.");
    std::wstring models;
    for (const std::string& m : info.models)
        if (m != "osd") models += (models.empty() ? L"" : L", ") + FromUtf8(m);
    return TrF(L"Found: {1}  ({2})", {info.exe, models});
}

// ---------------------------------------------------------------------------
// A window with native controls and its own modal loop.
// ---------------------------------------------------------------------------
class NativeDialog {
public:
    virtual ~NativeDialog() {
        if (font_) DeleteObject(font_);
        if (bold_) DeleteObject(bold_);
    }

    bool Run(HWND owner, HINSTANCE inst, const std::wstring& title, int w96, int h96) {
        owner_ = owner;
        inst_ = inst;
        static bool registered = false;
        if (!registered) {
            INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_TAB_CLASSES | ICC_STANDARD_CLASSES};
            InitCommonControlsEx(&icc);
            WNDCLASSEXW wc{};
            wc.cbSize = sizeof(wc);
            wc.lpfnWndProc = Proc;
            wc.hInstance = inst;
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
            wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
            wc.lpszClassName = kDialogClass;
            RegisterClassExW(&wc);
            registered = true;
        }
        dpi_ = DpiOf(owner);
        NONCLIENTMETRICSW ncm{};
        ncm.cbSize = sizeof(ncm);
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
        LOGFONTW lf = ncm.lfMessageFont;
        lf.lfHeight = -MulDiv(9, dpi_, 72);
        font_ = CreateFontIndirectW(&lf);
        lf.lfWeight = FW_SEMIBOLD;
        lf.lfHeight = -MulDiv(11, dpi_, 72);
        bold_ = CreateFontIndirectW(&lf);

        RECT want{0, 0, S(w96), S(h96)};
        AdjustWindowRectEx(&want, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
        const int w = want.right - want.left, h = want.bottom - want.top;
        RECT ownerRect{};
        if (owner && IsWindowVisible(owner)) GetWindowRect(owner, &ownerRect);
        else SystemParametersInfoW(SPI_GETWORKAREA, 0, &ownerRect, 0);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(MonitorFromRect(&ownerRect, MONITOR_DEFAULTTONEAREST), &mi);
        int x = (ownerRect.left + ownerRect.right - w) / 2, y = (ownerRect.top + ownerRect.bottom - h) / 2;
        x = std::clamp(x, static_cast<int>(mi.rcWork.left), std::max(static_cast<int>(mi.rcWork.left),
                                                                      static_cast<int>(mi.rcWork.right) - w));
        y = std::clamp(y, static_cast<int>(mi.rcWork.top), std::max(static_cast<int>(mi.rcWork.top),
                                                                     static_cast<int>(mi.rcWork.bottom) - h));
        DWORD ex = WS_EX_DLGMODALFRAME | WS_EX_TOPMOST | WS_EX_CONTROLPARENT;
        if (UiRtl()) ex |= WS_EX_LAYOUTRTL;
        hwnd_ = CreateWindowExW(ex, kDialogClass, title.c_str(), WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, w, h, owner,
                                nullptr, inst, this);
        if (!hwnd_) return false;
        Build();
        if (owner) EnableWindow(owner, FALSE);
        ShowWindow(hwnd_, SW_SHOW);
        SetForegroundWindow(hwnd_);
        MSG m;
        while (!done_ && GetMessageW(&m, nullptr, 0, 0) > 0) {
            if (!IsDialogMessageW(hwnd_, &m)) {
                TranslateMessage(&m);
                DispatchMessageW(&m);
            }
        }
        if (owner) EnableWindow(owner, TRUE);
        if (hwnd_) DestroyWindow(hwnd_);
        if (owner) SetForegroundWindow(owner);
        return true;
    }

protected:
    virtual void Build() = 0;
    virtual void OnCommand(int id, int code) = 0;
    virtual void OnTabChanged() {}
    virtual LRESULT OnApp(UINT, WPARAM, LPARAM) { return 0; }
    void Close() { done_ = true; }

    int S(int v) const { return MulDiv(v, dpi_, 96); }

    HWND Add(const wchar_t* cls, const std::wstring& text, DWORD style, int x, int y, int w, int h, int id,
             DWORD ex = 0) {
        HWND c = CreateWindowExW(ex, cls, text.c_str(), WS_CHILD | WS_VISIBLE | style, S(x), S(y), S(w), S(h), hwnd_,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst_, nullptr);
        SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font_), FALSE);
        if (page_ >= 0) pages_[static_cast<size_t>(page_)].push_back(c);
        return c;
    }
    HWND Label(const std::wstring& t, int x, int y, int w, int h = 18, int id = 0) {
        return Add(L"STATIC", t, SS_LEFT | SS_NOPREFIX, x, y + 3, w, h, id);
    }
    HWND Check(int id, const std::wstring& t, bool on, int x, int y, int w) {
        HWND c = Add(L"BUTTON", t, BS_AUTOCHECKBOX | WS_TABSTOP, x, y, w, 22, id);
        SendMessageW(c, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
        return c;
    }
    HWND Button(int id, const std::wstring& t, int x, int y, int w, int h = 26) {
        return Add(L"BUTTON", t, BS_PUSHBUTTON | WS_TABSTOP, x, y, w, h, id);
    }
    HWND Edit(int id, const std::wstring& t, int x, int y, int w, DWORD style = 0, int h = 23) {
        return Add(L"EDIT", t, ES_AUTOHSCROLL | WS_TABSTOP | style, x, y, w, h, id, WS_EX_CLIENTEDGE);
    }
    HWND Combo(int id, const std::vector<std::wstring>& items, int sel, int x, int y, int w, bool editable = false) {
        HWND c = Add(L"COMBOBOX", L"", (editable ? CBS_DROPDOWN | CBS_AUTOHSCROLL : CBS_DROPDOWNLIST) | WS_VSCROLL |
                                           WS_TABSTOP,
                     x, y, w, 260, id);
        for (const std::wstring& s : items) SendMessageW(c, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s.c_str()));
        if (sel >= 0) SendMessageW(c, CB_SETCURSEL, static_cast<WPARAM>(sel), 0);
        return c;
    }
    HWND Item(int id) const { return GetDlgItem(hwnd_, id); }
    bool Checked(int id) const { return SendMessageW(Item(id), BM_GETCHECK, 0, 0) == BST_CHECKED; }
    int Sel(int id) const { return static_cast<int>(SendMessageW(Item(id), CB_GETCURSEL, 0, 0)); }
    std::wstring Text(int id) const { return WindowText(Item(id)); }
    void SetText(int id, const std::wstring& t) { SetWindowTextW(Item(id), t.c_str()); }

    // Recreates every control (after the UI language changed; mirrored for RTL).
    void RebuildAll(const std::wstring& title) {
        while (HWND c = GetWindow(hwnd_, GW_CHILD)) DestroyWindow(c);
        pages_.clear();
        page_ = -1;
        LONG_PTR ex = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
        ex = UiRtl() ? (ex | WS_EX_LAYOUTRTL) : (ex & ~static_cast<LONG_PTR>(WS_EX_LAYOUTRTL));
        SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, ex);
        SetWindowTextW(hwnd_, title.c_str());
        Build();
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    void BeginPage() {
        pages_.emplace_back();
        page_ = static_cast<int>(pages_.size()) - 1;
    }
    void EndPages() { page_ = -1; }
    void ShowPage(size_t idx) {
        for (size_t i = 0; i < pages_.size(); ++i)
            for (HWND c : pages_[i]) ShowWindow(c, i == idx ? SW_SHOW : SW_HIDE);
    }

    HWND hwnd_ = nullptr;
    HWND owner_ = nullptr;
    HINSTANCE inst_ = nullptr;
    HFONT font_ = nullptr, bold_ = nullptr;
    int dpi_ = 96;
    std::vector<std::vector<HWND>> pages_;
    int page_ = -1;

private:
    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        NativeDialog* self;
        if (msg == WM_NCCREATE) {
            self = static_cast<NativeDialog*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->hwnd_ = h;
        } else {
            self = reinterpret_cast<NativeDialog*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        }
        if (!self) return DefWindowProcW(h, msg, wp, lp);
        switch (msg) {
            case WM_COMMAND:
                self->OnCommand(LOWORD(wp), HIWORD(wp));
                return 0;
            case WM_NOTIFY: {
                const NMHDR* n = reinterpret_cast<const NMHDR*>(lp);
                if (n->idFrom == kIdTab && n->code == TCN_SELCHANGE) self->OnTabChanged();
                return 0;
            }
            case WM_CLOSE:
                self->OnCommand(IDCANCEL, 0);
                return 0;
            case WM_CTLCOLORSTATIC:
            case WM_CTLCOLORBTN: {  // labels and check boxes on the white page
                HDC dc = reinterpret_cast<HDC>(wp);
                SetBkColor(dc, GetSysColor(COLOR_WINDOW));
                SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
                return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
            }
            case WM_NCDESTROY:
                SetWindowLongPtrW(h, GWLP_USERDATA, 0);
                self->hwnd_ = nullptr;
                break;
            default:
                if (msg >= WM_APP) return self->OnApp(msg, wp, lp);
                break;
        }
        return DefWindowProcW(h, msg, wp, lp);
    }

    bool done_ = false;
};

struct ModelsMsg {
    std::vector<std::wstring> models;
    std::wstring error;
};

struct PullMsg {
    std::wstring model;
    bool ok = false;
    std::wstring error;
};

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------
class SettingsDialog final : public NativeDialog {
public:
    SettingsDialog(Config& cfg, const DialogContext& ctx, SettingsPage start) : cfg_(cfg), ctx_(ctx), start_(start) {}
    DialogResult result;

private:
    // Layout: labels on the left, controls on the right, below the tabs.
    static constexpr int kW = 580, kLabelX = 22, kLabelW = 170, kCtrlX = 200, kCtrlW = 356, kTop = 46, kRow = 32;

    void Build() override {
        tab_ = Add(WC_TABCONTROLW, L"", WS_TABSTOP | WS_CLIPSIBLINGS, 10, 10, kW - 20, 470, kIdTab);
        const wchar_t* names[] = {L"General", L"Reading the chat", L"Writing", L"Translator", L"Game & start"};
        for (int i = 0; i < 5; ++i) {
            std::wstring n;
            for (wchar_t c : Tr(names[i])) n += c == L'&' ? std::wstring(L"&&") : std::wstring(1, c);
            TCITEMW item{};
            item.mask = TCIF_TEXT;
            item.pszText = const_cast<wchar_t*>(n.c_str());
            SendMessageW(tab_, TCM_INSERTITEMW, static_cast<WPARAM>(i), reinterpret_cast<LPARAM>(&item));
        }
        BuildGeneral();
        BuildReading();
        BuildWriting();
        BuildTranslator();
        BuildGame();
        EndPages();
        Button(IDOK, Tr(L"OK"), kW - 220, 490, 100);
        Button(IDCANCEL, Tr(L"Cancel"), kW - 112, 490, 100);
        SendMessageW(tab_, TCM_SETCURSEL, static_cast<WPARAM>(start_), 0);
        ShowPage(static_cast<size_t>(start_));
        RefreshStatus();
    }

    int Y(int row) const { return kTop + row * kRow; }

    void BuildGeneral() {
        BeginPage();
        int r = 0;
        Label(Tr(L"Language / Sprache / اللغة"), kLabelX, Y(r), kLabelW);
        std::vector<std::wstring> ui;
        int uiSel = 0;
        for (size_t i = 0; i < UiLanguages().size(); ++i) {
            ui.push_back(UiLanguages()[i].native);
            if (UiLanguages()[i].lang == cfg_.uiLang) uiSel = static_cast<int>(i);
        }
        Combo(kUiLang, ui, uiSel, kCtrlX, Y(r++), kCtrlW);

        Label(Tr(L"Translate the chat into"), kLabelX, Y(r), kLabelW);
        std::vector<std::wstring> langs{Tr(L"Windows language")};
        int readSel = 0;
        for (size_t i = 0; i < Languages().size(); ++i) {
            langs.push_back(std::wstring(Languages()[i].native) + L"  (" + Languages()[i].code + L")");
            if (!cfg_.readLang.empty() && FindLanguage(cfg_.readLang) == &Languages()[i]) readSel = static_cast<int>(i) + 1;
        }
        Combo(kReadLang, langs, readSel, kCtrlX, Y(r++), kCtrlW);

        Label(Tr(L"“Send as” languages"), kLabelX, Y(r), kLabelW);
        std::wstring joined;
        for (const std::wstring& c : cfg_.writeLangs) joined += (joined.empty() ? L"" : L", ") + c;
        Edit(kWriteLangs, joined, kCtrlX, Y(r++), kCtrlW);
        Label(Tr(L"Codes like EN-GB, FR, ES, DE, AR, ZH-HANS. Ctrl+L switches while typing."), kCtrlX, Y(r++) - 6,
              kCtrlW, 30);

        Label(Tr(L"Text size"), kLabelX, Y(r), kLabelW);
        std::vector<std::wstring> sizes;
        int sizeSel = 1;
        for (size_t i = 0; i < std::size(kFontSizes); ++i) {
            sizes.push_back(std::to_wstring(kFontSizes[i]) + L" %");
            if (kFontSizes[i] == cfg_.fontPercent) sizeSel = static_cast<int>(i);
        }
        Combo(kFontSize, sizes, sizeSel, kCtrlX, Y(r++), 120);

        Label(Tr(L"Opacity"), kLabelX, Y(r), kLabelW);
        std::vector<std::wstring> ops;
        int opSel = 2;
        for (size_t i = 0; i < std::size(kOpacities); ++i) {
            ops.push_back(std::to_wstring(kOpacities[i] * 100 / 255) + L" %");
            if (std::abs(kOpacities[i] - cfg_.opacity) < 13) opSel = static_cast<int>(i);
        }
        Combo(kOpacity, ops, opSel, kCtrlX, Y(r++), 120);

        Label(Tr(L"Show / hide hotkey"), kLabelX, Y(r), kLabelW);
        Edit(kHotkey, cfg_.hotkey, kCtrlX, Y(r++), 160);
    }

    void BuildReading() {
        BeginPage();
        int r = 0;
        Check(kReaderOn, Tr(L"Read the GW2 chat and translate it permanently"), cfg_.readerEnabled, kLabelX, Y(r++),
              kW - 50);
        Label(Tr(L"Text recognition"), kLabelX, Y(r), kLabelW);
        Combo(kOcrEngine,
              {Tr(L"Automatic (fastest: Windows; Tesseract only for very small text)"), L"Tesseract",
               Tr(L"Windows (built in)")},
              static_cast<int>(cfg_.ocr), kCtrlX, Y(r++), kCtrlW);
        Label(Tr(L"Tesseract folder"), kLabelX, Y(r), kLabelW);
        Edit(kTessPath, cfg_.tesseractPath, kCtrlX, Y(r), kCtrlW - 96);
        Button(kTessBrowse, Tr(L"Browse…"), kCtrlX + kCtrlW - 90, Y(r++) - 1, 90);
        Label(TesseractStatus(cfg_.tesseractPath), kCtrlX, Y(r++) - 6, kCtrlW, 34, kTessStatus);
        Button(kTessGet, Tr(L"Get Tesseract (free)…"), kCtrlX, Y(r++) - 4, 200);
        Check(kChinese, Tr(L"Also read Chinese (Simplified, needs chi_sim)"), cfg_.readChinese, kLabelX, Y(r++),
              kW - 50);
        Label(Tr(L"Read every (ms)"), kLabelX, Y(r), kLabelW);
        Edit(kInterval, std::to_wstring(cfg_.readerIntervalMs), kCtrlX, Y(r++), 90, ES_NUMBER);
        Check(kShowSystem, Tr(L"Show system lines (events, notices)"), cfg_.showSystemLines, kLabelX, Y(r++), kW - 50);
        Check(kCaptures, Tr(L"Save diagnostic pictures (switches off after 15 minutes)"), cfg_.saveCaptures, kLabelX,
              Y(r++), kW - 50);
        Button(kPickRegion, Tr(L"Set the chat area…"), kLabelX, Y(r) + 4, 200);
        Button(kCover, Tr(L"Lay over the GW2 chat"), kLabelX + 210, Y(r++) + 4, 200);
    }

    void BuildWriting() {
        BeginPage();
        int r = 0;
        Check(kSpell, Tr(L"Spell checking (Windows, follows your keyboard language)"), cfg_.spellEnabled, kLabelX,
              Y(r++), kW - 50);
        Label(Tr(L"Autocorrection"), kLabelX, Y(r), kLabelW);
        Combo(kAutoCorrect,
              {Tr(L"Off"), Tr(L"Safe (only Windows' sure fixes)"), Tr(L"Like a phone keyboard (recommended)")},
              static_cast<int>(cfg_.autoCorrect), kCtrlX, Y(r++), kCtrlW);
        Check(kSuggest, Tr(L"Word bar with suggestions (Tab takes the highlighted word)"), cfg_.suggestions, kLabelX,
              Y(r++), kW - 50);
        Check(kLearn, Tr(L"Learn the words I send"), cfg_.learnWords, kLabelX, Y(r), 260);
        Button(kForgetAll, Tr(L"Delete everything learned…"), kCtrlX + 80, Y(r) - 2, 220);
        Label(Tr(L"Stays on this PC. Right-click a word to forget just that one."), kCtrlX + 80, Y(r++) + 24, kCtrlW - 80,
              20, kForgetStatus);
        Check(kLt, Tr(L"Grammar check with LanguageTool (online)"), cfg_.languageTool, kLabelX, Y(r++), kW - 50);
        Label(Tr(L"LanguageTool server"), kLabelX, Y(r), kLabelW);
        Edit(kLtUrl, cfg_.languageToolUrl, kCtrlX, Y(r++), kCtrlW);
        Label(Tr(L"The public server allows 20 checks per minute; your own server has no limit."), kCtrlX, Y(r++) - 6,
              kCtrlW, 30);
        Check(kBackTr, Tr(L"Show the back-translation of my message"), cfg_.backTranslate, kLabelX, Y(r++), kW - 50);
        Label(Tr(L"Enter does"), kLabelX, Y(r), kLabelW);
        Combo(kSendMode, {Tr(L"Send into the GW2 chat"), Tr(L"Only copy (not a single key reaches the game)")},
              cfg_.copyOnly ? 1 : 0, kCtrlX, Y(r++), kCtrlW);
        Check(kReturnFocus, Tr(L"Back to this window after sending"), cfg_.returnFocus, kLabelX, Y(r++), kW - 50);
    }

    void BuildTranslator() {
        BeginPage();
        int r = 0;
        Label(Tr(L"Translator"), kLabelX, Y(r), kLabelW);
        Combo(kEngine,
              {Tr(L"Automatic (best available)"), Tr(L"Basic – MyMemory (free, no account)"), L"DeepL",
               Tr(L"LLM (local or cloud)")},
              static_cast<int>(cfg_.engine), kCtrlX, Y(r++), kCtrlW);
        Label(EngineNote(cfg_.engine), kCtrlX, Y(r++) - 6, kCtrlW, 30, kEngineNote);
        Label(Tr(L"DeepL API key"), kLabelX, Y(r), kLabelW);
        Edit(kDeepL, cfg_.deeplKey, kCtrlX, Y(r++), kCtrlW);
        Label(Tr(L"MyMemory e-mail (optional)"), kLabelX, Y(r), kLabelW);
        Edit(kEmail, cfg_.basicEmail, kCtrlX, Y(r++), kCtrlW);
        Label(Tr(L"LLM address"), kLabelX, Y(r), kLabelW);
        Edit(kLlmUrl, cfg_.llmUrl, kCtrlX, Y(r++), kCtrlW);
        Label(Tr(L"LLM model"), kLabelX, Y(r), kLabelW);
        HWND model = Combo(kLlmModel, {}, -1, kCtrlX, Y(r), kCtrlW - 126, true);
        SetWindowTextW(model, cfg_.llmModel.c_str());
        Button(kLlmLoad, Tr(L"Load models"), kCtrlX + kCtrlW - 120, Y(r++) - 1, 120);
        Label(Tr(L"LLM API key (cloud only)"), kLabelX, Y(r), kLabelW);
        Edit(kLlmKey, cfg_.llmKey, kCtrlX, Y(r++), kCtrlW, ES_PASSWORD);
        Check(kFixOcr, Tr(L"LLM repairs text-recognition errors in chat lines"), cfg_.llmFixOcr, kLabelX, Y(r++),
              kW - 50);
        // Local translation, prominent: nothing leaves the PC.
        Label(Tr(L"Translate locally (nothing leaves this PC)"), kLabelX, Y(r), kLabelW, 30);
        std::vector<std::wstring> models;
        for (const LocalModelOffer& m : LocalModelOffers()) models.push_back(m.id);
        Combo(kLocalModel, models, 0, kCtrlX, Y(r), kCtrlW - 126);
        Button(kPull, Tr(L"Install"), kCtrlX + kCtrlW - 120, Y(r++) - 1, 120);
        Label(Tr(LocalModelOffers()[0].summary), kCtrlX, Y(r++) - 4, kCtrlW, 30, kLocalInfo);
        Button(kGetOllama, Tr(L"Get Ollama (free)…"), kCtrlX, Y(r) - 2, 170);
        Label(Tr(L"Runs the models; LM Studio works too (http://localhost:1234)."), kCtrlX + 180, Y(r++) - 2,
              kCtrlW - 180, 30, kPullStatus);
        Button(kTest, Tr(L"Test the translator"), kLabelX, Y(r), 180);
        Label(L"", kLabelX + 190, Y(r++), kW - 230, 40, kTestStatus);
    }

    void BuildGame() {
        BeginPage();
        int r = 0;
        Label(Tr(L"Guild Wars 2 folder"), kLabelX, Y(r), kLabelW);
        Edit(kGw2Dir, cfg_.gw2Dir.empty() ? FindGw2Dir() : cfg_.gw2Dir, kCtrlX, Y(r), kCtrlW - 160);
        Button(kGw2Find, Tr(L"Find"), kCtrlX + kCtrlW - 154, Y(r) - 1, 70);
        Button(kGw2Browse, Tr(L"Browse…"), kCtrlX + kCtrlW - 80, Y(r++) - 1, 80);
        Button(kInstall, Tr(L"Install into the GW2 folder"), kLabelX, Y(r), 220);
        Label(InstallStatusText(), kLabelX + 230, Y(r++) - 2, kW - 270, 34, kInstallStatus);
        Check(kAutostart, Tr(L"Start with Windows (stays hidden until GW2 runs)"), IsAutostartEnabled(), kLabelX,
              Y(r++), kW - 50);
        Check(kDock, Tr(L"Dock to the GW2 window (moves with it)"), cfg_.dock, kLabelX, Y(r++), kW - 50);
        Check(kFollow, Tr(L"Show and hide together with the game"), cfg_.followGame, kLabelX, Y(r++), kW - 50);
        Check(kFocusGameChat, Tr(L"Focus translator when in-game chat is opened"), cfg_.focusOnGameChat, kLabelX,
              Y(r++), kW - 50);
        Label(Tr(L"Connections (read only, nothing is hooked)"), kLabelX, Y(r++), kW - 50);
        Add(L"EDIT", L"", ES_MULTILINE | ES_READONLY | WS_VSCROLL | ES_AUTOVSCROLL, kLabelX, Y(r) - 4, kW - 44, 120,
            kStatus, WS_EX_CLIENTEDGE);
        r += 4;
        Button(kRefresh, Tr(L"Refresh"), kLabelX, Y(r), 110);
        Button(kSetup, Tr(L"Guided setup…"), kLabelX + 120, Y(r++), 160);
    }

    // Who receives the text with the chosen translator (privacy, one line).
    static std::wstring EngineNote(Engine e) {
        switch (e) {
            case Engine::Basic:
                return Tr(L"MyMemory is a public translation memory: texts are sent to mymemory.translated.net and may be stored.");
            case Engine::DeepL:
                return Tr(L"Texts are sent to DeepL (deepl.com).");
            case Engine::Llm:
                return Tr(L"A local LLM keeps everything on this PC; a cloud address sends the texts there.");
            default:
                return Tr(L"Uses DeepL or the LLM when set up, otherwise MyMemory (texts leave this PC).");
        }
    }

    std::wstring InstallStatusText() const {
        const std::wstring here = CurrentExeDir();
        const std::wstring dir = Trim(cfg_.gw2Dir.empty() ? FindGw2Dir() : cfg_.gw2Dir);
        if (!dir.empty() && CompareStringOrdinal(here.c_str(), -1, InstallDirFor(dir).c_str(), -1, TRUE) == CSTR_EQUAL)
            return Tr(L"Installed here.");
        return TrF(L"Runs from: {1}", {here});
    }

    void RefreshStatus() {
        if (ctx_.connectionStatus) SetText(kStatus, ctx_.connectionStatus());
    }

    void OnTabChanged() override { ShowPage(static_cast<size_t>(SendMessageW(tab_, TCM_GETCURSEL, 0, 0))); }

    void OnCommand(int id, int code) override {
        switch (id) {
            case IDOK:
                Save();
                result.saved = true;
                Close();
                break;
            case IDCANCEL:
                Close();
                break;
            case kTessBrowse: {
                const std::wstring dir = PickFolder(hwnd_, Tr(L"Tesseract folder"), Text(kTessPath));
                if (!dir.empty()) {
                    SetText(kTessPath, dir);
                    SetText(kTessStatus, TesseractStatus(dir));
                }
                break;
            }
            case kTessPath:
                if (code == EN_KILLFOCUS) SetText(kTessStatus, TesseractStatus(Text(kTessPath)));
                break;
            case kTessGet:
                ShellExecuteW(hwnd_, L"open", kTesseractUrl, nullptr, nullptr, SW_SHOWNORMAL);
                break;
            case kPickRegion:
            case kCover:
                Save();
                result.saved = true;
                result.action = id == kPickRegion ? DialogResult::Action::PickRegion : DialogResult::Action::CoverChat;
                Close();
                break;
            case kLlmLoad:
                LoadModels();
                break;
            case kEngine:
                if (code == CBN_SELCHANGE) SetText(kEngineNote, EngineNote(static_cast<Engine>(std::max(0, Sel(kEngine)))));
                break;
            case kLocalModel:
                if (code == CBN_SELCHANGE && Sel(kLocalModel) >= 0)
                    SetText(kLocalInfo, Tr(LocalModelOffers()[static_cast<size_t>(Sel(kLocalModel))].summary));
                break;
            case kGetOllama:
                ShellExecuteW(hwnd_, L"open", L"https://ollama.com/download", nullptr, nullptr, SW_SHOWNORMAL);
                break;
            case kPull:
                PullModel();
                break;
            case kForgetAll:
                if (ctx_.forgetLearned &&
                    MessageBoxW(hwnd_, Tr(L"Delete every word the tool has learned from your messages, in all languages?").c_str(),
                                Tr(L"Delete everything learned").c_str(),
                                MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2 | (UiRtl() ? MB_RTLREADING | MB_RIGHT : 0)) == IDYES) {
                    ctx_.forgetLearned();
                    SetText(kForgetStatus, Tr(L"Deleted. The word bar starts from scratch."));
                }
                break;
            case kTest:
                TestTranslator();
                break;
            case kGw2Find: {
                const std::wstring dir = FindGw2Dir();
                SetText(kGw2Dir, dir);
                if (dir.empty()) SetText(kInstallStatus, Tr(L"Not found – please choose the folder."));
                break;
            }
            case kGw2Browse: {
                const std::wstring dir = PickFolder(hwnd_, Tr(L"Guild Wars 2 folder"), Text(kGw2Dir));
                if (!dir.empty()) {
                    const std::wstring resolved = ResolveGw2Dir(dir);
                    SetText(kGw2Dir, resolved.empty() ? dir : resolved);
                }
                break;
            }
            case kInstall:
                Install();
                break;
            case kRefresh:
                RefreshStatus();
                break;
            case kSetup:
                Save();
                result.saved = true;
                result.action = DialogResult::Action::RunSetup;
                Close();
                break;
            default:
                break;
        }
    }

    // Ollama downloads the chosen model; then it is set as the translator.
    void PullModel() {
        const int sel = Sel(kLocalModel);
        if (sel < 0) return;
        const std::wstring model = LocalModelOffers()[static_cast<size_t>(sel)].id;
        SetText(kPullStatus, TrF(L"Installing {1} … (a few minutes for gigabytes)", {model}));
        EnableWindow(Item(kPull), FALSE);
        std::thread([h = hwnd_, url = Text(kLlmUrl), model] {
            auto msg = std::make_unique<PullMsg>();
            msg->model = model;
            if (!OllamaReachable(url)) {
                msg->error = Tr(L"Ollama is not running – install it first (button on the left), then try again.");
            } else {
                msg->ok = PullOllamaModel(url, model, &msg->error);
            }
            if (PostMessageW(h, WM_APP_PULL, 0, reinterpret_cast<LPARAM>(msg.get()))) msg.release();
        }).detach();
    }

    void LoadModels() {
        SetText(kTestStatus, Tr(L"Asking the server for its models …"));
        EnableWindow(Item(kLlmLoad), FALSE);
        std::thread([h = hwnd_, url = Text(kLlmUrl), key = Text(kLlmKey)] {
            auto msg = std::make_unique<ModelsMsg>();
            msg->models = FetchLlmModels(url, key, &msg->error);
            if (PostMessageW(h, WM_APP_MODELS, 0, reinterpret_cast<LPARAM>(msg.get()))) msg.release();
        }).detach();
    }

    void TestTranslator() {
        Config probe = cfg_;
        Collect(probe);
        SetText(kTestStatus, Tr(L"Testing …"));
        EnableWindow(Item(kTest), FALSE);
        std::shared_ptr<Translator> t;
        Engine e = probe.engine;
        if (e == Engine::Auto) e = !probe.deeplKey.empty() ? Engine::DeepL : !probe.llmModel.empty() ? Engine::Llm : Engine::Basic;
        if (e == Engine::DeepL) t = MakeDeepLTranslator(probe.deeplKey);
        else if (e == Engine::Llm) {
            LlmSettings s;
            s.url = probe.llmUrl;
            s.model = probe.llmModel;
            s.apiKey = probe.llmKey;
            s.timeoutMs = probe.llmTimeoutSec * 1000;
            t = MakeLlmTranslator(s);
        } else {
            t = MakeMyMemoryTranslator(probe.basicEmail);
        }
        const std::wstring target = probe.readLang.empty() ? std::wstring(L"DE") : probe.readLang;
        std::thread([h = hwnd_, t, target] {
            const TranslateResult r = t->Translate({{L"Hello! Who wants to join the world boss?", false}}, L"EN", target);
            auto* text = new std::wstring(r.ok ? t->Name() + L": " + r.text : r.error);
            if (!PostMessageW(h, WM_APP_TEST, r.ok ? 1 : 0, reinterpret_cast<LPARAM>(text))) delete text;
        }).detach();
    }

    LRESULT OnApp(UINT msg, WPARAM wp, LPARAM lp) override {
        if (msg == WM_APP_PULL) {
            std::unique_ptr<PullMsg> m(reinterpret_cast<PullMsg*>(lp));
            EnableWindow(Item(kPull), TRUE);
            if (!m->ok) {
                SetText(kPullStatus, TrF(L"Not installed: {1}", {m->error}));
                return 0;
            }
            // Ready: the local model becomes the translator (saved with OK).
            if (Trim(Text(kLlmUrl)).empty()) SetText(kLlmUrl, L"http://localhost:11434");
            SetWindowTextW(Item(kLlmModel), m->model.c_str());
            SendMessageW(Item(kEngine), CB_SETCURSEL, static_cast<WPARAM>(Engine::Llm), 0);
            SetText(kEngineNote, EngineNote(Engine::Llm));
            SetText(kPullStatus, TrF(L"{1} is installed and set as translator – OK saves it.", {m->model}));
            return 0;
        }
        if (msg == WM_APP_MODELS) {
            std::unique_ptr<ModelsMsg> m(reinterpret_cast<ModelsMsg*>(lp));
            EnableWindow(Item(kLlmLoad), TRUE);
            HWND combo = Item(kLlmModel);
            const std::wstring current = WindowText(combo);
            SendMessageW(combo, CB_RESETCONTENT, 0, 0);
            for (const std::wstring& s : m->models) SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s.c_str()));
            SetWindowTextW(combo, current.c_str());
            if (m->models.empty()) SetText(kTestStatus, TrF(L"No models: {1}", {m->error}));
            else {
                SetText(kTestStatus, TrF(L"{1} models found – pick one in the list.", {std::to_wstring(m->models.size())}));
                if (Trim(current).empty()) SendMessageW(combo, CB_SETCURSEL, 0, 0);
                SendMessageW(combo, CB_SHOWDROPDOWN, TRUE, 0);
            }
            return 0;
        }
        if (msg == WM_APP_TEST) {
            std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(lp));
            EnableWindow(Item(kTest), TRUE);
            SetText(kTestStatus, (wp ? L"[OK] " : L"[!] ") + *text);
            return 0;
        }
        return 0;
    }

    void Install() {
        const std::wstring rawDir = Trim(Text(kGw2Dir));
        const std::wstring resolved = ResolveGw2Dir(rawDir);
        const std::wstring dir = resolved.empty() ? rawDir : resolved;
        std::wstring target;
        if (IsGw2Dir(dir)) {
            target = InstallDirFor(dir);
        } else {
            SetText(kInstallStatus, Tr(L"That is not a Guild Wars 2 folder (Gw2-64.exe missing)."));
            return;
        }
        InstallResult r = InstallTo(target);
        if (!r.ok) {
            const std::wstring fallback = UserInstallDir();
            const int answer = MessageBoxW(
                hwnd_,
                TrF(L"The GW2 folder is not writable ({1}).\n\nInstall into your user folder instead?\n{2}",
                    {r.error, fallback})
                    .c_str(),
                L"GW2 Chat Translator", MB_YESNO | MB_ICONQUESTION | (UiRtl() ? MB_RTLREADING | MB_RIGHT : 0));
            if (answer != IDYES) return;
            r = InstallTo(fallback);
            if (!r.ok) {
                SetText(kInstallStatus, r.error);
                return;
            }
        }
        cfg_.gw2Dir = dir;
        if (r.alreadyThere) {
            SetText(kInstallStatus, Tr(L"Installed here."));
            return;
        }
        SetText(kInstallStatus, TrF(L"Installed: {1}", {r.exePath}));
        if (Checked(kAutostart)) SetAutostart(true, r.exePath);
        const int answer = MessageBoxW(hwnd_, Tr(L"Installed. Start the installed copy now? This window closes.").c_str(),
                                       L"GW2 Chat Translator",
                                       MB_YESNO | MB_ICONQUESTION | (UiRtl() ? MB_RTLREADING | MB_RIGHT : 0));
        if (answer == IDYES) {
            Save();
            result.saved = true;
            result.action = DialogResult::Action::RestartInto;
            result.restartExe = r.exePath;
            Close();
        }
    }

    // Reads every control into `c`.
    void Collect(Config& c) const {
        const int ui = Sel(kUiLang);
        if (ui >= 0 && static_cast<size_t>(ui) < UiLanguages().size()) c.uiLang = UiLanguages()[ui].lang;
        const int read = Sel(kReadLang);
        if (read == 0) c.readLang.clear();
        else if (read > 0 && static_cast<size_t>(read - 1) < Languages().size()) c.readLang = Languages()[read - 1].code;
        std::vector<std::wstring> writes;
        for (const std::wstring& code : ParseLangList(Text(kWriteLangs)))
            if (const LangInfo* l = FindLanguage(code)) writes.push_back(l->code);
        if (!writes.empty()) c.writeLangs = writes;
        if (Sel(kFontSize) >= 0) c.fontPercent = kFontSizes[Sel(kFontSize)];
        if (Sel(kOpacity) >= 0) c.opacity = kOpacities[Sel(kOpacity)];
        if (!Trim(Text(kHotkey)).empty()) c.hotkey = Trim(Text(kHotkey));

        c.readerEnabled = Checked(kReaderOn);
        if (Sel(kOcrEngine) >= 0) c.ocr = static_cast<OcrChoice>(Sel(kOcrEngine));
        c.tesseractPath = Trim(Text(kTessPath));
        c.readChinese = Checked(kChinese);
        const int interval = _wtoi(Text(kInterval).c_str());
        if (interval >= 250 && interval <= 10000) c.readerIntervalMs = interval;
        c.showSystemLines = Checked(kShowSystem);
        c.saveCaptures = Checked(kCaptures);

        c.spellEnabled = Checked(kSpell);
        if (Sel(kAutoCorrect) >= 0) c.autoCorrect = static_cast<AutoCorrectMode>(Sel(kAutoCorrect));
        c.suggestions = Checked(kSuggest);
        c.learnWords = Checked(kLearn);
        c.languageTool = Checked(kLt);
        if (!Trim(Text(kLtUrl)).empty()) c.languageToolUrl = Trim(Text(kLtUrl));
        c.backTranslate = Checked(kBackTr);
        c.copyOnly = Sel(kSendMode) == 1;
        c.returnFocus = Checked(kReturnFocus);

        if (Sel(kEngine) >= 0) c.engine = static_cast<Engine>(Sel(kEngine));
        c.deeplKey = Trim(Text(kDeepL));
        c.basicEmail = Trim(Text(kEmail));
        c.llmUrl = Trim(Text(kLlmUrl));
        c.llmModel = Trim(WindowText(Item(kLlmModel)));
        c.llmKey = Trim(Text(kLlmKey));
        c.llmFixOcr = Checked(kFixOcr);

        const std::wstring rawDir = Trim(Text(kGw2Dir));
        const std::wstring resolved = ResolveGw2Dir(rawDir);
        c.gw2Dir = resolved.empty() ? rawDir : resolved;
        c.dock = Checked(kDock);
        c.followGame = Checked(kFollow);
        c.focusOnGameChat = Checked(kFocusGameChat);
    }

    void Save() {
        Collect(cfg_);
        const bool autostart = Checked(kAutostart);
        if (autostart != IsAutostartEnabled() || (autostart && AutostartTarget() != CurrentExePath()))
            SetAutostart(autostart, CurrentExePath());
        cfg_.SaveAll();
    }

    Config& cfg_;
    const DialogContext& ctx_;
    SettingsPage start_;
    HWND tab_ = nullptr;
};

// ---------------------------------------------------------------------------
// Guided setup: 1 language & install, 2 prepare the GW2 chat, 3 mark the area
// ---------------------------------------------------------------------------
class SetupWizard final : public NativeDialog {
public:
    SetupWizard(Config& cfg, const DialogContext& ctx) : cfg_(cfg), ctx_(ctx), startLang_(GetUiLang()) {}
    ~SetupWizard() override {
        if (!result.saved) SetUiLang(startLang_);  // cancelled: the language stays as it was
    }
    DialogResult result;

private:
    static constexpr int kW = 560;

    void Build() override {
        Add(L"STATIC", L"", SS_LEFT | SS_NOPREFIX, 24, 16, kW - 48, 28, kStepTitle);
        SendMessageW(Item(kStepTitle), WM_SETFONT, reinterpret_cast<WPARAM>(bold_), FALSE);

        // Step 1
        BeginPage();
        Label(Tr(L"Language / Sprache / اللغة"), 24, 60, 190);
        std::vector<std::wstring> ui;
        int uiSel = 0;
        for (size_t i = 0; i < UiLanguages().size(); ++i) {
            ui.push_back(UiLanguages()[i].native);
            if (UiLanguages()[i].lang == cfg_.uiLang) uiSel = static_cast<int>(i);
        }
        Combo(kUiLang, ui, uiSel, 220, 60, 300);
        Label(Tr(L"Translate the chat into"), 24, 96, 190);
        std::vector<std::wstring> langs{Tr(L"Windows language")};
        int readSel = 0;
        for (size_t i = 0; i < Languages().size(); ++i) {
            langs.push_back(std::wstring(Languages()[i].native) + L"  (" + Languages()[i].code + L")");
            if (!cfg_.readLang.empty() && FindLanguage(cfg_.readLang) == &Languages()[i]) readSel = static_cast<int>(i) + 1;
        }
        Combo(kReadLang, langs, readSel, 220, 96, 300);
        Label(Tr(L"Guild Wars 2 folder"), 24, 140, 190);
        const std::wstring found = cfg_.gw2Dir.empty() ? FindGw2Dir() : cfg_.gw2Dir;
        Edit(kGw2Dir, found, 220, 140, 214);
        Button(kGw2Browse, Tr(L"Browse…"), 440, 139, 80);
        Check(kInstall, Tr(L"Install into the GW2 folder (addons\\GW2ChatTranslator)"), !found.empty(), 24, 178,
              kW - 48);
        Check(kAutostart, Tr(L"Start with Windows and appear when GW2 runs"), true, 24, 206, kW - 48);
        Label(Tr(L"Nothing is put into the game itself: no DLL, no hook. The tool only looks at the screen and "
                 L"the clipboard, and sends a line only when you press Enter."),
              24, 244, kW - 48, 54);

        // Step 2
        BeginPage();
        Label(Tr(L"The tool reads what GW2 shows in its chat panel. Prepare it once:\n\n"
                 L"1.  Keep the GW2 chat panel open. It may be small (8–12 lines are enough). A minimized "
                 L"chat cannot be read.\n\n"
                 L"2.  Use a chat tab that shows all channels (right-click the tab in GW2 → tick all channels). "
                 L"Filter here with our own tabs instead.\n\n"
                 L"3.  GW2 Options → Chat: turn timestamps on and choose a large text size. Our window lies over "
                 L"the chat anyway, so large letters cost you nothing and are read much better.\n\n"
                 L"4.  Whispers in a minimized chat only flash for a moment: with the panel open nothing is missed."),
              24, 56, kW - 48, 260);

        // Step 3
        BeginPage();
        Label(Tr(L"Last step: draw a frame around the text lines of the GW2 chat (without the tabs and the input "
                 L"line). GW2 must be running.\n\nAfterwards this window can lie exactly over the GW2 chat and "
                 L"replace it: the GW2 chat stays open underneath and keeps being read, you only see the "
                 L"translated one."),
              24, 56, kW - 48, 120);
        Check(kCover, Tr(L"Lay this window over the GW2 chat afterwards"), true, 24, 190, kW - 48);
        Button(kPickRegion, Tr(L"Mark the chat area now"), 24, 226, 240, 30);
        Label(Tr(L"You can redo all of this later: menu ≡ → Setup."), 24, 276, kW - 48, 22);
        EndPages();

        Button(kBack, Tr(L"< Back"), kW - 330, 330, 100);
        Button(kNext, Tr(L"Next >"), kW - 222, 330, 100);
        Button(IDCANCEL, Tr(L"Close"), kW - 114, 330, 100);
        Go(0);
    }

    void Go(int step) {
        step_ = std::clamp(step, 0, 2);
        static const wchar_t* titles[] = {L"1 / 3  –  Language and installation", L"2 / 3  –  Prepare the GW2 chat",
                                          L"3 / 3  –  Mark the chat"};
        SetText(kStepTitle, Tr(titles[step_]));
        ShowPage(static_cast<size_t>(step_));
        EnableWindow(Item(kBack), step_ > 0);
        SetText(kNext, step_ == 2 ? Tr(L"Finish") : Tr(L"Next >"));
        if (step_ == 2) {
            const bool running = ctx_.isGw2Running ? ctx_.isGw2Running() : false;
            EnableWindow(Item(kPickRegion), running);
        }
    }

    bool ApplyStep1() {
        const int ui = Sel(kUiLang);
        if (ui >= 0 && static_cast<size_t>(ui) < UiLanguages().size()) cfg_.uiLang = UiLanguages()[ui].lang;
        const int read = Sel(kReadLang);
        if (read == 0) cfg_.readLang.clear();
        else if (read > 0 && static_cast<size_t>(read - 1) < Languages().size()) cfg_.readLang = Languages()[read - 1].code;
        const std::wstring rawDir = Trim(Text(kGw2Dir));
        const std::wstring resolved = ResolveGw2Dir(rawDir);
        const std::wstring dir = resolved.empty() ? rawDir : resolved;
        if (!dir.empty()) cfg_.gw2Dir = dir;
        // Saved before installing: the installed copy takes this settings file along.
        cfg_.setupDone = true;
        cfg_.SaveAll();
        std::wstring exe = CurrentExePath();
        if (Checked(kInstall)) {
            if (!IsGw2Dir(dir)) {
                MessageBoxW(hwnd_, Tr(L"That is not a Guild Wars 2 folder (Gw2-64.exe missing).").c_str(),
                            L"GW2 Chat Translator", MB_OK | MB_ICONWARNING | (UiRtl() ? MB_RTLREADING | MB_RIGHT : 0));
                return false;
            }
            InstallResult r = InstallTo(InstallDirFor(dir));
            if (!r.ok) r = InstallTo(UserInstallDir());
            if (!r.ok) {
                MessageBoxW(hwnd_, r.error.c_str(), L"GW2 Chat Translator", MB_OK | MB_ICONWARNING);
                return false;
            }
            if (!r.alreadyThere) installedExe_ = r.exePath;
            exe = r.exePath;
        }
        SetAutostart(Checked(kAutostart), exe);
        return true;
    }

    void Finish(bool pick) {
        const bool running = ctx_.isGw2Running ? ctx_.isGw2Running() : false;
        if (!running) pick = false;
        if (!cfg_.regionSet) {
            cfg_.regionSet = true;
            cfg_.regionLeft = 10;
            cfg_.regionFromBottom = 300;
            cfg_.regionWidth = 460;
            cfg_.regionHeight = 260;
        }
        cfg_.setupDone = true;
        cfg_.SaveAll();
        result.saved = true;
        if (!installedExe_.empty()) {
            // The installed copy marks the chat with its own (copied) settings file.
            result.action = DialogResult::Action::RestartInto;
            result.restartExe = installedExe_;
            result.markChatAfterRestart = pick;
            result.coverAfterRestart = Checked(kCover);
        } else if (pick) {
            result.action = Checked(kCover) ? DialogResult::Action::CoverChat : DialogResult::Action::PickRegion;
        }
        Close();
    }

    void OnCommand(int id, int code) override {
        switch (id) {
            case kUiLang:
                // Switch the language right away, so the next steps are readable.
                if (code == CBN_SELCHANGE) {
                    const int ui = Sel(kUiLang);
                    if (ui >= 0 && static_cast<size_t>(ui) < UiLanguages().size()) {
                        const int read = Sel(kReadLang);
                        if (read == 0) cfg_.readLang.clear();
                        else if (read > 0 && static_cast<size_t>(read - 1) < Languages().size())
                            cfg_.readLang = Languages()[read - 1].code;
                        if (!Trim(Text(kGw2Dir)).empty()) cfg_.gw2Dir = Trim(Text(kGw2Dir));
                        cfg_.uiLang = UiLanguages()[ui].lang;
                        SetUiLang(cfg_.uiLang);
                        RebuildAll(Tr(L"Setup") + L" \u2013 GW2 Chat Translator");
                    }
                }
                break;
            case kBack:
                Go(step_ - 1);
                break;
            case kNext:
                if (step_ == 0 && !ApplyStep1()) break;
                if (step_ == 2) Finish(false);
                else Go(step_ + 1);
                break;
            case kPickRegion: {
                const bool running = ctx_.isGw2Running ? ctx_.isGw2Running() : false;
                if (!running) {
                    MessageBoxW(hwnd_, Tr(L"GW2 window not found (start the game)").c_str(),
                                L"GW2 Chat Translator", MB_OK | MB_ICONINFORMATION | (UiRtl() ? MB_RTLREADING | MB_RIGHT : 0));
                    break;
                }
                Finish(true);
                break;
            }
            case kGw2Browse: {
                const std::wstring dir = PickFolder(hwnd_, Tr(L"Guild Wars 2 folder"), Text(kGw2Dir));
                if (!dir.empty()) {
                    const std::wstring resolved = ResolveGw2Dir(dir);
                    SetText(kGw2Dir, resolved.empty() ? dir : resolved);
                }
                break;
            }
            case IDCANCEL:
                Close();
                break;
            default:
                break;
        }
    }

    Config& cfg_;
    const DialogContext& ctx_;
    UiLang startLang_;
    int step_ = 0;
    std::wstring installedExe_;
};

}  // namespace

DialogResult ShowSettingsDialog(HWND owner, HINSTANCE inst, Config& cfg, const DialogContext& ctx, SettingsPage start) {
    SettingsDialog dlg(cfg, ctx, start);
    dlg.Run(owner, inst, Tr(L"Settings") + L" – GW2 Chat Translator", 580, 530);
    return dlg.result;
}

DialogResult ShowSetupWizard(HWND owner, HINSTANCE inst, Config& cfg, const DialogContext& ctx) {
    SetupWizard dlg(cfg, ctx);
    dlg.Run(owner, inst, Tr(L"Setup") + L" – GW2 Chat Translator", 560, 372);
    return dlg.result;
}

}  // namespace gct
