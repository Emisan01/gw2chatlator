// settings_dialog.cpp — settings window and guided setup, built from
// native controls without a resource script.
#include "settings_dialog.hpp"

#include <commctrl.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

#include "app/modal_scope.hpp"
#include "core/cloud_mt_protocol.hpp"
#include "core/hotkey.hpp"
#ifndef GCT_KEYBOARD_APP  // GW2 Keyboard: no chat reading, no OCR
#include "core/rapid_models.hpp"
#include "win/rapid_ocr.hpp"
#include "win/tesseract_ocr.hpp"
#else
#include "core/voice.hpp"
#include "win/audio_capture.hpp"
#include "win/mic_sessions.hpp"
#include "win/voice_models.hpp"
#endif
#include "core/gw2_install.hpp"
#include "core/langs.hpp"
#include "core/i18n.hpp"
#include "core/languages.hpp"
#include "core/text.hpp"
#include "win/deepl_translator.hpp"
#include "win/gw2_locate.hpp"
#include "win/online_translators.hpp"

namespace gct {
namespace {

constexpr wchar_t kDialogClass[] = L"GW2ChatTranslatorDialog";
constexpr UINT WM_APP_MODELS = WM_APP + 50;
constexpr UINT WM_APP_TEST = WM_APP + 51;
constexpr UINT WM_APP_PULL = WM_APP + 52;
constexpr UINT WM_APP_COMPARE = WM_APP + 53;
constexpr UINT WM_APP_RAPID = WM_APP + 54;
constexpr UINT WM_APP_OCRCMP = WM_APP + 55;
constexpr UINT WM_APP_LTTEST = WM_APP + 56;
constexpr UINT WM_APP_FIND_LOCAL = WM_APP + 57;
#ifdef GCT_KEYBOARD_APP
constexpr UINT WM_APP_VOICE_PROGRESS = WM_APP + 58;  // WPARAM = percent of the model download
constexpr UINT WM_APP_VOICE_DONE = WM_APP + 59;      // LPARAM = new std::wstring (error, empty = done)
constexpr UINT WM_APP_VOICE_TEST = WM_APP + 60;      // LPARAM = new std::wstring (result of the test recording)
#endif
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
    kSpell, kAutoCorrect, kSuggest, kLearn, kTranslateWriting, kVoiceBoost, kLiveView, kMicMode, kMicKey, kMicGlobalKey, kMicGuard, kMicSens,
    kMicSensValue, kMicPause, kMicPreRoll, kMicMinMs, kMicMute, kForgetAll, kForgetStatus, kLt, kLtUrl, kBackTr, kSendMode, kReturnFocus,
    // Translator
    kEngine, kEngineNote, kLocalModel, kPull, kLocalInfo, kGetOllama, kLlmFindLocal, kPullStatus, kDeepL, kEmail, kLlmUrl, kLlmModel, kLlmLoad, kLlmKey, kFixOcr, kTest, kTestStatus,
    kGoogleKey, kGoogleGet, kMsKey, kMsRegion, kMsGet, kDeepLGet, kLlmPreset, kLlmGetKey, kLlmNote, kLibreUrl, kLibreKey, kLibreGet,
    kCorrInfo, kCorrExport, kCorrImport, kCorrClear, kTechCompare, kLibreLocal, kDesktop, kSecondLook, kMyWords, kSkipMore, kFontFace, kHotkeyClear, kHotkeyStatus, kStartMenu, kOnlyTr, kHelpOcr, kHelpCapture, kOcrFixes, kRapidStatus, kRapidGet, kTechOcrCompare, kWriteIn, kLtProvider, kLtTest, kLtStatus, kChatEngine, kKeyboardOnly, kLearnFile, kKeepMap, kReadWhat, kTour, kLoadExt,
    // Game & start
    kGw2Dir, kGw2Find, kGw2Browse, kInstall, kInstallStatus, kAutostart, kDock, kFollow, kFocusGameChat, kBridgeRead, kScreenshots, kLoadExtras, kRemoveExtras, kInstallBridge, kRemoveBridge, kHotkeySet, kPttSet, kPttValue, kStatus, kRefresh, kSetup,
    // Wizard
    // Technical
    kOcrZoom, kKeyHold, kStepDelay, kTechText, kTechRefresh, kTechCopy, kTechSaveF16, kTechStatus,
    // Voice (GW2 Keyboard)
    kVoiceOn, kVoiceModel, kVoiceGet, kVoiceModelStatus, kVoiceLang, kVoiceTranslate, kVoiceKey, kVoiceMode,
    kVoiceAppend, kVoiceMaxSec, kVoiceLive, kVoiceDevice, kVoiceThreads, kVoiceGlossary, kVoiceTest, kVoiceTestStatus,
    kVoiceFixes, kVoiceHint,
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

#ifndef GCT_KEYBOARD_APP  // GW2 Keyboard: no chat reading, no OCR
std::wstring TesseractStatus(const std::wstring& configured) {
    TesseractInfo info;
    if (!FindTesseract(configured, &info)) return Tr(L"[--] Not installed – Windows OCR is used (enough for most).");
    size_t models = 0;
    for (const std::string& m : info.models)
        if (m != "osd") ++models;
    // Only the packs for your languages are loaded; all installed ones are not a problem.
    return TrF(L"[OK] Found, {1} language packs installed.", {std::to_wstring(models)});
}
#endif

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
        const ModalScope modal;  // the reader does not use pictures while this dialog is open
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
        // Never in a screen capture: the free screen area or the chat may lie under it (invariant 5).
        if (!ModalScope::showOnScreenshots) SetWindowDisplayAffinity(hwnd_, WDA_EXCLUDEFROMCAPTURE);
        Build();
        if (owner) EnableWindow(owner, FALSE);
        ShowWindow(hwnd_, SW_SHOW);
        SetForegroundWindow(hwnd_);
        MSG m{};
        bool quit = false;
        while (!done_) {
            if (GetMessageW(&m, nullptr, 0, 0) <= 0) {  // the program is quitting while this dialog is open
                quit = true;
                break;
            }
            if (!IsDialogMessageW(hwnd_, &m)) {
                TranslateMessage(&m);
                DispatchMessageW(&m);
            }
        }
        if (quit) PostQuitMessage(static_cast<int>(m.wParam));  // handed on to the main loop (it ends the program)
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
        // The opened list is as wide as its longest entry: nothing is cut off there.
        if (HDC dc = GetDC(c)) {
            HGDIOBJ old = SelectObject(dc, font_);
            int widest = 0;
            for (const std::wstring& s : items) {
                SIZE sz{};
                GetTextExtentPoint32W(dc, s.c_str(), static_cast<int>(s.size()), &sz);
                widest = std::max(widest, static_cast<int>(sz.cx));
            }
            SelectObject(dc, old);
            ReleaseDC(c, dc);
            SendMessageW(c, CB_SETDROPPEDWIDTH, static_cast<WPARAM>(widest + S(12) + GetSystemMetrics(SM_CXVSCROLL)), 0);
        }
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

struct FindLocalMsg {
    bool found = false;
    LocalServerResult result;
    std::wstring error;
};

// The language lists ("Language of this window", "Translate the chat into"): Windows language first, then all.
std::vector<std::wstring> LanguageChoices() {
    std::vector<std::wstring> l{Tr(L"Windows language")};
    for (const LangInfo& x : Languages()) l.push_back(std::wstring(x.native) + L"  (" + x.code + L")");
    return l;
}
int LanguageChoiceOf(const std::wstring& code) {
    if (Trim(code).empty()) return 0;
    for (size_t i = 0; i < Languages().size(); ++i)
        if (FindLanguage(code) == &Languages()[i]) return static_cast<int>(i) + 1;
    return 0;
}
std::wstring LanguageAt(int choice) {  // "" = Windows language
    return choice > 0 && static_cast<size_t>(choice - 1) < Languages().size() ? Languages()[choice - 1].code : L"";
}

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

#ifdef GCT_KEYBOARD_APP
// Modal: the learned misheard words of the voice input, "heard = meant" per line. True when confirmed.
bool EditVoiceFixes(HWND owner, HINSTANCE inst, std::wstring* text);

// Model size in MB as the downloads count it (rounded up).
std::wstring VoiceMb(uint64_t bytes) { return std::to_wstring((bytes + (1ull << 20) - 1) >> 20); }

// The language of the keyboard layout ("de-DE"), like the input box reads it.
std::wstring KeyboardLayoutLocale() {
    const LANGID lang = LOWORD(reinterpret_cast<UINT_PTR>(GetKeyboardLayout(0)));
    wchar_t name[LOCALE_NAME_MAX_LENGTH] = {};
    if (LCIDToLocaleName(MAKELCID(lang, SORT_DEFAULT), name, LOCALE_NAME_MAX_LENGTH, 0) <= 0) return {};
    return name;
}
#endif

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------
// Asks for a key in a small window: two keys (Ctrl+T), a third optional (Ctrl+Alt+T); shows live whether Windows has
// it free. `mine` = the key this tool holds now (it counts as free). False = cancelled.
bool PickKey(HWND owner, HINSTANCE inst, const std::wstring& title, const std::wstring& current, const std::wstring& mine,
             bool needModifier, std::wstring* key);

class SettingsDialog final : public NativeDialog {
public:
    SettingsDialog(Config& cfg, const DialogContext& ctx, SettingsPage start) : cfg_(cfg), ctx_(ctx), start_(start) {}
#ifdef GCT_KEYBOARD_APP
    ~SettingsDialog() override {
        if (voiceCancel_) *voiceCancel_ = true;  // a running model download ends with the dialog
    }
#endif
    DialogResult result;

private:
    // Layout: labels on the left, controls on the right, below the tabs.
    static constexpr int kW = 580, kLabelX = 22, kLabelW = 170, kCtrlX = 200, kCtrlW = 356, kTop = 46, kRow = 32;

    void Build() override {
        tab_ = Add(WC_TABCONTROLW, L"", WS_TABSTOP | WS_CLIPSIBLINGS, 10, 10, kW - 20, 470, kIdTab);
#ifdef GCT_KEYBOARD_APP
        const wchar_t* names[] = {L"General", L"Writing", L"Voice", L"Microphone", L"Translator", L"Game & start",
                                  L"Technical"};
#else
        const wchar_t* names[] = {L"General", L"Reading the chat", L"Writing", L"Translator", L"Game & start",
                                  L"Technical"};
#endif
        for (int i = 0; i < static_cast<int>(std::size(names)); ++i) {
            std::wstring n;
            for (wchar_t c : Tr(names[i])) n += c == L'&' ? std::wstring(L"&&") : std::wstring(1, c);
            TCITEMW item{};
            item.mask = TCIF_TEXT;
            item.pszText = const_cast<wchar_t*>(n.c_str());
            SendMessageW(tab_, TCM_INSERTITEMW, static_cast<WPARAM>(i), reinterpret_cast<LPARAM>(&item));
        }
        BuildGeneral();
#ifndef GCT_KEYBOARD_APP  // GW2 Keyboard: no chat reading, no OCR
        BuildReading();
#endif
        BuildWriting();
#ifdef GCT_KEYBOARD_APP
        BuildVoice();
        BuildMicrophone();
#endif
        BuildTranslator();
        BuildGame();
        BuildTechnical();
        EndPages();
        Button(IDOK, Tr(L"OK"), kW - 220, 490, 100);
        Button(IDCANCEL, Tr(L"Cancel"), kW - 112, 490, 100);
        SendMessageW(tab_, TCM_SETCURSEL, static_cast<WPARAM>(TabOf(start_)), 0);
        ShowPage(static_cast<size_t>(TabOf(start_)));
        UpdateTranslatorView();
        lastPreset_ = Sel(kLlmPreset);
        RefreshStatus();
    }

    int Y(int row) const { return kTop + row * kRow; }

    // The tab of a page (= the order of the Build* calls): GW2 Keyboard has no "Reading the chat" page, the full
    // tool no "Voice" page (each falls back to General).
    static int TabOf(SettingsPage p) {
        switch (p) {
#ifdef GCT_KEYBOARD_APP
            case SettingsPage::General:
            case SettingsPage::Reading: return 0;
            case SettingsPage::Writing: return 1;
            case SettingsPage::Voice: return 2;
            case SettingsPage::Microphone: return 3;
            case SettingsPage::Translator: return 4;
            case SettingsPage::Game: return 5;
            case SettingsPage::Technical: return 6;
#else
            case SettingsPage::General:
            case SettingsPage::Microphone:
            case SettingsPage::Voice: return 0;
            case SettingsPage::Reading: return 1;
            case SettingsPage::Writing: return 2;
            case SettingsPage::Translator: return 3;
            case SettingsPage::Game: return 4;
            case SettingsPage::Technical: return 5;
#endif
        }
        return 0;
    }

    void BuildGeneral() {
        BeginPage();
        int r = 0;
        // The two language lists side by side, the same list in both.
        constexpr int kHalf = (kW - 2 * kLabelX - 16) / 2;
        Label(Tr(L"Language of this window"), kLabelX, Y(r) - 4, kHalf);
#ifdef GCT_KEYBOARD_APP
        r++;
        Combo(kUiLang, LanguageChoices(), LanguageChoiceOf(cfg_.uiLangCode), kLabelX, Y(r) - 10, kHalf);
        // Our windows are hidden from screenshots by default (the chat translator must never read them); here they
        // may be shown, e.g. to share a picture of the settings.
        Check(kScreenshots, Tr(L"Show my windows on screenshots"), cfg_.showOnScreenshots, kLabelX + kHalf + 16,
              Y(r++) - 8, kHalf);
#else
        Label(Tr(L"Translate the chat into"), kLabelX + kHalf + 16, Y(r++) - 4, kHalf);
        Combo(kUiLang, LanguageChoices(), LanguageChoiceOf(cfg_.uiLangCode), kLabelX, Y(r) - 10, kHalf);
        Combo(kReadLang, LanguageChoices(), LanguageChoiceOf(cfg_.readLang), kLabelX + kHalf + 16, Y(r++) - 10, kHalf);

        // Everything is translated; these are the exceptions (a click on a line still translates it).
        Label(Tr(L"Do not translate"), kLabelX, Y(r), kLabelW);
        auto has = [&](const wchar_t* code) {
            for (const std::wstring& c : cfg_.understoodLangs)
                if (PrimaryLang(c) == code) return true;
            return false;
        };
        const wchar_t* quick[] = {L"EN", L"DE", L"FR", L"ES"};
        for (int i = 0; i < 4; ++i) Check(kSkipEn + i, quick[i], has(quick[i]), kCtrlX + i * 52, Y(r), 50);
        // One more language of your choice (other codes from the settings file are kept).
        std::wstring other;
        for (const std::wstring& c : cfg_.understoodLangs) {
            const std::wstring p = PrimaryLang(c);
            if (other.empty() && p != L"EN" && p != L"DE" && p != L"FR" && p != L"ES") other = c;
        }
        Check(kSkipMore, L"", !other.empty(), kCtrlX + 4 * 52, Y(r), 20);
        std::vector<std::wstring> langs;
        for (const LangInfo& l : Languages()) langs.push_back(std::wstring(l.native) + L"  (" + l.code + L")");
        Combo(kUnderstood, langs, std::max(0, LanguageChoiceOf(other.empty() ? L"IT" : other) - 1), kCtrlX + 4 * 52 + 22,
              Y(r++), kCtrlW - 4 * 52 - 22);
#endif

        // Text: size in points (slider or typed) and the font right behind it; live preview behind the dialog.
        Label(Tr(L"Text size"), kLabelX, Y(r), kLabelW);
        HWND size = Add(TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, kCtrlX - 6, Y(r), 150, 26, kFontSize);
        SendMessageW(size, TBM_SETRANGE, TRUE, MAKELPARAM(kMinPt, kMaxPt));
        SendMessageW(size, TBM_SETPOS, TRUE, PtOf(cfg_.fontPercent));
        Edit(kFontSizeValue, std::to_wstring(PtOf(cfg_.fontPercent)), kCtrlX + 148, Y(r), 40, ES_NUMBER);
        Label(L"pt", kCtrlX + 192, Y(r), 22);
        int faceSel = 0;
        std::vector<std::wstring> faces;
        for (size_t i = 0; i < std::size(kFonts); ++i) {
            faces.push_back(kFonts[i]);
            if (cfg_.fontFace == kFonts[i]) faceSel = static_cast<int>(i);
        }
        Combo(kFontFace, faces, faceSel, kCtrlX + 216, Y(r++), kCtrlW - 216);
        Label(Tr(L"Transparency"), kLabelX, Y(r), kLabelW);
        HWND tr = Add(TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, kCtrlX - 6, Y(r), 150, 26, kOpacity);
        SendMessageW(tr, TBM_SETRANGE, TRUE, MAKELPARAM(0, 75));
        SendMessageW(tr, TBM_SETPOS, TRUE, TransparencyOf(cfg_.opacity));
        Label(std::to_wstring(TransparencyOf(cfg_.opacity)) + L" %", kCtrlX + 148, Y(r++), 60, 18, kOpacityValue);

        // Keys: the current one, and a button that asks for a new one (with a check whether it is free).
#ifndef GCT_KEYBOARD_APP
        r++;
#endif
        hotkey_ = cfg_.hotkey;
        Label(Tr(L"Show / hide hotkey"), kLabelX, Y(r), kLabelW);
        Label(KeyShown(hotkey_), kCtrlX, Y(r) + 3, 150, 20, kHotkeyStatus);
        Button(kHotkeySet, Tr(L"Set hotkey…"), kCtrlX + 156, Y(r++) - 1, 150);
#ifdef GCT_KEYBOARD_APP
        // Push to talk: the key anywhere (also in GW2) – the same setting as on the Microphone page.
        Label(Tr(L"Push-to-talk key"), kLabelX, Y(r), kLabelW);
        Label(KeyShown(cfg_.voiceGlobalKey), kCtrlX, Y(r) + 3, 150, 20, kPttValue);
        Button(kPttSet, Tr(L"Set key…"), kCtrlX + 156, Y(r++) - 1, 150);

        // Extensions, each loaded on your click: the chat translation by OCR, and party/squad chat as exact text.
        r++;
        const bool have = ctx_.extensionInstalled && ctx_.extensionInstalled();
        Label(Tr(L"Extension: translating the chat (OCR)"), kLabelX, Y(r++), kW - 50);
        Label(have ? Tr(L"[OK] Loaded. It reads the GW2 chat and shows it in your language.")
                   : Tr(L"[--] Not loaded. It reads the GW2 chat and shows it in your language (about 15 MB, from the "
                        L"official release on GitHub)."),
              kLabelX, Y(r) - 4, kW - 250, 40);
        Button(kLoadExt, have ? Tr(L"Start chat translation") : Tr(L"Load extension…"), kW - 222, Y(r++) - 2, 196);

        const int extras = ctx_.extrasState ? ctx_.extrasState() : -1;
        const int bridge = ctx_.bridgeState ? ctx_.bridgeState() : -1;
        Label(Tr(L"Extension: party and squad chat as exact text (Nexus)"), kLabelX, Y(r++) + 6, kW - 50);
        if (extras < 0) {
            Label(Tr(L"[--] No Guild Wars 2 add-ons folder found (is Nexus installed?)."), kLabelX, Y(r++) + 4, kW - 50);
        } else {
            Label(Tr(L"Unofficial Extras (ArcDPS, by Krappa322)"), kLabelX + 12, Y(r) + 6, 250);
            Label(extras == 1 ? Tr(L"[OK] installed") : Tr(L"[--] missing"), kLabelX + 268, Y(r) + 6, 110);
            Button(extras == 1 ? kRemoveExtras : kLoadExtras, extras == 1 ? Tr(L"Remove…") : Tr(L"Load…"), kW - 222,
                   Y(r++) + 2, 196);
            Label(Tr(L"GW2 Chat Bridge (our Nexus add-on)"), kLabelX + 12, Y(r) + 6, 250);
            Label(bridge == 1   ? Tr(L"[OK] installed")
                  : bridge == 2 ? Tr(L"[!] older version")
                  : bridge == 3 ? Tr(L"[--] not in this package")
                                : Tr(L"[--] missing"),
                  kLabelX + 268, Y(r) + 6, 110);
            if (bridge == 1) Button(kRemoveBridge, Tr(L"Remove…"), kW - 222, Y(r) + 2, 196);
            else if (bridge == 0 || bridge == 2)
                Button(kInstallBridge, bridge == 2 ? Tr(L"Update…") : Tr(L"Install…"), kW - 222, Y(r) + 2, 196);
            r++;
        }
#endif
    }

    static constexpr int kMinPt = 8, kMaxPt = 32;     // chat text size in points (100 % = 11 pt)
    static constexpr const wchar_t* kFonts[] = {L"Segoe UI", L"Verdana", L"Tahoma", L"Arial", L"Calibri",
                                                L"Trebuchet MS", L"Consolas"};
    static int PtOf(int percent) { return std::clamp((percent * 11 + 50) / 100, kMinPt, kMaxPt); }
    static int PercentOf(int pt) { return std::clamp((pt * 100 + 5) / 11, 70, 300); }

    // A key as shown next to its button ("Ctrl+Alt+T", or "none").
    static std::wstring KeyShown(const std::wstring& key) { return key.empty() ? Tr(L"none") : key; }
    void SetHotkeyControl(int id, const std::wstring& value) {
        WORD v = 0;
        if (const auto cur = ParseHotkey(value)) {
            BYTE f = 0;
            if (cur->mods & kModCtrl) f |= HOTKEYF_CONTROL;
            if (cur->mods & kModAlt) f |= HOTKEYF_ALT;
            if (cur->mods & kModShift) f |= HOTKEYF_SHIFT;
            v = MAKEWORD(static_cast<BYTE>(cur->vk), f);
        }
        SendMessageW(Item(id), HKM_SETHOTKEY, v, 0);
    }
    std::wstring hotkey_;  // show / hide hotkey as chosen in this dialog

#ifndef GCT_KEYBOARD_APP  // GW2 Keyboard: no chat reading, no OCR
    void BuildReading() {
        BeginPage();
        int r = 0;
        // Reading on/off lives in the main window (dot in the header, first menu entry).
        Check(kKeyboardOnly, Tr(L"Keyboard only: no chat reading at all, only the typing help"), cfg_.keyboardOnly,
              kLabelX, Y(r++), kW - 50);
        Label(Tr(L"Text recognition"), kLabelX, Y(r), kLabelW);
        Combo(kOcrEngine,
              {Tr(L"Automatic (Windows OCR; RapidOCR for small text)"), Tr(L"Tesseract (separate install)"),
               Tr(L"Windows OCR (built in, fast)"), Tr(L"RapidOCR (open source, best with small text)")},
              static_cast<int>(cfg_.ocr), kCtrlX, Y(r), kCtrlW - 30);
        Button(kHelpOcr, L"?", kCtrlX + kCtrlW - 24, Y(r++) - 1, 24);
        Label(ctx_.readingAdvice ? ctx_.readingAdvice() : L"", kCtrlX, Y(r++) - 6, kCtrlW, 34);
        Label(Tr(L"Picture of the chat"), kLabelX, Y(r), kLabelW);
        Combo(kCapture,
              {Tr(L"Automatic (no yellow frame)"), Tr(L"Game window (Windows 10: yellow frame)"),
               Tr(L"Screen (never a frame)")},
              cfg_.captureMode, kCtrlX, Y(r), kCtrlW - 30);
        Button(kHelpCapture, L"?", kCtrlX + kCtrlW - 24, Y(r++) - 1, 24);
        // Tesseract: an optional second recognition for small letters.
        Label(Tr(L"Tesseract (optional)"), kLabelX, Y(r), kLabelW);
        Edit(kTessPath, cfg_.tesseractPath, kCtrlX, Y(r), kCtrlW - 108);
        Button(kTessBrowse, Tr(L"Browse…"), kCtrlX + kCtrlW - 102, Y(r++) - 1, 102);
        Label(TesseractStatus(cfg_.tesseractPath), kCtrlX, Y(r) - 4, kCtrlW - 150, 34, kTessStatus);
        Button(kTessGet, Tr(L"Get Tesseract…"), kCtrlX + kCtrlW - 140, Y(r++) - 4, 140);
        // RapidOCR: open-source models (PaddleOCR) that run on this PC; only the groups for your languages.
        Label(Tr(L"RapidOCR (optional)"), kLabelX, Y(r), kLabelW);
        Label(RapidStatus(), kCtrlX, Y(r) - 2, kCtrlW - 150, 34, kRapidStatus);
        Button(kRapidGet, Tr(L"Install…"), kCtrlX + kCtrlW - 140, Y(r++) - 2, 140);
        EnableWindow(Item(kRapidGet), RapidMissing());  // only when something for your languages is missing
        Label(Tr(L"Read every"), kLabelX, Y(r), kLabelW);
        Edit(kInterval, std::to_wstring(cfg_.readerIntervalMs), kCtrlX, Y(r), 60, ES_NUMBER);
        Label(Tr(L"ms (200–2000, 400 recommended; shorter only reacts sooner, it does not read more exactly)"),
              kCtrlX + 68, Y(r++) - 4, kCtrlW - 68, 34);
        Check(kShowSystem, Tr(L"Filter system messages (events, notices)"), !cfg_.showSystemLines, kLabelX, Y(r++),
              kW - 50);
        Check(kOnlyTr, Tr(L"Show only translations (lines in your languages and unclear ones stay hidden)"),
              cfg_.onlyTranslations, kLabelX, Y(r++), kW - 50);
        Check(kSecondLook, Tr(L"Smart artifact correction (misread words are read again and learned)"),
              cfg_.secondLook, kLabelX, Y(r), 380);
        Button(kOcrFixes, Tr(L"Learned…"), kCtrlX + kCtrlW - 110, Y(r++) - 2, 110);
        Check(kCaptures, Tr(L"Save diagnostic pictures (switches off after 15 minutes)"), cfg_.saveCaptures, kLabelX,
              Y(r++), kW - 50);
        // What is read: the GW2 chat (found by itself) or any text in a screen area; then its frame.
        Combo(kReadWhat, {Tr(L"Read: the GW2 chat"), Tr(L"Read: a screen area (any text)")}, cfg_.freeArea ? 1 : 0,
              kLabelX, Y(r) + 4, 220);
        Button(kPickRegion, Tr(L"Set read area…"), kLabelX + 230, Y(r) + 3, 140);
        Button(kCover, Tr(L"Lay over the GW2 chat"), kLabelX + 378, Y(r++) + 3, 160);
    }
#endif

    void BuildWriting() {
        BeginPage();
        int r = 0;
        // The language you type in: spelling, the word bar and what is learned all follow it.
        Label(Tr(L"I write in"), kLabelX, Y(r), kLabelW);
        {
            std::vector<std::wstring> langs{Tr(L"Keyboard language (switches with it)")};
            int sel = 0;
            for (size_t i = 0; i < Languages().size(); ++i) {
                langs.push_back(std::wstring(Languages()[i].native) + L"  (" + Languages()[i].code + L")");
                if (!cfg_.writeIn.empty() && FindLanguage(cfg_.writeIn) == &Languages()[i]) sel = static_cast<int>(i) + 1;
            }
            Combo(kWriteIn, langs, sel, kCtrlX, Y(r++), kCtrlW);
        }
        Check(kSpell, Tr(L"Spell checking (Windows, offline)"), cfg_.spellEnabled, kLabelX, Y(r), 270);
        Label(Tr(L"Autocorrection"), kLabelX + 280, Y(r), 100);
        Combo(kAutoCorrect, {Tr(L"Off"), Tr(L"Safe"), Tr(L"Like a phone")}, static_cast<int>(cfg_.autoCorrect),
              kLabelX + 380, Y(r++), kW - kLabelX - 400);
        Check(kSuggest, Tr(L"Word suggestions: grey after the cursor – Space writes it, Tab shows the next"),
              cfg_.suggestions, kLabelX, Y(r++), kW - 50);
        // Learning from what you send: right away, or later when you choose (no time for it while playing).
        Combo(kLearn, {Tr(L"Learn what I send – while I play"), Tr(L"Learn what I send – later, when I choose"),
                       Tr(L"Do not learn what I send")},
              !cfg_.learnWords ? 2 : cfg_.learnLater ? 1 : 0, kLabelX, Y(r), 250);
        Button(kForgetAll, Tr(L"Delete everything learned…"), kCtrlX + 80, Y(r++) - 2, 220);
        Label(Tr(L"Stays on this PC. Right-click a word to forget just that one."), kLabelX + 20, Y(r) - 8,
              kCtrlX + 50 - kLabelX, 34, kForgetStatus);
        Button(kMyWords, Tr(L"My words…"), kCtrlX + 80, Y(r++) - 6, 220);
        // A typing profile from texts you wrote anyway (chats, mails, notes): the word bar knows your way of writing
        // from the start.
        Label(Tr(L"Typing profile from your own texts (chats, mails, notes as .txt)"), kLabelX + 20, Y(r) - 4,
              kCtrlX + 50 - kLabelX, 34);
        Button(kLearnFile, Tr(L"Learn from my texts…"), kCtrlX + 80, Y(r++) - 4, 220);
        // Grammar: whole sentences, online (or your own server); blue marks with suggestions on right-click.
        Label(Tr(L"Grammar check"), kLabelX, Y(r), kLabelW);
        const bool publicLt = cfg_.languageToolUrl.find(L"api.languagetool.org") != std::wstring::npos;
        Combo(kLtProvider,
              {Tr(L"Off"), Tr(L"LanguageTool – free, no account (20 checks a minute)"),
               Tr(L"Own LanguageTool server (free, no limit)")},
              !cfg_.languageTool ? 0 : publicLt ? 1 : 2, kCtrlX, Y(r++), kCtrlW);
        Edit(kLtUrl, cfg_.languageToolUrl, kCtrlX, Y(r) - 4, kCtrlW - 96);
        Button(kLtTest, Tr(L"Test"), kCtrlX + kCtrlW - 90, Y(r++) - 5, 90);
        Label(Tr(L"Checks the whole message after a pause and marks mistakes blue (right-click: suggestions)."),
              kCtrlX, Y(r++) - 8, kCtrlW, 34, kLtStatus);
        Check(kBackTr, Tr(L"Show the back-translation of my message"), cfg_.backTranslate, kLabelX, Y(r++), kW - 50);
        Label(Tr(L"Enter does"), kLabelX, Y(r), kLabelW);
        Combo(kSendMode, {Tr(L"Send into the GW2 chat"), Tr(L"Only copy (not a single key reaches the game)")},
              cfg_.copyOnly ? 1 : 0, kCtrlX, Y(r++), kCtrlW);
        // Focus behaviour: when the keyboard focus comes back to this window.
        Label(Tr(L"Focus behaviour"), kLabelX, Y(r), kLabelW);
        Check(kReturnFocus, Tr(L"back here after sending"), cfg_.returnFocus, kCtrlX, Y(r), 150);
        Check(kFocusGameChat, Tr(L"here when the GW2 chat opens"), cfg_.focusOnGameChat, kCtrlX + 156, Y(r++),
              kCtrlW - 156);
        // Correction memory (right-click a translated line -> "Correct this translation").
        Label(Tr(L"Corrected translations"), kLabelX, Y(r), kLabelW);
        Label(ctx_.correctionsInfo ? ctx_.correctionsInfo() : L"", kCtrlX, Y(r) - 14, kCtrlW, 16, kCorrInfo);
        Button(kCorrExport, Tr(L"Export…"), kCtrlX, Y(r) + 2, 110);
        Button(kCorrImport, Tr(L"Import…"), kCtrlX + 118, Y(r) + 2, 110);
        Button(kCorrClear, Tr(L"Delete all…"), kCtrlX + 236, Y(r++) + 2, 120);
        // What the word help saves: key presses compared with the letters sent.
        Label(Tr(L"Key presses saved"), kLabelX, Y(r) + 8, kLabelW);
        Label(TypingSavings(), kCtrlX, Y(r++) + 8, kCtrlW, 34);
        UpdateLtFields();
    }

    // "today 38 % (120 keys for 195 letters) · in total 31 % in 412 messages"; negative when you corrected a lot.
    std::wstring TypingSavings() const {
        auto pct = [](int keys, int chars) {
            return std::to_wstring(chars > 0 ? 100 - static_cast<long long>(keys) * 100 / chars : 0);
        };
        SYSTEMTIME st;
        GetLocalTime(&st);
        const bool today = cfg_.typingDay == st.wYear * 10000 + st.wMonth * 100 + st.wDay && cfg_.dayChars > 0;
        if (cfg_.totalChars <= 0) return Tr(L"Counted from your next message (letters you send vs. keys you press).");
        return (today ? TrF(L"Today {1} % ({2} keys for {3} letters)",
                            {pct(cfg_.dayKeys, cfg_.dayChars), std::to_wstring(cfg_.dayKeys), std::to_wstring(cfg_.dayChars)}) +
                            L" · "
                      : std::wstring()) +
               TrF(L"in total {1} % in {2} messages",
                   {pct(cfg_.totalKeys, cfg_.totalChars), std::to_wstring(cfg_.totalMessages)});
    }

    // Grammar provider: the address follows the choice; your own server keeps what you typed.
    void UpdateLtFields() {
        const int p = Sel(kLtProvider);
        EnableWindow(Item(kLtUrl), p == 2);
        EnableWindow(Item(kLtTest), p != 0);
        if (p == 1) SetText(kLtUrl, L"https://api.languagetool.org");
        else if (p == 2 && Text(kLtUrl).find(L"api.languagetool.org") != std::wstring::npos)
            SetText(kLtUrl, L"http://localhost:8010");
    }

    // One test sentence with a mistake: shows whether the server answers and finds it.
    void TestLanguageTool() {
        SetText(kLtStatus, Tr(L"Testing …"));
        EnableWindow(Item(kLtTest), FALSE);
        std::thread([h = hwnd_, url = Trim(Text(kLtUrl))] {
            const LtResult r = CheckWithLanguageTool(url, L"This is a tset.", L"en-US", L"");
            auto* text = new std::wstring(
                r.ok ? TrF(L"[OK] Connected – found {1} mistake(s) in a test sentence.", {std::to_wstring(r.matches.size())})
                     : TrF(L"[!] Not reachable: {1}", {r.error}));
            if (!PostMessageW(h, WM_APP_LTTEST, 0, reinterpret_cast<LPARAM>(text))) delete text;
        }).detach();
    }

#ifdef GCT_KEYBOARD_APP
    // Voice input: hold, speak, release – the text lands in the input box and goes through the same chain as typed
    // text; Enter stays yours. Model management looks like the local translator models (size, memory, install).
    // A Windows hotkey field (HOTKEY_CLASS) holding "Ctrl+Alt+V" / "F8"; empty = none.
    HWND HotkeyControl(int id, const std::wstring& value, int x, int y, int w) {
        HWND hk = Add(HOTKEY_CLASSW, L"", WS_TABSTOP, x, y, w, 24, id, WS_EX_CLIENTEDGE);
        if (const auto cur = ParseHotkey(value)) {
            BYTE f = 0;
            if (cur->mods & kModCtrl) f |= HOTKEYF_CONTROL;
            if (cur->mods & kModAlt) f |= HOTKEYF_ALT;
            if (cur->mods & kModShift) f |= HOTKEYF_SHIFT;
            SendMessageW(hk, HKM_SETHOTKEY, MAKEWORD(static_cast<BYTE>(cur->vk), f), 0);
        }
        return hk;
    }
    std::wstring HotkeyValue(int id) const {
        const WORD v = static_cast<WORD>(SendMessageW(Item(id), HKM_GETHOTKEY, 0, 0));
        Hotkey hk;
        hk.vk = LOBYTE(v);
        const BYTE f = HIBYTE(v);
        if (f & HOTKEYF_CONTROL) hk.mods |= kModCtrl;
        if (f & HOTKEYF_ALT) hk.mods |= kModAlt;
        if (f & HOTKEYF_SHIFT) hk.mods |= kModShift;
        return hk.vk ? FormatHotkey(hk) : L"";
    }

    // Settings → Microphone (GW2 Keyboard): how a recording starts and ends, the keys, voice activation, the device,
    // boost, and keeping the dictation out of voice chats.
    void BuildMicrophone() {
        BeginPage();
        int r = 0;
        // How a recording starts and ends – for everyone.
        Label(Tr(L"Starting"), kLabelX, Y(r), kLabelW);
        Combo(kMicMode,
              {Tr(L"Hold the button or key while you speak"), Tr(L"Press once – a pause ends it"),
               Tr(L"Voice activation – starts when you speak")},
              cfg_.voiceActivation ? 2 : cfg_.voiceToggle ? 1 : 0, kCtrlX, Y(r++), kCtrlW);
        // Keys: in this window (plain keys allowed), and anywhere (a Windows hotkey: works while GW2 is in front).
        Label(Tr(L"Key in this window"), kLabelX, Y(r), kLabelW);
        HotkeyControl(kMicKey, cfg_.voiceKey, kCtrlX, Y(r), 170);
        Label(Tr(L"Key anywhere (also in GW2)"), kLabelX, Y(r + 1), kLabelW);
        HotkeyControl(kMicGlobalKey, cfg_.voiceGlobalKey, kCtrlX, Y(r + 1), 170);
        Label(Tr(L"Starts; a pause ends it; this window then takes the focus for your Enter. GW2 does not get this key."),
              kCtrlX + 178, Y(r) - 2, kCtrlW - 178, 64);
        r += 2;
        Label(Tr(L"Microphone"), kLabelX, Y(r), kLabelW);
        std::vector<std::wstring> mics{Tr(L"Windows default")};
        voiceDeviceIds_ = {L""};
        int micSel = 0;
        for (const AudioDevice& d : AudioCapture::Devices()) {
            if (!cfg_.voiceDevice.empty() && d.id == cfg_.voiceDevice) micSel = static_cast<int>(mics.size());
            mics.push_back(d.name);
            voiceDeviceIds_.push_back(d.id);
        }
        if (!cfg_.voiceDevice.empty() && micSel == 0) {  // unplugged: kept until you choose another one
            micSel = static_cast<int>(mics.size());
            mics.push_back(Tr(L"(not connected)"));
            voiceDeviceIds_.push_back(cfg_.voiceDevice);
        }
        Combo(kVoiceDevice, mics, micSel, kCtrlX, Y(r), kCtrlW - 136);
        Combo(kVoiceBoost, {Tr(L"Boost: off"), Tr(L"Boost: +6 dB"), Tr(L"Boost: +12 dB"), Tr(L"Boost: automatic")},
              std::clamp(cfg_.voiceBoost, 0, 3), kCtrlX + kCtrlW - 130, Y(r++), 130);
        Label(Tr(L"Pause that ends it"), kLabelX, Y(r), kLabelW);
        Edit(kMicPause, std::to_wstring(cfg_.voicePauseMs), kCtrlX, Y(r), 60, ES_NUMBER);
        Label(Tr(L"ms (400–3000)"), kCtrlX + 66, Y(r++), 110);

        // Only for voice activation (greyed otherwise).
        r++;
        Label(Tr(L"Voice activation"), kLabelX, Y(r) - 8, kLabelW);
        Label(Tr(L"Voice activation is not exclusive: everything you say lands in the box, and Discord still hears it. "
                 L"With a voice chat open, start with the key or a click."),
              kCtrlX, Y(r++) - 8, kCtrlW, 34);
        Label(Tr(L"Voice activation listens"), kLabelX, Y(r), kLabelW);
        Combo(kMicGuard,
              {Tr(L"only after the key anywhere switched it on"), Tr(L"while this window is in front"),
               Tr(L"always (everything you say lands in the box)")},
              std::clamp(cfg_.voiceGuard, 0, 2), kCtrlX, Y(r++), kCtrlW);
        Label(Tr(L"Sensitivity"), kLabelX, Y(r), kLabelW);
        HWND sens = Add(TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, kCtrlX, Y(r), kCtrlW - 60, 26, kMicSens);
        SendMessageW(sens, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
        SendMessageW(sens, TBM_SETPOS, TRUE, std::clamp(cfg_.voiceSensitivity, 0, 100));
        Label(std::to_wstring(cfg_.voiceSensitivity), kCtrlX + kCtrlW - 52, Y(r++), 50, 18, kMicSensValue);
        Label(Tr(L"Kept from before"), kLabelX, Y(r), kLabelW);
        Edit(kMicPreRoll, std::to_wstring(cfg_.voicePreRollMs), kCtrlX, Y(r), 60, ES_NUMBER);
        Label(Tr(L"ms – so the first word is not lost"), kCtrlX + 66, Y(r), 200);
        Edit(kMicMinMs, std::to_wstring(cfg_.voiceMinMs), kCtrlX + kCtrlW - 116, Y(r), 50, ES_NUMBER);
        Label(Tr(L"ms speech starts it"), kCtrlX + kCtrlW - 62, Y(r++) - 6, 64, 34);
        // It cannot tell what was meant for a voice chat: it pauses while Discord, TeamSpeak ... have the microphone.
        Check(kMicMute, Tr(L"Pause while a voice chat uses the microphone (Discord, TeamSpeak …)"), cfg_.voiceChatPause,
              kLabelX, Y(r++), kW - 44);
        UpdateMicView();
    }

    // The voice activation fields only count with voice activation.
    void UpdateMicView() {
        const bool on = Sel(kMicMode) == 2;
        for (int id : {kMicGuard, kMicSens, kMicSensValue, kMicPreRoll, kMicMinMs, kMicMute}) EnableWindow(Item(id), on);
    }

    void BuildVoice() {
        BeginPage();
        voiceCatalog_ = LoadVoiceCatalog(cfg_.dataDir);
        voiceHw_ = ProbeVoiceHardware();
        voiceWhy_.clear();
        voiceOk_ = !ctx_.voiceAvailable || ctx_.voiceAvailable(&voiceWhy_);
        // Without AVX2 or with few cores only base answers in time.
        voiceRecommended_.clear();
        if ((!voiceHw_.avx2 || voiceHw_.logicalProcessors < 4) && voiceCatalog_.Find("base-q5_1"))
            voiceRecommended_ = "base-q5_1";
        if (voiceRecommended_.empty() && voiceCatalog_.Recommended()) voiceRecommended_ = voiceCatalog_.Recommended()->id;
        voiceCtrls_.clear();
        int r = 0;
        HWND on = Add(L"BUTTON",
                      Tr(L"Voice input: hold the microphone button or the key, speak, release – check the text, Enter "
                         L"sends"),
                      BS_AUTOCHECKBOX | BS_MULTILINE | WS_TABSTOP, kLabelX, Y(r++) - 6, kW - 44, 34, kVoiceOn);
        SendMessageW(on, BM_SETCHECK, cfg_.voiceEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
        group_ = &voiceCtrls_;  // everything below except the hint is disabled when voice input cannot work

        // Model: size and memory in the list; status (downloaded, disk space, quality for your language) below.
        Label(Tr(L"Model"), kLabelX, Y(r), kLabelW);
        std::vector<std::wstring> models;
        voiceModelIds_.clear();
        const std::string wanted = cfg_.voiceModel.empty() ? voiceRecommended_ : ToUtf8(cfg_.voiceModel);
        int modelSel = -1, recommendedSel = 0;
        for (const VoiceModelInfo& m : voiceCatalog_.models) {
            std::wstring item =
                TrF(L"{1} – {2} MB, RAM ~{3} MB", {FromUtf8(m.name), VoiceMb(m.bytes), std::to_wstring(m.ramMb)});
            if (m.id == voiceRecommended_) {
                item = TrF(L"{1} – recommended", {item});
                recommendedSel = static_cast<int>(models.size());
            }
            if (m.id == wanted) modelSel = static_cast<int>(models.size());
            models.push_back(item);
            voiceModelIds_.push_back(m.id);
        }
        Combo(kVoiceModel, models, modelSel >= 0 ? modelSel : recommendedSel, kCtrlX, Y(r++), kCtrlW);
        Label(L"", kCtrlX, Y(r) - 5, kCtrlW - 128, 32, kVoiceModelStatus);
        Button(kVoiceGet, Tr(L"Download…"), kCtrlX + kCtrlW - 120, Y(r++) - 3, 120);

        // Language: the one you write in (default), detected, or a fixed one; whisper can also give English directly.
        Label(Tr(L"Language"), kLabelX, Y(r), kLabelW);
        std::vector<std::wstring> langs{Tr(L"As I write"), Tr(L"Detect automatically")};
        voiceLangValues_ = {L"", L"auto"};
        const bool fixedLang = !cfg_.voiceLang.empty() && cfg_.voiceLang != L"auto";
        const LangInfo* fixed = fixedLang ? FindLanguage(cfg_.voiceLang) : nullptr;
        int langSel = cfg_.voiceLang == L"auto" ? 1 : 0;
        for (const LangInfo& l : Languages()) {
            if (WhisperLang(l.code).empty()) continue;  // the recognizer does not know it
            if (&l == fixed) langSel = static_cast<int>(langs.size());
            langs.push_back(std::wstring(l.native) + L"  (" + l.code + L")");
            voiceLangValues_.push_back(l.code);
        }
        if (fixedLang && langSel == 0) {  // a code from the settings file that is not in the list stays
            langSel = static_cast<int>(langs.size());
            langs.push_back(cfg_.voiceLang);
            voiceLangValues_.push_back(cfg_.voiceLang);
        }
        Combo(kVoiceLang, langs, langSel, kCtrlX, Y(r++), kCtrlW);
        Check(kVoiceTranslate, Tr(L"Directly into English (whisper translates; normally your translator does it)"),
              cfg_.voiceTranslate, kLabelX, Y(r++), kW - 44);

        Label(Tr(L"New recording"), kLabelX, Y(r), kLabelW);
        Combo(kVoiceAppend, {Tr(L"replaces the last dictation"), Tr(L"is appended")}, cfg_.voiceAppend ? 1 : 0, kCtrlX,
              Y(r++), kCtrlW);
        Label(Tr(L"Longest recording"), kLabelX, Y(r), kLabelW);
        Edit(kVoiceMaxSec, std::to_wstring(cfg_.voiceMaxSec), kCtrlX, Y(r), 50, ES_NUMBER);
        Label(L"s", kCtrlX + 56, Y(r), 16);
        Check(kVoiceLive, Tr(L"Live text while speaking (more CPU)"), cfg_.voiceLive, kCtrlX + 80, Y(r++), kCtrlW - 80);


        Label(Tr(L"CPU threads"), kLabelX, Y(r), kLabelW);
        const unsigned logical = std::max(1u, voiceHw_.logicalProcessors);
        std::vector<std::wstring> threads{TrF(L"Automatic ({1})", {std::to_wstring(VoiceThreads(0, logical))})};
        voiceThreadValues_ = {0};
        int threadSel = 0;
        const int most = std::min(8, static_cast<int>(logical));
        for (int t = 1; t <= most; ++t) {
            if (t == cfg_.voiceThreads) threadSel = static_cast<int>(threads.size());
            threads.push_back(std::to_wstring(t));
            voiceThreadValues_.push_back(t);
        }
        if (cfg_.voiceThreads > most) {  // more from the settings file stays
            threadSel = static_cast<int>(threads.size());
            threads.push_back(std::to_wstring(cfg_.voiceThreads));
            voiceThreadValues_.push_back(cfg_.voiceThreads);
        }
        Combo(kVoiceThreads, threads, threadSel, kCtrlX, Y(r), 150);
        Check(kVoiceGlossary, Tr(L"Know my names and GW2 words"), cfg_.voiceGlossary, kCtrlX + 160, Y(r++),
              kCtrlW - 160);

        Button(kVoiceTest, Tr(L"Test (speak 4 s)"), kLabelX, Y(r) - 1, 170);
        Label(Tr(L"Records 4 s with the choices above – nothing is saved."), kLabelX + 180, Y(r++) - 2, kW - 44 - 180,
              50, kVoiceTestStatus);
        Button(kVoiceFixes, Tr(L"Learned…"), kLabelX, Y(r++) - 1, 170);
        group_ = nullptr;
        Label(L"", kLabelX, Y(r) - 2, kW - 44, 34, kVoiceHint);

        if (!voiceOk_)
            for (HWND h : voiceCtrls_) EnableWindow(h, FALSE);
        EnableWindow(Item(kVoiceTest), voiceOk_ && ctx_.voiceTest && !voiceTesting_);
        EnableWindow(Item(kVoiceFixes), voiceOk_ && ctx_.voiceFixesText && ctx_.setVoiceFixes);
        RefreshVoice();
    }

    const VoiceModelInfo* SelectedVoiceModel() const {
        const int i = Sel(kVoiceModel);
        return i >= 0 && static_cast<size_t>(i) < voiceModelIds_.size()
                   ? voiceCatalog_.Find(voiceModelIds_[static_cast<size_t>(i)])
                   : nullptr;
    }

    // "" (as I write), "auto" or a language code.
    std::wstring VoiceLangValue() const {
        const int i = Sel(kVoiceLang);
        return i >= 0 && static_cast<size_t>(i) < voiceLangValues_.size() ? voiceLangValues_[static_cast<size_t>(i)]
                                                                         : cfg_.voiceLang;
    }

    // The language the recognizer listens for: "" = detected; else a code ("DE", "de-DE").
    std::wstring VoiceLangCode() const {
        const std::wstring v = VoiceLangValue();
        if (v == L"auto") return L"";
        if (!v.empty()) return v;
        // As I write: the writing language as chosen on the Writing page now, else the keyboard layout's.
        const int w = Sel(kWriteIn);
        if (w > 0 && static_cast<size_t>(w - 1) < Languages().size()) return Languages()[static_cast<size_t>(w - 1)].code;
        if (w < 0 && !cfg_.writeIn.empty()) return cfg_.writeIn;
        return KeyboardLayoutLocale();
    }

    static std::wstring VoiceQualityWord(int q) {
        return q >= 3 ? Tr(L"very well") : q == 2 ? Tr(L"well") : q == 1 ? Tr(L"weakly") : Tr(L"hardly");
    }

    std::wstring VoiceModelStatus() const {
        const VoiceModelInfo* m = SelectedVoiceModel();
        if (!m) return Tr(L"[--] No voice model in the list.");
        const std::wstring dir = cfg_.VoiceModelDir();
        std::wstring s;
        if (voiceDownloading_ == m->id)
            s = TrF(L"Downloading … {1} % of {2} MB", {std::to_wstring(voicePercent_), VoiceMb(m->bytes)});
        else if (voiceErrorId_ == m->id && !voiceError_.empty())
            s = TrF(L"[!] Not downloaded: {1}", {voiceError_});
        else if (VoiceModelInstalled(dir, *m))
            s = Tr(L"[OK] Downloaded.");
        else if (const uint64_t freeBytes = FreeDiskBytes(dir))
            s = TrF(L"[--] Not downloaded: {1} MB needed, {2} MB free.", {VoiceMb(m->bytes), VoiceMb(freeBytes)});
        else
            s = TrF(L"[--] Not downloaded: {1} MB needed.", {VoiceMb(m->bytes)});
        const std::wstring code = VoiceLangCode();
        const std::string wl = WhisperLang(code);
        if (!wl.empty())
            s += L" " + TrF(L"{1}: recognized {2}.", {LanguageLabel(code), VoiceQualityWord(m->QualityFor(wl))});
        return s;
    }

    // One line below everything: why it cannot work, what does not fit this PC or language, else the privacy note.
    std::wstring VoiceHint() const {
        if (!voiceOk_) return L"[!] " + voiceWhy_;
        if (!voiceHw_.avx2) return Tr(L"[!] No AVX2 on this PC: base is the right model.");
        const VoiceModelInfo* m = SelectedVoiceModel();
        if (m && voiceHw_.ramAvailable &&
            voiceHw_.ramAvailable < ((static_cast<uint64_t>(m->ramMb) * 3 / 2) << 20))
            return TrF(L"[!] Only {1} MB of memory are free – {2} needs about {3} MB. A smaller model is safer.",
                       {VoiceMb(voiceHw_.ramAvailable), FromUtf8(m->name), std::to_wstring(m->ramMb)});
        const std::string wl = WhisperLang(VoiceLangCode());
        if (m && !wl.empty() && m->QualityFor(wl) < 2) {
            const VoiceModelInfo* better = nullptr;  // the smallest larger model that recognizes it well
            for (const VoiceModelInfo& o : voiceCatalog_.models)
                if (o.bytes > m->bytes && o.QualityFor(wl) >= 2 && (!better || o.bytes < better->bytes)) better = &o;
            if (better)
                return TrF(L"[!] {1} recognizes this language poorly – {2} or larger is better.",
                           {FromUtf8(m->name), FromUtf8(better->name)});
            return TrF(L"[!] {1} recognizes this language poorly.", {FromUtf8(m->name)});
        }
        if (VoiceLangValue() == L"auto") return Tr(L"A fixed language is more reliable for short sentences.");
        return Tr(L"Runs on this PC – no audio leaves it, nothing is recorded to disk.");
    }

    void RefreshVoice() {
        const VoiceModelInfo* m = SelectedVoiceModel();
        const bool busy = !voiceDownloading_.empty();
        const bool thisOne = m && busy && voiceDownloading_ == m->id;
        const bool installed = m && !thisOne && VoiceModelInstalled(cfg_.VoiceModelDir(), *m);
        SetText(kVoiceGet, thisOne ? Tr(L"Cancel") : installed ? Tr(L"Delete…") : Tr(L"Download…"));
        EnableWindow(Item(kVoiceGet), voiceOk_ && m && (!busy || (thisOne && voiceCancel_ && !*voiceCancel_)));
        SetText(kVoiceModelStatus, VoiceModelStatus());
        SetText(kVoiceHint, VoiceHint());
    }

    // Download (asked first), cancel a running download, or delete the chosen model.
    void VoiceGetClicked() {
        const VoiceModelInfo* m = SelectedVoiceModel();
        if (!m) return;
        if (!voiceDownloading_.empty()) {  // the button says "Cancel"
            if (voiceDownloading_ == m->id && voiceCancel_) *voiceCancel_ = true;
            RefreshVoice();
            return;
        }
        const std::wstring dir = cfg_.VoiceModelDir();
        const std::wstring name = FromUtf8(m->name);
        const UINT rtl = UiRtl() ? MB_RTLREADING | MB_RIGHT : 0;
        if (VoiceModelInstalled(dir, *m)) {
            if (MessageBoxW(hwnd_,
                            TrF(L"Delete the voice model {1}? It frees {2} MB; you can download it again.",
                                {name, VoiceMb(m->bytes)})
                                .c_str(),
                            Tr(L"Voice input").c_str(), MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2 | rtl) != IDYES)
                return;
            voiceError_.clear();
            const bool deleted = DeleteVoiceModel(dir, *m);
            RefreshVoice();
            if (!deleted)
                SetText(kVoiceModelStatus, TrF(L"[!] Could not delete {1} – is it in use?", {VoiceModelPath(dir, *m)}));
            return;
        }
        // Where it comes from, as the catalog says (https only).
        const size_t hostEnd = m->url.find('/', 8);
        std::wstring source = FromUtf8(m->url.substr(8, hostEnd == std::string::npos ? std::string::npos : hostEnd - 8));
        if (m->provider == "whisper") source += L" (whisper.cpp)";
        if (MessageBoxW(hwnd_,
                        TrF(L"Download the voice model {1} ({2} MB from {3})?\n\nIt turns your speech into text on this "
                            L"PC only: no audio leaves it and nothing is recorded. Licence: {4}.",
                            {name, VoiceMb(m->bytes), source,
                             FromUtf8(m->licence.empty() ? std::string("MIT") : m->licence)})
                            .c_str(),
                        Tr(L"Voice input").c_str(), MB_YESNO | MB_ICONQUESTION | rtl) != IDYES)
            return;
        voiceError_.clear();
        voiceDownloading_ = m->id;
        voicePercent_ = 0;
        auto cancel = std::make_shared<std::atomic<bool>>(false);
        voiceCancel_ = cancel;
        RefreshVoice();
        std::thread([h = hwnd_, dir, model = *m, cancel] {
            std::wstring error;
            int last = -1;
            const bool ok = DownloadVoiceModel(
                dir, model,
                [&](uint64_t done, uint64_t total) {
                    if (!total) total = model.bytes;
                    const int pct = total ? static_cast<int>(std::min<uint64_t>(done * 100 / total, 100)) : 0;
                    if (pct != last) {  // at most ~100 messages
                        last = pct;
                        PostMessageW(h, WM_APP_VOICE_PROGRESS, static_cast<WPARAM>(pct), 0);
                    }
                    return true;
                },
                cancel.get(), &error);
            if (ok) error.clear();
            else if (error.empty()) error = Tr(L"Download failed.");
            auto* msg = new std::wstring(std::move(error));
            if (!PostMessageW(h, WM_APP_VOICE_DONE, 0, reinterpret_cast<LPARAM>(msg))) delete msg;
        }).detach();
    }

    // A few seconds through the real chain with the choices on this page (not saved yet).
    void TestVoice() {
        if (!ctx_.voiceTest || voiceTesting_) return;
        const VoiceModelInfo* m = SelectedVoiceModel();
        if (!m || !VoiceModelInstalled(cfg_.VoiceModelDir(), *m)) {
            SetText(kVoiceTestStatus, Tr(L"[--] Download the model first."));
            return;
        }
        Config probe = cfg_;
        Collect(probe);
        voiceTesting_ = true;
        EnableWindow(Item(kVoiceTest), FALSE);
        SetText(kVoiceTestStatus, Tr(L"Speak now (4 s) …"));
        ctx_.voiceTest(probe, hwnd_, WM_APP_VOICE_TEST);
    }
#endif

    void BuildTranslator() {
        BeginPage();
        int r = 0;
#ifdef GCT_KEYBOARD_APP
        // The whole translation of what you write can be switched off: then only the typing help runs.
        Check(kTranslateWriting, Tr(L"Translate my text"), cfg_.translateWriting, kLabelX, Y(r), kLabelW);
#else
        Label(Tr(L"Translator"), kLabelX, Y(r), kLabelW);
#endif
        Combo(kEngine,
              {Tr(L"Automatic (the first one set up)"), Tr(L"MyMemory (free, no account)"),
               Tr(L"Google Translate (free contingent, key)"), Tr(L"Microsoft Translator (free contingent, key)"),
               Tr(L"DeepL (free contingent, key)"), Tr(L"Own translation server (any address)"),
               Tr(L"AI model (local or cloud)")},
              EngineIndex(cfg_.engine), kCtrlX, Y(r++), kCtrlW);
        shownEngine_ = cfg_.engine;
        Label(EngineNote(cfg_.engine), kCtrlX, Y(r++) - 6, kCtrlW, 30, kEngineNote);
#ifndef GCT_KEYBOARD_APP  // GW2 Keyboard: no chat reading, no OCR
        // The chat may use another translator than your messages (e.g. a free one for reading, DeepL for writing).
        Label(Tr(L"For the chat"), kLabelX, Y(r), kLabelW);
        Combo(kChatEngine,
              {Tr(L"Same as for my messages"), Tr(L"Automatic (the first one set up)"), Tr(L"MyMemory (free, no account)"),
               Tr(L"Google Translate (free contingent, key)"), Tr(L"Microsoft Translator (free contingent, key)"),
               Tr(L"DeepL (free contingent, key)"), Tr(L"Own translation server (any address)"),
               Tr(L"AI model (local or cloud)")},
              cfg_.chatSameEngine ? 0 : EngineIndex(cfg_.chatEngine) + 1, kCtrlX, Y(r++), kCtrlW);
#endif
        const int top = r;  // the sections below share the same rows; only the chosen one is shown

        // Automatic
        group_ = &secAuto_;
        Label(Tr(L"Automatic takes the first translator that is set up: DeepL, Google, Microsoft, your own server, "
                 L"then the AI model; without any, MyMemory. Choose one in the list to enter its key."),
              kCtrlX, Y(top), kCtrlW, 60);

        // MyMemory
        group_ = &secBasic_;
        Label(Tr(L"What MyMemory does: it translates the lines it gets and keeps them in its public translation "
                 L"memory. Your learned words stay on this PC. With another translator MyMemory is not used at all."),
              kCtrlX, Y(top), kCtrlW, 60);
        // Its e-mail: also shown with "Automatic" – without any key that is MyMemory, so the field is there at once.
        group_ = &secEmail_;
        r = top + 2;
        Label(Tr(L"Your e-mail (optional)"), kLabelX, Y(r), kLabelW);
        Edit(kEmail, cfg_.basicEmail, kCtrlX, Y(r++), kCtrlW);
        Label(Tr(L"No registration: any address of yours works. Without it 5,000 characters a day, with it 50,000 – "
                 L"MyMemory only uses it to count your contingent."),
              kCtrlX, Y(r++) - 6, kCtrlW, 44);

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
#ifndef GCT_KEYBOARD_APP  // GW2 Keyboard: no chat reading, no OCR
        Check(kFixOcr, Tr(L"The same model also repairs misread chat lines"), cfg_.llmFixOcr,
              kCtrlX, Y(r++), kCtrlW);
#endif
        Label(Tr(kLlmPresets[preset].note), kCtrlX, Y(r++) - 4, kCtrlW, 30, kLlmNote);
        // Local models, with an install button (only for the local provider).
        group_ = &secLocal_;
        std::vector<std::wstring> models;
        for (const LocalModelOffer& m : LocalModelOffers()) models.push_back(m.id);
        Label(Tr(L"Install a model"), kLabelX, Y(r), kLabelW);
        Combo(kLocalModel, models, 0, kCtrlX, Y(r), kCtrlW - 126);
        Button(kPull, Tr(L"Install"), kCtrlX + kCtrlW - 120, Y(r++) - 1, 120);
        Label(Tr(LocalModelOffers()[0].summary), kCtrlX, Y(r++) - 4, kCtrlW, 30, kLocalInfo);
        Button(kGetOllama, Tr(L"Get Ollama (free)…"), kCtrlX, Y(r) - 2, 160);
        Button(kLlmFindLocal, Tr(L"Find local server"), kCtrlX + 170, Y(r) - 2, 160);
        Label(Tr(L"Runs the models; LM Studio works too."), kCtrlX + 340, Y(r++) - 2,
              kCtrlW - 340, 30, kPullStatus);
        group_ = nullptr;

        Button(kTest, Tr(L"Test the translator"), kLabelX, Y(12), 180);
        Label(L"", kLabelX + 190, Y(12), kW - 230, 40, kTestStatus);
    }

    // Only the section of the chosen translator is visible (all keep their values).
    void UpdateTranslatorView() {
        const bool page = SendMessageW(tab_, TCM_GETCURSEL, 0, 0) == TabOf(SettingsPage::Translator);
        const Engine e = shownEngine_;
        const bool local = Sel(kLlmPreset) == kPresetLocal;
        auto show = [&](const std::vector<HWND>& g, bool on) {
            for (HWND h : g) ShowWindow(h, page && on ? SW_SHOW : SW_HIDE);
        };
        show(secAuto_, e == Engine::Auto);
        show(secBasic_, e == Engine::Basic);
        show(secEmail_, e == Engine::Basic || e == Engine::Auto);
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
#ifndef GCT_KEYBOARD_APP  // GW2 Keyboard needs no game folder (it finds the game window by itself)
        Label(Tr(L"Guild Wars 2 folder"), kLabelX, Y(r), kLabelW);
        Edit(kGw2Dir, cfg_.gw2Dir.empty() ? FindGw2Dir() : cfg_.gw2Dir, kCtrlX, Y(r), kCtrlW - 182);
        Button(kGw2Find, Tr(L"Find"), kCtrlX + kCtrlW - 176, Y(r) - 1, 70);
        Button(kGw2Browse, Tr(L"Browse…"), kCtrlX + kCtrlW - 102, Y(r++) - 1, 102);
#endif
        Button(kInstall, Tr(L"Install for this Windows user"), kLabelX, Y(r), 240);
        Label(InstallStatusText(), kLabelX + 250, Y(r++) - 2, kW - 290, 34, kInstallStatus);
        Check(kAutostart, Tr(L"Start with Windows (stays hidden until GW2 runs)"), IsAutostartEnabled(), kLabelX,
              Y(r++), kW - 50);
        Check(kStartMenu, Tr(L"Entry in the start menu"), cfg_.startMenu && StartMenuShortcutExists(), kLabelX, Y(r++),
              kW - 50);
        Check(kDock, Tr(L"Dock to the GW2 window (moves with it)"), cfg_.dock, kLabelX, Y(r++), kW - 50);
        Check(kFollow, Tr(L"Show and hide together with the game"), cfg_.followGame, kLabelX, Y(r++), kW - 50);
        // The optional Nexus add-on hands over party/squad chat as exact text (it needs Unofficial Extras in the game).
        Check(kBridgeRead, Tr(L"Read party and squad chat from the Nexus add-on (GW2 Chat Bridge)"), cfg_.bridgeRead,
              kLabelX, Y(r++), kW - 50);
        // Like the GW2 chat, the log empties on a map change – unless kept; and old messages may go after a while.
#ifndef GCT_KEYBOARD_APP  // GW2 Keyboard: no chat reading, no OCR
        Check(kKeepMap, Tr(L"Keep the chat when GW2 clears it (map change)"), cfg_.keepAfterMap, kLabelX, Y(r++),
              kW - 50);
        Label(Tr(L"Connections (read only, nothing is hooked)"), kLabelX, Y(r++), kW - 50);
        Add(L"EDIT", L"", ES_MULTILINE | ES_READONLY | WS_VSCROLL | ES_AUTOVSCROLL, kLabelX, Y(r) - 4, kW - 44, 90,
            kStatus, WS_EX_CLIENTEDGE);
        r += 3;
        Button(kRefresh, Tr(L"Refresh"), kLabelX, Y(r), 110);
        Button(kSetup, Tr(L"Guided setup…"), kLabelX + 120, Y(r++), 160);
#endif
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
#ifndef GCT_KEYBOARD_APP  // GW2 Keyboard: no chat reading, no OCR
        Label(Tr(L"Enlargement for recognition"), kLabelX, Y(r), kLabelW);
        Edit(kOcrZoom, std::to_wstring(cfg_.ocrScale), kCtrlX, Y(r), 60, ES_NUMBER);
        Label(Tr(L"0 = automatic from the line spacing, 1–4 fixed"), kCtrlX + 70, Y(r++) + 4, kCtrlW - 70, 20);
#endif
        Label(Tr(L"Key held when sending (ms)"), kLabelX, Y(r), kCtrlX - kLabelX);
        Edit(kKeyHold, std::to_wstring(cfg_.send.keyHoldMs), kCtrlX, Y(r), 60, ES_NUMBER);
        Label(Tr(L"raise it at low FPS if messages go missing"), kCtrlX + 70, Y(r++) + 4, kCtrlW - 70, 20);
        Label(Tr(L"Pause between keys (ms)"), kLabelX, Y(r), kLabelW);
        Edit(kStepDelay, std::to_wstring(cfg_.send.stepDelayMs), kCtrlX, Y(r++), 60, ES_NUMBER);
        Add(L"EDIT", L"", ES_MULTILINE | ES_READONLY | WS_VSCROLL | WS_HSCROLL | ES_AUTOVSCROLL | ES_AUTOHSCROLL, kLabelX,
            Y(r) - 2, kW - 44, 250, kTechText, WS_EX_CLIENTEDGE);
        r += 8;
        Button(kTechRefresh, Tr(L"Refresh"), kLabelX, Y(r), 92);
        Button(kTechCopy, Tr(L"Copy diagnosis"), kLabelX + 98, Y(r), 136);
        Button(kTechCompare, Tr(L"Compare translators"), kLabelX + 240, Y(r), 148);
        // The whole tool on one page, for someone who never saw it.
        Button(kTour, Tr(L"What can this tool do? (2 minutes)"), kLabelX, Y(r) + 30, 240);
#ifdef GCT_KEYBOARD_APP
        // Everything technical, live while you play: its own small window (the dialog can be closed).
        Button(kLiveView, Tr(L"Live view…"), kLabelX + 248, Y(r) + 30, 150);
#else  // GW2 Keyboard: no chat reading, no OCR
        Button(kTechOcrCompare, Tr(L"Compare recognition"), kLabelX + 394, Y(r), 148);
        Button(kTechSaveF16, Tr(L"Save .f16 capture"), kLabelX + 248, Y(r) + 30, 150);
#endif
        Label(Tr(L"Numbers and settings only, no chat text."), kLabelX, Y(r) + 62, kW - 44, 20, kTechStatus);
        RefreshTechnical();
    }

    void RefreshTechnical() {
        if (ctx_.technicalStatus) SetText(kTechText, ctx_.technicalStatus());
    }

    void CopyTechnical() {
        // Always the numbers and settings – never chat text a comparison may have put into the box.
        const std::wstring text = ctx_.technicalStatus ? ctx_.technicalStatus() : Text(kTechText);
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
        for (auto* g : {&secAuto_, &secBasic_, &secEmail_, &secGoogle_, &secMicrosoft_, &secDeepL_, &secLibre_, &secLlm_, &secLocal_}) g->clear();
    }

    void OnSlider() override {
#ifdef GCT_KEYBOARD_APP
        if (Item(kMicSens))
            SetText(kMicSensValue, std::to_wstring(SendMessageW(Item(kMicSens), TBM_GETPOS, 0, 0)));
#endif
        const int pt = static_cast<int>(SendMessageW(Item(kFontSize), TBM_GETPOS, 0, 0));
        const int transparency = static_cast<int>(SendMessageW(Item(kOpacity), TBM_GETPOS, 0, 0));
        syncing_ = true;
        if (_wtoi(Text(kFontSizeValue).c_str()) != pt) SetText(kFontSizeValue, std::to_wstring(pt));
        syncing_ = false;
        SetText(kOpacityValue, std::to_wstring(transparency) + L" %");
        Preview();
    }

    // Live preview of size, font and transparency on the window behind the dialog.
    void Preview() {
        if (!ctx_.preview) return;
        const int pt = static_cast<int>(SendMessageW(Item(kFontSize), TBM_GETPOS, 0, 0));
        const int transparency = static_cast<int>(SendMessageW(Item(kOpacity), TBM_GETPOS, 0, 0));
        const int face = std::max(0, Sel(kFontFace));
        ctx_.preview(OpacityOf(transparency), PercentOf(pt), kFonts[static_cast<size_t>(face) % std::size(kFonts)]);
    }

    void OnCommand(int id, int code) override {
        switch (id) {
            case IDOK:
                Save();
                result.saved = true;
                // Switched between the GW2 chat and a screen area: the chat is found (and shown to check), a screen
                // area is taken – or drawn when there is none yet.
                if (Item(kReadWhat) && (Sel(kReadWhat) == 1) != cfg_.freeArea)
                    result.action = Sel(kReadWhat) == 1 ? DialogResult::Action::UseFreeArea
                                                        : DialogResult::Action::DetectChat;
                Close();
                break;
            case IDCANCEL:
                if (ctx_.preview) ctx_.preview(cfg_.opacity, cfg_.fontPercent, cfg_.fontFace);  // undo the live preview
                Close();
                break;
#ifdef GCT_KEYBOARD_APP
#endif
            case kLiveView:
                if (ctx_.openLiveView) ctx_.openLiveView();
                break;
            case kTour:
            case kLoadExt:
            case kLoadExtras:
            case kRemoveExtras:
            case kInstallBridge:
            case kRemoveBridge:
                Save();
                result.saved = true;
                result.action = id == kTour            ? DialogResult::Action::ShowTour
                                : id == kLoadExt       ? DialogResult::Action::LoadExtension
                                : id == kLoadExtras    ? DialogResult::Action::LoadExtras
                                : id == kRemoveExtras  ? DialogResult::Action::RemoveExtras
                                : id == kInstallBridge ? DialogResult::Action::InstallBridge
                                                       : DialogResult::Action::RemoveBridge;
                Close();
                break;
#ifndef GCT_KEYBOARD_APP  // GW2 Keyboard: no chat reading, no OCR
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
#endif
            case kPickRegion:
            case kCover:
                Save();
                result.saved = true;
                result.action = id == kCover            ? DialogResult::Action::CoverChat
                                : Sel(kReadWhat) == 1   ? DialogResult::Action::PickFreeArea
                                                        : DialogResult::Action::PickRegion;
                Close();
                break;
            case kLlmLoad:
                LoadModels();
                break;
            case kEngine:
                if (code == CBN_SELCHANGE) {
                    shownEngine_ = EngineAt(Sel(kEngine));
                    SetText(kEngineNote, EngineNote(shownEngine_));
                    UpdateTranslatorView();
                }
                break;
            case kChatEngine:  // "same" shows the section of the messages' translator; else the chat one's (key entry)
                if (code == CBN_SELCHANGE) {
                    shownEngine_ = Sel(kChatEngine) > 0 ? EngineAt(Sel(kChatEngine) - 1) : EngineAt(Sel(kEngine));
                    SetText(kEngineNote, EngineNote(shownEngine_));
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
            case kFontSizeValue:  // typed size: the slider follows (only whole, sensible values)
                if (code == EN_CHANGE && !syncing_) {
                    const int pt = _wtoi(Text(kFontSizeValue).c_str());
                    if (pt >= kMinPt && pt <= kMaxPt) {
                        SendMessageW(Item(kFontSize), TBM_SETPOS, TRUE, pt);
                        Preview();
                    }
                }
                break;
            case kFontFace:
                if (code == CBN_SELCHANGE) Preview();
                break;
            case kHotkeySet: {  // letters alone would be taken from every program: Ctrl or Alt is required
                std::wstring key = hotkey_;
                if (PickKey(hwnd_, inst_, Tr(L"Show / hide hotkey"), hotkey_, cfg_.hotkey, true, &key)) {
                    hotkey_ = key;
                    SetText(kHotkeyStatus, KeyShown(hotkey_));
                }
                break;
            }
#ifdef GCT_KEYBOARD_APP
            case kPttSet: {  // the key anywhere of the Microphone page (works while GW2 is in front)
                std::wstring key = HotkeyValue(kMicGlobalKey);
                if (PickKey(hwnd_, inst_, Tr(L"Push-to-talk key"), key, cfg_.voiceGlobalKey, false, &key)) {
                    SetHotkeyControl(kMicGlobalKey, key);
                    SetText(kPttValue, KeyShown(key));
                }
                break;
            }
            case kMicGlobalKey:
                if (code == EN_CHANGE) SetText(kPttValue, KeyShown(HotkeyValue(kMicGlobalKey)));
                break;
#endif
            case kHelpOcr:
                MessageBoxW(hwnd_,
                            Tr(L"Text recognition (OCR, optical character recognition) turns the picture of the chat into "
                               L"text.\n\n"
                               L"Windows OCR: built into Windows (Windows.Media.Ocr), nothing to install. Measured on 4K "
                               L"chat: about 0.1 s per picture, 0.5–3 % errors. Best with normal and large text.\n\n"
                               L"Tesseract: free open-source recognition, separate install. About 1.5–3.5 s per picture "
                               L"(it starts each time and loads its language models), more exact with very small text.\n\n"
                               L"RapidOCR: open-source deep-learning recognition (PaddleOCR models), runs on this PC. "
                               L"Measured on small (1080p) chat: 8–31 % errors where Windows OCR had 63–81 %. About 1 s "
                               L"for the first picture, then ~0.1 s (unchanged lines are not read again).\n\n"
                               L"Automatic: Windows OCR; for very small text RapidOCR (if installed), else Tesseract.\n\n"
                               L"What helps both most: a larger chat font in GW2 – more pixels per letter.")
                                .c_str(),
                            Tr(L"Text recognition").c_str(), MB_OK | MB_ICONINFORMATION | (UiRtl() ? MB_RTLREADING | MB_RIGHT : 0));
                break;
#ifndef GCT_KEYBOARD_APP  // GW2 Keyboard: no chat reading, no OCR
            case kRapidGet:
                InstallRapid();
                break;
#endif
            case kLtProvider:
                if (code == CBN_SELCHANGE) UpdateLtFields();
                break;
            case kLtTest:
                TestLanguageTool();
                break;
            case kHelpCapture:
                MessageBoxW(hwnd_,
                            Tr(L"How the picture of the chat is taken:\n\n"
                               L"Game window (WGC, Windows Graphics Capture): Windows hands over the content of the GW2 "
                               L"window itself, even under our window. On Windows 10 Windows then draws a yellow frame "
                               L"around the game (it cannot be switched off there).\n\n"
                               L"Screen (DXGI, DirectX desktop duplication): a picture of the screen as you see it, never a "
                               L"frame. Our own windows are hidden from it.\n\n"
                               L"Automatic: game window on Windows 11 (frame switched off), screen on Windows 10.")
                                .c_str(),
                            Tr(L"Picture of the chat").c_str(), MB_OK | MB_ICONINFORMATION | (UiRtl() ? MB_RTLREADING | MB_RIGHT : 0));
                break;
            case kOcrFixes:
                if (ctx_.ocrFixesText && ctx_.setOcrFixes) {
                    std::wstring text = ctx_.ocrFixesText();
                    if (EditOcrFixes(hwnd_, inst_, &text)) ctx_.setOcrFixes(text);
                }
                break;
            case kLearnFile:
                if (ctx_.learnFromFile) {
                    const std::wstring path = PickTextFile(hwnd_, Tr(L"Learn from my texts"), false, L"");
                    if (!path.empty()) SetText(kForgetStatus, ctx_.learnFromFile(path));
                }
                break;
            case kMyWords:
                if (ctx_.myWordsText && ctx_.setMyWords) {
                    std::wstring text = ctx_.myWordsText();
                    if (EditMyWords(hwnd_, inst_, &text)) ctx_.setMyWords(text);
                }
                break;
#ifdef GCT_KEYBOARD_APP
            case kMicMode:
                if (code == CBN_SELCHANGE) UpdateMicView();
                break;
            case kVoiceModel:
            case kVoiceLang:
            case kWriteIn:  // "As I write" follows the writing language
                if (code == CBN_SELCHANGE) RefreshVoice();
                break;
            case kVoiceGet:
                VoiceGetClicked();
                break;
            case kVoiceTest:
                TestVoice();
                break;
            case kVoiceFixes:
                if (ctx_.voiceFixesText && ctx_.setVoiceFixes) {
                    std::wstring text = ctx_.voiceFixesText();
                    if (EditVoiceFixes(hwnd_, inst_, &text)) ctx_.setVoiceFixes(text);
                }
                break;
#endif
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
            case kTechOcrCompare:
                if (ctx_.compareOcr) {
                    EnableWindow(Item(kTechOcrCompare), FALSE);
                    SetText(kTechText, Tr(L"Reading one picture of the chat with every text recognition …"));
                    ctx_.compareOcr(hwnd_, WM_APP_OCRCMP);
                }
                break;
            case kTechSaveF16:
                if (ctx_.saveF16) {
                    ctx_.saveF16();
                    SetText(kTechStatus, Tr(L"Saving next chat capture as .f16 …"));
                }
                break;
            case kLocalModel:
                if (code == CBN_SELCHANGE && Sel(kLocalModel) >= 0)
                    SetText(kLocalInfo, Tr(LocalModelOffers()[static_cast<size_t>(Sel(kLocalModel))].summary));
                break;
            case kGetOllama:
                ShellExecuteW(hwnd_, L"open", L"https://ollama.com/download", nullptr, nullptr, SW_SHOWNORMAL);
                break;
            case kLlmFindLocal:
                FindLocalServer();
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

    void FindLocalServer() {
        SetText(kPullStatus, Tr(L"Searching for local server …"));
        EnableWindow(Item(kLlmFindLocal), FALSE);
        std::thread([h = hwnd_] {
            auto msg = std::make_unique<FindLocalMsg>();
            msg->found = DiscoverLocalLlmServer(&msg->result, &msg->error);
            if (PostMessageW(h, WM_APP_FIND_LOCAL, 0, reinterpret_cast<LPARAM>(msg.get()))) msg.release();
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

#ifndef GCT_KEYBOARD_APP  // GW2 Keyboard: no chat reading, no OCR
    // The RapidOCR model groups your languages need (Latin always).
    std::vector<std::wstring> RapidGroupsNeeded() const {
        std::vector<std::wstring> langs = cfg_.writeLangs;
        langs.push_back(cfg_.readLang);
        langs.push_back(cfg_.chatLang);
        return RapidGroupsFor(langs);
    }

    // Something for your languages is not there yet (and could be downloaded).
    bool RapidMissing() const {
        if (!RapidRecognizer::RuntimeAvailable(nullptr)) return false;
        for (const std::wstring& id : RapidGroupsNeeded())
            if (const RapidModelGroup* g = FindRapidGroup(id); g && RapidGroupDir(*g, cfg_.RapidDir()).empty()) return true;
        return false;
    }

    std::wstring RapidStatus() const {
        if (!RapidRecognizer::RuntimeAvailable(nullptr))
            return Tr(L"[--] Not in this build (onnxruntime.dll missing).");
        std::wstring have, missing;
        int mb = 0;
        for (const std::wstring& id : RapidGroupsNeeded()) {
            const RapidModelGroup* g = FindRapidGroup(id);
            if (!g) continue;
            if (!RapidGroupDir(*g, cfg_.RapidDir()).empty()) have += (have.empty() ? L"" : L", ") + std::wstring(g->id);
            else {
                missing += (missing.empty() ? L"" : L", ") + std::wstring(g->id);
                mb += g->sizeMb;
            }
        }
        if (missing.empty()) return TrF(L"[OK] Installed for your languages ({1}).", {have});
        return TrF(L"[--] Not installed – about {1} MB for your languages ({2}).", {std::to_wstring(mb), missing});
    }

    // Downloads the model groups for your languages (official RapidOCR files, checksums checked).
    void InstallRapid() {
        if (!RapidRecognizer::RuntimeAvailable(nullptr)) return;
        std::vector<const RapidModelGroup*> todo;
        int mb = 0;
        for (const std::wstring& id : RapidGroupsNeeded())
            if (const RapidModelGroup* g = FindRapidGroup(id); g && RapidGroupDir(*g, cfg_.RapidDir()).empty()) {
                todo.push_back(g);
                mb += g->sizeMb;
            }
        if (todo.empty()) {
            SetText(kRapidStatus, RapidStatus());
            return;
        }
        if (MessageBoxW(hwnd_,
                        TrF(L"Download the open-source RapidOCR models for your languages (about {1} MB, from the "
                            L"RapidAI project on ModelScope)?\n\nThey read the chat on this PC only; nothing is sent "
                            L"anywhere. Licence: Apache-2.0.",
                            {std::to_wstring(mb)})
                            .c_str(),
                        L"RapidOCR", MB_YESNO | MB_ICONQUESTION | (UiRtl() ? MB_RTLREADING | MB_RIGHT : 0)) != IDYES)
            return;
        EnableWindow(Item(kRapidGet), FALSE);
        SetText(kRapidStatus, Tr(L"Downloading …"));
        std::thread([h = hwnd_, todo, dir = cfg_.RapidDir()] {
            std::wstring error;
            for (const RapidModelGroup* g : todo)
                if (!DownloadRapidGroup(*g, dir, &error)) break;
            auto* msg = new std::wstring(error);
            if (!PostMessageW(h, WM_APP_RAPID, 0, reinterpret_cast<LPARAM>(msg))) delete msg;
        }).detach();
    }
#endif

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
        if (msg == WM_APP_FIND_LOCAL) {
            std::unique_ptr<FindLocalMsg> m(reinterpret_cast<FindLocalMsg*>(lp));
            EnableWindow(Item(kLlmFindLocal), TRUE);
            if (!m->found) {
                SetText(kPullStatus, m->error);
            } else {
                SetText(kLlmUrl, m->result.url);
                HWND combo = Item(kLlmModel);
                SendMessageW(combo, CB_RESETCONTENT, 0, 0);
                for (const std::wstring& s : m->result.models) {
                    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s.c_str()));
                }
                if (!m->result.models.empty()) {
                    SendMessageW(combo, CB_SETCURSEL, 0, 0);
                    SetWindowTextW(combo, m->result.models[0].c_str());
                }
                SetText(kPullStatus, TrF(L"{1} found ({2} models)", {m->result.name, std::to_wstring(m->result.models.size())}));
            }
            return 0;
        }
        if (msg == WM_APP_LTTEST) {
            std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(lp));
            EnableWindow(Item(kLtTest), TRUE);
            SetText(kLtStatus, *text);
            return 0;
        }
        if (msg == WM_APP_OCRCMP) {
            std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(lp));
            EnableWindow(Item(kTechOcrCompare), TRUE);
            SetText(kTechText, *text);
            return 0;
        }
#ifndef GCT_KEYBOARD_APP  // GW2 Keyboard: no chat reading, no OCR
        if (msg == WM_APP_RAPID) {
            std::unique_ptr<std::wstring> err(reinterpret_cast<std::wstring*>(lp));
            EnableWindow(Item(kRapidGet), RapidMissing());
            SetText(kRapidStatus, err->empty() ? RapidStatus() : TrF(L"[!] Not installed: {1}", {*err}));
            if (err->empty()) SendMessageW(Item(kOcrEngine), CB_SETCURSEL, 0, 0);  // automatic uses it from now on
            return 0;
        }
#endif
#ifdef GCT_KEYBOARD_APP
        if (msg == WM_APP_VOICE_PROGRESS) {
            if (!voiceDownloading_.empty()) {
                voicePercent_ = static_cast<int>(wp);
                const VoiceModelInfo* m = SelectedVoiceModel();
                if (m && m->id == voiceDownloading_) SetText(kVoiceModelStatus, VoiceModelStatus());
            }
            return 0;
        }
        if (msg == WM_APP_VOICE_DONE) {
            std::unique_ptr<std::wstring> err(reinterpret_cast<std::wstring*>(lp));
            const std::string id = voiceDownloading_;
            const bool cancelled = voiceCancel_ && *voiceCancel_;
            voiceDownloading_.clear();
            voiceCancel_.reset();
            voiceError_ = cancelled ? std::wstring() : *err;  // cancelled on purpose is no error
            voiceErrorId_ = id;
            if (err->empty())  // ready: the downloaded model is the chosen one (saved with OK)
                for (size_t i = 0; i < voiceModelIds_.size(); ++i)
                    if (voiceModelIds_[i] == id) SendMessageW(Item(kVoiceModel), CB_SETCURSEL, i, 0);
            RefreshVoice();
            return 0;
        }
        if (msg == WM_APP_VOICE_TEST) {
            std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(lp));
            voiceTesting_ = false;
            EnableWindow(Item(kVoiceTest), voiceOk_);
            SetText(kVoiceTestStatus, text ? *text : std::wstring());
            return 0;
        }
#endif
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
        const std::wstring existing = FindInstalledExe();
        const std::wstring target =
            existing.empty() ? UserInstallDir() : existing.substr(0, existing.find_last_of(L"\\/"));
        InstallResult r = InstallTo(target);
        if (!r.ok) {
            SetText(kInstallStatus, r.error);
            return;
        }
        if (!dir.empty()) cfg_.gw2Dir = dir;
        if (r.alreadyThere) {
            SetText(kInstallStatus, Tr(L"Installed here."));
            return;
        }
        SetText(kInstallStatus, TrF(L"Installed: {1}", {r.exePath}));
        if (Checked(kAutostart)) SetAutostart(true, r.exePath);
        CreateStartMenuShortcut(r.exePath);
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
        if (Sel(kUiLang) >= 0) {
            c.uiLangCode = LanguageAt(Sel(kUiLang));
            c.uiLang = EffectiveUiLang(c.uiLangCode);
        }
#ifndef GCT_KEYBOARD_APP  // GW2 Keyboard: no chat reading, no OCR
        if (Sel(kReadLang) >= 0) c.readLang = LanguageAt(Sel(kReadLang));
#endif
        // "Send as" languages are no longer set here: the window offers every language and remembers the used ones.
        c.fontPercent = PercentOf(static_cast<int>(SendMessageW(Item(kFontSize), TBM_GETPOS, 0, 0)));
        c.fontFace = kFonts[static_cast<size_t>(std::max(0, Sel(kFontFace))) % std::size(kFonts)];
        c.opacity = OpacityOf(static_cast<int>(SendMessageW(Item(kOpacity), TBM_GETPOS, 0, 0)));
        c.hotkey = hotkey_;  // empty = no hotkey

#ifndef GCT_KEYBOARD_APP  // GW2 Keyboard: no chat reading, no OCR
        // Reading on/off is switched in the main window.
        if (Sel(kOcrEngine) >= 0) c.ocr = static_cast<OcrChoice>(Sel(kOcrEngine));
        if (Sel(kCapture) >= 0) c.captureMode = Sel(kCapture);
        c.tesseractPath = Trim(Text(kTessPath));
        // Chinese for Tesseract by itself, when one of your languages is Chinese (it makes Tesseract slower).
        c.readChinese = PrimaryLang(c.readLang) == L"ZH" || PrimaryLang(c.chatLang) == L"ZH" ||
                        std::any_of(c.writeLangs.begin(), c.writeLangs.end(),
                                    [](const std::wstring& l) { return PrimaryLang(l) == L"ZH"; });
        const int interval = _wtoi(Text(kInterval).c_str());
        if (interval > 0) c.readerIntervalMs = std::clamp(interval, 200, 2000);
        c.onlyTranslations = Checked(kOnlyTr);
        c.keepAfterMap = Checked(kKeepMap);
        c.showSystemLines = !Checked(kShowSystem);  // the box says "filter
        c.secondLook = Checked(kSecondLook);
        c.saveCaptures = Checked(kCaptures);
#endif

        c.spellEnabled = Checked(kSpell);
        if (Sel(kAutoCorrect) >= 0) c.autoCorrect = static_cast<AutoCorrectMode>(Sel(kAutoCorrect));
        c.suggestions = Checked(kSuggest);
        c.learnWords = Sel(kLearn) != 2;
        c.learnLater = Sel(kLearn) == 1;
        c.languageTool = Sel(kLtProvider) > 0;
        if (!Trim(Text(kLtUrl)).empty()) c.languageToolUrl = Trim(Text(kLtUrl));
        if (Sel(kWriteIn) >= 0)
            c.writeIn = Sel(kWriteIn) == 0 ? L"" : Languages()[static_cast<size_t>(Sel(kWriteIn) - 1)].code;
        c.backTranslate = Checked(kBackTr);
#ifdef GCT_KEYBOARD_APP
        c.voiceEnabled = Checked(kVoiceOn);
        if (const VoiceModelInfo* m = SelectedVoiceModel()) {
            // The catalog's recommendation stays "" (it follows a newer catalog) unless a model was chosen before.
            const VoiceModelInfo* rec = voiceCatalog_.Recommended();
            c.voiceModel = cfg_.voiceModel.empty() && rec && rec->id == m->id ? std::wstring() : FromUtf8(m->id);
        }
        auto pick = [this](int id, const auto& values, auto* out) {
            const int i = Sel(id);
            if (i >= 0 && static_cast<size_t>(i) < values.size()) *out = values[static_cast<size_t>(i)];
        };
        pick(kVoiceLang, voiceLangValues_, &c.voiceLang);
        c.voiceTranslate = Checked(kVoiceTranslate);
        c.voiceKey = HotkeyValue(kMicKey);
        c.voiceGlobalKey = HotkeyValue(kMicGlobalKey);
        c.voiceToggle = Sel(kMicMode) == 1;
        c.voiceActivation = Sel(kMicMode) == 2;
        if (Sel(kMicGuard) >= 0) c.voiceGuard = Sel(kMicGuard);
        c.voiceSensitivity = static_cast<int>(SendMessageW(Item(kMicSens), TBM_GETPOS, 0, 0));
        if (const int v = _wtoi(Text(kMicPause).c_str()); v > 0) c.voicePauseMs = std::clamp(v, 400, 3000);
        c.voicePreRollMs = std::clamp(_wtoi(Text(kMicPreRoll).c_str()), 0, 1000);
        if (const int v = _wtoi(Text(kMicMinMs).c_str()); v > 0) c.voiceMinMs = std::clamp(v, 100, 1500);
        c.voiceChatPause = Checked(kMicMute);
        c.voiceAppend = Sel(kVoiceAppend) == 1;
        if (const int sec = _wtoi(Text(kVoiceMaxSec).c_str()); sec > 0) c.voiceMaxSec = std::clamp(sec, 5, 120);
        c.voiceLive = Checked(kVoiceLive);
        pick(kVoiceThreads, voiceThreadValues_, &c.voiceThreads);
        pick(kVoiceDevice, voiceDeviceIds_, &c.voiceDevice);
        c.voiceGlossary = Checked(kVoiceGlossary);
        c.translateWriting = Checked(kTranslateWriting);
        if (Sel(kVoiceBoost) >= 0) c.voiceBoost = Sel(kVoiceBoost);
#endif
#ifndef GCT_KEYBOARD_APP  // GW2 Keyboard: no chat reading, no OCR
        c.ocrScale = std::clamp(_wtoi(Text(kOcrZoom).c_str()), 0, 4);
#endif
        c.send.keyHoldMs = std::clamp(_wtoi(Text(kKeyHold).c_str()), 5, 500);
        c.send.stepDelayMs = std::clamp(_wtoi(Text(kStepDelay).c_str()), 20, 1000);
#ifndef GCT_KEYBOARD_APP  // GW2 Keyboard: no chat reading, no OCR
        c.understoodLangs.clear();
        const wchar_t* quick[] = {L"EN", L"DE", L"FR", L"ES"};
        for (int i = 0; i < 4; ++i)
            if (Checked(kSkipEn + i)) c.understoodLangs.push_back(quick[i]);
        if (Checked(kSkipMore) && Sel(kUnderstood) >= 0)
            c.understoodLangs.push_back(PrimaryLang(Languages()[static_cast<size_t>(Sel(kUnderstood))].code));
        // Further codes from the settings file (not shown here) stay.
        bool firstOther = true;
        for (const std::wstring& l : cfg_.understoodLangs) {
            const std::wstring p = PrimaryLang(l);
            if (p == L"EN" || p == L"DE" || p == L"FR" || p == L"ES") continue;
            if (firstOther) {
                firstOther = false;
                continue;
            }
            if (std::find(c.understoodLangs.begin(), c.understoodLangs.end(), l) == c.understoodLangs.end())
                c.understoodLangs.push_back(l);
        }
#endif
        // Everything that is read is translated (the channel choice is gone); a click translates the rest.
        c.autoTranslate = DefaultAutoTranslate();
        c.copyOnly = Sel(kSendMode) == 1;
        c.returnFocus = Checked(kReturnFocus);

        if (Sel(kEngine) >= 0) c.engine = EngineAt(Sel(kEngine));
#ifndef GCT_KEYBOARD_APP  // GW2 Keyboard: no chat reading, no OCR
        if (Sel(kChatEngine) >= 0) {
            c.chatSameEngine = Sel(kChatEngine) == 0;
            c.chatEngine = c.chatSameEngine ? Engine::Auto : EngineAt(Sel(kChatEngine) - 1);
        }
        c.keyboardOnly = Checked(kKeyboardOnly);
#endif
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
#ifndef GCT_KEYBOARD_APP  // GW2 Keyboard: no chat reading, no OCR
        c.llmFixOcr = Checked(kFixOcr);
#endif

#ifndef GCT_KEYBOARD_APP
        const std::wstring rawDir = Trim(Text(kGw2Dir));
        const std::wstring resolved = ResolveGw2Dir(rawDir);
        c.gw2Dir = resolved.empty() ? rawDir : resolved;
#endif
        c.dock = Checked(kDock);
        c.followGame = Checked(kFollow);
        c.bridgeRead = Checked(kBridgeRead);
#ifdef GCT_KEYBOARD_APP
        c.showOnScreenshots = Checked(kScreenshots);
#endif
        c.focusOnGameChat = Checked(kFocusGameChat);
    }

    void Save() {
        Collect(cfg_);
        const bool autostart = Checked(kAutostart);
        if (autostart != IsAutostartEnabled() || (autostart && AutostartTarget() != CurrentExePath()))
            SetAutostart(autostart, CurrentExePath());
        cfg_.startMenu = Checked(kStartMenu);
        if (cfg_.startMenu) CreateStartMenuShortcut(CurrentExePath());
        else RemoveStartMenuShortcut();
        cfg_.SaveAll();
    }

    Config& cfg_;
    const DialogContext& ctx_;
    SettingsPage start_;
    HWND tab_ = nullptr;
    // Translator page: one section per translator, only the chosen one is visible.
    std::vector<HWND> secAuto_, secBasic_, secEmail_, secGoogle_, secMicrosoft_, secDeepL_, secLibre_, secLlm_, secLocal_;
    Engine shownEngine_ = Engine::Auto;  // whose key section is visible (the translator combo changed last)
    int lastPreset_ = -1;
    bool syncing_ = false;  // slider and size field update each other
#ifdef GCT_KEYBOARD_APP
    // Voice page: the catalog and this PC, the values behind the combo boxes, download and test state.
    VoiceCatalog voiceCatalog_;
    VoiceHardware voiceHw_;
    std::string voiceRecommended_;            // catalog id recommended for this PC
    std::vector<std::string> voiceModelIds_;  // combo index -> catalog id
    std::vector<std::wstring> voiceLangValues_, voiceKeyValues_, voiceDeviceIds_;
    std::vector<int> voiceThreadValues_;
    std::vector<HWND> voiceCtrls_;  // disabled when voice input cannot work (recognizer DLLs missing)
    bool voiceOk_ = true;
    std::wstring voiceWhy_;
    std::string voiceDownloading_;  // catalog id of the running download
    int voicePercent_ = 0;
    std::shared_ptr<std::atomic<bool>> voiceCancel_;  // set by "Cancel" and when the dialog closes
    std::wstring voiceError_;     // the last download error ...
    std::string voiceErrorId_;    // ... and for which model
    bool voiceTesting_ = false;
#endif
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
        // Both language lists are the same: "Windows language" first, then every language.
        Label(Tr(L"Language of this window"), 24, 60, 190);
        Combo(kUiLang, LanguageChoices(), LanguageChoiceOf(cfg_.uiLangCode), 220, 60, 300);
        Label(Tr(L"Translate the chat into"), 24, 96, 190);
        Combo(kReadLang, LanguageChoices(), LanguageChoiceOf(cfg_.readLang), 220, 96, 300);
        Label(Tr(L"Guild Wars 2 folder"), 24, 140, 190);
        const std::wstring found = cfg_.gw2Dir.empty() ? FindGw2Dir() : cfg_.gw2Dir;
        Edit(kGw2Dir, found, 220, 140, 196);
        Button(kGw2Browse, Tr(L"Browse…"), 420, 139, 100);
        // Installed for this Windows user (%LOCALAPPDATA%\\Programs), like Discord or VS Code: the download folder is
        // never where it keeps running. Unticked = it stays where it is (portable).
        Check(kInstall, Tr(L"Install for this Windows user (recommended)"), true, 24, 178, kW - 48);
        // Both free choices: an installed copy is always in the start menu, so the tool can never get lost.
        // Starting by hand is the default; autostart keeps it hidden in the background until GW2 appears.
        Check(kDesktop, Tr(L"Shortcut on the desktop"), true, 24, 206, kW - 48);
        Check(kStartMenu, Tr(L"Entry in the start menu"), cfg_.startMenu, 24, 234, kW - 48);
        Check(kAutostart, Tr(L"Start with Windows and appear when GW2 runs"), false, 24, 262, kW - 48);
        // Only the typing help, or the chat translation too (that one needs the chat marked later).
        Check(kKeyboardOnly, Tr(L"Keyboard only: no chat reading at all, only the typing help"), cfg_.keyboardOnly, 24,
              290, kW - 48);
        EndPages();

        // One page: the chat itself is found later, when it is open.
        Button(kNext, Tr(L"Finish"), kW - 222, 332, 100);
        Button(IDCANCEL, Tr(L"Close"), kW - 114, 332, 100);
        SetText(kStepTitle, Tr(L"Setup – one step"));
        ShowPage(0);
    }

    bool ApplyStep1() {
        cfg_.uiLangCode = LanguageAt(Sel(kUiLang));
        cfg_.uiLang = EffectiveUiLang(cfg_.uiLangCode);
        cfg_.readLang = LanguageAt(Sel(kReadLang));
        const std::wstring rawDir = Trim(Text(kGw2Dir));
        const std::wstring resolved = ResolveGw2Dir(rawDir);
        const std::wstring dir = resolved.empty() ? rawDir : resolved;
        if (!dir.empty()) cfg_.gw2Dir = dir;
        cfg_.startMenu = Checked(kStartMenu);
        cfg_.keyboardOnly = Checked(kKeyboardOnly);
        // Saved before installing: the installed copy takes this settings file along.
        cfg_.setupDone = true;
        cfg_.SaveAll();
        std::wstring exe = CurrentExePath();
        if (Checked(kInstall)) {
            // An older install in the game folder is updated where it is; otherwise the user folder.
            const std::wstring existing = FindInstalledExe();
            const std::wstring target =
                existing.empty() ? UserInstallDir() : existing.substr(0, existing.find_last_of(L"\\/"));
            InstallResult r = InstallTo(target);
            if (!r.ok) {
                MessageBoxW(hwnd_, r.error.c_str(), L"GW2 Chat Translator", MB_OK | MB_ICONWARNING);
                return false;
            }
            if (!r.alreadyThere) installedExe_ = r.exePath;
            exe = r.exePath;
        }
        // Autostart and the shortcut always point to the copy that keeps running: the installed one.
        SetAutostart(Checked(kAutostart), exe);
        if (Checked(kInstall) && Checked(kStartMenu)) CreateStartMenuShortcut(exe);  // start menu + Windows search
        else if (!Checked(kStartMenu)) RemoveStartMenuShortcut();
        if (Checked(kDesktop)) CreateDesktopShortcut(exe);
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
                    if (Sel(kUiLang) >= 0) {
                        cfg_.readLang = LanguageAt(Sel(kReadLang));
                        if (!Trim(Text(kGw2Dir)).empty()) cfg_.gw2Dir = Trim(Text(kGw2Dir));
                        cfg_.uiLangCode = LanguageAt(Sel(kUiLang));
                        cfg_.uiLang = EffectiveUiLang(cfg_.uiLangCode);
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

// A small text editor: one line (a word's meaning) or many (the list of your words).
class TextDialog final : public NativeDialog {
public:
    TextDialog(std::wstring label, std::wstring hint, std::wstring text, bool multiline)
        : label_(std::move(label)), hint_(std::move(hint)), text_(std::move(text)), multi_(multiline) {}
    bool ok = false;
    std::wstring text;
    int Height() const { return multi_ ? 380 : 180; }

private:
    static constexpr int kW = 480;
    enum : int { kEditText = 300 };

    void Build() override {
        Label(label_, 16, 12, kW - 32, 34);
        const int h = multi_ ? 230 : 26;
        HWND e = Edit(kEditText, text_, 16, 50, kW - 32,
                      multi_ ? ES_MULTILINE | ES_WANTRETURN | WS_VSCROLL | ES_AUTOVSCROLL : 0, h);
        Label(hint_, 16, 58 + h, kW - 32, 36);
        Button(IDOK, Tr(L"OK"), kW - 228, Height() - 40, 104);
        Button(IDCANCEL, Tr(L"Cancel"), kW - 116, Height() - 40, 100);
        SetFocus(e);
        SendMessageW(e, EM_SETSEL, 0, -1);
    }

    void OnCommand(int id, int) override {
        if (id == IDOK) {
            text = Text(kEditText);
            ok = true;
            Close();
        } else if (id == IDCANCEL) {
            Close();
        }
    }

    std::wstring label_, hint_, text_;
    bool multi_;
};

class KeyDialog final : public NativeDialog {
public:
    KeyDialog(std::wstring current, std::wstring mine, bool needModifier)
        : current_(std::move(current)), mine_(std::move(mine)), needModifier_(needModifier) {}
    bool ok = false;
    std::wstring key;
    static constexpr int kW = 420, kH = 196;

private:
    enum : int { kKey = 300, kKeyStatus, kKeyNone };

    void Build() override {
        Label(Tr(L"Press the keys: two of them (for example Ctrl+T), a third one is optional (Ctrl+Alt+T)."), 16, 12,
              kW - 32, 34);
        HWND hk = Add(HOTKEY_CLASSW, L"", WS_TABSTOP, 16, 52, 220, 26, kKey, WS_EX_CLIENTEDGE);
        if (needModifier_)  // letters alone would be taken from every program
            SendMessageW(hk, HKM_SETRULES, HKCOMB_NONE | HKCOMB_S, MAKELPARAM(HOTKEYF_CONTROL | HOTKEYF_ALT, 0));
        if (const auto cur = ParseHotkey(current_)) {
            BYTE f = 0;
            if (cur->mods & kModCtrl) f |= HOTKEYF_CONTROL;
            if (cur->mods & kModAlt) f |= HOTKEYF_ALT;
            if (cur->mods & kModShift) f |= HOTKEYF_SHIFT;
            SendMessageW(hk, HKM_SETHOTKEY, MAKEWORD(static_cast<BYTE>(cur->vk), f), 0);
        }
        Button(kKeyNone, Tr(L"None"), 244, 51, 90);
        Label(L"", 16, 88, kW - 32, 50, kKeyStatus);
        Button(IDOK, Tr(L"OK"), kW - 228, kH - 40, 104);
        Button(IDCANCEL, Tr(L"Cancel"), kW - 116, kH - 40, 100);
        UpdateStatus();
        SetFocus(hk);
    }

    std::wstring Value() const {
        const WORD v = static_cast<WORD>(SendMessageW(Item(kKey), HKM_GETHOTKEY, 0, 0));
        Hotkey hk;
        hk.vk = LOBYTE(v);
        const BYTE f = HIBYTE(v);
        if (f & HOTKEYF_CONTROL) hk.mods |= kModCtrl;
        if (f & HOTKEYF_ALT) hk.mods |= kModAlt;
        if (f & HOTKEYF_SHIFT) hk.mods |= kModShift;
        return hk.vk ? FormatHotkey(hk) : L"";
    }

    // Free or taken? Windows tells us for hotkeys of other programs; keys a game or add-on (arcdps) reads itself
    // cannot be seen from outside.
    void UpdateStatus() {
        const std::wstring text = Value();
        std::wstring status;
        if (text.empty()) {
            status = Tr(L"No key.");
        } else if (const auto hk = ParseHotkey(text); !hk) {
            status = Tr(L"[!] This key cannot be used – take a letter, digit or F-key with Ctrl or Alt.");
        } else if (const auto mine = ParseHotkey(mine_); mine && mine->vk == hk->vk && mine->mods == hk->mods) {
            status = Tr(L"[OK] In use by this tool. Keys of the game and its add-ons (e.g. arcdps) cannot be checked – "
                        L"three keys clash least.");
        } else if (RegisterHotKey(hwnd_, 0x7FFF, hk->mods | MOD_NOREPEAT, hk->vk)) {
            UnregisterHotKey(hwnd_, 0x7FFF);
            status = Tr(L"[OK] Free in Windows. Keys of the game and its add-ons (e.g. arcdps) cannot be checked – "
                        L"three keys clash least.");
        } else {
            status = Tr(L"[!] Already taken by another program – choose another one.");
        }
        SetText(kKeyStatus, status);
    }

    void OnCommand(int id, int code) override {
        if (id == kKey && code == EN_CHANGE) {
            UpdateStatus();
        } else if (id == kKeyNone) {
            SendMessageW(Item(kKey), HKM_SETHOTKEY, 0, 0);
            UpdateStatus();
        } else if (id == IDOK) {
            key = Value();
            ok = true;
            Close();
        } else if (id == IDCANCEL) {
            Close();
        }
    }

    std::wstring current_, mine_;
    bool needModifier_;
};

bool PickKey(HWND owner, HINSTANCE inst, const std::wstring& title, const std::wstring& current, const std::wstring& mine,
             bool needModifier, std::wstring* key) {
    KeyDialog dlg(current, mine, needModifier);
    dlg.Run(owner, inst, title, KeyDialog::kW, KeyDialog::kH);
    if (dlg.ok) *key = dlg.key;
    return dlg.ok;
}

#ifdef GCT_KEYBOARD_APP
bool EditVoiceFixes(HWND owner, HINSTANCE inst, std::wstring* text) {
    TextDialog dlg(Tr(L"Words the voice input heard wrong – one per line as “heard = meant” (for example Red = Raid)."),
                   Tr(L"They are replaced right after recognition. Lines under [de], [en] … count only for that language, "
                      L"lines above them for every language. Delete a line if a fix is wrong; add your own. Stays on this "
                      L"PC."),
                   *text, true);
    dlg.Run(owner, inst, Tr(L"Learned misheard words"), 480, dlg.Height());
    if (dlg.ok) *text = dlg.text;
    return dlg.ok;
}
#endif

}  // namespace

bool AskWordMeaning(HWND owner, HINSTANCE inst, const std::wstring& word, std::wstring* meaning) {
    TextDialog dlg(TrF(L"What does “{1}” mean? (in plain words of the same language)", {word}),
                   Tr(L"Example: finds = finde es. Before translating, the word is replaced by this; it also counts as "
                      L"correct. Empty = only mark the word as correct."),
                   *meaning, false);
    dlg.Run(owner, inst, Tr(L"Explain a word"), 480, dlg.Height());
    if (dlg.ok) *meaning = Trim(dlg.text);
    return dlg.ok;
}

bool EditMyWords(HWND owner, HINSTANCE inst, std::wstring* text) {
    TextDialog dlg(Tr(L"Your words: slang, abbreviations, mixed language – one per line as “word = meaning”."),
                   Tr(L"Before translating, each word is replaced by its meaning (your text stays as written). A line "
                      L"without “=” only marks the word as correct. Stays on this PC."),
                   *text, true);
    dlg.Run(owner, inst, Tr(L"My words"), 480, dlg.Height());
    if (dlg.ok) *text = dlg.text;
    return dlg.ok;
}

bool EditOcrFixes(HWND owner, HINSTANCE inst, std::wstring* text) {
    TextDialog dlg(Tr(L"Recognition errors the smart artifact correction has learned – one per line as "
                      L"“as read = correct”."),
                   Tr(L"They are fixed right after reading, without reading the word again. Delete a line if a fix is "
                      L"wrong; add your own. Stays on this PC (ocr-fixes.txt) – you can share the file."),
                   *text, true);
    dlg.Run(owner, inst, Tr(L"Learned recognition fixes"), 480, dlg.Height());
    if (dlg.ok) *text = dlg.text;
    return dlg.ok;
}

bool AskCorrection(HWND owner, HINSTANCE inst, const std::wstring& original, std::wstring* translation) {
    CorrectionDialog dlg(original, *translation);
    dlg.Run(owner, inst, Tr(L"Correct the translation"), 520, 272);
    if (dlg.ok) *translation = dlg.text;
    return dlg.ok;
}

DialogResult ShowSettingsDialog(HWND owner, HINSTANCE inst, Config& cfg, const DialogContext& ctx, SettingsPage start) {
    SettingsDialog dlg(cfg, ctx, start);
#ifdef GCT_KEYBOARD_APP
    dlg.Run(owner, inst, Tr(L"Settings") + L" – GW2 Keyboard", 580, 530);
#else
    dlg.Run(owner, inst, Tr(L"Settings") + L" – GW2 Chat Translator", 580, 530);
#endif
    return dlg.result;
}

DialogResult ShowSetupWizard(HWND owner, HINSTANCE inst, Config& cfg, const DialogContext& ctx) {
    SetupWizard dlg(cfg, ctx);
    dlg.Run(owner, inst, Tr(L"Setup") + L" – GW2 Chat Translator", 560, 370);
    return dlg.result;
}

}  // namespace gct
