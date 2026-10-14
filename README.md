# GW2 Keyboard for Nexus

The chat of Guild Wars 2 in your language – **read it, write and speak in yours**, the chat gets it translated. A
Nexus add-on, right inside the game. A line goes into the chat only when you press Enter yourself.

**[⬇ Download GW2 Keyboard for Nexus (ZIP)](https://github.com/Emisan01/gw2chatlator/releases/latest/download/GW2Keyboard-Nexus.zip)**
– unpack it into your `Guild Wars 2\addons` folder (where [Nexus](https://raidcore.gg/Nexus) keeps its add-ons) and
load **GW2 Keyboard** in Nexus (version 1.7.5). Checked by VirusTotal: [1.7.5 ZIP, 2 of 66 scanners](https://www.virustotal.com/gui/file/2d32ac0cbda127bd44f47704cfeb75e2b1175c0aa438adf8bce4cab2467c0455)
– both are machine-learning guesses (DeepInstinct, Rising "AI"/RDML), common for new unsigned programs; no signature
scanner flags it (SHA-256 `2d32ac0c…2467c0455`). All versions: [Releases](https://github.com/Emisan01/gw2chatlator/releases)
· what changed: [CHANGELOG.md](CHANGELOG.md).

> **New in 1.7.5** – the chat window you can play with instead of GW2's own:
> whisper tabs in the header (click a name, or right-click to open one) · item links like in the game – the name in its
> rarity colour, details on mouse-over · fold it up to its header with one click · translation only when you want it ·
> a learning mode for the words you keep correcting yourself · the voice module sets itself up on the first start and
> shows how much CPU each voice model needs.

> The green **Code → Download ZIP** button only contains the source code, not the add-on – use the link above.

## What it does

- **Every channel in your language:** map, whispers, guild, party, squad – foreign lines are translated as they come
  in (switch it on in the options, or click a single line); hover a line for the original. The chat comes as exact
  text from **Events: Chat** by Vonsh, an add-on in the Nexus library.
- **A chat window built like GW2's own:** tabs at the top – the chat and up to five whisper tabs (click a name, or
  right-click the header to open one); fold it up to its header (V) and open it again (A); it snaps to the screen edges
  and appears only on a map, never on the character screen. Item links show as the game shows them – the name in its
  rarity colour, icon, type and description on mouse-over; web links open only after a question.
- **Write in your language,** the chat gets the translation: you see it (and what it means in your language again)
  before Enter sends it. Longer than one chat line: split into several. Translators: MyMemory (free), Google,
  Microsoft, DeepL, your own LibreTranslate server.
- **Typing help that learns you:** grey completion from Windows' word prediction (Space writes it), three suggestions
  from what you write (Tab picks one, Space writes it), autocorrection like a phone keyboard (Backspace undoes it).
  **Right-click in the input:** the words you corrected yourself while typing – learned once, written right from then
  on – and your abbreviations with their meaning for the translator ("finds = I think").
- **Voice input – the first in the Nexus world:** hold the microphone (or your push-to-talk key, GW2 does not get it),
  right-click it to speak until a pause, or let voice activation listen (it pauses while Discord & co. use the
  microphone). Recognition runs **on your PC** (whisper.cpp) in a small helper process on the CPU cores GW2 does not
  use – the game keeps its frames. Each voice model shows its CPU peak and time; one that is too slow hands over to the
  next smaller one.
- **Safe sending:** waits for each frame of the game, refuses to be used as a bot or spam macro. Optional: send right
  after speaking (off by default – releasing the push-to-talk key counts as your Enter).
- **Failsafe:** an error switches the add-on off for the session, never the game.
- English, Deutsch and العربية.

## Install (one minute)

1. Install [Nexus](https://raidcore.gg/Nexus) if you do not have it.
2. Unpack the ZIP into `Guild Wars 2\addons`: `gw2keyboard_nexus.dll` lies next to the other add-ons, the folder
   `GW2Keyboard` beside it.
3. In the game: Nexus → Addons → **GW2 Keyboard** → Load. On the first start it loads **Events: Chat**, its voice
   module and a small voice model by itself, and tells you what is ready.
4. Translation is yours to switch on: menu ≡ → Options → Translator (the chat, your text), Options → Voice (speech
   into English).

Updating: unpack the newer ZIP over the old one (settings and learned words stay in `addons\GW2Keyboard`).

## Keys

| Key | Does |
|---|---|
| Enter | send (or copy in "only copy" mode); a long message goes out in several parts |
| Ctrl+Enter | a line break in the input |
| Space / Tab / Shift+Tab | write the grey word / pick a word of the word bar (Space writes it) |
| Backspace right after an autocorrection | undo it (and learn the word) |
| Up / Down in the empty input | what you sent (this session) |
| Ctrl+Alt+K (Nexus → Keybinds) | show / hide the window |
| your push-to-talk key (Options → Microphone) | speak while you hold it – GW2 does not get the key |
| right-click on the microphone | speak until a pause |

## Translators: free is enough, better is possible

| | Cost | Quality | Notes |
|---|---|---|---|
| **MyMemory** | free, no account | decent | 5,000 characters/day, 50,000 with any e-mail address (a made-up one works) |
| **DeepL, Google, Microsoft** | own key (free contingents) | very good | Options → Translator |
| **LibreTranslate** | own server, free | decent | your own address |

Incoming chat is only ever displayed. Whatever comes back from a translator can never send anything.

## ArenaNet rules and account safety

ArenaNet forbids programs that give an unfair advantage, automate gameplay, allow unattended play or harm others. No
third-party tool is checked or approved by ArenaNet, so using one is always at your own risk
([third-party policy](https://help.guildwars2.com/hc/en-us/articles/360013625034-Policy-Third-Party-Programs)). That is
why GW2 Keyboard works like this:

- It is a Nexus add-on like the others in the Nexus library. It reads no game memory itself: the chat comes from the
  Nexus event of Events: Chat. The voice recognizer runs in its own small process outside the game.
- **It never sends on its own.** One line per Enter you press. A tool that reads chat and answers by itself would be a
  bot, and there is none of that here, AI or not.
- **"Only copy" mode:** Enter just copies the line. You paste it in GW2 yourself, so not a single synthetic key reaches
  the game.
- It never stores other players' chat. (Raidcore: what you do counts – translating is fine, collecting chat data is
  not.)

## Privacy: what leaves your PC

**Your voice never leaves your PC** – recognition runs locally, nothing is recorded to disk.

**Text goes only to the translator you chose,** and only while translation is switched on – that includes the chat
lines of other players you translate. MyMemory is a public translation memory that may store what it receives. API
keys are stored encrypted for your Windows account. Item names come from the public GW2 API (only the item's number is
sent).

**What it learns stays local.** It learns only from the messages *you* send, never from other players' chat.
Options → Writing → *Delete everything learned* starts from scratch.

## Files

In `Guild Wars 2\addons\GW2Keyboard\`:

| File | Content |
|---|---|
| `gw2-chat-translator.ini` | settings (keys encrypted) |
| `learned\` | words, pairs and triples learned from what you sent, your typical typos (safe to delete) |
| `my-words.txt`, `voice-fixes.txt` | your words and abbreviations, words the recognizer misheard |
| `models\` | voice models (Options → Voice: use, remove, open the folder) |
| `gw2kb_voice.exe`, `voice\` | the voice module |

## Building

**Visual Studio 2022+:** `cmake -S . -B build && cmake --build build --config Release --target gw2keyboard_nexus gw2kb_voice`
– the add-on lands in `build\addons\nexus-keyboard\Release\`, the voice module in its `GW2Keyboard\` folder
(whisper.cpp is fetched and checked by CMake).

**Tests:** `nexus_kb_selftest.exe <gw2keyboard_nexus.dll>` loads the add-on outside the game (a real ImGui context, a
stand-in Nexus API) and types, sends and unloads; `core_tests` runs the core on any system
(`g++ -std=c++17 -I src tests/core_tests.cpp src/core/*.cpp`). More in [CLAUDE.md](CLAUDE.md).

**Translations:** all UI text is English in the code, wrapped in `Tr()`. The German and Arabic tables are
`src/core/i18n_de.cpp` and `src/core/i18n_ar.cpp`. `python3 tools/i18n_check.py` lists missing entries.

## Dedicated to Kassiopeia 🐢

This tool is dedicated to Kassiopeia, the tortoise from Michael Ende's *Momo* who sees a little into the future and
shows it, quietly, in glowing letters on her shell – just before it happens. That is what the grey suggestion tries
to be: calm, never pushy, one step ahead. And like her, it is on the side of those who want to keep their time, not
of the grey gentlemen who steal it: every key press it saves is yours.

## Origin

The idea of a floating input window that sends into the GW2 chat comes from
[Grammarly-support-overlay-for-guild-wars-2](https://github.com/hazratali-uydevelopers/Grammarly-support-overlay-for-guild-wars-2)
(ISC). No code was taken from it; this is a native rewrite in C++.

## License

MIT, see [LICENSE](LICENSE). Third-party components shipped with the add-on (whisper.cpp, Dear ImGui, the Nexus
headers) keep their own licences: [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt).
