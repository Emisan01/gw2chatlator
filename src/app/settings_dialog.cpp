// settings_dialog.cpp — settings window and guided setup, built from
// native controls without a resource script.
#include "settings_dialog.hpp"

#include <commctrl.h>
#include <shellapi.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

#include "core/cloud_mt_protocol.hpp"
#include "core/gw2_install.hpp"
#include "core/langs.hpp"
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
constexpr UINT WM_APP_COMPARE = WM_APP + 53;
constexpr wchar_t kTesseractUrl[] = L"https://github.com/UB-Mannheim/tesseract/wiki";

enum : int {
    kIdTab = 100,
    // General
    kUiLang, kReadLang, kWriteLangs, kFontSize, kOpacity, kHotkey, kFontSizeValue, kOpacityValue, kUnderstood,
    kSkipEn, kSkipDe, kSkipFr, kSkipEs,
    kAutoWhisper, kAutoGroup, kAutoGuild, kAutoMap, kAutoTeam,
    // Reading
    kReaderOn, kOcrEngine, kCapture, kTessPath, kTessBrowse, kTessStatus, kTessGet, kChinese, kInterval, kShowSystem, kCaptures,
    kPickRegion, kCover,
    // Writing
    kSpell, kAutoCorrect, kSuggest, kLearn, kForgetAll, kForgetStatus, kLt, kLtUrl, kBackTr, kSendMode, kReturnFocus,
    // Translator
    kEngine, kEngineNote, kLocalModel, kPull, kLocalInfo, kGetOllama, kPullStatus, kDeepL, kEmail, kLlmUrl, kLlmModel, kLlmLoad, kLlmKey, kFixOcr, kTest, kTestStatus,
    kGoogleKey, kGoogleGet, kMsKey, kMsRegion, kMsGet, kDeepLGet, kLlmPreset, kLlmGetKey, kLlmNote, kLibreUrl, kLibreKey, kLibreGet,
    kCorrInfo, kCorrExport, kCorrImport, kCorrClear, kTechCompare, kLibreLocal,
    // Game & start
    kGw2Dir, kGw2Find, kGw2Browse, kInstall, kInstallStatus, kAutostart, kDock, kFollow, kFocusGameChat, kStatus, kRefresh, kSetup,
    // Wizard
    // Technical
    kOcrZoom, kKeyHold, kStepDelay, kTechText, kTechRefresh, kTechCopy, kTechStatus,
    kBack, kNext, kStepTitle, kStepText,
};

// Transparency in percent (0 = opaque) <-> layered-window alpha.
int TransparencyOf(int opacity) { return std::clamp((255 - opacity) * 100 / 255, 0, 75); }
int OpacityOf(int transparency) { return 255 - std::clamp(transparency, 0, 75) * 255 / 100; }

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
    virtual void OnRebuild() {}
    virtual void OnSlider() {}
    virtual LRESULT OnApp(UINT, WPARAM, LPARAM) { return 0; }
    void Close() { done_ = true; }

    int S(int v) const { return MulDiv(v, dpi_, 96); }

    HWND Add(const wchar_t* cls, const std::wstring& text, DWORD style, int x, int y, int w, int h, int id,
             DWORD ex = 0) {
        HWND c = CreateWindowExW(ex, cls, text.c_str(), WS_CHILD | WS_VISIBLE | style, S(x), S(y), S(w), S(h), hwnd_,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst_, nullptr);
        SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font_), FALSE);
        if (page_ >= 0) pages_[static_cast<size_t>(page_)].push_back(c);
        if (group_) group_->push_back(c);
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
        group_ = nullptr;
        OnRebuild();
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
    std::vector<HWND>* group_ = nullptr;  // controls created now also go here (sections shown on demand)

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
            case WM_HSCROLL:
                self->OnSlider();
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

// Translators in the order of the list (the ini key stays the enum).
constexpr Engine kEngineOrder[] = {Engine::Auto,  Engine::Basic, Engine::Google, Engine::Microsoft,
                                   Engine::DeepL, Engine::Libre, Engine::Llm};
constexpr int kEngineCount = static_cast<int>(sizeof(kEngineOrder) / sizeof(kEngineOrder[0]));
int EngineIndex(Engine e) {
    for (int i = 0; i < kEngineCount; ++i)
        if (kEngineOrder[i] == e) return i;
    return 0;
}
Engine EngineAt(int index) { return index >= 0 && index < kEngineCount ? kEngineOrder[index] : Engine::Auto; }

// AI models: one click fills address and model; the button opens the page
// where the key is made. All speak the OpenAI chat format.
struct LlmPreset {
    const wchar_t* name;
    const wchar_t* url;     // empty = keep what is there (custom)
    const wchar_t* model;   // empty = keep / pick with "Load models"
    const wchar_t* keyUrl;  // nullptr = no key needed
    const wchar_t* note;    // English UI text (Tr)
};
const LlmPreset kLlmPresets[] = {
    {L"Local (Ollama / LM Studio)", L"http://localhost:11434", L"", nullptr,
     L"Runs on this PC: nothing leaves it, no costs. Needs a graphics card for good speed."},
    {L"Claude (Anthropic)", L"https://api.anthropic.com/v1", L"claude-haiku-4-5",
     L"https://console.anthropic.com/settings/keys",
     L"Very good and fast. Paid by use: roughly 1 $ per 1,000 chat lines with Haiku."},
    {L"Gemini (Google, free key)", L"https://generativelanguage.googleapis.com/v1beta/openai", L"gemini-flash-latest",
     L"https://aistudio.google.com/apikey",
     L"Free key without a credit card (daily limits). On the free tier Google may use the texts to improve its "
     L"products."},
    {L"GPT (OpenAI)", L"https://api.openai.com/v1", L"gpt-5-mini", L"https://platform.openai.com/api-keys",
     L"Very good. Paid by use; \"Load models\" shows the current models."},
    {L"Mistral (free tier)", L"https://api.mistral.ai/v1", L"mistral-small-latest",
     L"https://console.mistral.ai/api-keys", L"European provider with a free experiment tier (limits per minute)."},
    {L"Groq (free key, very fast)", L"https://api.groq.com/openai/v1", L"llama-3.3-70b-versatile",
     L"https://console.groq.com/keys", L"Free key with daily limits, answers in a fraction of a second."},
    {L"OpenRouter (many models)", L"https://openrouter.ai/api/v1", L"", L"https://openrouter.ai/settings/keys",
     L"One key for many providers, some models free. Pick one with \"Load models\"."},
    {L"Other address", L"", L"", nullptr, L"Any OpenAI-compatible address."},
};
constexpr int kPresetCount = static_cast<int>(sizeof(kLlmPresets) / sizeof(kLlmPresets[0]));
constexpr int kPresetLocal = 0, kPresetOther = kPresetCount - 1;

// Which preset an address belongs to (by host).
int PresetOf(const std::wstring& url) {
    const std::wstring u = CaseFold(Trim(url));
    if (u.empty() || IsLocalLlmUrl(u)) return kPresetLocal;
    for (int i = 1; i < kPresetOther; ++i) {
        std::wstring host = CaseFold(kLlmPresets[i].url);
        host = host.substr(0, host.find(L'/', 8));
        if (u.rfind(host, 0) == 0) return i;
    }
    return kPresetOther;
}

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
        const wchar_t* names[] = {L"General", L"Reading the chat", L"Writing", L"Translator", L"Game & start",
                                  L"Technical"};
        for (int i = 0; i < 6; ++i) {
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
        BuildTechnical();
        EndPages();
        Button(IDOK, Tr(L"OK"), kW - 220, 490, 100);
        Button(IDCANCEL, Tr(L"Cancel"), kW - 112, 490, 100);
        SendMessageW(tab_, TCM_SETCURSEL, static_cast<WPARAM>(start_), 0);
        ShowPage(static_cast<size_t>(start_));
        UpdateTranslatorView();
        lastPreset_ = Sel(kLlmPreset);
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

        // Everything is translated; these are the exceptions (a click on a line still translates it).
        Label(Tr(L"Do not translate"), kLabelX, Y(r), kLabelW);
        auto has = [&](const wchar_t* code) {
            for (const std::wstring& c : cfg_.understoodLangs)
                if (PrimaryLang(c) == code) return true;
            return false;
        };
        const wchar_t* quick[] = {L"EN", L"DE", L"FR", L"ES"};
        for (int i = 0; i < 4; ++i) Check(kSkipEn + i, quick[i], has(quick[i]), kCtrlX + i * 60, Y(r), 56);
        std::wstring others;
        for (const std::wstring& c : cfg_.understoodLangs) {
            const std::wstring p = PrimaryLang(c);
            if (p != L"EN" && p != L"DE" && p != L"FR" && p != L"ES") others += (others.empty() ? L"" : L", ") + c;
        }
        HWND more = Edit(kUnderstood, others, kCtrlX + 245, Y(r++), 110);
        SendMessageW(more, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(Tr(L"more: IT, PL …").c_str()));
        Label(Tr(L"Translate in"), kLabelX, Y(r), kLabelW);
        const ChannelMask a = cfg_.autoTranslate;
        Check(kAutoWhisper, Tr(L"Whisper"), (a & ChannelBit(Channel::Whisper)) != 0, kCtrlX, Y(r), 110);
        Check(kAutoGroup, Tr(L"Party & squad"), (a & ChannelBit(Channel::Party)) != 0, kCtrlX + 115, Y(r), 130);
        Check(kAutoGuild, Tr(L"Guild"), (a & ChannelBit(Channel::Guild)) != 0, kCtrlX + 250, Y(r++), 100);
        Check(kAutoMap, Tr(L"Map & say"), (a & ChannelBit(Channel::Map)) != 0, kCtrlX, Y(r) - 6, 110);
        Check(kAutoTeam, Tr(L"Team (WvW)"), (a & ChannelBit(Channel::Team)) != 0, kCtrlX + 115, Y(r) - 6, 130);
        Label(Tr(L"Unticked: click a line."), kCtrlX + 250, Y(r++) - 2, 110, 20);

        Label(Tr(L"“Send as” languages"), kLabelX, Y(r), kLabelW);
        std::wstring joined;
        for (const std::wstring& c : cfg_.writeLangs) joined += (joined.empty() ? L"" : L", ") + c;
        Edit(kWriteLangs, joined, kCtrlX, Y(r++), kCtrlW);
        Label(Tr(L"Codes like EN-GB, FR, ES, DE, AR, ZH-HANS. Ctrl+L switches while typing."), kCtrlX, Y(r++) - 6,
              kCtrlW, 30);

        // Sliders with a live preview on the window behind.
        Label(Tr(L"Text size"), kLabelX, Y(r), kLabelW);
        HWND size = Add(TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, kCtrlX, Y(r), kCtrlW - 60, 26, kFontSize);
        SendMessageW(size, TBM_SETRANGE, TRUE, MAKELPARAM(80, 160));
        SendMessageW(size, TBM_SETPOS, TRUE, cfg_.fontPercent);
        Label(std::to_wstring(cfg_.fontPercent) + L" %", kCtrlX + kCtrlW - 54, Y(r++), 54, 18, kFontSizeValue);
        Label(Tr(L"Transparency"), kLabelX, Y(r), kLabelW);
        HWND tr = Add(TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, kCtrlX, Y(r), kCtrlW - 60, 26, kOpacity);
        SendMessageW(tr, TBM_SETRANGE, TRUE, MAKELPARAM(0, 75));
        SendMessageW(tr, TBM_SETPOS, TRUE, TransparencyOf(cfg_.opacity));
        Label(std::to_wstring(TransparencyOf(cfg_.opacity)) + L" %", kCtrlX + kCtrlW - 54, Y(r++), 54, 18, kOpacityValue);
        Label(Tr(L"0 % = not transparent at all."), kCtrlX, Y(r++) - 8, kCtrlW, 20);

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
        Label(Tr(L"Picture of the chat"), kLabelX, Y(r), kLabelW);
        Combo(kCapture,
              {Tr(L"Automatic (no yellow frame)"), Tr(L"Game window (Windows 10: yellow frame)"),
               Tr(L"Screen (never a frame)")},
              cfg_.captureMode, kCtrlX, Y(r++), kCtrlW);
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
        Button(kForgetAll, Tr(L"Delete everything learned…"), kCtrlX + 80, Y(r++) - 2, 220);
        // Own row: below the button it used to run into the next line.
        Label(Tr(L"Stays on this PC. Right-click a word to forget just that one."), kLabelX + 20, Y(r++) - 8,
              kW - 70, 20, kForgetStatus);
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
        // Correction memory (right-click a translated line → "Correct this translation").
        Label(Tr(L"Corrected translations"), kLabelX, Y(r), kLabelW);
        Label(ctx_.correctionsInfo ? ctx_.correctionsInfo() : L"", kCtrlX, Y(r++), kCtrlW, 18, kCorrInfo);
        Button(kCorrExport, Tr(L"Export…"), kCtrlX, Y(r) - 4, 110);
        Button(kCorrImport, Tr(L"Import…"), kCtrlX + 118, Y(r) - 4, 110);
        Button(kCorrClear, Tr(L"Delete all…"), kCtrlX + 236, Y(r++) - 4, 120);
    }

    void BuildTranslator() {
        BeginPage();
        int r = 0;
        Label(Tr(L"Translator"), kLabelX, Y(r), kLabelW);
        Combo(kEngine,
              {Tr(L"Automatic (the first one set up)"), Tr(L"MyMemory (free, no account)"),
               Tr(L"Google Translate (free contingent, key)"), Tr(L"Microsoft Translator (free contingent, key)"),
               Tr(L"DeepL (free contingent, key)"), Tr(L"Own translation server (any address)"),
               Tr(L"AI model (local or cloud)")},
              EngineIndex(cfg_.engine), kCtrlX, Y(r++), kCtrlW);
        Label(EngineNote(cfg_.engine), kCtrlX, Y(r++) - 6, kCtrlW, 30, kEngineNote);
        const int top = r;  // the sections below share the same rows; only the chosen one is shown

        // Automatic
        group_ = &secAuto_;
        Label(Tr(L"Automatic takes the first translator that is set up: DeepL, Google, Microsoft, your own server, "
                 L"then the AI model; without any, MyMemory. Choose one in the list to enter its key."),
              kCtrlX, Y(top), kCtrlW, 60);

        // MyMemory
        group_ = &secBasic_;
        r = top;
        Label(Tr(L"E-mail (optional)"), kLabelX, Y(r), kLabelW);
        Edit(kEmail, cfg_.basicEmail, kCtrlX, Y(r++), kCtrlW);
        Label(Tr(L"Without an account 5,000 characters a day. With your e-mail address 50,000 a day – no sign-up, "
                 L"the address is only sent along with each request."),
              kCtrlX, Y(r++) - 6, kCtrlW, 44);
        Label(Tr(L"What MyMemory does: it translates the lines it gets and keeps them in its public translation "
                 L"memory. Your learned words stay on this PC. With another translator MyMemory is not used at all."),
              kCtrlX, Y(r++) + 8, kCtrlW, 60);

        // Google
        group_ = &secGoogle_;
        r = top;
        Label(Tr(L"API key"), kLabelX, Y(r), kLabelW);
        Edit(kGoogleKey, cfg_.googleKey, kCtrlX, Y(r++), kCtrlW, ES_PASSWORD);
        Button(kGoogleGet, Tr(L"Get a Google key…"), kCtrlX, Y(r++), 200);
        Label(Tr(L"500,000 characters a month free. Needs a Google Cloud project with the Cloud Translation API "
                 L"switched on and billing set up (the free part costs nothing). Texts go to Google."),
              kCtrlX, Y(r++), kCtrlW, 60);

        // Microsoft
        group_ = &secMicrosoft_;
        r = top;
        Label(Tr(L"API key"), kLabelX, Y(r), kLabelW);
        Edit(kMsKey, cfg_.msKey, kCtrlX, Y(r++), kCtrlW, ES_PASSWORD);
        Label(Tr(L"Region"), kLabelX, Y(r), kLabelW);
        Edit(kMsRegion, cfg_.msRegion, kCtrlX, Y(r++), 160);
        Button(kMsGet, Tr(L"Get a Microsoft key…"), kCtrlX, Y(r++), 200);
        Label(Tr(L"Free tier F0: 2 million characters a month. In Azure create a \"Translator\" resource, copy key 1 "
                 L"and its region (e.g. westeurope). Texts go to Microsoft."),
              kCtrlX, Y(r++), kCtrlW, 60);

        // DeepL
        group_ = &secDeepL_;
        r = top;
        Label(Tr(L"API key"), kLabelX, Y(r), kLabelW);
        Edit(kDeepL, cfg_.deeplKey, kCtrlX, Y(r++), kCtrlW, ES_PASSWORD);
        Button(kDeepLGet, Tr(L"Get a DeepL key…"), kCtrlX, Y(r++), 200);
        Label(Tr(L"DeepL API Free: 500,000 characters a month, very good quality. DeepL asks for a credit card to "
                 L"verify you (the free plan is not charged). Texts go to DeepL."),
              kCtrlX, Y(r++), kCtrlW, 60);

        // Own server (LibreTranslate-compatible): anything not in the list
        group_ = &secLibre_;
        r = top;
        Label(Tr(L"Address"), kLabelX, Y(r), kLabelW);
        Edit(kLibreUrl, cfg_.libreUrl, kCtrlX, Y(r++), kCtrlW);
        Label(Tr(L"API key (if needed)"), kLabelX, Y(r), kLabelW);
        Edit(kLibreKey, cfg_.libreKey, kCtrlX, Y(r++), kCtrlW, ES_PASSWORD);
        Button(kLibreGet, Tr(L"What is LibreTranslate?…"), kCtrlX, Y(r), 172);
        Button(kLibreLocal, Tr(L"Set up on this PC…"), kCtrlX + 180, Y(r++), 176);
        Label(Tr(L"Any LibreTranslate-compatible server, e.g. one you run yourself (free, unlimited, nothing leaves "
                 L"your network: http://localhost:5000) or an instance you have access to."),
              kCtrlX, Y(r++), kCtrlW, 60);

        // AI model (the same model translates and, if ticked, repairs recognition errors)
        group_ = &secLlm_;
        r = top;
        Label(Tr(L"Provider"), kLabelX, Y(r), kLabelW);
        std::vector<std::wstring> presets;
        for (const LlmPreset& p : kLlmPresets) presets.push_back(Tr(p.name));
        const int preset = PresetOf(cfg_.llmUrl);
        Combo(kLlmPreset, presets, preset, kCtrlX, Y(r), kCtrlW - 136);
        Button(kLlmGetKey, Tr(L"Get API key…"), kCtrlX + kCtrlW - 130, Y(r++) - 1, 130);
        Label(Tr(L"Address"), kLabelX, Y(r), kLabelW);
        Edit(kLlmUrl, cfg_.llmUrl, kCtrlX, Y(r++), kCtrlW);
        Label(Tr(L"Model"), kLabelX, Y(r), kLabelW);
        HWND model = Combo(kLlmModel, {}, -1, kCtrlX, Y(r), kCtrlW - 126, true);
        SetWindowTextW(model, cfg_.llmModel.c_str());
        Button(kLlmLoad, Tr(L"Load models"), kCtrlX + kCtrlW - 120, Y(r++) - 1, 120);
        Label(Tr(L"API key"), kLabelX, Y(r), kLabelW);
        Edit(kLlmKey, cfg_.llmKey, kCtrlX, Y(r++), kCtrlW, ES_PASSWORD);
        Check(kFixOcr, Tr(L"The same model also repairs recognition errors in incoming lines"), cfg_.llmFixOcr,
              kCtrlX, Y(r++), kCtrlW);
        Label(Tr(kLlmPresets[preset].note), kCtrlX, Y(r++) - 4, kCtrlW, 30, kLlmNote);
        // Local models, with an install button (only for the local provider).
        group_ = &secLocal_;
        std::vector<std::wstring> models;
        for (const LocalModelOffer& m : LocalModelOffers()) models.push_back(m.id);
        Label(Tr(L"Install a model"), kLabelX, Y(r), kLabelW);
        Combo(kLocalModel, models, 0, kCtrlX, Y(r), kCtrlW - 126);
        Button(kPull, Tr(L"Install"), kCtrlX + kCtrlW - 120, Y(r++) - 1, 120);
        Label(Tr(LocalModelOffers()[0].summary), kCtrlX, Y(r++) - 4, kCtrlW, 30, kLocalInfo);
        Button(kGetOllama, Tr(L"Get Ollama (free)…"), kCtrlX, Y(r) - 2, 170);
        Label(Tr(L"Runs the models; LM Studio works too (http://localhost:1234)."), kCtrlX + 180, Y(r++) - 2,
              kCtrlW - 180, 30, kPullStatus);
        group_ = nullptr;

        Button(kTest, Tr(L"Test the translator"), kLabelX, Y(12), 180);
        Label(L"", kLabelX + 190, Y(12), kW - 230, 40, kTestStatus);
    }

    // Only the section of the chosen translator is visible (all keep their values).
    void UpdateTranslatorView() {
        const bool page = SendMessageW(tab_, TCM_GETCURSEL, 0, 0) == static_cast<LRESULT>(SettingsPage::Translator);
        const Engine e = EngineAt(Sel(kEngine));
        const bool local = Sel(kLlmPreset) == kPresetLocal;
        auto show = [&](const std::vector<HWND>& g, bool on) {
            for (HWND h : g) ShowWindow(h, page && on ? SW_SHOW : SW_HIDE);
        };
        show(secAuto_, e == Engine::Auto);
        show(secBasic_, e == Engine::Basic);
        show(secGoogle_, e == Engine::Google);
        show(secMicrosoft_, e == Engine::Microsoft);
        show(secDeepL_, e == Engine::DeepL);
        show(secLibre_, e == Engine::Libre);
        show(secLlm_, e == Engine::Llm);
        show(secLocal_, e == Engine::Llm && local);
        EnableWindow(Item(kLlmGetKey), kLlmPresets[std::max(0, Sel(kLlmPreset))].keyUrl != nullptr);
    }

    void ApplyPreset() {
        const int i = Sel(kLlmPreset);
        if (i < 0) return;
        const LlmPreset& p = kLlmPresets[i];
        if (*p.url) SetText(kLlmUrl, p.url);
        // A model of another provider makes no sense here: preset model, else empty (pick with "Load models").
        bool cloudModel = false;
        for (const LlmPreset& q : kLlmPresets)
            cloudModel = cloudModel || (*q.model && WindowText(Item(kLlmModel)) == q.model);
        if (*p.model) SetWindowTextW(Item(kLlmModel), p.model);
        else if (i != kPresetOther && cloudModel) SetWindowTextW(Item(kLlmModel), L"");
        SetText(kLlmNote, Tr(p.note));
        UpdateTranslatorView();
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
            case Engine::Google:
                return Tr(L"Texts are sent to Google (Cloud Translation).");
            case Engine::Microsoft:
                return Tr(L"Texts are sent to Microsoft (Azure Translator).");
            case Engine::Libre:
                return Tr(L"Texts are sent to the server you enter (your own one keeps them in your network).");
            case Engine::Llm:
                return Tr(L"A local model keeps everything on this PC; a cloud provider receives the texts.");
            default:
                return Tr(L"The translator with a key is used, otherwise MyMemory (texts leave this PC).");
        }
    }

    // Technical page: the parameters that matter for speed and recognition,
    // the live numbers, and a diagnosis to copy (no chat text in it).
    void BuildTechnical() {
        BeginPage();
        int r = 0;
        Label(Tr(L"Enlargement for recognition"), kLabelX, Y(r), kLabelW);
        Edit(kOcrZoom, std::to_wstring(cfg_.ocrScale), kCtrlX, Y(r), 60, ES_NUMBER);
        Label(Tr(L"0 = automatic from the line spacing, 1–4 fixed"), kCtrlX + 70, Y(r++) + 4, kCtrlW - 70, 20);
        Label(Tr(L"Key held when sending (ms)"), kLabelX, Y(r), kLabelW);
        Edit(kKeyHold, std::to_wstring(cfg_.send.keyHoldMs), kCtrlX, Y(r), 60, ES_NUMBER);
        Label(Tr(L"raise it at low FPS if messages do not arrive"), kCtrlX + 70, Y(r++) + 4, kCtrlW - 70, 20);
        Label(Tr(L"Pause between keys (ms)"), kLabelX, Y(r), kLabelW);
        Edit(kStepDelay, std::to_wstring(cfg_.send.stepDelayMs), kCtrlX, Y(r++), 60, ES_NUMBER);
        Add(L"EDIT", L"", ES_MULTILINE | ES_READONLY | WS_VSCROLL | WS_HSCROLL | ES_AUTOVSCROLL | ES_AUTOHSCROLL, kLabelX,
            Y(r) - 2, kW - 44, 250, kTechText, WS_EX_CLIENTEDGE);
        r += 8;
        Button(kTechRefresh, Tr(L"Refresh"), kLabelX, Y(r), 100);
        Button(kTechCopy, Tr(L"Copy diagnosis"), kLabelX + 108, Y(r), 140);
        Button(kTechCompare, Tr(L"Compare translators"), kLabelX + 256, Y(r), 170);
        Label(Tr(L"Numbers and settings only, no chat text."), kLabelX, Y(r) + 30, kW - 44, 20, kTechStatus);
        RefreshTechnical();
    }

    void RefreshTechnical() {
        if (ctx_.technicalStatus) SetText(kTechText, ctx_.technicalStatus());
    }

    void CopyTechnical() {
        const std::wstring text = Text(kTechText);
        if (text.empty() || !OpenClipboard(hwnd_)) return;
        EmptyClipboard();
        const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
        if (HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
            if (void* p = GlobalLock(g)) {
                std::memcpy(p, text.c_str(), bytes);
                GlobalUnlock(g);
                if (!SetClipboardData(CF_UNICODETEXT, g)) GlobalFree(g);
            } else {
                GlobalFree(g);
            }
        }
        CloseClipboard();
        SetText(kTechStatus, Tr(L"Copied – paste it where you report the problem."));
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

    void OnTabChanged() override {
        ShowPage(static_cast<size_t>(SendMessageW(tab_, TCM_GETCURSEL, 0, 0)));
        UpdateTranslatorView();
    }
    void OnRebuild() override {
        for (auto* g : {&secAuto_, &secBasic_, &secGoogle_, &secMicrosoft_, &secDeepL_, &secLibre_, &secLlm_, &secLocal_}) g->clear();
    }

    void OnSlider() override {
        const int size = static_cast<int>(SendMessageW(Item(kFontSize), TBM_GETPOS, 0, 0));
        const int transparency = static_cast<int>(SendMessageW(Item(kOpacity), TBM_GETPOS, 0, 0));
        SetText(kFontSizeValue, std::to_wstring(size) + L" %");
        SetText(kOpacityValue, std::to_wstring(transparency) + L" %");
        if (ctx_.preview) ctx_.preview(OpacityOf(transparency), size);
    }

    void OnCommand(int id, int code) override {
        switch (id) {
            case IDOK:
                Save();
                result.saved = true;
                Close();
                break;
            case IDCANCEL:
                if (ctx_.preview) ctx_.preview(cfg_.opacity, cfg_.fontPercent);  // undo the live preview
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
                if (code == CBN_SELCHANGE) {
                    SetText(kEngineNote, EngineNote(EngineAt(Sel(kEngine))));
                    UpdateTranslatorView();
                }
                break;
            case kLlmPreset:
                if (code == CBN_SELCHANGE) {
                    // A key belongs to one provider: never send it to another one.
                    if (Sel(kLlmPreset) != lastPreset_) SetText(kLlmKey, L"");
                    lastPreset_ = Sel(kLlmPreset);
                    ApplyPreset();
                }
                break;
            case kLlmGetKey:
                if (const int i = Sel(kLlmPreset); i >= 0 && kLlmPresets[i].keyUrl)
                    ShellExecuteW(hwnd_, L"open", kLlmPresets[i].keyUrl, nullptr, nullptr, SW_SHOWNORMAL);
                break;
            case kGoogleGet:
                ShellExecuteW(hwnd_, L"open", L"https://console.cloud.google.com/apis/library/translate.googleapis.com",
                              nullptr, nullptr, SW_SHOWNORMAL);
                break;
            case kMsGet:
                ShellExecuteW(hwnd_, L"open", L"https://portal.azure.com/#create/Microsoft.CognitiveServicesTextTranslation",
                              nullptr, nullptr, SW_SHOWNORMAL);
                break;
            case kCorrExport:
                if (ctx_.exportCorrections) {
                    const std::wstring path = PickTextFile(hwnd_, Tr(L"Export corrections"), true, L"gw2-corrections.txt");
                    if (!path.empty()) SetText(kCorrInfo, ctx_.exportCorrections(path));
                }
                break;
            case kCorrImport:
                if (ctx_.importCorrections) {
                    const std::wstring path = PickTextFile(hwnd_, Tr(L"Import corrections"), false, L"");
                    if (!path.empty()) SetText(kCorrInfo, ctx_.importCorrections(path));
                }
                break;
            case kCorrClear:
                if (ctx_.clearCorrections &&
                    MessageBoxW(hwnd_, Tr(L"Delete every corrected translation?").c_str(), Tr(L"Corrected translations").c_str(),
                                MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2 | (UiRtl() ? MB_RTLREADING | MB_RIGHT : 0)) == IDYES) {
                    ctx_.clearCorrections();
                    SetText(kCorrInfo, ctx_.correctionsInfo ? ctx_.correctionsInfo() : L"");
                }
                break;
            case kLibreLocal:
                SetUpLocalLibre();
                break;
            case kLibreGet:
                ShellExecuteW(hwnd_, L"open", L"https://github.com/LibreTranslate/LibreTranslate", nullptr, nullptr,
                              SW_SHOWNORMAL);
                break;
            case kDeepLGet:
                ShellExecuteW(hwnd_, L"open", L"https://www.deepl.com/pro-api", nullptr, nullptr, SW_SHOWNORMAL);
                break;
            case kTechRefresh:
                RefreshTechnical();
                break;
            case kTechCopy:
                CopyTechnical();
                break;
            case kTechCompare:
                CompareTranslators();
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

    // The translator for one engine with the settings as they are in the dialog (nullptr: not set up).
    static std::shared_ptr<Translator> MakeFor(const Config& probe, Engine e) {
        switch (e) {
            case Engine::Basic: return MakeMyMemoryTranslator(probe.basicEmail);
            case Engine::DeepL: return probe.deeplKey.empty() ? nullptr : MakeDeepLTranslator(probe.deeplKey);
            case Engine::Google: return probe.googleKey.empty() ? nullptr : MakeGoogleTranslator(probe.googleKey);
            case Engine::Microsoft:
                return probe.msKey.empty() ? nullptr : MakeMicrosoftTranslator(probe.msKey, probe.msRegion);
            case Engine::Libre: return probe.libreUrl.empty() ? nullptr : MakeLibreTranslator(probe.libreUrl, probe.libreKey);
            case Engine::Llm: {
                if (probe.llmModel.empty()) return nullptr;
                LlmSettings s;
                s.url = probe.llmUrl;
                s.model = probe.llmModel;
                s.apiKey = probe.llmKey;
                s.timeoutMs = probe.llmTimeoutSec * 1000;
                return MakeLlmTranslator(s);
            }
            default: return nullptr;
        }
    }

    // A small local translator with only your languages: LibreTranslate (Argos
    // models, CPU, ~100 MB per language) started with --load-only. We do not
    // install anything ourselves: the commands go to the clipboard, the address
    // is set, "Test the translator" checks it.
    void SetUpLocalLibre() {
        Config probe = cfg_;
        Collect(probe);
        std::vector<std::wstring> codes{L"en"};  // English is the bridge between the packs
        auto add = [&](const std::wstring& lang) {
            const std::wstring c = LibreLang(lang);
            if (c != L"auto" && std::find(codes.begin(), codes.end(), c) == codes.end()) codes.push_back(c);
        };
        if (probe.readLang.empty()) {
            wchar_t iso[16] = {};
            if (GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_SISO639LANGNAME, iso, 16)) add(iso);
        } else {
            add(probe.readLang);
        }
        for (const std::wstring& l : probe.writeLangs) add(l);
        add(probe.chatLang);
        std::wstring list;
        for (const std::wstring& c : codes) list += (list.empty() ? L"" : L",") + c;
        const std::wstring cmd = L"pip install libretranslate\r\nlibretranslate --load-only " + list;
        if (OpenClipboard(hwnd_)) {
            EmptyClipboard();
            const size_t bytes = (cmd.size() + 1) * sizeof(wchar_t);
            if (HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
                if (void* p = GlobalLock(g)) {
                    std::memcpy(p, cmd.c_str(), bytes);
                    GlobalUnlock(g);
                    if (!SetClipboardData(CF_UNICODETEXT, g)) GlobalFree(g);
                } else {
                    GlobalFree(g);
                }
            }
            CloseClipboard();
        }
        SetText(kLibreUrl, L"http://localhost:5000");
        MessageBoxW(hwnd_,
                    TrF(L"A small translator on this PC, only with your languages: {1} (about 100 MB each, no graphics "
                        L"card needed, nothing leaves the PC).\n\n"
                        L"1. Install Python 3 (python.org) if you do not have it.\n"
                        L"2. Open a command window and run:\n\n{2}\n\n"
                        L"   (with Docker instead: docker run -p 5000:5000 libretranslate/libretranslate --load-only {1})\n"
                        L"3. The first start downloads the language packs, then it answers at http://localhost:5000.\n\n"
                        L"The commands are in the clipboard and the address is filled in. Then press \"Test the "
                        L"translator\".",
                        {list, cmd})
                        .c_str(),
                    Tr(L"Set up on this PC").c_str(), MB_OK | MB_ICONINFORMATION | (UiRtl() ? MB_RTLREADING | MB_RIGHT : 0));
    }

    // Quality test: the same invented chat lines through every translator that is set up,
    // with the time each took. You judge which reads naturally.
    void CompareTranslators() {
        Config probe = cfg_;
        Collect(probe);
        std::vector<std::shared_ptr<Translator>> list;
        for (Engine e : {Engine::Basic, Engine::Google, Engine::Microsoft, Engine::DeepL, Engine::Libre, Engine::Llm})
            if (auto t = MakeFor(probe, e)) list.push_back(t);
        const std::wstring target = probe.readLang.empty() ? std::wstring(L"DE") : probe.readLang;
        EnableWindow(Item(kTechCompare), FALSE);
        SetText(kTechText, TrF(L"Comparing {1} translators into {2} … (each line goes to every one of them)",
                               {std::to_wstring(list.size()), LanguageLabel(target)}));
        std::thread([h = hwnd_, list, target] {
            // Invented lines in the style of the GW2 chat: slang, typos, several languages.
            static const wchar_t* kLines[] = {
                L"anyone up for the world boss in 5 min? need 2 more",
                L"kann mir jemand bei der fraktale helfen bin neu xD",
                L"alguien para la mazmorra? necesito ayuda con el jefe",
                L"merci pour l'aide, on se revoit au portail !",
                L"انا جديد في اللعبة، من يساعدني؟",
                L"ty all gg wp, see u tomorrow at the same time",
            };
            std::wstring out;
            for (const auto& t : list) {
                std::vector<std::vector<Segment>> items;
                for (const wchar_t* l : kLines) items.push_back({{l, false}});
                const ULONGLONG t0 = GetTickCount64();
                const std::vector<TranslateResult> rs = t->TranslateBatch(items, L"", target);
                const ULONGLONG ms = GetTickCount64() - t0;
                out += L"== " + t->Name() + L"  (" + std::to_wstring(ms) + L" ms) ==\r\n";
                for (size_t i = 0; i < rs.size() && i < items.size(); ++i)
                    out += std::wstring(L"  ") + kLines[i] + L"\r\n    → " +
                           (rs[i].ok ? rs[i].text : L"[!] " + rs[i].error) + L"\r\n";
                out += L"\r\n";
            }
            auto* text = new std::wstring(std::move(out));
            if (!PostMessageW(h, WM_APP_COMPARE, 0, reinterpret_cast<LPARAM>(text))) delete text;
        }).detach();
    }

    void TestTranslator() {
        Config probe = cfg_;
        Collect(probe);
        SetText(kTestStatus, Tr(L"Testing …"));
        EnableWindow(Item(kTest), FALSE);
        std::shared_ptr<Translator> t;
        Engine e = probe.engine;
        if (e == Engine::Auto)
            e = !probe.deeplKey.empty()    ? Engine::DeepL
                : !probe.googleKey.empty() ? Engine::Google
                : !probe.msKey.empty()     ? Engine::Microsoft
                : !probe.libreUrl.empty()  ? Engine::Libre
                : !probe.llmModel.empty()  ? Engine::Llm
                                           : Engine::Basic;
        if (e == Engine::DeepL) t = MakeDeepLTranslator(probe.deeplKey);
        else if (e == Engine::Google) t = MakeGoogleTranslator(probe.googleKey);
        else if (e == Engine::Microsoft) t = MakeMicrosoftTranslator(probe.msKey, probe.msRegion);
        else if (e == Engine::Libre) t = MakeLibreTranslator(probe.libreUrl, probe.libreKey);
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
            SendMessageW(Item(kEngine), CB_SETCURSEL, static_cast<WPARAM>(EngineIndex(Engine::Llm)), 0);
            SetText(kEngineNote, EngineNote(Engine::Llm));
            UpdateTranslatorView();
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
        if (msg == WM_APP_COMPARE) {
            std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(lp));
            EnableWindow(Item(kTechCompare), TRUE);
            SetText(kTechText, *text + Tr(L"Which reads naturally? The time is for all six lines together."));
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
        c.fontPercent = static_cast<int>(SendMessageW(Item(kFontSize), TBM_GETPOS, 0, 0));
        c.opacity = OpacityOf(static_cast<int>(SendMessageW(Item(kOpacity), TBM_GETPOS, 0, 0)));
        if (!Trim(Text(kHotkey)).empty()) c.hotkey = Trim(Text(kHotkey));

        c.readerEnabled = Checked(kReaderOn);
        if (Sel(kOcrEngine) >= 0) c.ocr = static_cast<OcrChoice>(Sel(kOcrEngine));
        if (Sel(kCapture) >= 0) c.captureMode = Sel(kCapture);
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
        c.ocrScale = std::clamp(_wtoi(Text(kOcrZoom).c_str()), 0, 4);
        c.send.keyHoldMs = std::clamp(_wtoi(Text(kKeyHold).c_str()), 5, 500);
        c.send.stepDelayMs = std::clamp(_wtoi(Text(kStepDelay).c_str()), 20, 1000);
        c.understoodLangs.clear();
        const wchar_t* quick[] = {L"EN", L"DE", L"FR", L"ES"};
        for (int i = 0; i < 4; ++i)
            if (Checked(kSkipEn + i)) c.understoodLangs.push_back(quick[i]);
        for (const std::wstring& l : ParseLangList(Text(kUnderstood))) c.understoodLangs.push_back(l);
        ChannelMask a = 0;
        if (Checked(kAutoWhisper)) a |= ChannelBit(Channel::Whisper);
        if (Checked(kAutoGroup)) a |= ChannelBit(Channel::Party) | ChannelBit(Channel::Squad);
        if (Checked(kAutoGuild)) a |= ChannelBit(Channel::Guild);
        if (Checked(kAutoMap)) a |= ChannelBit(Channel::Map) | ChannelBit(Channel::Say);
        if (Checked(kAutoTeam)) a |= ChannelBit(Channel::Team);
        c.autoTranslate = a;
        c.copyOnly = Sel(kSendMode) == 1;
        c.returnFocus = Checked(kReturnFocus);

        if (Sel(kEngine) >= 0) c.engine = EngineAt(Sel(kEngine));
        c.deeplKey = Trim(Text(kDeepL));
        c.googleKey = Trim(Text(kGoogleKey));
        c.msKey = Trim(Text(kMsKey));
        c.msRegion = Trim(Text(kMsRegion));
        c.libreUrl = Trim(Text(kLibreUrl));
        c.libreKey = Trim(Text(kLibreKey));
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
    // Translator page: one section per translator, only the chosen one is visible.
    std::vector<HWND> secAuto_, secBasic_, secGoogle_, secMicrosoft_, secDeepL_, secLibre_, secLlm_, secLocal_;
    int lastPreset_ = -1;
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
        Label(Tr(L"Language of this window"), 24, 60, 190);
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
        Label(Tr(L"Then just open your GW2 chat: this window finds it by itself and lies over it. "
                 L"Turn timestamps on in GW2 (Options → Chat); a large chat text reads best."),
              24, 240, kW - 48, 44);
        Label(Tr(L"Nothing is put into the game itself: no DLL, no hook. The tool only looks at the screen and "
                 L"the clipboard, and sends a line only when you press Enter."),
              24, 290, kW - 48, 34);
        EndPages();

        // One page: the chat itself is found later, when it is open.
        Button(kNext, Tr(L"Finish"), kW - 222, 336, 100);
        Button(IDCANCEL, Tr(L"Close"), kW - 114, 336, 100);
        SetText(kStepTitle, Tr(L"Setup – one step"));
        ShowPage(0);
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

    // No chat area is guessed here: while none is set, the main window looks
    // for the open GW2 chat and lies over it.
    void Finish() {
        cfg_.setupDone = true;
        cfg_.SaveAll();
        result.saved = true;
        if (!installedExe_.empty()) {  // the installed copy goes on with its own (copied) settings file
            result.action = DialogResult::Action::RestartInto;
            result.restartExe = installedExe_;
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
            case kNext:
                if (ApplyStep1()) Finish();
                break;
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

    std::wstring installedExe_;
};

// ---------------------------------------------------------------------------
// Correcting one translation (correction memory)
// ---------------------------------------------------------------------------
class CorrectionDialog final : public NativeDialog {
public:
    CorrectionDialog(std::wstring original, std::wstring translation)
        : original_(std::move(original)), text_(std::move(translation)) {}
    bool ok = false;
    std::wstring text;

private:
    static constexpr int kW = 520;
    enum : int { kOrig = 300, kText };

    void Build() override {
        Label(Tr(L"Original"), 16, 12, kW - 32);
        Edit(kOrig, original_, 16, 34, kW - 32, ES_MULTILINE | ES_READONLY | WS_VSCROLL | ES_AUTOVSCROLL, 54);
        Label(Tr(L"Your translation (remembered for this text and similar wording)"), 16, 98, kW - 32);
        HWND t = Edit(kText, text_, 16, 120, kW - 32, ES_MULTILINE | WS_VSCROLL | ES_AUTOVSCROLL, 64);
        Label(Tr(L"Stays on this PC. Applies to every translator; changed words are also corrected in later "
                 L"translations."),
              16, 190, kW - 32, 34);
        Button(IDOK, Tr(L"Remember"), kW - 228, 232, 104);
        Button(IDCANCEL, Tr(L"Cancel"), kW - 116, 232, 100);
        SetFocus(t);
        SendMessageW(t, EM_SETSEL, 0, -1);
    }

    void OnCommand(int id, int) override {
        if (id == IDOK) {
            text = Trim(Text(kText));
            for (wchar_t& c : text)
                if (c == L'\r' || c == L'\n') c = L' ';
            ok = !text.empty();
            Close();
        } else if (id == IDCANCEL) {
            Close();
        }
    }

    std::wstring original_, text_;
};

}  // namespace

bool AskCorrection(HWND owner, HINSTANCE inst, const std::wstring& original, std::wstring* translation) {
    CorrectionDialog dlg(original, *translation);
    dlg.Run(owner, inst, Tr(L"Correct the translation"), 520, 272);
    if (dlg.ok) *translation = dlg.text;
    return dlg.ok;
}

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
