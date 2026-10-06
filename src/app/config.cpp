// config.cpp
#include "config.hpp"

#include <algorithm>
#include <cwchar>
#include <iterator>

#include "core/text.hpp"
#include "win/files.hpp"

namespace gct {
namespace {

// ASCII only, so GetPrivateProfileString reads it the same in every locale.
const char kHeader[] =
    "; GW2 Chat Translator - Einstellungen\r\n"
    "; Die meisten Punkte lassen sich auch im Fenster ueber das Menue (=) aendern.\r\n"
    "; Aenderungen hier gelten ab dem naechsten Start.\r\n"
    "\r\n";

const char kTranslateSections[] =
    "[Translate]\r\n"
    "; Uebersetzer: auto | basic | deepl | llm\r\n"
    ";   auto  = DeepL, wenn ein Key eingetragen ist, sonst LLM, wenn ein Modell eingetragen ist, sonst Basis\r\n"
    ";   basic = MyMemory: kostenlos, ohne Anmeldung, begrenztes Tageskontingent\r\n"
    "Engine=auto\r\n"
    "; Deine Lesesprache: in diese Sprache wird der GW2-Chat uebersetzt. Leer = Windows-Sprache.\r\n"
    "; Codes: DE, EN-GB, EN-US, FR, ES, IT, PT-BR, NL, PL, TR, RU, UK, AR, HE, ZH-HANS, ZH-HANT, JA, KO, LA ...\r\n"
    "ReadLang=\r\n"
    "; Sprachen fuer \"Senden als\" (Strg+L schaltet durch; am Ende kommt immer \"Original\").\r\n"
    "WriteLangs=EN-GB,FR,ES,DE\r\n"
    "; Deine Nachricht zur Kontrolle in deine Sprache zurueckuebersetzen (1/0)\r\n"
    "BackTranslate=1\r\n"
    "; Wartezeit nach dem Tippen, bevor uebersetzt wird (ms)\r\n"
    "DebounceMs=500\r\n"
    "\r\n"
    "[Basic]\r\n"
    "; Optional: deine E-Mail-Adresse erhoeht das freie MyMemory-Kontingent (5.000 -> 50.000 Zeichen/Tag).\r\n"
    "Email=\r\n"
    "\r\n"
    "[LLM]\r\n"
    "; Optional: jedes OpenAI-kompatible Sprachmodell.\r\n"
    "; Lokal und kostenlos: Ollama (http://localhost:11434) oder LM Studio (http://localhost:1234)\r\n"
    "; Cloud: Adresse des Anbieters + ApiKey. Leer lassen = kein LLM.\r\n"
    "Url=http://localhost:11434\r\n"
    "; z.B. qwen2.5:7b, llama3.1:8b, gemma2:9b - ohne Modell ist das LLM aus\r\n"
    "Model=\r\n"
    "ApiKey=\r\n"
    "TimeoutSec=60\r\n"
    "\r\n"
    "[Reader]\r\n"
    "; Den GW2-Chat mitlesen und dauerhaft uebersetzen (Texterkennung von Windows, offline)\r\n"
    "Enabled=1\r\n"
    "; Wie oft der Chat gelesen wird (ms)\r\n"
    "IntervalMs=900\r\n"
    "; Sprache der Texterkennung, z.B. de-DE, en-US. Leer = Windows-Sprachen.\r\n"
    "OcrLanguage=\r\n"
    "; Vergroesserung vor der Texterkennung (1-4). Kleine UI-Groesse in GW2: 3 probieren.\r\n"
    "OcrScale=2\r\n"
    "; Systemzeilen (ohne Sprecher) anzeigen\r\n"
    "ShowSystem=0\r\n"
    "; Diagnose: Aufnahmen und erkannten Text im Ordner captures speichern\r\n"
    "SaveCaptures=0\r\n"
    "; Chat-Bereich relativ zur linken unteren Ecke des GW2-Fensters.\r\n"
    "; Wird im Fenster mit \"Chat-Bereich festlegen\" gesetzt.\r\n"
    "RegionLeft=\r\n"
    "RegionFromBottom=\r\n"
    "RegionWidth=\r\n"
    "RegionHeight=\r\n"
    "; Kanalfarben (RRGGBB). Rechtsklick auf eine Chatzeile -> \"Farbe gehoert zu ...\" kalibriert sie.\r\n"
    "; ColorSay= ColorMap= ColorParty= ColorSquad= ColorTeam= ColorWhisper= ColorGuild=\r\n"
    "\r\n";

const char kRestSections[] =
    "[DeepL]\r\n"
    "; Optional, beste Qualitaet: https://www.deepl.com/pro-api  (Free-Keys enden auf :fx)\r\n"
    "ApiKey=\r\n"
    "\r\n"
    "[Spelling]\r\n"
    "; Rechtschreibpruefung von Windows (offline). Sie folgt deinem Tastaturlayout.\r\n"
    "Enabled=1\r\n"
    "; Sichere Tippfehler beim Tippen automatisch korrigieren (Strg+Z macht es rueckgaengig)\r\n"
    "AutoCorrect=1\r\n"
    "\r\n"
    "[Glossary]\r\n"
    "; Offizielle GW2-Namen (Karten, Klassen, Elite-Spezialisierungen, WvW-Ziele, Reittiere)\r\n"
    "; aus der GW2-API: Loewenstein wird zu Lion's Arch statt woertlich uebersetzt.\r\n"
    "Enabled=1\r\n"
    "RefreshDays=14\r\n"
    "\r\n"
    "[Hotkey]\r\n"
    "; Fenster holen bzw. ausblenden. Beispiele: Ctrl+Alt+T, Ctrl+Shift+F9, F10\r\n"
    "Toggle=Ctrl+Alt+T\r\n"
    "\r\n"
    "[Chat]\r\n"
    "; So kommt deine Nachricht in den Chat:\r\n"
    ";   send = Enter im Fenster holt GW2 nach vorn und sendet die Zeile (Enter, Einfuegen, Enter)\r\n"
    ";   copy = Enter kopiert nur; du fuegst sie in GW2 selbst ein. Keine einzige Taste geht ans Spiel.\r\n"
    "SendMode=send\r\n"
    "; GW2 erlaubt 199 Zeichen pro Chatzeile; laengere Nachrichten werden aufgeteilt\r\n"
    "MaxLength=199\r\n"
    "; 1 = nach dem Senden zurueck ins Uebersetzerfenster, 0 = im Spiel bleiben\r\n"
    "ReturnFocus=0\r\n"
    "; Pause zwischen den Tastendruecken an GW2 (ms). Bei niedriger FPS erhoehen.\r\n"
    "StepDelayMs=80\r\n"
    "RestoreDelayMs=250\r\n"
    "\r\n"
    "[Window]\r\n"
    "; Position leer = automatisch. Wird beim Verschieben gespeichert.\r\n"
    "X=\r\n"
    "Y=\r\n"
    "Width=520\r\n"
    "Height=460\r\n"
    "; 120 (sehr durchsichtig) bis 255 (deckend)\r\n"
    "Opacity=238\r\n"
    "; Mit dem Spiel ein- und ausblenden (1/0)\r\n"
    "FollowGame=1\r\n"
    "; An GW2 andocken: das Fenster wandert mit dem Spielfenster (Menue im Fenster)\r\n"
    "Dock=0\r\n"
    "\r\n"
    "[Tabs]\r\n"
    "; Tabs wie im GW2-Chat, am einfachsten per Rechtsklick auf einen Tab einstellen.\r\n"
    "; TabN=Name|Kanaele  (say, map, party, squad, team, guild, whisper, system, other)\r\n"
    "Tab1=Chat|say,map,party,squad,team,guild,whisper,system,other\r\n"
    "Tab2=Fl\\u00fcstern|whisper\r\n"
    "Active=1\r\n";

struct ColorKey {
    Channel channel;
    const wchar_t* key;
};
constexpr ColorKey kColorKeys[] = {
    {Channel::Say, L"ColorSay"},     {Channel::Map, L"ColorMap"},         {Channel::Party, L"ColorParty"},
    {Channel::Squad, L"ColorSquad"}, {Channel::Team, L"ColorTeam"},       {Channel::Whisper, L"ColorWhisper"},
    {Channel::Guild, L"ColorGuild"},
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

Engine ParseEngine(const std::wstring& s) {
    const std::wstring v = ToLowerAscii(s);
    if (v == L"basic" || v == L"basis" || v == L"mymemory") return Engine::Basic;
    if (v == L"deepl") return Engine::DeepL;
    if (v == L"llm") return Engine::Llm;
    return Engine::Auto;
}

}  // namespace

const wchar_t* EngineKey(Engine e) {
    switch (e) {
        case Engine::Basic: return L"basic";
        case Engine::DeepL: return L"deepl";
        case Engine::Llm: return L"llm";
        default: return L"auto";
    }
}

void Config::Load(const std::wstring& dir) {
    dataDir = dir;
    iniPath = dir + L"\\gw2-chat-translator.ini";
    if (GetFileAttributesW(iniPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        WriteFileAtomic(iniPath, std::string(kHeader) + kTranslateSections + kRestSections);
    } else if (!Ini(iniPath).HasSection(L"Reader")) {
        // INI from v0.2: append the new sections so the options are visible.
        AppendFileBytes(iniPath, std::string("\r\n; --- neu in v0.3 ---\r\n") + kTranslateSections);
    }

    const Ini ini(iniPath);
    engine = ParseEngine(ini.Str(L"Translate", L"Engine", L"auto"));
    // v0.2 had SourceLang/TargetLangs under [DeepL]; used as fallbacks.
    readLang = ToUpperAscii(ini.Has(L"Translate", L"ReadLang") ? ini.Str(L"Translate", L"ReadLang", L"")
                                                                : ini.Str(L"DeepL", L"SourceLang", L""));
    std::wstring langs = ini.Str(L"Translate", L"WriteLangs", L"");
    if (langs.empty()) langs = ini.Str(L"DeepL", L"TargetLangs", L"");
    if (auto t = ParseLangList(langs); !t.empty()) writeLangs = t;
    backTranslate = ini.Bool(L"Translate", L"BackTranslate", true);
    debounceMs = ini.Int(L"Translate", L"DebounceMs", ini.Int(L"DeepL", L"DebounceMs", 500, 150, 5000), 150, 5000);

    basicEmail = ini.Str(L"Basic", L"Email", L"");
    deeplKey = ini.Str(L"DeepL", L"ApiKey", L"");
    llmUrl = ini.Str(L"LLM", L"Url", L"http://localhost:11434");
    llmModel = ini.Str(L"LLM", L"Model", L"");
    llmKey = ini.Str(L"LLM", L"ApiKey", L"");
    llmTimeoutSec = ini.Int(L"LLM", L"TimeoutSec", 60, 5, 600);

    readerEnabled = ini.Bool(L"Reader", L"Enabled", true);
    readerIntervalMs = ini.Int(L"Reader", L"IntervalMs", 900, 250, 10000);
    ocrLanguage = ini.Str(L"Reader", L"OcrLanguage", L"");
    ocrScale = ini.Int(L"Reader", L"OcrScale", 2, 1, 4);
    showSystemLines = ini.Bool(L"Reader", L"ShowSystem", false);
    saveCaptures = ini.Bool(L"Reader", L"SaveCaptures", false);
    regionLeft = ini.Int(L"Reader", L"RegionLeft", 0, -20000, 20000);
    regionFromBottom = ini.Int(L"Reader", L"RegionFromBottom", 0, -20000, 20000);
    regionWidth = ini.Int(L"Reader", L"RegionWidth", 0, 0, 20000);
    regionHeight = ini.Int(L"Reader", L"RegionHeight", 0, 0, 20000);
    regionSet = regionWidth >= 40 && regionHeight >= 20;

    palette = DefaultChannelColors();
    for (const ColorKey& ck : kColorKeys) {
        Rgb rgb;
        if (!RgbFromHex(ini.Str(L"Reader", ck.key, L""), rgb)) continue;
        for (ChannelColor& c : palette)
            if (c.channel == ck.channel) c.rgb = rgb;
    }

    spellEnabled = ini.Bool(L"Spelling", L"Enabled", true);
    autoCorrect = ini.Bool(L"Spelling", L"AutoCorrect", true);

    glossaryEnabled = ini.Bool(L"Glossary", L"Enabled", true);
    glossaryRefreshDays = ini.Int(L"Glossary", L"RefreshDays", 14, 1, 365);

    hotkey = ini.Str(L"Hotkey", L"Toggle", L"Ctrl+Alt+T");

    maxLength = ini.Int(L"Chat", L"MaxLength", 199, 20, 2000);
    returnFocus = ini.Bool(L"Chat", L"ReturnFocus", false);
    copyOnly = ToLowerAscii(ini.Str(L"Chat", L"SendMode", L"send")) == L"copy";
    send.stepDelayMs = ini.Int(L"Chat", L"StepDelayMs", 80, 20, 1000);
    send.restoreDelayMs = ini.Int(L"Chat", L"RestoreDelayMs", 250, 50, 3000);

    x = ini.Int(L"Window", L"X", kAutoPos, kAutoPos, 32000);
    y = ini.Int(L"Window", L"Y", kAutoPos, kAutoPos, 32000);
    w = ini.Int(L"Window", L"Width", 520, 380, 4000);
    h = ini.Int(L"Window", L"Height", 460, 300, 4000);
    opacity = ini.Int(L"Window", L"Opacity", 238, 120, 255);
    followGame = ini.Bool(L"Window", L"FollowGame", true);
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

void Config::ResetColors() {
    palette = DefaultChannelColors();
    for (const ColorKey& ck : kColorKeys) WritePrivateProfileStringW(L"Reader", ck.key, nullptr, iniPath.c_str());
}

}  // namespace gct
