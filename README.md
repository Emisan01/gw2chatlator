# GW2Chatlator for Nexus

The chat of Guild Wars 2 in your language – **read it, write and speak in yours**, the chat gets it translated. A
Nexus add-on, right inside the game. A line goes into the chat only when you press Enter yourself – or, if you switch on Hands free, when you finish speaking.

## ⬇ [Download GW2Chatlator.dll](https://github.com/Emisan01/gw2chatlator/releases/latest/download/GW2Chatlator.dll)

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
  and appears only on a map, never on the character screen. Chat codes (waypoints, items …) are found by
  GW2Chatlator itself and show their kind and code on mouse-over. A click on a waypoint opens its preview: GW2's map
  around it, the points near it, your last ones and a collection of your own (menu ≡ → Waypoints). `/self` in the footer's
  channels, or typed, whispers to yourself: a code for your eyes only, no one gets spammed. Web links open only after a
  question.
- **Write in your language,** the chat gets the translation: you see it (and what it means in your language again)
  before Enter sends it. Longer than one chat line: split into several. Translators: MyMemory (free), Google,
  Microsoft, DeepL, Supertext, your own LibreTranslate server.
- **Typing help that learns you:** grey completion from Windows' word prediction (Space writes it), three suggestions
  from what you write (Tab picks one, Space writes it), autocorrection like a phone keyboard (Backspace undoes it).
  **Right-click in the input:** the words you corrected yourself while typing – learned once, written right from then
  on – and your abbreviations with their meaning for the translator ("finds = I think").
  **Text blocks** (menu ≡): save what you wrote, put it back with a click.
- **Voice input – the first in the Nexus world:** hold the microphone (or your push-to-talk key, GW2 does not get it),
  right-click it to speak until a pause, or let voice activation listen (it pauses while Discord & co. use the
  microphone). Recognition runs **on your PC** (whisper.cpp) in a small helper process on the CPU cores GW2 does not
  use – the game keeps its frames. Each voice model shows its CPU peak and time; one that is too slow hands over to the
  next smaller one. **Speak your language, send another:** what you say follows "Send" like what you type – into
  English on your PC, into other languages through your translator.
- **Safe sending:** waits for each frame of the game, refuses to be used as a bot or spam macro. Optional, off by default:
  send right after you finish speaking – speech pause, push-to-talk release or mouse click.
- **Start in three settings:** Options → Start – one switch for the chat's translation (also Ctrl+Alt+T), then
  *I read in*, *I understand*, *I send* (the same choice as "Send" under the input) and the translator. The other
  pages sit in a side bar like Nexus' own: Chat, Typing help, Voice, Translator, Window, Technical. Everything applies
  at once.
- **Failsafe:** an error switches the add-on off for the session, never the game.
- English, Deutsch, Français, Español, Italiano, Polski, Português, Türkçe, Русский.

## Install (one minute)

1. Install [Nexus](https://raidcore.gg/Nexus) if you do not have it.
2. Put `GW2Chatlator.dll` into `Guild Wars 2\addons`, next to the other add-ons (or unpack the ZIP there: the DLL
   and the folder `GW2Keyboard` beside it).
3. In the game: Nexus → Addons → **GW2Chatlator** → Load. Nothing is downloaded by itself: the empty chat shows the
   modules – **Events: Chat** (reads the chat; typing help works without it) with an "Install" button, and the voice
   module only once you switch voice input on (Options → Voice); its model comes after that, each on your
   click.
4. To remove it again: everything it brought has a remove button – Events: Chat in Options → Technical, the voice
   module and the voice models in Options → Voice – and at the bottom of Options → Technical "Remove everything of
   GW2Chatlator …"; then Nexus → Addons → Uninstall.
5. Translation is yours to switch on: menu ≡ → Options → Start (or Ctrl+Alt+T) – what you send in is chosen there
   too, for typing and speaking alike.

Updating: replace the DLL (or unpack the newer ZIP over the old one) (settings and learned words stay in `addons\GW2Keyboard`).

### What is downloaded when

| What | When | From |
|---|---|---|
| `GW2Chatlator.dll` | you download it, or Nexus updates it | the release of its version |
| Events: Chat | your click on "Install" | its own GitHub release |
| voice module | your click in Options → Voice | the `extras` release |
| voice models | your click in Options → Voice | whisper.cpp on Hugging Face |
| local translation module, language models, M2M100 | your click in Options → Translator | the `extras` release |
| graphics card module (NVIDIA, about 560 MB) | your click in Options → Translator | the `extras` release |
| table of waypoint names (a few hundred KB) | when the first waypoint code is in view, unless chat code names are off in Options → Chat; kept 30 days | the `extras` release |
| map tiles of a waypoint's preview | when the preview shows a point | GW2's public tile server (`tiles.guildwars2.com`) |
| a newer local translation module | your click on "Update local translation module" (shown only when yours is outdated) | the `extras` release |

Every download is checked by SHA-256 – the one GitHub publishes, for voice models the one built into the add-on.
Apart from that small table, nothing is downloaded in the background.

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
| **Supertext** | own key, paid (as of November 2026: EUR 4.90 a month plus EUR 20 per million characters) | not measured yet | Options → Translator |
| **LibreTranslate** | own server, free | decent | your own address |
| **Local MT (OPUS-MT, NLLB-200, M2M100)** | free | offline after download | Options → Translator: fast Helsinki-NLP pair models or Meta's universal models, on the CPU or, optionally, on an NVIDIA card |

The optional local translator runs CTranslate2 outside GW2. It downloads nothing until you request the helper or a
specific language model. Each model is an independent package; check Options → Translator for the installed models and
remove them there. Model licenses vary and each package includes its own license and attribution: NLLB-200 is
CC-BY-NC-4.0 (non-commercial), M2M100 is MIT, Helsinki-NLP's differ per pair.

On an NVIDIA card from GTX 16 and RTX 20 upwards the optional graphics card module runs the model on the card, about
half the time on the RTX 3070 it was tested on, and leaves the CPU to the game. It is off by default. The options show
where the model runs; without room on the card or on a card it cannot use, the CPU translates. Other makers' cards stay
on the CPU, CTranslate2 only runs on NVIDIA's.

Incoming chat is only ever displayed. Whatever comes back from a translator can never send anything.

## ArenaNet rules and account safety

ArenaNet forbids programs that give an unfair advantage, automate gameplay, allow unattended play or harm others. No
third-party tool is checked or approved by ArenaNet, so using one is always at your own risk
([third-party policy](https://help.guildwars2.com/hc/en-us/articles/360013625034-Policy-Third-Party-Programs)). That is
why GW2Chatlator works like this:

- It is a Nexus add-on like the others in the Nexus library. It reads no game memory itself: the chat comes from the
  Nexus event of Events: Chat. The voice recognizer runs in its own small process outside the game.
- **It never sends on its own.** One line per Enter you press – or, only if you switch Hands free on (off by default),
  one line when you finish speaking (pause, release of the push-to-talk key or a mouse click). A tool that reads chat
  and answers by itself would be a bot, and there is none of that here, AI or not.
- **GW2's own map flight, only if you switch it on (off by default):** a click of yours on a point in the waypoint
  preview whispers its code to yourself and clicks that link once with the real mouse. GW2's chat line stays in that
  whisper; a channel chosen in the footer comes back with your next message. One message and one synthetic click per click of yours, nothing after it: your decision, at your own risk
  under ArenaNet's rules. The map is never dragged, game memory is never touched.
- **"Only copy" mode:** Enter just copies the line. You paste it in GW2 yourself, so not a single synthetic key reaches
  the game.
- It never stores other players' chat. (Raidcore: what you do counts – translating is fine, collecting chat data is
  not.)

## Privacy: what leaves your PC

**Your voice never leaves your PC** – recognition runs locally, nothing is recorded to disk.

**Text goes only to the translator you chose,** and only while translation is switched on – that includes the chat
lines of other players you translate. MyMemory is a public translation memory that may store what it receives. API
keys are stored encrypted for your Windows account.

**Names of chat codes:** to show a code's name and details the add-on asks GW2's public API (`api.guildwars2.com`): only
the code's number and your reading language go out, never a chat text or a name. Waypoints come from a table in this
project's `extras` release. Options → Chat turns both off; then a code shows its kind. The preview's map comes from GW2's
public tile server (`tiles.guildwars2.com`): only the tile's numbers go out.

**What it learns stays local.** It learns only from the messages *you* send, never from other players' chat.
Options → Writing → *Delete everything learned* starts from scratch.

**Nothing of other players' messages is kept or exported.** No chat log is saved; a player's language is known only
for the session, and so are the translations of chat lines (kept in memory so the same line is never
paid twice, gone when the game closes). "Copy protocol" (Options → Technical, for a bug report) holds versions and counts only – how often
the last lines broke which rule – never a text, a name, a channel or a length.

## Files

In `Guild Wars 2\addons\GW2Keyboard\`:

| File | Content |
|---|---|
| `gw2-chat-translator.ini` | settings (keys encrypted) |
| `learned\` | words, pairs and triples learned from what you sent, your typical typos (safe to delete) |
| `my-words.txt`, `voice-fixes.txt` | your words and abbreviations, words the recognizer misheard |
| `gw2-terms.txt` | GW2 terms never translated: yours on top (right-click in the input → "A GW2 term", or edit the file; several words are one term, a note may follow after a tab), the built-in list below with what each means (rewritten by every version) |
| `my-gw2-words.txt`, `my-names.txt` | words never marked as spelling errors, names you taught |
| `snippets.txt` | your text blocks (menu ≡ → Text blocks) |
| `models\` | voice models (Options → Voice: use, remove, open the folder) |
| `gw2kb_voice.exe`, `voice\` | the voice module |
| `gw2kb_translate.exe`, `vcomp140.dll` | optional local MT helper (CTranslate2 for OPUS-MT, NLLB-200 & M2M100); installed from Options → Translator |
| `gw2kb_translate_gpu.exe`, `cublas*.dll`, `cudart64_12.dll` | optional graphics card module for NVIDIA cards; installed from Options → Translator |
| `poi-<lang>.tsv` | names and positions of waypoints in your reading language (fetched once, kept 30 days, safe to delete) |
| `poi-history.txt`, `poi-collection.txt` | the waypoints you flew to and the ones you keep, as numbers only (safe to delete) |
| `translate\<pair>\` | individual local translation models downloaded on request |

## Building

**Visual Studio 2022+:** `cmake -S . -B build && cmake --build build --config Release --target GW2Chatlator gw2kb_voice`
– the add-on lands in `build\addons\nexus-keyboard\Release\GW2Chatlator.dll`, the voice module in its `GW2Keyboard\` folder
(whisper.cpp is fetched and checked by CMake).

To build the optional native local translator, configure a separate directory with
`cmake -S . -B build-localmt -DGCT_LOCAL_MT=ON`, then build `gw2kb_translate`. `python tools/pack_release.py`
creates the helper ZIP when it exists. Convert and package model assets with
`python tools/package_local_mt_models.py --convert --pairs de-en en-de`; attach the generated ZIPs as release assets
for the same version. Conversion downloads the selected Helsinki-NLP checkpoints and writes them under the ignored
`local/translation-spike/models` folder.

**Tests:** `nexus_kb_selftest.exe <GW2Chatlator.dll>` loads the add-on outside the game (a real ImGui context, a
stand-in Nexus API) and types, sends and unloads; `core_tests` runs the core on any system
(`g++ -std=c++17 -I src tests/core_tests.cpp src/core/*.cpp`). More in [AGENTS.md](AGENTS.md).
During development, run checks for the changed areas with `python tools/verify_release.py --changed`; unrelated test
suites are skipped. A release requires `python tools/verify_release.py --full`: from a clean commit it builds every
artifact, runs all eight gates and writes the receipt `build/dist/verification.json`.

**Translations:** all UI text is English in the code, wrapped in `Tr()`. The tables (German, French, Spanish,
Italian, Polish, Portuguese, Turkish, Russian) are `src/core/i18n_<code>.cpp`. `python3 tools/i18n_check.py` lists missing entries.

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
