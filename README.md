# GW2Chatlator for Nexus

The chat of Guild Wars 2 in your language – **read it, write and speak in yours**, the chat gets it translated. A
Nexus add-on, right inside the game. A line goes into the chat only when you press Enter yourself.

## ⬇ [Download gw2keyboard_nexus.dll – version 2.0.4](https://github.com/Emisan01/gw2chatlator/releases/download/v2.0.4/gw2keyboard_nexus.dll)

Put the DLL into your `Guild Wars 2\addons` folder (where [Nexus](https://raidcore.gg/Nexus) keeps its add-ons) and
load **GW2Chatlator** in Nexus. Nothing is downloaded by itself: the window shows what is there and what you can add
with one click – **Events: Chat** (to read the chat) first, the voice module and a voice model only if you switch voice
input on.
all versions: [Releases](https://github.com/Emisan01/gw2chatlator/releases) · what changed: [CHANGELOG.md](CHANGELOG.md).

> **New in 2.0** – a window made of building blocks with two profiles (**compact view**, **mini mode**: nothing but one
> input line with its word prediction), sharp text in pt, themes (Slate, Grey, or your own colours with automatic
> contrast), the grey word that finishes yours even without the word bar, translation that applies the moment you set
> it, and a mouse logic without dead ends. **Nothing downloads by itself** – every module on your click – and
> **everything it brought can be removed again** with one button. Sending is hardened: only ever on your Enter, at most
> three chat lines per Enter, no way for another add-on to send through it.
>
> Every change, version by version: [CHANGELOG.md](CHANGELOG.md).

Checked by VirusTotal: [the 1.8.0 DLL – 0 of 71 scanners](https://www.virustotal.com/gui/file/24127b749fc15c3d4afa630a5c7e19b0f3e7001ead8f331637445a80eaabbe58).

> The green **Code → Download ZIP** button only contains the source code, not the add-on – use the link above.

## What it does

- **Every channel in your language:** map, whispers, guild, party, squad – foreign lines are translated as they come
  in (switch it on in the options, or click a single line); hover a line for the original. The chat comes as exact
  text from **Events: Chat** by Vonsh, an add-on in the Nexus library.
- **A chat window built like GW2's own:** tabs at the top – the chat and up to five whisper tabs (click a name, or
  right-click the header to open one); fold it up to its header (V) and open it again (A); it snaps to the screen edges
  and appears only on a map, never on the character screen. Waypoints and items show their name, map and details on
  mouse-over via Decoder Ring; a click copies the code (`[&...]`) to your clipboard, Shift+click puts it into your message.
  Web links open only after a question.
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
2. Put `gw2keyboard_nexus.dll` into `Guild Wars 2\addons`, next to the other add-ons (or unpack the ZIP there: the DLL
   and the folder `GW2Keyboard` beside it).
3. In the game: Nexus → Addons → **GW2Chatlator** → Load. Nothing is downloaded by itself: the empty chat shows the
   modules – **Events: Chat** (reads the chat; typing help works without it) with an "Install" button, and the voice
   module only once you switch voice input on (Options → General → Modules); its model comes after that, each on your
   click.
4. To remove it again: Options → General → Modules has a remove button for everything it brought (Events: Chat, the
   voice module, the voice models) and "Remove everything of GW2Chatlator …"; then Nexus → Addons → Uninstall.
5. Translation is yours to switch on: menu ≡ → Options → Translator (the chat, your text), Options → Voice (speech
   into English).

Updating: replace the DLL (or unpack the newer ZIP over the old one) (settings and learned words stay in `addons\GW2Keyboard`).

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
why GW2Chatlator works like this:

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
keys are stored encrypted for your Windows account. Item and waypoint names are resolved locally or cached via Decoder Ring.

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
