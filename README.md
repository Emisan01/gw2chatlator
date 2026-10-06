# GW2 Chat Translator

A window next to (or over) the Guild Wars 2 chat that translates both ways. The whole GW2 chat appears in your language, and what you write, in German, Arabic, Chinese or Latin, goes into the channel you choose, corrected and translated.

No hook, no memory reading, nothing injected into the game. The tool only looks at what is on the screen anyway, and it sends a line only when you press Enter yourself.

The window is available in **English, Deutsch and العربية**. Use menu ≡ → **Language / Sprache / اللغة**. That entry has the same name in every language, so you can always find your way back.

## Install (one minute)

1. Download `GW2ChatTranslator.exe` and start it anywhere.
2. The **guided setup** opens on the first start:
   - **Language and installation.** Pick the window language and the language you want to read. GW2 is found on its own (registry, Steam libraries, usual folders). With one tick the tool copies itself into `Guild Wars 2\addons\GW2ChatTranslator\`, a clean folder of its own. Another tick starts it with Windows: it stays hidden and appears when GW2 runs.
   - **Prepare the GW2 chat.** Keep the GW2 chat panel open (8–12 lines are enough). Use a chat tab with all channels, turn timestamps on, and choose text size medium or larger.
   - **Mark the chat.** Draw a frame around the text lines of the GW2 chat. The window then lies exactly over the GW2 chat and replaces it. The real chat keeps being read underneath, because Windows leaves our window out of its own capture (Windows 10 version 2004 or newer).
3. Optional: menu ≡ → Settings → Translator, to add a DeepL key or a local LLM.

You can run the setup again at any time from menu ≡ → Setup. Every setting lives in menu ≡ → Settings, so you never need to edit the ini.

**Start with GW2 without a hook:** the "start with Windows" option adds an entry to the Windows autostart (`HKCU\…\Run`, `--wait-for-gw2`). The tool then sits in the tray and checks every two seconds whether a GW2 window exists. Nothing is put into the game folder except our own subfolder, and nothing is loaded into the game.

## What it does

**Reading**
- Reads the GW2 chat from the screen and shows every new message in your reading language, with the original in small text below it.
- Text recognition uses **Tesseract** when it is installed. Tesseract is much more accurate on the small GW2 font: in our tests on real captures about 92–96 % of the words compared to ~56 %. Otherwise Windows' built-in recognition is used. Tesseract is free: install it with the [UB Mannheim installer](https://github.com/UB-Mannheim/tesseract/wiki) and tick English, German, French, Spanish (and Chinese Simplified if you want). The tool reads `eng+deu+fra+spa` by default; Chinese is optional. Arabic does not need to be read: you write it.
- Recognizes the channel (text colour or channel tag), the speaker, guild tags, timestamps (including OCR-mangled ones such as `C9;17J`) and wrapped lines. It drops the tab bar and the input line, and puts yellow event notices into the system lines.
- **Tabs like in GW2:** right-click a tab to choose its channels. New tabs come from templates (All, Party, Guild, Map, WvW, Whisper), with unread counters. A tab that shows exactly one channel writes to that channel.
- **Whispers** have their own tab. Right-click a whisper → Reply to ….
- Messages already in your language are not translated, repeated lines come from a cache, and your own lines do not show up twice.
- With an LLM as translator, lines read from the screen also get obvious recognition errors repaired ("Mnuten" → "Minuten").

**Writing**
- Type in any language and script; Arabic and Hebrew run right to left. Spell checking follows your keyboard layout.
- **Autocorrection like a phone keyboard** (Off / Safe / Phone):
  - A typo is fixed when you finish the word (`komtm` → `kommt`).
  - **Backspace right after the fix undoes it** and remembers the word.
  - The **word bar** above the input offers completions, corrections and the next word. **Tab** takes the highlighted one.
  - The tool **learns the words you send**, per language, in `learned\learned_<lang>.txt`.
- **Optional grammar check with LanguageTool:** the public server (max. 20 checks per minute) or your own server. Grammar issues get blue marks, and right-click shows the suggestions.
- **Send as:** English, French, Arabic, Chinese …, or Original (only corrected). 38 languages are available.
- **Preview** of what arrives in the game, plus the **back-translation** into your language.
- Official game names come from the GW2 API: "Löwenstein" becomes "Lion's Arch" instead of a literal translation. Chat codes and LFG/WvW/DPS stay untouched.
- Longer than 199 characters: the message is split at sentence borders, and every Enter sends one part.
- Scripts GW2 cannot show: the preview warns you. **Ctrl+U** writes the message in Latin letters instead (Arabizi, Pinyin, Romaji).

## Translators: basic is enough, more is possible

| | Cost | Quality | Notes |
|---|---|---|---|
| **Basic** (MyMemory) | free, no account | decent | 5,000 characters/day, 50,000 with an e-mail address |
| **DeepL** | own API key | very good | Settings → Translator; the free keys end in `:fx` |
| **Local LLM** (Ollama, LM Studio) | free, unlimited, offline | good to very good | "Load models" lists what your server has. Ollama + `qwen2.5:7b` works well |
| **Cloud LLM** | depends on the provider | very good | any OpenAI-compatible address plus API key |

The LLM receives chat lines strictly as data. Instructions someone writes into the chat get translated, not followed. Whatever the LLM returns is only displayed; it can never send anything.

## Keys

| Key | Does |
|---|---|
| Enter | send (or copy in "only copy" mode); for split messages: the next part |
| Ctrl+Enter | send the original without translation |
| Tab | take the highlighted word of the word bar |
| Backspace right after an autocorrection | undo it (and learn the word) |
| Ctrl+L | next "send as" language |
| Ctrl+U | Latin letters |
| Ctrl+Tab | next tab |
| Esc | back to the game |
| Ctrl+Alt+T (changeable) | show / hide the window, system wide |

## ArenaNet rules and account safety

ArenaNet forbids programs that give an unfair advantage, automate gameplay, allow unattended play or harm others. Supervised macros that trigger one action per key press are allowed. No third-party tool is checked or approved by ArenaNet, so using one is always at your own risk ([third-party policy](https://help.guildwars2.com/hc/en-us/articles/360013625034-Policy-Third-Party-Programs)). That is why this tool works like this:

- **A separate exe, not a DLL.** Nothing is loaded into the GW2 process: no hook, no memory reading, no changed game files. Installing only creates our own folder `addons\GW2ChatTranslator`.
- **It only reads what you see,** in the area you marked yourself. MumbleLink, GW2's official interface, tells it the character name and whether the chat line is open.
- **It never sends on its own.** One line per Enter you press. A tool that reads chat and answers by itself would be a bot, and there is none of that here, LLM or not.
- **"Only copy" mode:** Enter just copies the line. You paste it in GW2 yourself, so not a single synthetic key reaches the game.

## What leaves your PC

Only text, and only to the services you chose:
- MyMemory, DeepL or a cloud LLM receives the text to translate.
- LanguageTool receives what you type, and only while the grammar check is on.
- Official names come from `api.guildwars2.com`.

With a local LLM, nothing leaves the PC. Screenshots are never sent anywhere.

## Files

Everything sits next to the exe, or in `%LOCALAPPDATA%\GW2ChatTranslator` if that folder is not writable.

| File | Content |
|---|---|
| `gw2-chat-translator.ini` | settings (may contain your DeepL/LLM key – do not share it) |
| `my-gw2-words.txt` | your own words that are never marked as errors |
| `learned\learned_<lang>.txt` | words and word pairs learned from what you sent (safe to delete) |
| `cache\gw2names_<lang>.tsv` | official names from the GW2 API, refreshed every 14 days |
| `captures\` | diagnostics only. Switches itself off after 15 minutes, keeps at most 20 pictures, deletes them after 3 days |

Leftover temp files from interrupted writes are removed on start.

## Limits

- Only what GW2 shows in its chat panel can be read. A minimized chat shows nothing. Whispers that only flash in a minimized chat can be missed, so keep the panel open and let our window lie over it.
- Without a hook there is no other source for all channels. arcdps "unofficial extras" offers exact text only for party/squad (and needs a DLL in the game). An optional add-on for that is on the roadmap, off by default.
- Names read by text recognition can contain errors. `/r` (reply to the last whisper) is always safe.

## Building

**Visual Studio 2022:** File → Open → Folder; CMake is picked up automatically. Build `GW2ChatTranslator.exe`.

**MinGW (also from Linux):**

```
cmake -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-win
```

**Tests:** `g++ -std=c++17 -I src tests/core_tests.cpp src/core/*.cpp && ./a.out` runs the core tests on any system. Run it from the repo root, because it reads `tests/data`. The Wine end-to-end harness is described in [CLAUDE.md](CLAUDE.md).

**Translations:** all UI text is English in the code, wrapped in `Tr()`. The German and Arabic tables are `src/core/i18n_de.cpp` and `src/core/i18n_ar.cpp`. `python3 tools/i18n_check.py` lists missing entries.

## Origin

The idea of a floating input window that sends into the GW2 chat comes from [Grammarly-support-overlay-for-guild-wars-2](https://github.com/hazratali-uydevelopers/Grammarly-support-overlay-for-guild-wars-2) (ISC). No code was taken from it; this is a native rewrite in C++/Win32.
