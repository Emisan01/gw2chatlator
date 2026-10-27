// config.cpp
#include "config.hpp"

#include "win/secret.hpp"

#include <algorithm>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <map>
#include <mutex>
#include <tuple>

#include "core/text.hpp"
#include "win/files.hpp"

namespace gct {
namespace {

// ASCII only, so GetPrivateProfileString reads it the same in every locale.
// The ini a new player gets: only what the add-on reads (everything can be changed in Options).
const char kTemplate[] =
    "; GW2Chatlator - settings\r\n"
    "; Everything here can also be changed in the add-on: menu (=) -> Options. Changes here apply on the next start.\r\n"
    "\r\n"
    "[General]\r\n"
    "; Language of the window: empty = Windows language, en, de, fr, es, it, pl, pt, tr\r\n"
    "UiLanguage=\r\n"
    "\r\n"
    "[Translate]\r\n"
    "; Translator: auto | basic | deepl | google | microsoft | libre | local\r\n"
    ";   auto  = DeepL, Google, Microsoft or your server if a key / address is set, else basic\r\n"
    ";   basic = MyMemory: free, no account, limited daily quota\r\n"
    "Engine=auto\r\n"
    "; Languages for \"Send\" (the footer's choice); the chat is read in the language you write in\r\n"
    "WriteLangs=EN-GB,FR,ES,DE\r\n"
    "; Translate your message back into your language as a check (1/0)\r\n"
    "BackTranslate=1\r\n"
    "; Languages you understand: not translated by themselves (a click still translates), e.g. EN,DE\r\n"
    "Understood=\r\n"
    "\r\n"
    "[Basic]\r\n"
    "; Optional: your e-mail address raises the free MyMemory quota (5,000 -> 50,000 characters/day).\r\n"
    "Email=\r\n"
    "\r\n"
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
    "; Windows spell checking (offline). It follows the language you write in.\r\n"
    "Enabled=1\r\n"
    "; Autocorrection while typing: off | safe (only Windows' sure fixes) | phone (like a phone keyboard)\r\n"
    "AutoCorrectMode=phone\r\n"
    "; Word suggestions: the word bar and the grey word that finishes yours (Tab takes it)\r\n"
    "Suggestions=1\r\n"
    "; Learn the words you send (saved per language in the learned folder)\r\n"
    "Learn=1\r\n"
    "\r\n"
    "[Chat]\r\n"
    "; How your message gets into the chat:\r\n"
    ";   send = your Enter sends the line into GW2's chat (Enter, paste, Enter) - only ever on your Enter\r\n"
    ";   copy = Enter only copies; you paste it in GW2 yourself. Not a single key goes to the game.\r\n"
    "SendMode=send\r\n"
    "; GW2 allows 199 characters per chat line; longer messages are split (at most 3 lines per Enter)\r\n"
    "MaxLength=199\r\n"
    "; 1 = back to the input after sending, 0 = stay in the game\r\n"
    "ReturnFocus=0\r\n"
    "; 1 = key hold and pauses follow the game's FPS, 0 = the values below\r\n"
    "AutoTiming=1\r\n"
    "StepDelayMs=80\r\n"
    "KeyHoldMs=30\r\n"
    "RestoreDelayMs=250\r\n"
    "\r\n"
    "[Window]\r\n"
    "; 0 (fully see-through) to 255 (not see-through at all)\r\n"
    "Opacity=255\r\n"
    "; Text size in pt (8 - 28); new installations start at 14 pt\r\n"
    "FontPt=14\r\n";

// Versions before 2.0.2 appended this block on every start to an ini without the old [Reader] section (an upgrade
// path from the desktop program's v0.2) – the ini of a new player grew by it each time. Removed again on load: the
// copies are byte for byte what was appended (Windows writes into the first copy of a section only).
const char kOldAppended[] =
    "\r\n; --- new in v0.3 ---\r\n"
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
    "\r\n";

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
    if (v == L"local" || v == L"opus" || v == L"opus-mt") return Engine::Local;
    if (v == L"nllb" || v == L"nllb-200") return Engine::Nllb;
    return Engine::Auto;
}

}  // namespace

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
        case Engine::Local: return L"local";
        case Engine::Nllb: return L"nllb";
        default: return L"auto";
    }
}

UiLang EffectiveUiLang(const std::wstring& code) {
    if (!Trim(code).empty()) return UiLangFromCode(code, UiLang::En);
    const LANGID winUi = PRIMARYLANGID(GetUserDefaultUILanguage());
    switch (winUi) {
        case LANG_GERMAN: return UiLang::De;
        case LANG_FRENCH: return UiLang::Fr;
        case LANG_SPANISH: return UiLang::Es;
        case LANG_ITALIAN: return UiLang::It;
        case LANG_POLISH: return UiLang::Pl;
        case LANG_PORTUGUESE: return UiLang::Pt;
        case LANG_TURKISH: return UiLang::Tr;
        case LANG_RUSSIAN: return UiLang::Ru;
        default: return UiLang::En;
    }
}

void Config::Load(const std::wstring& dir) {
    dataDir = dir;
    iniPath = dir + L"\\gw2-chat-translator.ini";
    if (GetFileAttributesW(iniPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        WriteFileAtomic(iniPath, kTemplate);
    } else {
        std::string text;
        if (ReadFileBytes(iniPath, text) && text.find(kOldAppended) != std::string::npos) {
            for (size_t at; (at = text.find(kOldAppended)) != std::string::npos;) text.erase(at, std::strlen(kOldAppended));
            WriteFileAtomic(iniPath, text);
        }
    }

    const Ini ini(iniPath);
    uiLangCode = ini.Str(L"General", L"UiLanguage", L"");
    uiLang = EffectiveUiLang(uiLangCode);
    myMemoryDay = ini.Str(L"Basic", L"UsedDay", L"");
    myMemoryUsed = ini.Int(L"Basic", L"UsedChars", 0, 0, 10000000);
    engine = ParseEngine(ini.Str(L"Translate", L"Engine", L"auto"));
    // v0.2 had TargetLangs under [DeepL]; used as a fallback.
    std::wstring langs = ini.Str(L"Translate", L"WriteLangs", L"");
    if (langs.empty()) langs = ini.Str(L"DeepL", L"TargetLangs", L"");
    if (auto t = ParseLangList(langs); !t.empty()) writeLangs = t;
    backTranslate = ini.Bool(L"Translate", L"BackTranslate", true);
    translateWriting = ini.Bool(L"Translate", L"Writing", false);  // first use: sent as written
    understoodLangs = ParseLangList(ini.Str(L"Translate", L"Understood", L""));
    skipTypingLang = ini.Bool(L"Translate", L"SkipTypingLang", true);
    localQuality = ini.Int(L"Translate", L"LocalQuality", 2, 1, 4);
    if (localQuality != 1 && localQuality != 4) localQuality = 2;

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

    writeIn = ToUpperAscii(ini.Str(L"Spelling", L"WriteIn", L""));
    voiceEnabled = ini.Bool(L"Voice", L"Enabled", true);
    voiceLang = AsciiUnescape(ini.Str(L"Voice", L"Language", L"auto"));
    if (voiceLang.empty()) voiceLang = L"auto";
    voiceIntoEnglish = ini.Bool(L"Voice", L"IntoEnglish", false);
    voiceTranslate = ini.Bool(L"Voice", L"Translate", voiceIntoEnglish);  // up to 2.1 the switch was "into English"
    voiceToggle = ini.Str(L"Voice", L"Mode", L"hold") == L"toggle";
    voiceMaxSec = ini.Int(L"Voice", L"MaxSeconds", 30, 5, 120);
    voiceLive = ini.Bool(L"Voice", L"Live", true);
    voiceThreads = ini.Int(L"Voice", L"Threads", 0, 0, 16);
    voiceDevice = AsciiUnescape(ini.Str(L"Voice", L"Device", L""));
    voiceUnloadMin = ini.Int(L"Voice", L"UnloadMinutes", 10, 1, 240);
    voiceBoost = ini.Int(L"Voice", L"Boost", 0, 0, 3);
    voiceAutoSend = ini.Bool(L"Voice", L"AutoSend", false);
    voiceActivation = ini.Bool(L"Voice", L"Activation", false);
    voicePauseMs = ini.Int(L"Voice", L"PauseMs", 1500, 400, 3000);
    voicePreRollMs = ini.Int(L"Voice", L"PreRollMs", 300, 0, 1000);
    voiceSensitivity = ini.Int(L"Voice", L"Sensitivity", 50, 0, 100);
    voiceMinMs = ini.Int(L"Voice", L"MinSpeechMs", 250, 100, 1500);
    voiceGlobalKey = ini.Str(L"Voice", L"KeyAnywhere", L"");
    voiceChatPause = ini.Bool(L"Voice", L"PauseForVoiceChat", true);
    spellEnabled = ini.Bool(L"Spelling", L"Enabled", true);
    // v0.4 had AutoCorrect=1/0 (Windows' sure fixes only).
    autoCorrect = ParseAutoCorrect(ini.Str(L"Spelling", L"AutoCorrectMode", L""),
                                   ini.Bool(L"Spelling", L"AutoCorrect", true) ? AutoCorrectMode::Phone
                                                                               : AutoCorrectMode::Off);
    suggestions = ini.Bool(L"Spelling", L"Suggestions", true);
    learnWords = ini.Bool(L"Spelling", L"Learn", true);
    learnLater = ini.Bool(L"Spelling", L"LearnLater", false);



    maxLength = ini.Int(L"Chat", L"MaxLength", 199, 20, 2000);
    returnFocus = ini.Bool(L"Chat", L"ReturnFocus", false);
    copyOnly = ToLowerAscii(ini.Str(L"Chat", L"SendMode", L"send")) == L"copy";
    send.autoTiming = ini.Bool(L"Chat", L"AutoTiming", true);
    send.stepDelayMs = ini.Int(L"Chat", L"StepDelayMs", 80, 20, 1000);
    send.keyHoldMs = ini.Int(L"Chat", L"KeyHoldMs", 30, 5, 500);
    send.restoreDelayMs = ini.Int(L"Chat", L"RestoreDelayMs", 250, 50, 3000);

    opacity = ini.Int(L"Window", L"Opacity", 255, 0, 255);
    fontPercent = ini.Int(L"Window", L"FontPercent", 100, 70, 300);
    fontPt = ini.Int(L"Window", L"FontPt", 15, 0, 40);
    uiFontPt = ini.Int(L"Window", L"UiFontPt", 15, 0, 40);
    if (fontPt == 0) fontPt = 15;
    if (uiFontPt == 0) uiFontPt = 15;
    fontFace = ToLowerAscii(ini.Str(L"Window", L"FontFace", L"segoeui"));
    if (fontFace != L"seguisb" && fontFace != L"tahoma") fontFace = L"segoeui";  // only what we offer
    chatTabName = ini.Str(L"Window", L"ChatTabName", L"");
    focusOnGameChat = ini.Bool(L"Window", L"FocusOnGameChat", false);
    typingDay = ini.Int(L"Typing", L"Day", 0, 0, 99991231);
    dayKeys = ini.Int(L"Typing", L"DayKeys", 0, 0, 2000000000);
    dayChars = ini.Int(L"Typing", L"DayChars", 0, 0, 2000000000);
    totalKeys = ini.Int(L"Typing", L"TotalKeys", 0, 0, 2000000000);
    totalChars = ini.Int(L"Typing", L"TotalChars", 0, 0, 2000000000);
    totalMessages = ini.Int(L"Typing", L"TotalMessages", 0, 0, 2000000000);
}

namespace {
std::mutex g_writeMu;
std::mutex g_flushMu;
bool g_deferWrites = false;
bool g_blockWrites = false;
std::map<std::tuple<std::wstring, std::wstring, std::wstring>, std::wstring> g_pendingWrites;  // file, section, key
}  // namespace

void Config::SaveValue(const wchar_t* section, const wchar_t* key, const std::wstring& value) const {
    {
        std::lock_guard<std::mutex> lock(g_writeMu);
        if (g_blockWrites) return;
        if (g_deferWrites) {
            g_pendingWrites[std::make_tuple(iniPath, std::wstring(section), std::wstring(key))] = value;
            return;
        }
    }
    WritePrivateProfileStringW(section, key, value.c_str(), iniPath.c_str());
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, iniPath.c_str());
}

void Config::DeferWrites(bool on) {
    {
        std::lock_guard<std::mutex> lock(g_writeMu);
        g_deferWrites = on;
    }
    if (!on) FlushWrites();
}

bool Config::WritesPending() {
    std::lock_guard<std::mutex> lock(g_writeMu);
    return !g_pendingWrites.empty();
}

void Config::BlockWrites() {
    std::lock_guard<std::mutex> lock(g_writeMu);
    g_blockWrites = true;
    g_pendingWrites.clear();
}

void Config::FlushWrites() {
    std::lock_guard<std::mutex> flushLock(g_flushMu);
    std::map<std::tuple<std::wstring, std::wstring, std::wstring>, std::wstring> writes;
    {
        std::lock_guard<std::mutex> lock(g_writeMu);
        writes.swap(g_pendingWrites);
    }
    if (writes.empty()) return;

    // Group writes by target INI file path to batch modifications atomically per file
    std::map<std::wstring, std::vector<std::pair<std::wstring, std::pair<std::wstring, std::wstring>>>> byFile;
    for (const auto& w : writes) {
        byFile[std::get<0>(w.first)].push_back({std::get<1>(w.first), {std::get<2>(w.first), w.second}});
    }

    for (const auto& [targetPath, entries] : byFile) {
        if (targetPath.empty()) continue;
        const std::wstring tmpPath = targetPath + L".tmp";
        DeleteFileW(tmpPath.c_str());

        const bool existed = (GetFileAttributesW(targetPath.c_str()) != INVALID_FILE_ATTRIBUTES);
        bool writeViaTmp = true;

        if (existed) {
            if (!CopyFileW(targetPath.c_str(), tmpPath.c_str(), FALSE)) {
                writeViaTmp = false;
            }
        }

        if (!writeViaTmp) {
            for (const auto& e : entries) {
                WritePrivateProfileStringW(e.first.c_str(), e.second.first.c_str(), e.second.second.c_str(), targetPath.c_str());
            }
            WritePrivateProfileStringW(nullptr, nullptr, nullptr, targetPath.c_str());
            continue;
        }

        for (const auto& e : entries) {
            WritePrivateProfileStringW(e.first.c_str(), e.second.first.c_str(), e.second.second.c_str(), tmpPath.c_str());
        }
        // Flush Windows profile cache for the tmp file
        WritePrivateProfileStringW(nullptr, nullptr, nullptr, tmpPath.c_str());

        // Atomic swap (ArcDPS standard): atomically replace target file, preventing torn writes.
        // Retries briefly in case an external process (e.g. anti-virus) holds a transient read lock.
        bool swapped = false;
        for (int attempt = 0; attempt < 3 && !swapped; ++attempt) {
            if (attempt > 0) Sleep(10);
            if (existed) {
                swapped = ReplaceFileW(targetPath.c_str(), tmpPath.c_str(), nullptr,
                                       REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr) != 0;
            }
            if (!swapped) {
                swapped = MoveFileExW(tmpPath.c_str(), targetPath.c_str(),
                                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
            }
        }
        if (!swapped) {
            for (const auto& e : entries) {
                WritePrivateProfileStringW(e.first.c_str(), e.second.first.c_str(), e.second.second.c_str(), targetPath.c_str());
            }
            DeleteFileW(tmpPath.c_str());
        }
        // Invalidate/flush profile cache for target path so subsequent reads get new content
        WritePrivateProfileStringW(nullptr, nullptr, nullptr, targetPath.c_str());
    }
}

void Config::SaveAll() const {
    SaveValue(L"General", L"UiLanguage", uiLangCode);
    SaveValue(L"Translate", L"Engine", EngineKey(engine));
    std::wstring joined;
    for (const std::wstring& c : writeLangs) joined += (joined.empty() ? L"" : L",") + c;
    SaveValue(L"Translate", L"WriteLangs", joined);
    SaveBool(L"Translate", L"BackTranslate", backTranslate);
    SaveBool(L"Translate", L"Writing", translateWriting);
    {
        std::wstring list;
        for (const std::wstring& l : understoodLangs) list += (list.empty() ? L"" : L",") + l;
        SaveValue(L"Translate", L"Understood", list);
    }
    SaveBool(L"Translate", L"SkipTypingLang", skipTypingLang);
    SaveValue(L"Translate", L"LocalQuality", std::to_wstring(localQuality));  // was read, never written (until 2.6.4)
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
    SaveValue(L"Spelling", L"WriteIn", writeIn);
    SaveBool(L"Chat", L"AutoTiming", send.autoTiming);
    SaveValue(L"Chat", L"KeyHoldMs", std::to_wstring(send.keyHoldMs));
    SaveValue(L"Chat", L"StepDelayMs", std::to_wstring(send.stepDelayMs));
    SaveBool(L"Voice", L"Enabled", voiceEnabled);
    SaveValue(L"Voice", L"Model", AsciiEscape(voiceModel));
    SaveValue(L"Voice", L"Language", AsciiEscape(voiceLang));
    SaveBool(L"Voice", L"Translate", voiceTranslate);
    SaveBool(L"Voice", L"IntoEnglish", voiceIntoEnglish);
    SaveValue(L"Voice", L"Mode", voiceToggle ? L"toggle" : L"hold");
    SaveValue(L"Voice", L"MaxSeconds", std::to_wstring(voiceMaxSec));
    SaveBool(L"Voice", L"Live", voiceLive);
    SaveValue(L"Voice", L"Threads", std::to_wstring(voiceThreads));
    SaveValue(L"Voice", L"Device", AsciiEscape(voiceDevice));
    SaveValue(L"Voice", L"UnloadMinutes", std::to_wstring(voiceUnloadMin));
    SaveValue(L"Voice", L"Boost", std::to_wstring(voiceBoost));
    SaveBool(L"Voice", L"AutoSend", voiceAutoSend);
    SaveBool(L"Voice", L"Activation", voiceActivation);
    SaveValue(L"Voice", L"PauseMs", std::to_wstring(voicePauseMs));
    SaveValue(L"Voice", L"PreRollMs", std::to_wstring(voicePreRollMs));
    SaveValue(L"Voice", L"Sensitivity", std::to_wstring(voiceSensitivity));
    SaveValue(L"Voice", L"MinSpeechMs", std::to_wstring(voiceMinMs));
    SaveValue(L"Voice", L"KeyAnywhere", voiceGlobalKey);
    SaveBool(L"Voice", L"PauseForVoiceChat", voiceChatPause);
    SaveValue(L"Voice", L"VoiceChatApps", AsciiEscape(voiceChatApps));
    SaveBool(L"Spelling", L"Enabled", spellEnabled);
    SaveValue(L"Spelling", L"AutoCorrectMode", AutoCorrectKey(autoCorrect));
    SaveBool(L"Spelling", L"Suggestions", suggestions);
    SaveBool(L"Spelling", L"Learn", learnWords);
    SaveBool(L"Spelling", L"LearnLater", learnLater);
    SaveValue(L"Chat", L"SendMode", copyOnly ? L"copy" : L"send");
    SaveBool(L"Chat", L"ReturnFocus", returnFocus);
    SaveValue(L"Window", L"Opacity", std::to_wstring(opacity));
    SaveValue(L"Window", L"FontPt", std::to_wstring(fontPt));
    SaveValue(L"Window", L"UiFontPt", std::to_wstring(uiFontPt));
    SaveValue(L"Window", L"FontFace", fontFace);
    SaveValue(L"Window", L"ChatTabName", chatTabName);
    SaveBool(L"Window", L"FocusOnGameChat", focusOnGameChat);
}

}  // namespace gct
