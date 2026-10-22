// config.hpp — settings from gw2-chat-translator.ini (created with defaults
// and comments on first start; missing keys fall back to defaults). Every
// setting can also be changed in the window (menu ≡ → Settings).
#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "core/chat_line.hpp"
#include "core/i18n.hpp"
#include "win/gw2_sender.hpp"

namespace gct {

enum class Engine { Auto, Basic, DeepL, Llm, Google, Microsoft, Libre };
enum class AutoCorrectMode { Off, Safe, Phone };

struct Config {
    std::wstring dataDir;
    std::wstring iniPath;

    // [General]
    // Language of this window as chosen ("" = Windows language, else a code like "FR" or "de"); `uiLang` is the one
    // the window can show (core/i18n: UiLanguages).
    std::wstring uiLangCode;
    UiLang uiLang = UiLang::En;

    // [Translate]
    Engine engine = Engine::Auto;   // translates what you write
    std::vector<std::wstring> writeLangs{L"EN-GB", L"FR", L"ES", L"DE"};
    bool backTranslate = true;
    bool translateWriting = false;  // translate what you write (off, the first use: sent as written, only corrected)
    std::vector<std::wstring> understoodLangs;            // never translated automatically ("EN")
    bool skipTypingLang = true;                           // nor the language you write in (the list's first entry)

    // [Basic] MyMemory
    std::wstring basicEmail;
    std::wstring myMemoryDay;          // "20261006": the day myMemoryUsed counts for
    int myMemoryUsed = 0;              // characters sent today (free: 5,000 a day, 50,000 with an e-mail)
    // [DeepL]
    std::wstring deeplKey;
    // [Google] Cloud Translation (500,000 characters a month free)
    std::wstring googleKey;
    // [Microsoft] Translator (2 million characters a month free, key + Azure region)
    std::wstring msKey, msRegion;
    // [Libre] own translation server (LibreTranslate-compatible), any address
    std::wstring libreUrl, libreKey;
    // [LLM]
    std::wstring llmUrl, llmModel, llmKey;

    // [Spelling]
    std::wstring writeIn;  // the language you type in ("DE", "EN-GB"); empty = the keyboard layout's language
    bool spellEnabled = true;
    AutoCorrectMode autoCorrect = AutoCorrectMode::Phone;
    bool suggestions = true;     // word bar above the input (completions, next word)
    bool learnWords = true;      // learn from what you send
    bool learnLater = false;     // ... not while playing: collected, learned when you choose ([Spelling] LearnLater)

    // [Chat]
    int maxLength = 199;
    bool returnFocus = false;
    // true: Enter only copies the line; you paste it in GW2 yourself
    // (Enter, Ctrl+V, Enter). Not a single key is sent to the game.
    bool copyOnly = false;
    SendOptions send;

    int opacity = 255;
    int fontPercent = 100;            // chat text: 100 % = 11 pt (older ini; FontPt wins)
    int fontPt = 0;                   // 0 = like the game's interface size (first use); else the chat text size in pt – the add-on builds the font at this size (sharp)
    std::wstring fontFace = L"segoeui";  // its font: a file in Windows\Fonts (segoeui, seguisb, tahoma – kFontFaces)
    bool focusOnGameChat = false;  // focus input when in-game chat box gains focus
    // Key presses vs. letters sent (the word help's benefit): today and in total.
    int typingDay = 0;  // yyyymmdd
    int dayKeys = 0, dayChars = 0, totalKeys = 0, totalChars = 0, totalMessages = 0;

    // [Voice] — voice input of GW2 Keyboard (hold the microphone button or the key, speak, release, check, Enter).
    bool voiceEnabled = true;
    std::wstring voiceModel;          // catalog id ("small-q5_1"); "" = the catalog's recommended one
    std::wstring voiceLang;           // "" = the language you write in, "auto" = detect, else a language code
    bool voiceTranslate = false;      // "Translate what I say": what you said goes into the language at "Send"
    bool voiceIntoEnglish = false;    // whisper's own translation into English – the add-on keeps it (the switch on and
                                      // English at "Send"); the voice helper reads it
    bool voiceToggle = false;         // false: hold to talk; true: press to start, press again to stop
    int voiceMaxSec = 30;             // then the recording ends by itself
    bool voiceLive = true;            // live text while speaking (repeated passes, more CPU)
    int voiceThreads = 0;             // 0 = automatic (half the logical processors, at most 4)
    std::wstring voiceDevice;         // microphone endpoint id; "" = Windows default
    int voiceUnloadMin = 10;          // free the model after this many idle minutes
    bool voiceAutoSend = false;       // GW2 Keyboard for Nexus: send right after speaking (the key release is the press)
    int voiceBoost = 0;               // microphone boost: 0 off, 1 = +6 dB, 2 = +12 dB, 3 = automatic (levels speech)
    bool voiceActivation = false;     // voice activation: listens and starts by itself when you speak
    int voicePauseMs = 1200;          // silence after speech that ends a message (press-to-start, lock, activation)
    int voicePreRollMs = 300;         // voice activation keeps this much from before the speech
    int voiceSensitivity = 50;        // voice activation 0..100 (100 = reacts to quiet speech)
    int voiceMinMs = 250;             // voice activation: speech needed to start
    std::wstring voiceGlobalKey;      // key anywhere (Windows hotkey, also while GW2 is in front); "" = none
    // Voice activation is not exclusive (Discord hears you too, and it cannot tell what was meant for it): it pauses
    // while one of these voice chats has the microphone open; the key and a click still work.
    bool voiceChatPause = true;
    std::wstring voiceChatApps = L"Discord.exe, DiscordPTB.exe, DiscordCanary.exe, ts3client_win64.exe, TeamSpeak.exe, mumble.exe, Teams.exe, ms-teams.exe, Zoom.exe, Skype.exe, Guilded.exe";

    void Load(const std::wstring& dir);
    std::wstring VoiceModelDir() const { return dataDir + L"\\models"; }  // voice models (whisper)
    // Writes every setting that the settings dialog can change.
    void SaveAll() const;
    void SaveValue(const wchar_t* section, const wchar_t* key, const std::wstring& value) const;
    void SaveBool(const wchar_t* section, const wchar_t* key, bool v) const { SaveValue(section, key, v ? L"1" : L"0"); }
    // Each WritePrivateProfileString rewrites the whole file (~15 ms, more with a virus scanner) – the game hung on the
    // game thread. Deferred (the add-on), writes wait in memory, the newest per key, until FlushWrites (any thread).
    static void DeferWrites(bool on);
    static bool WritesPending();
    static void FlushWrites();
    // Everything of the add-on was removed: from now on nothing is written (pending writes are dropped).
    static void BlockWrites();

    std::wstring UserWordsPath() const;  // your own GW2 words (never marked as errors)
    std::wstring LearnedDir() const { return dataDir + L"\\learned"; }
    std::wstring CacheDir() const { return dataDir + L"\\cache"; }
};

// The window language for a choice ("" = Windows display language): one of UiLanguages, else English.
UiLang EffectiveUiLang(const std::wstring& code);
const wchar_t* EngineKey(Engine e);        // "auto", "basic", "deepl", "llm"
const wchar_t* AutoCorrectKey(AutoCorrectMode m);  // "off", "safe", "phone"

}  // namespace gct
