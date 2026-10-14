# NEXUS_PLAN — GW2 Chat Bridge (optional Nexus add-on)

Status 2026-10-14:
- The add-on and the receiver in GW2 Chat Translator are built.
- `bridge_selftest.exe` runs the whole chain without the game and passes.
- Not tested in the game yet.

Decision (user, 2026-10-14): map chat, whispers and the other channels **stay with the OCR reader**. Extras is closed
source (at the request of ArenaNet's community managers), and building our own chat hook would break the rules we want
to keep. The bridge only takes what Unofficial Extras offers: party and squad.

## Why

OCR reads every channel but makes mistakes. arcdps **Unofficial Extras** (Krappa322) gets the party and squad chat
from the game as exact text. A small Nexus add-on can pass those lines on to our programs, which then translate
them without any OCR error. The add-on is small and does very little.

Map chat, whispers, guild and the rest stay with OCR for now (see "Later" at the end).

## Rules (on top of CLAUDE.md)

1. **A separate, optional download.** The add-on lives in the game process. GW2 Keyboard and GW2 Chat Translator
   stay external programs that never touch the game (invariant 1). Nothing in the programs needs the add-on.
2. **The add-on only reads, and only writes into the pipe.**
   - It never sends a key or a chat line.
   - It never reads from the pipe, so the programs and anything else outside cannot tell it to do something.
   - It has no ImGui window and no settings. It logs connect/disconnect in the Nexus log.
3. **The game thread only copies.** `OnChatMessage` builds one JSON line and puts it in a small queue (256 lines,
   or 20 while no program listens). Our own thread does the pipe I/O. Exceptions never reach the game.
4. **Incoming chat is data** (invariant 4). The programs show and translate it and never act on it.
5. **Privacy.**
   - The pipe is created by the program with a DACL for the current user only.
   - The client connects with `SECURITY_IDENTIFICATION`, so the pipe's owner cannot act as the game process.
   - Lines are not written to disk by the add-on.
   - Fixtures use invented names (invariant 10).

## How the line gets to us

```
GW2 ─► arcdps ─► Unofficial Extras ─► Raidcore "ArcDPS Integration" (Nexus add-on)
          Events_Raise("EV_UNOFFICIAL_EXTRAS_CHAT_MESSAGE", const ChatMessageInfo*)
                 │
                 ▼
        GW2 Chat Bridge (gw2chat_bridge.dll)   Events_Subscribe, copy → queue → writer thread
                 │  \\.\pipe\GW2ChatBridge   one JSON line per message (core/bridge_protocol)
                 ▼
        GW2 Chat Translator / GW2 Keyboard     pipe server thread → UI thread → translate → show
```

- **Event:** Raidcore's integration subscribes to Unofficial Extras with `ExtrasSubscriberInfoV2`. It relays
  `ChatMessageCallback` as the Nexus event `EV_UNOFFICIAL_EXTRAS_CHAT_MESSAGE`. The payload is
  `ChatMessageInfo*`, which is `SquadMessageInfo` in newer `Definitions.h`. The layout is the same and is checked
  by a `static_assert` on 72 bytes.
- **Why not subscribe to Unofficial Extras directly?** Extras calls `arcdps_unofficial_extras_subscriber_init` only
  on add-ons that arcdps loaded. A Nexus add-on is not one of them. The relay is the way Nexus add-ons do it; cba4gw2
  uses the same pattern for combat events.
- **Requirements for players:** Nexus, arcdps (loaded by Nexus), Unofficial Extras, and Raidcore's ArcDPS
  Integration. All four are common in the Nexus library. Without them, the add-on just never receives a line.

## Protocol (core/bridge_protocol, version 1)

UTF-8, one JSON object per line, `\n` at the end, at most 8192 bytes.

```
{"v":1,"t":"hello","addon":"1.5.0"}
{"v":1,"t":"chat","ch":"squad","sub":-1,"bc":false,"time":"2026-10-14T11:45:24.888Z",
 "acc":":Invented.1234","char":"Nora Testwind","text":"..."}
```

- `ch` is `party` or `squad`. Party messages sent while you are in a squad arrive as `squad`; that comes from
  extras itself.
- `sub` is the subgroup 0–14, or -1 for the whole squad or party. `bc` is a commander broadcast.
- The reader drops anything unknown, wrong in version, malformed, too long, or without text.
- The text is cut at 1024 bytes, always on a character boundary.

## Phases

1. ✅ **Scaffold** (2026-10-14).
   - `addons/nexus`: CMake target `gw2chat_bridge` (MSVC, static CRT, option `GCT_NEXUS_BRIDGE`).
   - Pinned MIT headers in `third_party/`:
     - `Nexus.h` from RaidcoreGG/GW2Nexus-AddonTemplate (same content as RCGG-lib-nexus-api HEAD, API 6).
     - Unofficial Extras `Definitions.h` and `KeyBindStructs.h` at commit `33bd9ac5`.
   - `GetAddonDef`, Load/Unload, event subscription, writer thread, hello.
   - `core/bridge_protocol` with tests.
2. ✅ **Receiver in GW2 Chat Translator** (2026-10-14).
   - `win/bridge_pipe` and `MainWindow::OnBridge` are done.
   - Party/squad OCR lines are dropped once the bridge has delivered a line (`BridgeCarriesParty`).
   - The Technical page shows the bridge status.
   - Settings → Game & start: "Read party and squad chat from the Nexus add-on" (`[Reader] Bridge`, both programs).
   - GW2 Keyboard: the newest 3 party/squad lines (2 in the compact view) in a slim band above the preview.
     - The name is shown, and the text in the channel colour, translated into the writing language when its language
       is sure and not understood.
     - A click switches between translation and original. Own lines are left out. Lines leave after 5 minutes.
2b. ✅ **Add-on options page** (Nexus → Addons → GW2 Chat Bridge, ImGui 1.80 like Nexus).
   - It shows what is there: ArcDPS, Unofficial Extras, Nexus' ArcDPS Integration, the program, and the lines passed
     on.
   - If Extras is missing, the button "Install Unofficial Extras" does what the Nexus library does for its add-ons:
     - It takes the newest GitHub release of Krappa322, only `arcdps_unofficial_extras.dll`, only from that repository.
     - The download is checked against the SHA-256 GitHub reports and goes to `<GW2>\addons`.
     - Nexus loads it and hands it to ArcDPS.
   - Unofficial Extras is not in the Nexus library (checked 2026-10-14; only ArcDPS itself is, as Nexus' special case
     with its own updates). ArcDPS and the ArcDPS Integration stay Nexus' business – we never touch them.
   - The hello message carries `ex` (Extras loaded). The keyboard then says where to install it.
   Original design notes:
   - `win/bridge_pipe`: a server thread with `CreateNamedPipeW` (message mode is not needed because lines are
     framed). It uses a DACL for the current user from `ConvertStringSecurityDescriptorToSecurityDescriptorW`
     ("D:P(A;;GA;;;<user SID>)"), reads lines and passes each one to the UI thread with `PostMessage`. One client at a
     time; reconnects are handled.
   - **GW2 Chat Translator:** bridge lines become `ChatMessage`s with channel Party/Squad and `exact=true`, and go
     into the incoming pipeline (`HandleIncoming`, so cache, `SureLanguage` and the translator apply as for OCR
     lines). While the bridge is live (a hello was received and lines arrive), OCR lines of the party/squad channels
     are dropped, so nothing appears twice. The footer and the technical page show "Bridge: connected (1.5.0)".
   - **GW2 Keyboard:** has no chat log today. Option: a slim "incoming" strip above the preview (last 3
     party/squad lines plus their translation; click = whisper via `/w`, never sent automatically). This needs the
     user's decision; it changes the keyboard's "no reader" shape.
   - Own lines (the account matches MumbleLink's identity or the last sent lines) are not translated again.
3. **Release.**
   - The DLL goes as its own asset `gw2chat_bridge.dll` plus a short README.
   - Later, possibly listed in the Nexus library (Raidcore hosting gives a positive signature and auto-update; until
     then `UP_None`, because our repo's release tags belong to the programs).
   - A VirusTotal link goes with each release.
4. **Live test** with a real party/squad (the user and friends): order, special characters, subgroups, broadcasts,
   joining and leaving the squad, game restart, program started before or after the game.

## Later (decide at the end, user 2026-10-14)

- **Guiding the user, both ways** (user, 2026-10-14, to be decided later). Each side should be able to load the
  other.
  - From the game: the options page of the add-on offers a "Start GW2 Keyboard" button. It only acts on a click and
    never starts anything by itself, and it needs the program to be installed. It could also offer to download the
    keyboard if it is missing.
  - From the program: the menu offers "Get the Nexus bridge". It puts the add-on into <GW2>\addons, found by the
    file scan, never through the game process. Unofficial Extras goes in the same way. A restart of the game may be
    needed when an add-on cannot be hot-loaded.

- **More channels from Unofficial Extras.** The user is thinking about extending extras (its own project) so that map
  chat and whispers are also reported. Then OCR would only be needed for guild chat and whatever is left, or not at
  all. The protocol already carries `ch` as a string, so new channels are only new values plus the reader side.
- **NPC chat:** extras V3 has `ChatMessageCallback2` with `NpcMessageInfo`. Raidcore's relay forwards only V2 today.
- **A Blish HUD module** as an alternative host: same protocol, same pipe.
- **Presence check:** the add-on could report whether Unofficial Extras is loaded. The module name or its language
  event at startup would be candidates. Only useful once the programs show a status for it.
