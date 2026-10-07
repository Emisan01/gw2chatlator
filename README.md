# GW2 Chat Translator

A chat helper for Guild Wars 2: it helps you **write** (word suggestions that learn how you write, autocorrection like
a phone keyboard, translation into the chat's language) and **translates on demand** what others write – in the GW2
chat or in any area of the screen.

No hook, no memory reading, nothing injected into the game. The tool only looks at what is on the screen anyway, and it
sends a line only when you press Enter yourself.

The window is available in **English, Deutsch and العربية**. Use menu ≡ → **Language / Sprache / اللغة**. That entry
has the same name in every language, so you can always find your way back.

## Download

**[⬇ Download the latest version (Windows, ZIP, ~14 MB)](https://github.com/Emisan01/gw2chatlator/releases/latest/download/GW2ChatTranslator-win64.zip)**
– unpack it anywhere and start `GW2ChatTranslator.exe`. All versions: [Releases](https://github.com/Emisan01/gw2chatlator/releases) ·
what changed: [CHANGELOG.md](CHANGELOG.md).

> The green **Code → Download ZIP** button only contains the source code, not the program – use the link above.

Windows may warn about an unknown publisher on the first start (the exe is not signed yet): *More info → Run anyway*.

## Install (one minute)

1. Download the ZIP above, unpack it and start `GW2ChatTranslator.exe`.
2. The **setup** opens on the first start: window language, the language you read, and where it goes. Ticked by
   default: *Install for this Windows user* – the tool copies itself to `%LOCALAPPDATA%\Programs\GW2ChatTranslator`
   (no admin rights), the downloaded copy closes and the installed one starts. Free choices: desktop shortcut,
   start-menu entry, start with Windows (hidden until GW2 runs).
3. Open the GW2 chat on a map: the tool finds the timestamp lines by itself and sets the chat area (or draw it yourself:
   menu ≡ → Set the chat area).
4. Optional: menu ≡ → Settings → Translator, for a better translator (a free Gemini key, DeepL, Google, a local model).

**Updating:** start a newer downloaded version – it offers to update the installed copy (settings, learned words and
corrections stay). Every setting lives in menu ≡ → Settings; you never need to edit the ini.

## What it does

**Writing (the core)**
- **Word suggestions like Grammarly:** the likely word appears grey after the cursor and narrows with every letter.
  **Space** writes it, **Tab** shows the next suggestion (Shift+Tab back), **→** takes it without a space, Esc hides it.
- It **learns how you write** – words, pairs and triples of words from the messages *you* send, per language
  (`learned\learned_<lang>.txt`): after "kommst du" it offers "mit" – grey right after the space when it is almost
  always that word. What you pick counts more, the sentence so far counts more than plain frequency, and old habits
  fade as you write. Names and words of the current conversation are offered too (never learned). Settings → Writing
  shows how many key presses it saves.
- **Autocorrection like a phone keyboard** (Off / Safe / Phone): a typo is fixed when you finish the word
  (`komtm` → `kommt`), slips to the neighbouring key of your keyboard layout are recognized, **Backspace right after
  the fix undoes it**. Right-click a learned word → *Forget*.
- **My words:** teach it your slang ("finds = finde es") – it is never marked as an error and the translator gets the
  plain meaning.
- Type in any language and script; Arabic and Hebrew run right to left, Arabic spelling variants (أ/إ/آ/ا, ى/ي, ة/ه,
  harakat) count as the same word. Optional grammar check with LanguageTool (blue marks).
- **Translation into the chat's language** with a preview of what arrives in the game and the back-translation into
  your language. Chat codes, names and LFG/WvW/DPS stay untouched; official game names come from the GW2 API.
  Longer than 199 characters: split at sentence borders, one part per Enter. Scripts GW2 cannot show: **Ctrl+U**
  writes Latin letters (Arabizi, Pinyin, Romaji).

**Five helpers, each with its own job** – they overlap a little, but none of them changes what another one is for:

| Helper | Looks at | Does | Never | Learns | Online |
|---|---|---|---|---|---|
| Completion (grey word) | the word you are typing | finishes *this one word* | acts without Space, → or Tab | your words | no |
| Next word (word bar, grey after a space) | your last 1–2 words | offers the next word | writes anything by itself | word pairs and triples | no |
| Autocorrection | the word you just finished | fixes *this one word* (clear typos only) | touches a word you use; Backspace undoes it | yes (Backspace = "meant as written") | no |
| Grammar check (optional) | the whole sentence | marks it blue | changes your text (suggestions on right-click) | no | yes, while on |
| Translator | the finished sentence | translates it into the preview | edits your input | only your own corrections | yes |

The writing helpers learn only from what you write and run on this PC in a few milliseconds per key; neural models
(an AI translator, local or online) are only used for translating.

**Translating (on demand)**
- **Translate once now** (menu ≡): one look at the chat – scroll the GW2 chat up to something older and translate it.
- **Automatic translation (permanent):** new lines in another language are translated as they come. Only lines whose
  language is clear are sent to a translator; with **Show only translations** (default) lines in your languages do not
  appear at all.
- **Any screen area** (menu ≡): frame a website, a document, another game – its text is translated.
- Recognition errors are repaired where the dictionary knows the word (`syn!ax` → `syntax`) and left out where nothing
  makes sense; repairs are learned. Text recognition: Windows OCR, RapidOCR (open source, ships with the program, best
  for small text) or Tesseract.
- **Correction memory:** right-click a translated line → *Correct this translation…* – that text gets your translation
  from then on, with every translator.
- **A calm chat view:** no timestamps, no channel tags – the channel shows by the text colour taken from the game.
  Click a **name** → a whisper tab for that person; click the **text** → translated (again, if a translation did not
  fit). Tabs like in GW2 (channel sets, unread counters); links open only after a question.

## Translators: free is enough, better is possible

| | Cost | Quality | Notes |
|---|---|---|---|
| **MyMemory** | free, no account | decent | 5,000 characters/day, 50,000 with any e-mail address of yours |
| **Gemini, Claude, GPT … (AI model)** | Gemini has a free key | very good | also repairs recognition errors; any OpenAI-compatible address |
| **Local model** (Ollama) | free, offline | good | Settings → Translator → install a model (sizes and VRAM shown) |
| **DeepL, Google, Microsoft** | own key (free contingents) | very good | a "get key" button for each |
| **LibreTranslate** | own server, free | decent | "Set up on this PC…" with only your languages |

An AI model receives chat lines strictly as data. Instructions someone writes into the chat get translated, not
followed. Whatever comes back is only displayed; it can never send anything.

## Keys

| Key | Does |
|---|---|
| Enter | send (or copy in "only copy" mode); for split messages: the next part |
| Ctrl+Enter | send the original without translation |
| Space / Tab / → | write the grey suggestion / next suggestion / take it without a space (after a space: Tab takes the grey next word or phrase, → one word) |
| Backspace right after an autocorrection | undo it (and learn the word) |
| Ctrl+L | next language to write in |
| Ctrl+U | Latin letters |
| Ctrl+Tab | next tab |
| Esc | back to the game |
| Ctrl+Alt+Shift+T (changeable, or none) | show / hide the window, system wide |
| Win+H | Windows dictation – speak instead of typing, into the input box |

## ArenaNet rules and account safety

ArenaNet forbids programs that give an unfair advantage, automate gameplay, allow unattended play or harm others. No
third-party tool is checked or approved by ArenaNet, so using one is always at your own risk
([third-party policy](https://help.guildwars2.com/hc/en-us/articles/360013625034-Policy-Third-Party-Programs)). That is
why this tool works like this:

- **A separate exe, not a DLL.** Nothing is loaded into the GW2 process: no hook, no memory reading, no changed game
  files. Installing only creates our own folder, a Run entry and shortcuts if you choose them.
- **It only reads what you see,** in the area you marked yourself. MumbleLink, GW2's official interface, tells it
  whether you are on a map and whether the chat line is open.
- **It never sends on its own.** One line per Enter you press. A tool that reads chat and answers by itself would be a
  bot, and there is none of that here, AI or not.
- **"Only copy" mode:** Enter just copies the line. You paste it in GW2 yourself, so not a single synthetic key reaches
  the game.

## Privacy: what leaves your PC

**Pictures never leave your PC.** The chat is read from the screen and recognized locally. Diagnostic pictures
(`captures\`) are off by default, switch themselves off after 15 minutes and are deleted after 3 days. They show other
players' chat, so do not share them. Our own windows and dialogs are kept out of the capture.

**Text goes only to the translator you chose** (Settings → Technical shows where texts go right now). That includes the
chat lines of **other players** that you translate. MyMemory is a public translation memory that may store what it
receives; the tool says so once. A local model keeps everything on your PC. LanguageTool receives what you type only
while the grammar check is on. API keys are stored encrypted for your Windows account.

**What the tool learns stays local.** It learns only from the messages *you* send, never from other players' chat.
Settings → Writing → *Delete everything learned* starts from scratch.

## Files

Next to the installed exe (`%LOCALAPPDATA%\Programs\GW2ChatTranslator`), or in `%LOCALAPPDATA%\GW2ChatTranslator` if
that folder is not writable:

| File | Content |
|---|---|
| `gw2-chat-translator.ini` | settings (keys encrypted) |
| `learned\learned_<lang>.txt` | words, pairs and triples learned from what you sent (safe to delete) |
| `my-gw2-words.txt` | words that are never marked as errors |
| `my-words.txt`, `corrections.txt`, `ocr-fixes.txt` | your words, corrected translations, learned recognition fixes (shareable) |
| `cache\gw2names_<lang>.tsv` | official names from the GW2 API, refreshed every 14 days |
| `rapid\` | RapidOCR models (Latin ships with the program, other scripts are downloaded on request) |
| `captures\` | diagnostics only, see above |

## Limits

- Only what GW2 shows in its chat panel can be read; a minimized chat shows nothing.
- Text recognition is very good at 4K and still makes mistakes with small text (1080p); names can contain errors.
  `/r` (reply to the last whisper) is always safe.
- Without a hook there is no other source for all channels. arcdps "unofficial extras" offers exact text only for
  party/squad (and needs a DLL in the game). An optional add-on for that is on the roadmap, off by default.

## Building

**Visual Studio 2022:** `cmake -S . -B build && cmake --build build --config Release` (ONNX Runtime and the Latin
RapidOCR model are downloaded and checked by CMake).

**MinGW (also from Linux):**

```
cmake -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-win
```

**Tests:** `g++ -std=c++17 -I src tests/core_tests.cpp src/core/*.cpp && ./a.out` runs the core tests on any system
(from the repo root, it reads `tests/data`). The Wine end-to-end harness is described in [CLAUDE.md](CLAUDE.md).

**Translations:** all UI text is English in the code, wrapped in `Tr()`. The German and Arabic tables are
`src/core/i18n_de.cpp` and `src/core/i18n_ar.cpp`. `python3 tools/i18n_check.py` lists missing entries.

## Origin

The idea of a floating input window that sends into the GW2 chat comes from
[Grammarly-support-overlay-for-guild-wars-2](https://github.com/hazratali-uydevelopers/Grammarly-support-overlay-for-guild-wars-2)
(ISC). No code was taken from it; this is a native rewrite in C++/Win32.

## License

MIT, see [LICENSE](LICENSE). Third-party components shipped with the program (ONNX Runtime, the RapidOCR /
PaddleOCR recognition model) keep their own licences: [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt).
