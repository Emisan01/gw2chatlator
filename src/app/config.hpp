// config.hpp — settings from gw2-chat-translator.ini (created with defaults
// and comments on first start; missing keys fall back to defaults). Every
// setting can also be changed in the window (menu ≡ → Settings).
#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "core/chat_line.hpp"
#include "core/chat_tabs.hpp"
#include "core/i18n.hpp"
#include "win/gw2_sender.hpp"

namespace gct {

enum class Engine { Auto, Basic, DeepL, Llm };
enum class OcrChoice { Auto, Tesseract, Windows };
enum class AutoCorrectMode { Off, Safe, Phone };

struct Config {
    std::wstring dataDir;
    std::wstring iniPath;

    // [General]
    UiLang uiLang = UiLang::En;
    bool setupDone = false;     // the guided setup ran once
    std::wstring gw2Dir;        // found or chosen game folder

    // [Translate]
    Engine engine = Engine::Auto;
    std::wstring readLang;                    // empty = Windows language
    std::vector<std::wstring> writeLangs{L"EN-GB", L"FR", L"ES", L"DE"};
    bool backTranslate = true;
    int debounceMs = 500;

    // [Basic] MyMemory
    std::wstring basicEmail;
    bool myMemoryNoticeShown = false;  // the one-time privacy notice was shown
    // [DeepL]
    std::wstring deeplKey;
    // [LLM]
    std::wstring llmUrl, llmModel, llmKey;
    int llmTimeoutSec = 60;
    bool llmFixOcr = true;  // incoming lines: repair OCR errors while translating

    // [Reader]
    bool readerEnabled = true;
    int readerIntervalMs = 400;  // a look every 0.4 s; text recognition only runs when the picture changed
    OcrChoice ocr = OcrChoice::Auto;
    std::wstring tesseractPath;   // empty = search
    std::string tesseractLangs;   // empty = automatic ("eng+deu+fra+spa")
    bool readChinese = false;     // add Simplified Chinese to Tesseract
    std::wstring ocrLanguage;     // Windows OCR: empty = Windows languages
    int ocrScale = 0;             // 0 = automatic (line spacing), 1-4 fixed
    bool showSystemLines = false;
    bool saveCaptures = false;
    bool regionSet = false;
    int regionLeft = 0, regionFromBottom = 0, regionWidth = 0, regionHeight = 0;  // relative to GW2 client area
    std::vector<ChannelColor> palette;

    // [Spelling]
    bool spellEnabled = true;
    AutoCorrectMode autoCorrect = AutoCorrectMode::Phone;
    bool suggestions = true;     // word bar above the input (completions, next word)
    bool learnWords = true;      // learn from what you send
    bool languageTool = false;   // optional grammar check
    std::wstring languageToolUrl = L"https://api.languagetool.org";

    // [Glossary]
    bool glossaryEnabled = true;
    int glossaryRefreshDays = 14;

    // [Hotkey]
    std::wstring hotkey = L"Ctrl+Alt+T";

    // [Chat]
    int maxLength = 199;
    bool returnFocus = false;
    // true: Enter only copies the line; you paste it in GW2 yourself
    // (Enter, Ctrl+V, Enter). Not a single key is sent to the game.
    bool copyOnly = false;
    SendOptions send;

    // [Tabs] — like the tabs of the GW2 chat panel
    std::vector<ChatTab> tabs;  // ids 1..n
    int activeTab = 0;

    // [Window] — position in physical pixels (kAutoPos = place automatically),
    // size in 96-dpi pixels
    static constexpr int kAutoPos = -100000;
    int x = kAutoPos, y = kAutoPos, w = 520, h = 460;
    int opacity = 238;
    int fontPercent = 100;
    bool followGame = true;
    bool focusOnGameChat = false;  // focus input when in-game chat box gains focus
    // Docked: position kept relative to the bottom-left corner of the GW2
    // client area (physical pixels), follows the game window.
    bool dock = false;
    bool dockSet = false;
    int dockLeft = 0, dockFromBottom = 0, dockWidth = 0, dockHeight = 0;

    void Load(const std::wstring& dir);
    void SaveWindowRect(HWND wnd, float scale) const;
    void SaveRegion() const;
    void SaveColor(Channel ch, Rgb rgb);
    void ResetColors();
    void SaveTabs() const;
    void SaveDock() const;
    // Writes every setting that the settings dialog can change.
    void SaveAll() const;
    void SaveValue(const wchar_t* section, const wchar_t* key, const std::wstring& value) const;
    void SaveBool(const wchar_t* section, const wchar_t* key, bool v) const { SaveValue(section, key, v ? L"1" : L"0"); }

    std::wstring UserWordsPath() const;  // your own GW2 words (never marked as errors)
    std::wstring LearnedDir() const { return dataDir + L"\\learned"; }
    std::wstring CacheDir() const { return dataDir + L"\\cache"; }
    std::wstring CaptureDir() const { return dataDir + L"\\captures"; }
};

const wchar_t* EngineKey(Engine e);        // "auto", "basic", "deepl", "llm"
const wchar_t* OcrKey(OcrChoice o);        // "auto", "tesseract", "windows"
const wchar_t* AutoCorrectKey(AutoCorrectMode m);  // "off", "safe", "phone"

}  // namespace gct
