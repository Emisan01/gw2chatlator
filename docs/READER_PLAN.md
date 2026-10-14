# READER_PLAN — GW2 Keyboard as the core, reading the chat as modules

Status 2026-10-14: the user decided this plan. Stages 1 and 2 are built (tested with pipe stand-ins, not yet in the
game with a real reader + keyboard together). The reader mode needs a new GW2 Chat Translator release: the installed
1.4.0 does not know `--reader` and would start as the old full tool.

## Update 2026-10-14: every channel exactly – "Events: Chat"

Delta (Raidcore, the Nexus developer) pointed to **"Events: Chat" by Vonsh** (Nexus library id 0x67770263,
https://github.com/jsantorek/GW2-Chat): a Nexus add-on that raises `EV_CHAT:Message` for **every** chat channel
(map, say, whisper incl. "from me", guild, party, squad, squad messages, team). On the rules he said: what you do
counts, not how you get it – translating is fine, collecting chat data is the problem (privacy). We translate and
keep nothing of other players (invariant 10).

So the bridge listens to both: Events: Chat (src `chat`, every channel) and Unofficial Extras (src `extras`,
party/squad; left out in the add-on while Events: Chat delivers). With Events: Chat the keyboard drops every OCR
line, and OCR becomes optional: one switch turns the whole OCR module on or off (Settings → General). OCR stays useful
later for reading other areas of the screen.

## Layers (user, 2026-10-14)

| Layer | What it is | Reads | Runs |
|---|---|---|---|
| **Core** | GW2 Keyboard | nothing of the chat – typing help, voice, translators, sending | outside the game |
| **Module A** (optional) | Unofficial Extras + our Nexus add-on GW2 Chat Bridge | **party and squad only**, exact text | the add-on in the game (Nexus), passes lines on |
| **Module B** (optional) | the OCR reader | **everything visible in the chat area you chose** (text recognition on this PC) | outside the game |
| **A + B** | hybrid | A: party/squad, B: the rest (map, whisper, guild, say, team …) | the keyboard puts both together |

There is no separate "full version" any more: the main download is the keyboard, every module is loaded from it
(Settings → General → extensions). Each module says in one line what it reads – people must be able to see exactly
what is being read before they switch anything on.

Without Nexus + Unofficial Extras there is no bridge: the OCR reader then reads party and squad as well.

## Why not more from the add-on

The add-on only gets what Unofficial Extras hands over (party, squad). Anything else of the chat could only come from
hooks into the game's text drawing or its memory – the line we decided not to cross (Unofficial Extras itself is
closed source at ArenaNet's request). No trick moves text "past the picture" without such a hook – that is exactly
what OCR does from outside. Map chat, whispers and the rest stay with the OCR reader.

## One pipe, one direction, several senders

- The keyboard runs the pipe server `\\.\pipe\GW2ChatBridge` (user-only DACL, local only) with **several instances**,
  so the add-on and the reader can be connected at the same time.
- Every sender only writes; the keyboard never answers. This keeps the add-on in the game unsteerable (invariants 1,
  4), and the reader needs nothing back:
  - **Own lines** ("did I send this?") are classified in the keyboard, which knows what it sent.
  - **Our window must never be read** (invariant 5). While a reader is connected, the keyboard keeps its own window
    out of screen captures (`WDA_EXCLUDEFROMCAPTURE`), unless "show my windows on screenshots" is on. Then the reader
    must not trust what it reads where our window is: the keyboard marks the lines it shows itself and drops readings
    of them (`ShowsTranslation`-style check).
  - Dialogs of the keyboard are already kept out of captures.
- Protocol (`core/bridge_protocol`): `hello` carries `src` (`extras` | `ocr`) and `ex` (Unofficial Extras loaded).
  `chat` carries `src` and the channel as text: `party`, `squad`, `map`, `say`, `whisper`, `guild`, `team`, `system`,
  plus `out` for a whisper you sent ("To Name: …"). Older add-ons (no `src`) count as `extras`.

## Reading party/squad only once (hybrid)

1. **As early as possible:** when the add-on says Unofficial Extras is loaded, the reader is told
   (setting `[Reader] SkipPartySquad`, the reader follows it) and skips rows in party/squad colour **before** text
   recognition – the row colour is known from the line grid already. This saves real CPU.
2. **In the keyboard:** OCR lines of party/squad are dropped while an extras bridge with `ex=true` is connected (not
   only after its first line).
3. **Safety net:** an OCR line that is very similar (Dice ≥ 0.8) to a bridge line of the last 30 s is dropped,
   whatever channel its colour said.

OCR next to Unofficial Extras (user, 2026-10-14): when the bridge runs with Unofficial Extras, the keyboard **offers
prominently** to use the OCR reader only for "translate once" (it exists already) instead of reading all the time.
"Read all the time" (hybrid: the rest of the channels) stays a choice.

## Heavy channels

The reader reads only the area you chose. A channel that scrolls all the time (WvW combat log) would keep the CPU
busy: channel selection (which channels are wanted), a maximum reading rate, the row cache (unchanged rows cost
nothing), and a warning when the area looks like a combat log.

## Where the reading surfaces in the keyboard

- Stage 1: the existing band above the preview shows every incoming source.
- Stage 3: an expandable chat log in the keyboard (`ChatLogView` reused: tabs, whisper tabs, click = translate,
  correction memory), so nothing of the full tool's reading UI is lost.

## Stages

1. ✅ **Keyboard core accepts several senders.**
   - Pipe with several instances.
   - Protocol: `src`, all channels, `out`.
   - Hybrid drop in the keyboard (`ex=true` + similarity).
   - Self-exclusion from captures while a reader is connected.
   - Settings → General: one line per module saying what it reads.
2. ✅ **Reader mode in GW2 Chat Translator** (`--reader`).
   - Started by the keyboard's "Activate chat translation"; the keyboard keeps running.
   - Hidden in the tray, never shown by itself; no typing help, no translator, no own add-on pipe.
   - Lines go untranslated through `win/bridge_client` into the keyboard's pipe.
   - The window position is not saved (the settings file is shared with the keyboard).
   - The existing OCR pipeline sends its messages into the keyboard's pipe instead of its own log when the keyboard
     runs (`[Reader] ToKeyboard`).
   - It skips party/squad rows when the keyboard reports an extras bridge (via the shared ini, no back-channel).
   - Stays a separate exe; reuses everything measured so far.
3. **Chat log in the keyboard** (expandable, tabs), the band stays for the compact view.
4. **GW2 Chat Reader as its own small exe:**
   - The reading part of GW2 Chat Translator without the typing help and log: tray, area picker, reading settings.
   - "Load extension" in the keyboard then fetches it.
   - The old full download goes. Users of the full tool are moved over: installed keyboard + reader, autostart and
     learned words kept.
5. **Hybrid polish:** the reader's own skipping of party/squad rows before recognition, the heavy-channel guard, and
   the default for OCR next to Unofficial Extras.
