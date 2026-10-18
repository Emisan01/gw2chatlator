# GW2Chatlator for Nexus

The chat of Guild Wars 2 in your language – **read it, write and speak in yours**, the chat gets it translated. A
Nexus add-on, right inside the game. A line goes into the chat only when you press Enter yourself.

## ⬇ [Download gw2keyboard_nexus.dll – version 2.0.0](https://github.com/Emisan01/gw2chatlator/releases/download/v2.0.0/gw2keyboard_nexus.dll)

Put the DLL into your `Guild Wars 2\addons` folder (where [Nexus](https://raidcore.gg/Nexus) keeps its add-ons) and
load **GW2Chatlator** in Nexus. Nothing is downloaded by itself: the window shows what is there and what you can add
with one click – **Events: Chat** (to read the chat) first, the voice module and a voice model only if you switch voice
input on.
all versions: [Releases](https://github.com/Emisan01/gw2chatlator/releases) · what changed: [CHANGELOG.md](CHANGELOG.md).

> **New in 2.0.0** – rebuilt inside, shaped by you: lean inside: the add-on is split into modules, the voice model is freed after 10 minutes without
> use · "Compact view" and "Mini mode" are profiles of the window's parts (Options → Window, and the menu) · the learning
> window opens beside the window and can be dragged · held and dragged, the input moves the normal window too.

> **New in 1.9.13** – mini mode without dead ends: hold and drag moves it, its top edge sets the lines, a right-click
> on a word is the learning mode for that word, anywhere else the menu · long words (codes, links) break in the line
> too, and are sent whole.

> **New in 1.9.12** – the "Learn your corrections" window docks to the window's top right corner (no more jumping to
> the screen edge), opens only with a word you wrote, and has an X to close it.

> **New in 1.9.11** – "Translate the chat once" on its own: what you see of the chat goes into the language you write in,
> whatever the chat settings say, and again in the new language after you changed it · only what is in view is
> translated, never an old chat in full · the CBA theme: your own colours with automatic contrast and a code to share.

> **New in 1.9.10** – two views in the menu: "Mini mode" (nothing but the input, one line that grows) and "Compact
> view" (header, chat, input, footer – no word bar, no preview); your own settings of the parts stay underneath.

> **New in 1.9.9** – what you set applies at once: switching on "Translate the chat" translates what is already there,
> "Save" translates the text you have written, a new language you write in translates the chat again · "Translate
> the chat once" in the menu · three quick clicks minimise the mini mode too.

> **New in 1.9.8** – a real mini mode: without the chat the window is exactly its parts – down to nothing but the grey
> input (with the word bar on, its top is the window's top) · no more creeping when the window was a little too small.

> **New in 1.9.7** – the prediction without the word bar: the grey word finishes your word (Tab takes it) · the
> input grows with a longer text (up to 8 lines) and falls back once it is sent · the window steps aside on the
> world map like GW2's chat · the Cyan theme is gone.

> **New in 1.9.6** – out of the way when you do not need it: "Minimise when not in use" (after the seconds you set) and
> "Open again when GW2's chat opens (Enter)" – both optional · three quick clicks on the window minimise it · the
> minimise button can be hidden · the Cyan theme only colours buttons and text now.

> **New in 1.9.5** – a window made of building blocks, and themes: a new options tab "Window" with every part to
> switch on or off (header, chat, preview, word bar, footer, info texts) · "Prediction only" when nothing translates ·
> themes for the window itself – Slate, Grey, Cyan or Nexus' own · compact view keeps the word bar and the lines you set.

> **New in 1.9.4** – the header can be hidden too: Options → General → Window layout → Header. Without it a right-click
> on the window opens the menu (switch tabs, minimise, options, bring the header back).

> **New in 1.9.3** – sharp text in pt, the input stays at the bottom: the text size is set in pt (8–28) and the font is
> built at that size (Segoe UI) instead of being scaled up blurry · with the chat (and the footer) hidden the input is
> the lowest line and the window may be small, just the input.

> **New in 1.9.2** – no more hang on "Save", every slider works at once: the settings are written in the
> background (the game froze for about a second) · text size, transparency, brightness and the dark outline act
> immediately, transparency goes up to 100 % (the chat field fades with it, or no background at all) · the preview has
> the same field as the input · "Save" no longer turns back what you changed in the footer meanwhile.

> **New in 1.9.1** – the window stays where you put it, links like Discord, chat codes open the original:
> minimising and opening no longer moves the window (also at the screen edge) · web links ask "This link goes to … –
> open it?", no background checks · a click on a chat code opens it in GW2's own chat (the game's own map jump for
> waypoints); older waypoints open the game map with a hint where to look.

Checked by VirusTotal: [the 1.8.0 DLL – 0 of 71 scanners](https://www.virustotal.com/gui/file/24127b749fc15c3d4afa630a5c7e19b0f3e7001ead8f331637445a80eaabbe58).

> **New in 1.9.0** – Chat scroll trap fix & native mini-chat alignment:
> smooth mouse wheel scrolling (instant release of bottom anchor on upward scroll) ·
> dynamic bottom bar offset flush with native GW2 speech bubble across all UI scales and resolutions ·
> magnetic mini-button `<` snap congruently over native chat icon.

> **In 1.8.8** – Window geometry failsafe & fail-fast network timeouts:
> window geometry clamp overrides corrupted/squished legacy `imgui.ini` sizes (guaranteed 520 × 360 startup) ·
> strict 2000ms receive/connect timeout for background Cloudflare DoH security check eliminates any worker stalling ·
> includes all v1.8.7 Nexus GitHub autoupdate & defense-in-depth security hardening features.

> **In 1.8.7** – Nexus Autoupdate integration & security hardening:
> automatic updates via Nexus (`UP_GitHub`) directly from GitHub releases ·
> defense-in-depth security hardening against Unicode spoofing (bidi overrides `\u202E`, zero-width characters stripped) ·
> Punycode / IDN homoglyph phishing detection (`xn--...`) ·
> strict URL sanitization before browser launch ·
> bound link safety cache and auto-translation rate limiting against chat floods.

> **In 1.8.6** – uBlock-style scam/phishing filter, native chat link redirection & UI polish:
> clickable web links with local scam/phishing detection (`[Phishing]`) and URL shortener warning (`[Shortener]`) ·
> Level-2 Cloudflare Security DoH (1.1.1.2) background threat check & Cloudflare Radar inspection ·
> native chat link redirection: clicking in-game waypoints, items, skills or builds opens the native chat with full engine features (map pan, 3D preview) ·
> pixel-perfect bottom-bar snapping aligned with the native minimal chat button across all UI scales ·
> upward-opening footer popups and 0-allocation idle performance for rock-solid framerates.

> **In 1.8.5** – FPS-adapted automatic timing & performance optimization:
> dynamic key hold & inter-key step delays based on measured game FPS ·
> auto-disables automatic timing when adjusting manual sliders in Technical tab ·
> eliminated unnecessary foreground focus switches and delays when GW2 is already foreground ·
> cleaned up legacy desktop OCR and dead tabs logic.

> **In 1.8.4** – official rename to **GW2Chatlator** & Raidcore alignment:
> Nexus add-on definition and window titles updated to **GW2Chatlator** ·
> all understood language checkboxes can now be toggled freely (including English) ·
> new option "Do not translate the language I write in" to automatically skip chat lines in your typing language ·
> Technical tab displays version 1.8.4 cleanly.

> **In 1.8.1** – clean chat codes via Decoder Ring & honest fallback:
> chat messages parsed with `ChatLinks::SegmentLine` ·
> item, skill, trait, recipe, waypoint and build tooltips via Decoder Ring (rarity colours, stats, descriptions, icons) ·
> removed synthetic mouse-drag map focus ·
> click copies the link code (`[&...]`) directly to the clipboard, Shift+click inserts it into your message.

> **In 1.8.0** – the keyboard that stays in the background until it is needed:
> tick the languages you understand – only French, Spanish … is translated by itself, a click translates any line ·
> a click on a name opens a whisper tab that **answers in that player's language** · **item tooltips like the game's**
> (food effects with duration, weapon and armour stats, rune bonuses, crafting discipline) in your game's language ·
> **tabs of your own**: right-click the header for a copy of the chat with the channels you choose.

> **In 1.7.6** – it looks and links like GW2's own chat:
> the name in the channel colour, the message light, a dark glow around the letters (readable on snow and sky) and a
> brightness of your own · **waypoints** show their name and map, a click opens the world map right on the spot ·
> item details read better · the chat stays at the bottom while names of items arrive.

> **In 1.7.5** – the chat window you can play with instead of GW2's own:
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
4. Translation is yours to switch on: menu ≡ → Options → Translator (the chat, your text), Options → Voice (speech
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
