// config.cpp
#include "config.hpp"

#include "win/secret.hpp"

#include <algorithm>
#include <cwchar>
#include <iterator>

#include "core/text.hpp"
#include "win/files.hpp"

namespace gct {
namespace {

// ASCII only, so GetPrivateProfileString reads it the same in every locale.
const char kHeader[] =
    "; GW2 Chat Translator - settings\r\n"
    "; Everything here can also be changed in the window: menu (=) -> Settings.\r\n"
    "; Changes made in this file apply on the next start.\r\n"
    "\r\n"
    "[General]\r\n"
    "; Language of the window: en (English), de (Deutsch), ar (Arabic)\r\n"
    "UiLanguage=en\r\n"
    "; Guild Wars 2 folder (found automatically; used for the install and the add-on check)\r\n"
    "Gw2Dir=\r\n"
    "\r\n";

const char kTranslateSections[] =
    "[Translate]\r\n"
    "; Translator: auto | basic | deepl | google | microsoft | libre | llm\r\n"
    ";   auto  = DeepL, Google, Microsoft or your server if set, else the LLM if a model is set, else basic\r\n"
    ";   basic = MyMemory: free, no account, limited daily quota\r\n"
    "Engine=auto\r\n"
    "; Your reading language: the GW2 chat is translated into it. Empty = Windows language.\r\n"
    "; Codes: DE, EN-GB, EN-US, FR, ES, IT, PT-BR, NL, PL, TR, RU, UK, AR, HE, ZH-HANS, ZH-HANT, JA, KO, LA ...\r\n"
    "ReadLang=\r\n"
    "; Languages for \"Send as\" (Ctrl+L cycles; \"Original\" always comes last).\r\n"
    "WriteLangs=EN-GB,FR,ES,DE\r\n"
    "; Translate your message back into your language as a check (1/0)\r\n"
    "BackTranslate=1\r\n"
    "; Channels translated without a click: default = all (others: click a line)\r\n"
    "AutoChannels=default\r\n"
    "; Languages NOT to translate (your reading language never is), e.g. EN,DE. A click still translates.\r\n"
    "Understood=\r\n"
    "; Wait after typing before translating (ms)\r\n"
    "DebounceMs=500\r\n"
    "\r\n"
    "[Basic]\r\n"
    "; Optional: your e-mail address raises the free MyMemory quota (5,000 -> 50,000 characters/day).\r\n"
    "Email=\r\n"
    "\r\n"
    "[LLM]\r\n"
    "; Optional: any OpenAI-compatible language model.\r\n"
    "; Local and free: Ollama (http://localhost:11434) or LM Studio (http://localhost:1234)\r\n"
    "; Cloud: the provider's address + ApiKey. Leave the model empty = no LLM.\r\n"
    "Url=http://localhost:11434\r\n"
    "; e.g. qwen2.5:7b, llama3.1:8b, gemma2:9b\r\n"
    "Model=\r\n"
    "ApiKey=\r\n"
    "TimeoutSec=60\r\n"
    "; Repair text-recognition errors in chat lines while translating (1/0)\r\n"
    "FixOcr=1\r\n"
    "\r\n"
    "[Reader]\r\n"
    "; Read the GW2 chat from the screen and translate it permanently\r\n"
    "Enabled=1\r\n"
    "; How often the chat is read (ms)\r\n"
    "IntervalMs=400\r\n"
    "; Text recognition: auto (Windows; Tesseract for very small text) | tesseract | windows\r\n"
    "OcrEngine=auto\r\n"
    "; Picture: auto (window on Windows 11, screen on Windows 10: no yellow frame) | window | screen\r\n"
    "Capture=auto\r\n"
    "; Tesseract: folder or tesseract.exe (empty = search Program Files, PATH, .\\tesseract)\r\n"
    "TesseractPath=\r\n"
    "; Tesseract languages, e.g. eng+deu (empty = English, German, French, Spanish where installed)\r\n"
    "TesseractLang=\r\n"
    "; Also read Simplified Chinese (needs chi_sim, a bit slower)\r\n"
    "ReadChinese=0\r\n"
    "; Windows text recognition language, e.g. de-DE, en-US. Empty = Windows languages.\r\n"
    "OcrLanguage=\r\n"
    "; Enlargement before text recognition: 0 = automatic from the measured line spacing, 1-4 = fixed\r\n"
    "OcrZoom=0\r\n"
    "; Show system lines (without a speaker)\r\n"
    "ShowSystem=0\r\n"
    "; Diagnostics: save pictures and recognized text in the captures folder\r\n"
    "; (switches itself off after 15 minutes; old files are cleaned up)\r\n"
    "SaveCaptures=0\r\n"
    "; Chat area relative to the bottom-left corner of the GW2 window.\r\n"
    "; Set in the window with \"Set chat area\".\r\n"
    "RegionLeft=\r\n"
    "RegionFromBottom=\r\n"
    "RegionWidth=\r\n"
    "RegionHeight=\r\n"
    "; Free screen area instead of the chat: everything inside is translated (FreeArea=1).\r\n"
    "; FreeRect=left,top,width,height in screen pixels.\r\n"
    "FreeArea=0\r\n"
    "FreeRect=\r\n"
    "; Channel colours (RRGGBB). Right-click a chat line -> \"This line colour is\" calibrates them.\r\n"
    "; ColorSay= ColorMap= ColorParty= ColorSquad= ColorTeam= ColorWhisper= ColorGuild= ColorSystem=\r\n"
    "\r\n";

const char kRestSections[] =
    "[DeepL]\r\n"
    "; Optional, best quality: https://www.deepl.com/pro-api  (free keys end in :fx)\r\n"
    "ApiKey=\r\n"
    "\r\n"
    "[Google]\r\n"
    "; Optional: Google Cloud Translation, 500,000 characters a month free (Engine=google)\r\n"
    "ApiKey=\r\n"
    "\r\n"
    "[Microsoft]\r\n"
    "; Optional: Microsoft Translator, free tier F0 (Engine=microsoft). Region e.g. westeurope\r\n"
    "ApiKey=\r\n"
    "Region=\r\n"
    "\r\n"
    "[Libre]\r\n"
    "; Optional: own LibreTranslate-compatible server, e.g. http://localhost:5000 (Engine=libre)\r\n"
    "Url=\r\n"
    "ApiKey=\r\n"
    "\r\n"
    "[Spelling]\r\n"
    "; Windows spell checking (offline). It follows your keyboard layout.\r\n"
    "Enabled=1\r\n"
    "; Autocorrection while typing: off | safe (only Windows' sure fixes) | phone (like a phone keyboard)\r\n"
    "AutoCorrectMode=phone\r\n"
    "; Word bar above the input: completions and the next word (Tab takes the highlighted one)\r\n"
    "Suggestions=1\r\n"
    "; Learn the words you send (saved per language in the learned folder)\r\n"
    "Learn=1\r\n"
    "; Optional grammar check with LanguageTool (public server: max. 20 checks per minute)\r\n"
    "LanguageTool=0\r\n"
    "LanguageToolUrl=https://api.languagetool.org\r\n"
    "\r\n"
    "[Glossary]\r\n"
    "; Official GW2 names (maps, professions, elite specializations, WvW objectives, mounts)\r\n"
    "; from the GW2 API: Loewenstein becomes Lion's Arch instead of a literal translation.\r\n"
    "Enabled=1\r\n"
    "RefreshDays=14\r\n"
    "\r\n"
    "[Hotkey]\r\n"
    "; Show / hide the window. Examples: Ctrl+Alt+T, Ctrl+Shift+F9, F10\r\n"
    "Toggle=Ctrl+Alt+T\r\n"
    "\r\n"
    "[Chat]\r\n"
    "; How your message gets into the chat:\r\n"
    ";   send = Enter in the window brings GW2 to the front and sends the line (Enter, paste, Enter)\r\n"
    ";   copy = Enter only copies; you paste it in GW2 yourself. Not a single key goes to the game.\r\n"
    "SendMode=send\r\n"
    "; GW2 allows 199 characters per chat line; longer messages are split\r\n"
    "MaxLength=199\r\n"
    "; 1 = back to the translator window after sending, 0 = stay in the game\r\n"
    "ReturnFocus=0\r\n"
    "; Pause between the key presses to GW2 (ms). Raise it at low FPS.\r\n"
    "StepDelayMs=80\r\n"
    "; How long each key is held (ms): GW2 reads the keyboard once per frame. Raise it at low FPS.\r\n"
    "KeyHoldMs=30\r\n"
    "RestoreDelayMs=250\r\n"
    "\r\n"
    "[Window]\r\n"
    "; Position empty = automatic. Saved when you move the window.\r\n"
    "X=\r\n"
    "Y=\r\n"
    "Width=520\r\n"
    "Height=460\r\n"
    "; 60 (very transparent) to 255 (not transparent at all)\r\n"
    "Opacity=255\r\n"
    "; Text size in percent (90, 100, 115, 135)\r\n"
    "FontPercent=100\r\n"
    "; Show and hide together with the game (1/0)\r\n"
    "FollowGame=1\r\n"
    "; Dock to GW2: the window moves with the game window\r\n"
    "Dock=0\r\n"
    "\r\n"
    "[Tabs]\r\n"
    "; Tabs like in the GW2 chat; easiest to change by right-clicking a tab.\r\n"
    "; TabN=Name|channels  (say, map, party, squad, team, guild, whisper, system, other)\r\n"
    "Tab1=Chat|say,map,party,squad,team,guild,whisper,system,other\r\n"
        "Active=1\r\n";

struct ColorKey {
    Channel channel;
    const wchar_t* key;
};
constexpr ColorKey kColorKeys[] = {
    {Channel::Say, L"ColorSay"},     {Channel::Map, L"ColorMap"},         {Channel::Party, L"ColorParty"},
    {Channel::Squad, L"ColorSquad"}, {Channel::Team, L"ColorTeam"},       {Channel::Whisper, L"ColorWhisper"},
    {Channel::Guild, L"ColorGuild"},  {Channel::System, L"ColorSystem"},
};

class Ini {
public:
    explicit Ini(const std::wstring& path) : path_(path) {}

    std::wstring Str(const wchar_t* sec, const wchar_t* key, const wchar_t* def) const {
        wchar_t buf[1024];
        GetPrivateProfileStringW(sec, key, def, buf, static_cast<DWORD>(std::size(buf)), path_.c_str());
        return Trim(buf);
    }

    bool Has(const wchar_t* sec, const wchar_t* key) const {
        wchar_t buf[8];
        return GetPrivateProfileStringW(sec, key, L"\x01", buf, static_cast<DWORD>(std::size(buf)), path_.c_str()) !=
                   1 ||
               buf[0] != L'\x01';
    }

    bool HasSection(const wchar_t* sec) const {
        wchar_t buf[64];
        return GetPrivateProfileSectionW(sec, buf, static_cast<DWORD>(std::size(buf)), path_.c_str()) > 0;
    }

    // GetPrivateProfileInt turns negative numbers into 0, which would break
    // window positions on monitors left of / above the primary one.
    int Int(const wchar_t* sec, const wchar_t* key, int def, int lo, int hi) const {
        const std::wstring s = Str(sec, key, L"");
        long v = def;
        if (!s.empty()) {
            wchar_t* end = nullptr;
            const long parsed = std::wcstol(s.c_str(), &end, 10);
            if (end != s.c_str() && *end == 0) v = parsed;
        }
        return std::clamp(static_cast<int>(v), lo, hi);
    }

    bool Bool(const wchar_t* sec, const wchar_t* key, bool def) const { return Int(sec, key, def ? 1 : 0, 0, 1) != 0; }

private:
    std::wstring path_;
};

OcrChoice ParseOcr(const std::wstring& s) {
    const std::wstring v = ToLowerAscii(s);
    if (v == L"tesseract") return OcrChoice::Tesseract;
    if (v == L"windows") return OcrChoice::Windows;
    if (v == L"rapid" || v == L"rapidocr") return OcrChoice::Rapid;
    if (v == L"hybrid") return OcrChoice::Hybrid;
    return OcrChoice::Auto;
}

AutoCorrectMode ParseAutoCorrect(const std::wstring& s, AutoCorrectMode def) {
    const std::wstring v = ToLowerAscii(s);
    if (v == L"off" || v == L"0") return AutoCorrectMode::Off;
    if (v == L"safe") return AutoCorrectMode::Safe;
    if (v == L"phone") return AutoCorrectMode::Phone;
    return def;
}

Engine ParseEngine(const std::wstring& s) {
    const std::wstring v = ToLowerAscii(s);
    if (v == L"basic" || v == L"basis" || v == L"mymemory") return Engine::Basic;
    if (v == L"deepl") return Engine::DeepL;
    if (v == L"llm") return Engine::Llm;
    if (v == L"google") return Engine::Google;
    if (v == L"microsoft") return Engine::Microsoft;
    if (v == L"libre" || v == L"libretranslate") return Engine::Libre;
    return Engine::Auto;
}

}  // namespace

const wchar_t* OcrKey(OcrChoice o) {
    switch (o) {
        case OcrChoice::Tesseract: return L"tesseract";
        case OcrChoice::Windows: return L"windows";
        case OcrChoice::Rapid: return L"rapid";
        case OcrChoice::Hybrid: return L"hybrid";
        default: return L"auto";
    }
}

const wchar_t* AutoCorrectKey(AutoCorrectMode m) {
    switch (m) {
        case AutoCorrectMode::Off: return L"off";
        case AutoCorrectMode::Safe: return L"safe";
        default: return L"phone";
    }
}

std::wstring Config::UserWordsPath() const {
    // v0.4 called it gw2-woerter.txt; keep using an existing one.
    const std::wstring old = dataDir + L"\\gw2-woerter.txt";
    if (GetFileAttributesW(old.c_str()) != INVALID_FILE_ATTRIBUTES) return old;
    return dataDir + L"\\my-gw2-words.txt";
}

const wchar_t* EngineKey(Engine e) {
    switch (e) {
        case Engine::Basic: return L"basic";
        case Engine::DeepL: return L"deepl";
        case Engine::Llm: return L"llm";
        case Engine::Google: return L"google";
        case Engine::Microsoft: return L"microsoft";
        case Engine::Libre: return L"libre";
        default: return L"auto";
    }
}

UiLang EffectiveUiLang(const std::wstring& code) {
    if (!Trim(code).empty()) return UiLangFromCode(code, UiLang::En);
    const LANGID winUi = PRIMARYLANGID(GetUserDefaultUILanguage());
    return winUi == LANG_GERMAN ? UiLang::De : winUi == LANG_ARABIC ? UiLang::Ar : UiLang::En;
}

void Config::Load(const std::wstring& dir) {
    dataDir = dir;
    iniPath = dir + L"\\gw2-chat-translator.ini";
    if (GetFileAttributesW(iniPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        WriteFileAtomic(iniPath, std::string(kHeader) + kTranslateSections + kRestSections);
    } else if (!Ini(iniPath).HasSection(L"Reader")) {
        // INI from v0.2: append the new sections so the options are visible.
        AppendFileBytes(iniPath, std::string("\r\n; --- new in v0.3 ---\r\n") + kTranslateSections);
    }

    const Ini ini(iniPath);
    uiLangCode = ini.Str(L"General", L"UiLanguage", L"");
    uiLang = EffectiveUiLang(uiLangCode);
    setupDone = ini.Bool(L"General", L"SetupDone", false);
    startMenu = ini.Bool(L"General", L"StartMenu", true);
    myMemoryNoticeShown = ini.Bool(L"Basic", L"NoticeShown", false);
    myMemoryDay = ini.Str(L"Basic", L"UsedDay", L"");
    myMemoryUsed = ini.Int(L"Basic", L"UsedChars", 0, 0, 10000000);
    gw2Dir = AsciiUnescape(ini.Str(L"General", L"Gw2Dir", L""));
    engine = ParseEngine(ini.Str(L"Translate", L"Engine", L"auto"));
    // v0.2 had SourceLang/TargetLangs under [DeepL]; used as fallbacks.
    readLang = ToUpperAscii(ini.Has(L"Translate", L"ReadLang") ? ini.Str(L"Translate", L"ReadLang", L"")
                                                                : ini.Str(L"DeepL", L"SourceLang", L""));
    std::wstring langs = ini.Str(L"Translate", L"WriteLangs", L"");
    if (langs.empty()) langs = ini.Str(L"DeepL", L"TargetLangs", L"");
    if (auto t = ParseLangList(langs); !t.empty()) writeLangs = t;
    backTranslate = ini.Bool(L"Translate", L"BackTranslate", true);
    {
        const std::wstring autoList = ini.Str(L"Translate", L"AutoChannels", L"default");
        autoTranslate = autoList == L"default" ? DefaultAutoTranslate() : ParseChannels(autoList);
        understoodLangs = ParseLangList(ini.Str(L"Translate", L"Understood", L""));
    }
    chatLang = ToUpperAscii(ini.Str(L"Translate", L"ChatLang", L"EN-GB"));
    debounceMs = ini.Int(L"Translate", L"DebounceMs", ini.Int(L"DeepL", L"DebounceMs", 500, 150, 5000), 150, 5000);

    basicEmail = AsciiUnescape(ini.Str(L"Basic", L"Email", L""));
    deeplKey = UnprotectSecret(ini.Str(L"DeepL", L"ApiKey", L""));
    googleKey = UnprotectSecret(ini.Str(L"Google", L"ApiKey", L""));
    msKey = UnprotectSecret(ini.Str(L"Microsoft", L"ApiKey", L""));
    msRegion = AsciiUnescape(ini.Str(L"Microsoft", L"Region", L""));
    libreUrl = AsciiUnescape(ini.Str(L"Libre", L"Url", L""));
    libreKey = UnprotectSecret(ini.Str(L"Libre", L"ApiKey", L""));
    llmUrl = AsciiUnescape(ini.Str(L"LLM", L"Url", L"http://localhost:11434"));
    llmModel = AsciiUnescape(ini.Str(L"LLM", L"Model", L""));
    llmKey = UnprotectSecret(ini.Str(L"LLM", L"ApiKey", L""));
    llmTimeoutSec = ini.Int(L"LLM", L"TimeoutSec", 60, 5, 600);
    llmFixOcr = ini.Bool(L"LLM", L"FixOcr", true);

    readerEnabled = ini.Bool(L"Reader", L"Enabled", true);
    readerIntervalMs = ini.Int(L"Reader", L"IntervalMs", 400, 150, 10000);
    secondLook = ini.Bool(L"Reader", L"SecondLook", true);
    writeIn = ToUpperAscii(ini.Str(L"Spelling", L"WriteIn", L""));
    ocr = ParseOcr(ini.Str(L"Reader", L"OcrEngine", L"auto"));
    {
        const std::wstring c = ToLowerAscii(ini.Str(L"Reader", L"Capture", L"auto"));
        captureMode = c == L"window" ? 1 : c == L"screen" ? 2 : 0;
    }
    tesseractPath = AsciiUnescape(ini.Str(L"Reader", L"TesseractPath", L""));
    tesseractLangs = ToUtf8(ini.Str(L"Reader", L"TesseractLang", L""));
    readChinese = ini.Bool(L"Reader", L"ReadChinese", false);
    ocrLanguage = ini.Str(L"Reader", L"OcrLanguage", L"");
    // "OcrZoom" replaced "OcrScale" (fixed 2 by default) when the enlargement became automatic.
    ocrScale = ini.Int(L"Reader", L"OcrZoom", 0, 0, 4);
    showSystemLines = ini.Bool(L"Reader", L"ShowSystem", false);
    onlyTranslations = ini.Bool(L"Reader", L"OnlyTranslations", true);
    saveCaptures = ini.Bool(L"Reader", L"SaveCaptures", false);
    regionLeft = ini.Int(L"Reader", L"RegionLeft", 0, -20000, 20000);
    regionFromBottom = ini.Int(L"Reader", L"RegionFromBottom", 0, -20000, 20000);
    regionWidth = ini.Int(L"Reader", L"RegionWidth", 0, 0, 20000);
    regionHeight = ini.Int(L"Reader", L"RegionHeight", 0, 0, 20000);
    regionSet = regionWidth >= 40 && regionHeight >= 20;
    freeArea = ini.Bool(L"Reader", L"FreeArea", false);
    {
        int v[4] = {0, 0, 0, 0};
        const std::wstring r = ini.Str(L"Reader", L"FreeRect", L"");
        if (swscanf_s(r.c_str(), L"%d,%d,%d,%d", &v[0], &v[1], &v[2], &v[3]) == 4 && v[2] > 0 && v[3] > 0)
            freeRect = {v[0], v[1], v[0] + v[2], v[1] + v[3]};
    }
    if (!FreeSet()) freeArea = false;

    palette = DefaultChannelColors();
    for (const ColorKey& ck : kColorKeys) {
        Rgb rgb;
        if (!RgbFromHex(ini.Str(L"Reader", ck.key, L""), rgb)) continue;
        for (ChannelColor& c : palette)
            if (c.channel == ck.channel) c.rgb = rgb;
    }

    spellEnabled = ini.Bool(L"Spelling", L"Enabled", true);
    // v0.4 had AutoCorrect=1/0 (Windows' sure fixes only).
    autoCorrect = ParseAutoCorrect(ini.Str(L"Spelling", L"AutoCorrectMode", L""),
                                   ini.Bool(L"Spelling", L"AutoCorrect", true) ? AutoCorrectMode::Phone
                                                                               : AutoCorrectMode::Off);
    suggestions = ini.Bool(L"Spelling", L"Suggestions", true);
    learnWords = ini.Bool(L"Spelling", L"Learn", true);
    languageTool = ini.Bool(L"Spelling", L"LanguageTool", false);
    languageToolUrl = ini.Str(L"Spelling", L"LanguageToolUrl", L"https://api.languagetool.org");

    glossaryEnabled = ini.Bool(L"Glossary", L"Enabled", true);
    glossaryRefreshDays = ini.Int(L"Glossary", L"RefreshDays", 14, 1, 365);

    // Three keys by default: fewer clashes with other programs and game add-ons. Empty = no hotkey.
    hotkey = ini.Str(L"Hotkey", L"Toggle", L"Ctrl+Alt+Shift+T");

    maxLength = ini.Int(L"Chat", L"MaxLength", 199, 20, 2000);
    returnFocus = ini.Bool(L"Chat", L"ReturnFocus", false);
    copyOnly = ToLowerAscii(ini.Str(L"Chat", L"SendMode", L"send")) == L"copy";
    send.stepDelayMs = ini.Int(L"Chat", L"StepDelayMs", 80, 20, 1000);
    send.keyHoldMs = ini.Int(L"Chat", L"KeyHoldMs", 30, 5, 500);
    send.restoreDelayMs = ini.Int(L"Chat", L"RestoreDelayMs", 250, 50, 3000);

    x = ini.Int(L"Window", L"X", kAutoPos, kAutoPos, 32000);
    y = ini.Int(L"Window", L"Y", kAutoPos, kAutoPos, 32000);
    w = ini.Int(L"Window", L"Width", 520, 380, 4000);
    h = ini.Int(L"Window", L"Height", 460, 300, 4000);
    opacity = ini.Int(L"Window", L"Opacity", 255, 60, 255);
    fontPercent = ini.Int(L"Window", L"FontPercent", 100, 70, 300);
    fontFace = ini.Str(L"Window", L"Font", L"Segoe UI");
    followGame = ini.Bool(L"Window", L"FollowGame", true);
    focusOnGameChat = ini.Bool(L"Window", L"FocusOnGameChat", false);
    typingDay = ini.Int(L"Typing", L"Day", 0, 0, 99991231);
    dayKeys = ini.Int(L"Typing", L"DayKeys", 0, 0, 2000000000);
    dayChars = ini.Int(L"Typing", L"DayChars", 0, 0, 2000000000);
    totalKeys = ini.Int(L"Typing", L"TotalKeys", 0, 0, 2000000000);
    totalChars = ini.Int(L"Typing", L"TotalChars", 0, 0, 2000000000);
    totalMessages = ini.Int(L"Typing", L"TotalMessages", 0, 0, 2000000000);
    dock = ini.Bool(L"Window", L"Dock", false);
    dockLeft = ini.Int(L"Window", L"DockLeft", 0, -20000, 20000);
    dockFromBottom = ini.Int(L"Window", L"DockFromBottom", 0, -20000, 20000);
    dockWidth = ini.Int(L"Window", L"DockWidth", 0, 0, 20000);
    dockHeight = ini.Int(L"Window", L"DockHeight", 0, 0, 20000);
    dockSet = dockWidth >= 100 && dockHeight >= 100;

    tabs.clear();
    for (int i = 1; i <= 12; ++i) {
        ChatTab t;
        if (ParseTab(AsciiUnescape(ini.Str(L"Tabs", (L"Tab" + std::to_wstring(i)).c_str(), L"")), t)) tabs.push_back(t);
    }
    if (tabs.empty()) tabs = DefaultTabs();
    // The old default "Whisper" tab (everything + whispers only) is gone: one tab, colours tell the channel.
    // A player's own whisper tab (opened by clicking a name) stays.
    if (tabs.size() == 2 && tabs[0].channels == AllChannels() && tabs[1].channels == ChannelBit(Channel::Whisper) &&
        tabs[1].person.empty())
        tabs.pop_back();
    for (size_t i = 0; i < tabs.size(); ++i) tabs[i].id = static_cast<uint32_t>(i + 1);
    activeTab = ini.Int(L"Tabs", L"Active", 1, 1, static_cast<int>(tabs.size())) - 1;
}

void Config::SaveValue(const wchar_t* section, const wchar_t* key, const std::wstring& value) const {
    WritePrivateProfileStringW(section, key, value.c_str(), iniPath.c_str());
}

void Config::SaveWindowRect(HWND wnd, float scale) const {
    RECT rc;
    if (!wnd || !GetWindowRect(wnd, &rc)) return;
    SaveValue(L"Window", L"X", std::to_wstring(rc.left));
    SaveValue(L"Window", L"Y", std::to_wstring(rc.top));
    SaveValue(L"Window", L"Width", std::to_wstring(static_cast<int>((rc.right - rc.left) / scale + 0.5f)));
    SaveValue(L"Window", L"Height", std::to_wstring(static_cast<int>((rc.bottom - rc.top) / scale + 0.5f)));
}

void Config::SaveFreeArea() const {
    SaveBool(L"Reader", L"FreeArea", freeArea);
    SaveValue(L"Reader", L"FreeRect",
              std::to_wstring(freeRect.left) + L"," + std::to_wstring(freeRect.top) + L"," +
                  std::to_wstring(freeRect.right - freeRect.left) + L"," + std::to_wstring(freeRect.bottom - freeRect.top));
}

void Config::SaveRegion() const {
    SaveValue(L"Reader", L"RegionLeft", std::to_wstring(regionLeft));
    SaveValue(L"Reader", L"RegionFromBottom", std::to_wstring(regionFromBottom));
    SaveValue(L"Reader", L"RegionWidth", std::to_wstring(regionWidth));
    SaveValue(L"Reader", L"RegionHeight", std::to_wstring(regionHeight));
}

void Config::SaveColor(Channel ch, Rgb rgb) {
    for (ChannelColor& c : palette)
        if (c.channel == ch) c.rgb = rgb;
    for (const ColorKey& ck : kColorKeys)
        if (ck.channel == ch) SaveValue(L"Reader", ck.key, RgbToHex(rgb));
}

void Config::SaveTabs() const {
    // Rewrite the whole section so removed tabs disappear.
    std::wstring data = L"Active=" + std::to_wstring(activeTab + 1) + L'\0';
    for (size_t i = 0; i < tabs.size(); ++i)
        data += L"Tab" + std::to_wstring(i + 1) + L"=" + AsciiEscape(SerializeTab(tabs[i])) + L'\0';
    data += L'\0';
    WritePrivateProfileSectionW(L"Tabs", data.c_str(), iniPath.c_str());
}

void Config::SaveDock() const {
    SaveValue(L"Window", L"Dock", dock ? L"1" : L"0");
    SaveValue(L"Window", L"DockLeft", std::to_wstring(dockLeft));
    SaveValue(L"Window", L"DockFromBottom", std::to_wstring(dockFromBottom));
    SaveValue(L"Window", L"DockWidth", std::to_wstring(dockWidth));
    SaveValue(L"Window", L"DockHeight", std::to_wstring(dockHeight));
}

void Config::SaveAll() const {
    SaveValue(L"General", L"UiLanguage", uiLangCode);
    SaveBool(L"General", L"SetupDone", setupDone);
    SaveValue(L"General", L"Gw2Dir", AsciiEscape(gw2Dir));
    SaveValue(L"Translate", L"Engine", EngineKey(engine));
    SaveValue(L"Translate", L"ReadLang", readLang);
    std::wstring joined;
    for (const std::wstring& c : writeLangs) joined += (joined.empty() ? L"" : L",") + c;
    SaveValue(L"Translate", L"WriteLangs", joined);
    SaveBool(L"Translate", L"BackTranslate", backTranslate);
    SaveValue(L"Translate", L"AutoChannels", SerializeChannels(autoTranslate));
    {
        std::wstring list;
        for (const std::wstring& l : understoodLangs) list += (list.empty() ? L"" : L",") + l;
        SaveValue(L"Translate", L"Understood", list);
    }
    SaveValue(L"Basic", L"Email", AsciiEscape(basicEmail));
    SaveValue(L"DeepL", L"ApiKey", ProtectSecret(deeplKey));
    SaveValue(L"Google", L"ApiKey", ProtectSecret(googleKey));
    SaveValue(L"Microsoft", L"ApiKey", ProtectSecret(msKey));
    SaveValue(L"Microsoft", L"Region", AsciiEscape(msRegion));
    SaveValue(L"Libre", L"Url", AsciiEscape(libreUrl));
    SaveValue(L"Libre", L"ApiKey", ProtectSecret(libreKey));
    SaveValue(L"LLM", L"Url", AsciiEscape(llmUrl));
    SaveValue(L"LLM", L"Model", AsciiEscape(llmModel));
    SaveValue(L"LLM", L"ApiKey", ProtectSecret(llmKey));
    SaveBool(L"LLM", L"FixOcr", llmFixOcr);
    SaveBool(L"Reader", L"Enabled", readerEnabled);
    SaveValue(L"Reader", L"IntervalMs", std::to_wstring(readerIntervalMs));
    SaveValue(L"Reader", L"OcrEngine", OcrKey(ocr));
    SaveValue(L"Reader", L"Capture", captureMode == 1 ? L"window" : captureMode == 2 ? L"screen" : L"auto");
    SaveValue(L"Reader", L"OcrZoom", std::to_wstring(ocrScale));
    SaveBool(L"Reader", L"SecondLook", secondLook);
    SaveValue(L"Spelling", L"WriteIn", writeIn);
    SaveBool(L"General", L"StartMenu", startMenu);
    SaveValue(L"Chat", L"KeyHoldMs", std::to_wstring(send.keyHoldMs));
    SaveValue(L"Chat", L"StepDelayMs", std::to_wstring(send.stepDelayMs));
    SaveValue(L"Reader", L"TesseractPath", AsciiEscape(tesseractPath));
    SaveValue(L"Reader", L"TesseractLang", FromUtf8(tesseractLangs));
    SaveBool(L"Reader", L"ReadChinese", readChinese);
    SaveBool(L"Reader", L"ShowSystem", showSystemLines);
    SaveBool(L"Reader", L"OnlyTranslations", onlyTranslations);
    SaveBool(L"Reader", L"SaveCaptures", saveCaptures);
    SaveBool(L"Spelling", L"Enabled", spellEnabled);
    SaveValue(L"Spelling", L"AutoCorrectMode", AutoCorrectKey(autoCorrect));
    SaveBool(L"Spelling", L"Suggestions", suggestions);
    SaveBool(L"Spelling", L"Learn", learnWords);
    SaveBool(L"Spelling", L"LanguageTool", languageTool);
    SaveValue(L"Spelling", L"LanguageToolUrl", AsciiEscape(languageToolUrl));
    SaveValue(L"Hotkey", L"Toggle", hotkey);
    SaveValue(L"Chat", L"SendMode", copyOnly ? L"copy" : L"send");
    SaveBool(L"Chat", L"ReturnFocus", returnFocus);
    SaveValue(L"Window", L"Opacity", std::to_wstring(opacity));
    SaveValue(L"Window", L"FontPercent", std::to_wstring(fontPercent));
    SaveValue(L"Window", L"Font", fontFace);
    SaveBool(L"Window", L"FollowGame", followGame);
    SaveBool(L"Window", L"FocusOnGameChat", focusOnGameChat);
    SaveBool(L"Window", L"Dock", dock);
}

void Config::ResetColors() {
    palette = DefaultChannelColors();
    for (const ColorKey& ck : kColorKeys) WritePrivateProfileStringW(L"Reader", ck.key, nullptr, iniPath.c_str());
}

}  // namespace gct
