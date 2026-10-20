# CLAUDE.md — GW2 Keyboard for Nexus

A Nexus add-on (C++17, MSVC x64, ImGui 1.80 from Nexus) – the chat of Guild Wars 2 in your language, typing help,
voice input. This repository holds only the add-on. The old desktop programs (GW2 Keyboard exe, GW2 Chat Translator,
the OCR reader, the GW2 Chat Bridge) live in the branch `desktop-archive` and are not part of main (user, 2026-10-14);
an OCR module may come back later as an optional download – not now. Read this before changing anything. Several
agents/LLMs work on this repo: keep changes small, build + test before every commit, update this file when you learn
something the next agent needs.

## Layers (dependencies point downwards only)

```
addons/nexus-keyboard   the add-on
  src/kb_main.cpp       the Nexus glue only: load / unload, keybinds, callbacks, first start, test exports
  src/kb_shared.hpp     what the modules share: types, `extern` state, function declarations (keep its order: it was
                        generated from the one kb_main.cpp, things are declared before they are used). Its bigger
                        pieces stand in their own headers, included by it at their place: kb_worker.hpp (the worker
                        thread), kb_voice_helper.hpp, kb_cost.hpp (the frame profiler), kb_chatsource.hpp – a .cpp
                        includes kb_shared.hpp, never those directly
  src/kb_state.cpp      the shared state, layout load / save, window profiles (`ProfileOn` / `SetProfile`), languages
  src/kb_theme.cpp      themes (CBA too), the text font, chat colours, LookText, icons
  src/kb_input.cpp      the input: soft breaks, the grey word, Tab / Space, words under the mouse, your corrections
  src/kb_worker.cpp     worker jobs: spelling, translation, typing help, sending
  src/kb_voice.cpp      the voice helper (idle unload after [Voice] UnloadMinutes), push to talk, models
  src/kb_chat.cpp       incoming chat, tabs, Events: Chat, chat codes and links
  src/kb_chatsource.cpp the guard between Events: Chat and us: `OnChatEvents`, the shield (`CopyLineGuarded`:
                        VirtualQuery first, SEH last), the pause, reading on-off by hand, the technical details
  src/kb_window.cpp     the main window: header, chat, preview, word bar, input, footer, minimise / restore
  src/kb_waypoint.cpp   the waypoint "quantum macro" (optional) and the click-test key – see Invariant 1
  src/kb_options.cpp    the options window and the modules list
  src/chat_links.*      chat codes: "[Item name]" in rarity colour + mouse-over details via Decoder Ring;
                        a click (`HandleChatLinkClick`) minimises the window and opens GW2's chat line (GameBind
                        GB_UiChatFocus) – the user clicks the original there; back when the map closed again
                        (waypoint) / the chat line closed (`g_redirect`, MumbleLink), at most 45 s. A waypoint older
                        than 15 s: the game map opens (GB_MapToggle) and the status names the spot (`ChatLinks::Where`).
                        No add-on can trigger the game's own map jump – only a click in GW2's chat does.
  src/chat/             vendored ChatLinks codec + SpecData (PieUI/Decoder Ring, MIT)
  src/chat/DecoderRingApi.h  public ABI for Decoder Ring (PieOrCake, MIT)
  src/voice_module.*    loads the voice module (helper + whisper DLLs) from this version's GitHub release
  src/voice_helper_main.cpp  gw2kb_voice.exe: whisper outside the game process (stdin/stdout JSON lines)
  src/chat_events.hpp   our own declaration of Vonsh's "Events: Chat" event (static_asserts)
  third_party/          Dear ImGui 1.80, Nexus.h (MIT)
src/app                 config (the ini), spell_service (completion, autocorrection, learned words, typos)
src/kbd                 voice_session (WASAPI + whisper worker), whisper_engine (delay-loaded whisper.dll)
src/win                 Windows services without UI: http (WinHTTP), translators (MyMemory, DeepL, Google,
                        Microsoft, LibreTranslate), els (language detection), gw2_sender, mumble_link, spellcheck,
                        text_prediction, keyboard_layout, audio_capture, mic_sessions, cpu_cores, voice_models,
                        extras_install (GitHub release assets, SHA-256 checked), secret (DPAPI), files, hash,
                        ocr (Windows.Media.Ocr through the raw WinRT ABI – the waypoint macro)
src/core                portable logic, NO windows.h: text, json, i18n (+ de / ar tables), chat_line (channels),
                        chat_code, protect, slang, word_model, typo_memory, my_words, voice, voice_learn, send_guard,
                        chat_source (the rules for lines of Events: Chat), window_layout (the window's place as a
                        formula), link_find (a waypoint's line among recognized rows), translator protocols
```

- `core` compiles on any OS: `g++ -std=c++17 -I src tests/core_tests.cpp src/core/*.cpp`.
- Translation backends implement `core/translator.hpp` (thread-safe, blocking, called from worker threads; `keep`
  segments come back unchanged; set `quotaExceeded` when a free contingent is used up).

## How the add-on works

- **Threads:** the render callback only draws and copies (everything shared sits behind `g_mu`). Translations,
  spelling, sending run on `g_worker` (below normal priority). Chat codes are resolved via Decoder Ring, the voice
  helper its own process. `Worker::Stop(last)` runs the cleanup (COM / WinRT objects) on the worker before the DLL goes
  – without it the add-on crashed on unload.
- **Failsafe:** every callback runs behind try/catch + SEH (`SafeRender`, `SafeOptions`); an error sets `g_disabled`
  for the session, never takes the game down.
- **Chat source guard** (user, 2026-10-19: Nexus updates Events: Chat over night, unasked; updates and compatibility
  are Nexus' and Vonsh's business, ours is never to crash and never to show garbage – no on / off logic per version):
  `OnChatEvents` (kb_chatsource.cpp) copies each line in `CopyLineGuarded` – the only code that reads the other
  add-on's memory: it looks first (`VirtualQuery`: committed, readable, no guard page – catching a guard page fault
  would take another thread's stack guard), pointers outside user memory are never touched, strings bounded, SEH as the
  last net. Every copy is checked (`core/chat_source`: hard rules = what garbage does and the real source never does –
  type, pointer, UTF-8, missing end, control characters, name > 40; the time, the account pattern and flags are notes
  only: Vonsh's time source is not public). Reading pauses for the session on unreadable memory or when 5 of the last
  20 lines fail; "Resume" by hand; "Read the chat" off (`[ChatSource] Reading`) touches nothing. For the developer:
  Options > Technical (`RenderChatSourceDetails`) – the version of Events: Chat, new since the last session
  (`[ChatSource] LastVersion`; then a note once), the counts, the last lines as lengths and rules ("Copy protocol":
  never text or names). 2.1.0's probation (marker file, VerifiedVersion / BadVersion) was taken out again; its
  leftovers are removed at load. Other add-ons' code runs only behind a guard too:
  `ReadAddonDefGuarded` (the module scan calls every add-on's GetAddonDef), `ResolveGuarded` (Decoder Ring: a record
  with room to spare, strings ended, one fault = not asked again this session).
- **Waypoint/POI click** ("quantum macro"; user, 2026-10-19/20; `[Nexus] WaypointPoiClick`, off by default, Options >
  Window > More): no add-on can call GW2's own map jump without a hook, so a click on a waypoint or point of interest
  (Decoder Ring's kind `DP_Waypoint` / `DP_PointOfInterest` – never a vista, never another chat code) in our chat
  clicks the original in GW2's chat – only while it can still stand in view (`OriginalStillThere`, the user's "still
  alive" logic: not its age, not a map change – its place from the bottom, at most `kWpNewerLines` (8) lines after
  it; then also when older than 15 s; never a macro into nothing; GW2's chat is never scrolled; whether GW2 emptied
  its chat only the screen tells – no line seen, no click). Independent of our window (user, in-game 2026-10-20):
  `HandleChatLinkClick` neither minimises it nor opens GW2's chat line nor the map – GW2's chat line open = typing
  mode, there a click on a link does not fly the map, so the click happens only while it is closed (MumbleLink).
  `kb_waypoint` asks the render thread for one frame (`WaypointFrameTick`: the swap chain's back
  buffer – the game's picture, before any add-on draws – copied to a staging texture, read two frames later), reads
  it with Windows' own text recognition (tiles; then GW2's usual chat corner enlarged), frees it, finds the very line
  (`FindChatLink`: the link's name from Decoder Ring, then the speaker and the rest of the message; below `kMinFit`
  no click – better none than a wrong one) and clicks its centre once (`SyntheticClick`: our window lets the mouse
  through meanwhile – `WaypointPassThrough` → `ImGuiWindowFlags_NoInputs`, two frames counted before the cursor moves,
  so ImGui / Nexus never keep the click; foreground checked before every step; right before the button goes down the
  point must lie on GW2's own window (`WindowFromPoint`), the cursor where we put it (else the user's hand took over),
  no button held, GW2's chat line closed; the three pauses – before, held, after – each thrown anew within the user's
  from-to range, `[Nexus] WpBefore / WpHeld / WpAfter`, default 3-25 ms, the user's "dice logic", set under the
  checkbox; the cursor goes back). Not found: the code is copied, the user clicks it. Its own thread, ended on
  unload (`StopWaypointMacro`). Decoder Ring's language must be the game's (else no match).
- **Chat in:** Events: Chat (`EV_CHAT:Message`) → `OnChatEvents` (the guard above) → `AddLine` (`UnescapeChatText`, sanitised) →
  translated on the worker only when `[Nexus] TranslateChat=1` (default 0) or a click on the line asks for it – into
  the language you write in (the chat is read in it). `TranslateRecentLines` redoes the newest 20 when the translation
  is switched on, the language you write in or the translator changes, or once from the menu – only the lines in view
  (`g_visibleLines`), never an old chat in full. "Translate the chat once" (`TranslateLine(.., once)`) follows none of
  the chat settings: everything in view that is not already in the language you write in
  (`SureLanguage`, understood languages skipped – Translator tab "Languages I understand", `[Translate] Understood`).
  The detection runs for every line (local, no translator): `g_playerLang` (session only) remembers each player's
  language; their whisper tab then sends in it (`WhisperTab::sendLang` → `g_sendOverride`, "=" = as written; the
  footer's choice in a tab changes only that tab). Nothing of other players is stored (privacy, Raidcore).
- **Writing:** the multi-line input (soft wrap = '\n' in place of a space, `Flat()` turns it back) → `ComputeTyping`
  (word bar, grey completion, what Space writes) → debounce → `TranslateInput` (`MyWords::Expand` first, protected
  segments, back-translation) → Enter → `StartSend` → `SplitForChat` (199) → `SendNow` → `SendToGw2Chat`.
- **Tabs** (`WhisperTab`): whisper tabs (session only, ≤5, a click on a name) and tabs of your own (`custom`: right-click
  on the header → "New tab", a copy of the chat; right-click on such a tab = its settings: name, channels shown,
  channel written in; ≤6, kept in `[NexusTabs]` – settings only, never chat). ImGui id per tab = `uid`.
- **Window:** shown only on a map (MumbleLink `inMap`), never over the open world map (`g_mapOpen`; except
  `g_showOnMapUntil` while it names a waypoint's spot). Header: tabs (Chat, your own, whisper tabs),
  mic · V/A (fold to the header) · ≡ · ×; every part can be hidden (`g_layout`: header, chat, preview, word bar,
  footer) – read them through `Lay()`. "Compact view" and "Mini mode" (menu, and the top of Options → Window →
  Window layout) are presets of the parts (`SetProfile`): a profile is on exactly while the parts match it, changing
  a part switches it off, switching it off brings back the layout from before. No preview: the translation stands
  small in the input.
  Without the header a right-click on the window (not a chat line, not the input) opens the menu `kbmenu`
  with the tabs and "Minimise"; the input's learning popup then has "Menu …". Without the footer its "<" minimise
  button stands left of the input. "Info texts" off hides every grey status line (never the prediction's grey word).
  The window's theme (`Theme`: Nexus / Slate / Grey / Cba, `ColoursOf`, `PushTheme` for everything we draw) is only
  the window – the chat text keeps its channel colours (`kChannelLook`). Compact view never hides the word bar.
  Minimising: the ">" (optional, `minButton`), three quick clicks on the window (not the input), and "Minimise when not
  in use" (`autoMinimize` after `minimizeAfter` s; `g_lastUse` – input, hover, menus, sending, voice, GW2's chat line
  count as use). "Open again when GW2's chat opens" (`autoOpen`) only opens; the focus key always opens; taking over
  the focus stays `focusOnGameChat`. All of it goes through `g_minimizeRequest` / `g_expandRequest`.
  The input grows with its soft-wrapped lines (`g_boxLines`, up to 8) and the window grows upwards when it must
  (bottom kept, shrinks back). Without the word bar the grey word also shows its highlighted completion (what Tab
  takes; Space alone still keeps a valid word). The first Tab takes the highlighted word.
  `GW2KB_SHOT_TYPE=<text>` types into the input before the self-test's picture. Status text is drawn inside the input box (`BoxText`). Compact view = no
  preview / word bar, the translation small in the box. Scroll bar drawn at the left. Snap to the screen edges.
- **Right-click** only in the input with a word you wrote (learning mode; empty input without the header: the menu),
  docked to the window's top right corner (1 px), an X closes it: self-corrections noticed while typing (`NoteSelfFix`) →
  `SpellService::TeachFix` (never a dictionary word), and "my words" (meaning → translator). No menu on chat lines.
- **Nothing downloads by itself** (user, 2026-10-18): `FirstStart` only marks the setup done. The modules list (empty
  chat; Options → Technical for Events: Chat, Options → Voice for the voice module) offers each on a click: Events: Chat first (reading), then – only after voice
  input is switched on (off for new users unless the voice module is already there) – the voice module
  (`voice_module.cpp`: release ZIP via `DownloadReleaseAsset`, tar.exe, only gw2kb_voice.exe + voice\*.dll copied)
  and then a voice model. Unload: the voice helper ends after [Voice] UnloadMinutes without use.
- **Everything removable** (user: what we brought gets a remove button; each in its place, user 2026-10-20):
  Options → Technical – Events: Chat (only while Nexus does not hold it, else the way via Nexus) and reading it;
  Options → Voice – the voice module, the voice models (each with a second click); once, at the very bottom of
  Options → Technical, "Remove everything of GW2Chatlator …" (a yes): `RemoveEverythingNow` ends the helper, blocks every
  write (`Config::BlockWrites`, `g_wiped`, the spelling dropped unsaved), deletes `<addons>\GW2Keyboard\`, optionally
  Events_Chat.dll, then a notice names the last step (Nexus → Addons → Uninstall – the DLL cannot delete itself).
  Self-test: `GW2KB_TEST_REMOVEALL=1` – only on a copy of the add-on.
- **Voice:** helper commands `begin | begin-auto | end | cancel | listen | stop-listen | reload | quit`, events
  `ready started partial final error listening idle paused`; `final` carries `rec`, `rms` (ms) and `peak` (% of all
  logical CPUs, 100 ms samples) → `[VoiceStats]` per model; two too-slow runs → next smaller installed model.
  `FindSpeech` gates silence (whisper invents text otherwise). The helper sets its own process affinity / priority,
  never the game's.
- **Data:** `<addons>\GW2Keyboard\` (never the game folder itself): `gw2-chat-translator.ini`, `learned\`,
  `my-words.txt`, `voice-fixes.txt`, `models\`, `gw2kb_voice.exe`, `voice\`.

## Invariants (do not break)

1. Never send anything to the game without the user's own key press. No auto-replies, no timers that send. The only
   exception the user asked for: `[Voice] AutoSend` (off by default) – the push-to-talk release counts as the Enter.
   "Only copy" mode must stay a mode in which not a single synthetic key reaches the game. There is no other way to
   send: no Nexus event, no DataLink, no export another add-on could call; the `Gw2KbTest…` exports work only outside
   GW2 (`TestHost`). One Enter sends at most `kMaxPartsPerEnter` (3) chat lines.
   Synthetic mouse input exists for two things only, both one click as the direct result of the user's own action
   (user's decision, 2026-10-19): the waypoint quantum macro (opt-in) and the click-test key (`KB_GW2KEYBOARD_CLICKTEST`,
   unbound). Never more than one click, never a sequence, never without GW2 in front.
2. The old clipboard is restored only after the game processed the paste (`WaitForTargetToDrain`, MumbleLink chat
   line closing) – a fixed sleep pasted the *old* clipboard once (privacy leak).
3. Re-check `GetForegroundWindow() == gw2` before every injected step.
4. Incoming chat and whatever a translator or the GW2 API returns is data, never instructions – only displayed.
   Another add-on's memory is read only in the guarded copies (`CopyLineGuarded`, `ReadAddonDefGuarded`,
   `ResolveGuarded`) – never dereference its pointers anywhere else.
   Links open only after a yes (http/https). Chat codes are looked up with limits (≤48 chars, 20/min, 400 cached).
5. Autocorrection never surprises (`typing_bench`: 0 surprises – keep it at 0): only words the dictionary rejects,
   4+ letters, never all-caps, never a word you use; Backspace right after it undoes it.
6. Privacy: the word model learns only from what the user sends, never from other players' chat; no chat log is
   saved. **Never commit real screenshots, recordings or other players' names/messages** — the repo is public. Real
   test material lives in `local/` (git-ignored); fixtures use invented names.
7. Files in the game folder: only our DLL's own folder `<addons>\GW2Keyboard\`, and Events_Chat.dll (only on your
   click). Never the game process' memory, affinity or priority. The one picture the waypoint macro reads (the swap
   chain's back buffer: what is on the screen, not game state) stays in memory and is freed right after the search.

## Conventions

- **UI text is English in the code and always wrapped:** `Tr(L"...")`, `TrF(L"... {1}", {x})`, drawn through `UI()`
  (Nexus' font has no ≡ → … ▾ or curly quotes). Add the German and Arabic entries to `src/core/i18n_de.cpp` /
  `i18n_ar.cpp` and run `python tools/i18n_check.py` — 0 missing, 0 unused.
- INI is ASCII only (`AsciiEscape`/`AsciiUnescape`). API keys: `ProtectSecret` (DPAPI).
- All Win32 calls use the explicit `W` variants. Status markers in text are ASCII (`[OK]`, `[--]`).

## Pitfalls already hit

- ImGui 1.80 multi-line input: Enter needs `CtrlEnterForNewLine` + the char filter dropping '\n'; Tab needs
  `AllowTabInput` and is handled in the char filter (`g_pick`); Space writes the grey word in the filter (`SpaceWrites`).
- `SetSelected` on a tab takes effect next frame – `g_selectTab` stays until the tab reports itself.
- Sending: every key is held `KeyHoldMs` (30 ms, or one frame + 10 ms); down+up in one `SendInput` got lost. A
  whisper: "/w " typed, the name pasted + Tab, then the message pasted.
- `small` and `near` are macros (rpcndr.h / windef.h); `FoldString` is a macro → `CaseFold`. `GetPrivateProfileInt` turns negatives into 0.
- Python/heredoc edits turned `\n` / `\r` escapes into real control characters more than once – check C++ string
  literals after scripted edits (or use the Edit tool).
- Never minimise or move the window from inside a child (the chat area): ask with `g_minimizeRequest` /
  `g_expandRequest`, done on the main window. The window's rect is set in one place only – `PlaceWindow`, before
  `Begin`, from the formula; no `SetWindowPos` / `SetWindowSize` anywhere else (our own hold-drag moves the corner:
  `DragWindow`).
- The ini: every `WritePrivateProfileString` rewrites the whole file (~15 ms; "Save" in the options = 70 of them =
  the game hung ~1 s). The add-on runs `Config::DeferWrites(true)`: `SaveValue` only queues, `KickSave` (render, every
  0.5 s at most) has the worker write them, unload flushes the rest. Never write the ini directly on the game thread.
- Soft breaks: '\n' stands for a space, "\r\n" for nothing (a word longer than the line); `Flat()` turns them back,
  `RawOffset()` maps flat offsets into the box (every `g_edit`). '\r' never comes from the user (the filter drops it).
- Fitted window (no chat): hold the left button ¼ s and drag = move; dragging its top/bottom edge sets
  `inputLines`; right-click on a word = learning (`WordAtMouse`), elsewhere = the menu.
- After changing a struct in a shared header (app/config.hpp, kb_shared.hpp) build with `--clean-first`: MSBuild
  once left objects with the old layout (the self-test failed in strange places until a clean build).
- The window's place is a formula (`core/window_layout`): four numbers – the bottom-left corner, the width, the big
  window's height – give each frame's rect per mode (big: at least what its content needs, so a long input grows it
  and it shrinks back; fitted: its parts; the button). `AdoptRect` takes over only what changed against the rect the
  window had, in the mode it had – never a grown height as yours. Every incremental "move by the size change" before
  it crept (2.0.3 slid down in game). The equations are in core_tests (fixed point, shared corner, a drop lands where
  dropped, snapping twice = once, grow and shrink back, mini on/off), the self-test checks the real window.
- A hold never survives a new shape (big / fitted / button): `PlaceWindow` ends ours (`g_winHolding`, our own drag
  `g_ownDownAt`) and ImGui's own move (`g.MovingWindow`) on every mode change. Before, a press on a link that
  minimised the window had ImGui drag the button to where the big window was grabbed, and a press left over from
  before minimising made the opened window follow the mouse – both read back by the formula as the user's move (in
  game: "after a few clicks the window stood at the top"). Every size we give ImGui is the formula's, in whole
  pixels (ImGui floors a window's size; a raw button size of Nexus' font crept a pixel a frame).
- The formula must never ask ImGui for less than it allows: whole pixels (ceil), and at least `WindowMinSize`
  (`WinSpace::minSize*`) – ImGui enlarges a smaller window and the corner moves (the button and the mini window's
  first frame did). Read the window before `Begin` from `SizeFull`: `SetWindowSize` writes it, `Size` follows only
  at the next `Begin`. The self-test keeps no imgui.ini.
- Sending clears the learning-mode state (`g_lastWords`, `g_finishedLast/At`) with the input – the box never shows
  the empty text in between, and the old state blocked self-corrections in the next message.
- The CBA theme: eight roles (`g_cbaRoles`, [NexusTheme] Colours = a theme code), everything else derived in
  `ColoursOf`; `CbaShown` keeps text 4.5:1 and quiet text / accents 3:1 on every fill (core/contrast: WCAG with sRGB
  linearisation, unit-tested). A pasted theme code is data: `ParseThemeCode` accepts only its exact form.
- A function with `__try` must not hold C++ objects that need unwinding (C2712): keep the guarded copies plain (POD
  out-parameters, no std::string), do the rest outside. `try/catch` does not catch access violations (/EHsc).
- Web links: a Discord-like question only, no lookups in the background (the Cloudflare check sent every domain).
- Visual Studio may report itself incomplete after an update; then pin `CMAKE_GENERATOR_INSTANCE` with `,version=`.

## Testing

- Build: `cmake -S . -B build && cmake --build build --config Release`.
- `build\Release\core_tests.exe` – 0 failed.
- `build\addons\nexus-keyboard\Release\nexus_kb_selftest.exe <dll>` – loads the add-on with a real ImGui context and a
  stand-in Nexus API, types (wrap, Space, Tab, Enter in two parts, history, a self-correction), unloads: PASS. It
  writes `[Nexus] SetupDone=1` first (no downloads in tests).
- Look at the window without the game: `GW2KB_SHOT=local\shots\x.bmp` (optional `GW2KB_SHOT_ALPHA=150`,
  `GW2KB_SHOT_BRIGHT=1`) makes the self-test raise chat lines of every channel (invented names) and rasterize the
  frame into a BMP (software, no GPU). Chat text: `LookText` (GW2 palette `kChannelLook`: name strong, message light;
  a dark halo = 4 diagonal copies; `[NexusWindow] Glow / Brightness / GameColors`).
- The self-test poses as Events: Chat itself (it exports GetAddonDef with Vonsh's signature, `g_fakeChatVersion`)
  and as a broken Decoder Ring (`FakeDataLink`): a version new since the last session, a shifted layout, unreadable
  memory, a guard page (it must stay a guard page), endless strings, 3000 random lines, the pause and "Resume", reading
  off, the protocol, the version remembered after the unload. Without the look-first and the SEH the process crashes
  there (checked). The smoke
  test checks that all 500 realistic lines pass the guard (a rule too strict would pause reading for everyone).
- The window's corner against random walks: 400 steps (minimise, open, mini mode, clicks, a press held while the
  shape changes and the mouse moves, fractional frame heights) – the bottom-left corner must never move;
  `GW2KB_TEST_SEED=<n>` walks another way (it found the stale own-drag press on its first run).
- Self-test and smoke test refuse to run while GW2 runs (exit 3): they would read its MumbleLink, and a test must
  never reach a game. `FindGw2Window(pid)` returns only that process' window, never another GW2.
- `build\Release\typing_bench.exe [n]` after every change to the typing help: SURPRISES must stay 0.
- `build\Release\voice_bench.exe model lang threads wav…` with recordings in `local\voice`.
- Not verifiable here: the game itself – test in GW2 with Nexus (copy the DLL + `GW2Keyboard\` into `addons`).

## Releases & Release-Norm (Verbindlicher Standard)

`project(... VERSION)` in CMakeLists.txt → version.h → the add-on's definition. The package `GW2Keyboard-Nexus.zip`:
`gw2keyboard_nexus.dll` at the root, `GW2Keyboard\` with `gw2kb_voice.exe`, `voice\*.dll`, README.txt,
THIRD_PARTY_NOTICES.txt. The voice module installer downloads `GW2Keyboard-Nexus.zip` of the release tagged like the
DLL's version (else the latest) – every release must carry it.

**Verbindliche Stufenprüfung (Normierung vor jedem Release für alle):**
1. **Gate-Audit:** `python tools/verify_release.py` muss alle 8 Gates (Version, Nexus-Def, Unit-Tests, Selftest, Typing-Bench, i18n, Binary, Smoke-Stresstest) mit 0 Fehlern bestehen.
2. **Pre-Release Staging:** Neue Versionen werden auf GitHub grundsätzlich ZUERST als `Pre-Release` (oder Draft) angelegt – niemals direkt als ungeprüfter Main/Latest-Release.
3. **Schleifenprüfung & Sichtcheck:** Kein Release wird für alle Spieler (`Latest`) freigeschaltet, bevor der Maintainer die Version selbst in Ruhe in Guild Wars 2 mit Nexus im Spiel gesehen und verifiziert hat.
4. **Tempo & Demut:** Keine voreiligen Schnellschüsse oder übertriebenes KI-Selbstvertrauen. Wenn der Maintainer müde, krank oder zeitlich gebunden ist, bleibt die Version im Test-Status, bis ein ausgeruhter Check erfolgt ist.
5. **Kein Push / Release ohne explizite Freigabe des Maintainers.**
