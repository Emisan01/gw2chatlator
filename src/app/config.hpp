// config.hpp — settings from gw2-chat-translator.ini (created with defaults
// and comments on first start; missing keys fall back to defaults).
#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "core/chat_line.hpp"
#include "core/chat_tabs.hpp"
#include "win/gw2_sender.hpp"

namespace gct {

enum class Engine { Auto, Basic, DeepL, Llm };

struct Config {
    std::wstring dataDir;
    std::wstring iniPath;

    // [Translate]
    Engine engine = Engine::Auto;
    std::wstring readLang;                    // empty = Windows language
    std::vector<std::wstring> writeLangs{L"EN-GB", L"FR", L"ES", L"DE"};
    bool backTranslate = true;
    int debounceMs = 500;

    // [Basic] MyMemory
    std::wstring basicEmail;
    // [DeepL]
    std::wstring deeplKey;
    // [LLM]
    std::wstring llmUrl, llmModel, llmKey;
    int llmTimeoutSec = 60;

    // [Reader]
    bool readerEnabled = true;
    int readerIntervalMs = 900;
    std::wstring ocrLanguage;  // empty = Windows languages
    int ocrScale = 2;
    bool showSystemLines = false;
    bool saveCaptures = false;
    bool regionSet = false;
    int regionLeft = 0, regionFromBottom = 0, regionWidth = 0, regionHeight = 0;  // relative to GW2 client area
    std::vector<ChannelColor> palette;

    // [Spelling]
    bool spellEnabled = true;
    bool autoCorrect = true;

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
    bool followGame = true;
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
    void SaveValue(const wchar_t* section, const wchar_t* key, const std::wstring& value) const;

    std::wstring UserWordsPath() const { return dataDir + L"\\gw2-woerter.txt"; }
    std::wstring CacheDir() const { return dataDir + L"\\cache"; }
    std::wstring CaptureDir() const { return dataDir + L"\\captures"; }
};

const wchar_t* EngineKey(Engine e);  // "auto", "basic", "deepl", "llm"

}  // namespace gct
