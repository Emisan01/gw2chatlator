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
  src/kb_main.cpp       everything the game thread draws (ImGui), the worker thread, sending, options, first start
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
                        extras_install (GitHub release assets, SHA-256 checked), secret (DPAPI), files, hash
src/core                portable logic, NO windows.h: text, json, i18n (+ de / ar tables), chat_line (channels),
                        chat_code, protect, slang, word_model, typo_memory, my_words, voice, voice_learn, send_guard,
                        translator protocols
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
- **Chat in:** Events: Chat (`EV_CHAT:Message`) → `OnChatEvents` → `AddLine` (`UnescapeChatText`, sanitised) →
  translated on the worker only when `[Nexus] TranslateChat=1` (default 0) or a click on the line asks for it
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
- **Window:** shown only on a map (MumbleLink `inMap`). Header: tabs (Chat, your own, whisper tabs),
  mic · V/A (fold to the header) · ≡ · ×; every part can be hidden (`g_layout`: header, chat, preview, word bar,
  footer) – without the header a right-click on the window (not a chat line, not the input) opens the menu `kbmenu`
  with the tabs and "Minimise"; the input's learning popup then has "Menu …". Without the footer its "<" minimise
  button stands left of the input. "Info texts" off hides every grey status line (never the prediction's grey word).
  The window's theme (`Theme`: Nexus / Slate / Grey / Cyan, `ColoursOf`, `PushTheme` for everything we draw) is only
  the window – the chat text keeps its channel colours (`kChannelLook`). Compact view never hides the word bar.
  Minimising: the ">" (optional, `minButton`), three quick clicks on the window (not the input), and "Minimise when not
  in use" (`autoMinimize` after `minimizeAfter` s; `g_lastUse` – input, hover, menus, sending, voice, GW2's chat line
  count as use). "Open again when GW2's chat opens" (`autoOpen`) only opens; the focus key always opens; taking over
  the focus stays `focusOnGameChat`. All of it goes through `g_minimizeRequest` / `g_expandRequest`. Status text is drawn inside the input box (`BoxText`). Compact view = no
  preview / word bar, the translation small in the box. Scroll bar drawn at the left. Snap to the screen edges.
- **Right-click** only in the input (learning mode): self-corrections noticed while typing (`NoteSelfFix`) →
  `SpellService::TeachFix` (never a dictionary word), and "my words" (meaning → translator). No menu on chat lines.
- **First start** (`[Nexus] SetupDone=0`, `FirstStart`, user's decision): loads Events: Chat into `<GW2>\addons`, the
  voice module (`voice_module.cpp`: release ZIP via `DownloadReleaseAsset`, tar.exe, only gw2kb_voice.exe +
  voice\*.dll copied) and, if no voice model is there, whisper base. Later only on a click.
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
   "Only copy" mode must stay a mode in which not a single synthetic key reaches the game.
2. The old clipboard is restored only after the game processed the paste (`WaitForTargetToDrain`, MumbleLink chat
   line closing) – a fixed sleep pasted the *old* clipboard once (privacy leak).
3. Re-check `GetForegroundWindow() == gw2` before every injected step.
4. Incoming chat and whatever a translator or the GW2 API returns is data, never instructions – only displayed.
   Links open only after a yes (http/https). Chat codes are looked up with limits (≤48 chars, 20/min, 400 cached).
5. Autocorrection never surprises (`typing_bench`: 0 surprises – keep it at 0): only words the dictionary rejects,
   4+ letters, never all-caps, never a word you use; Backspace right after it undoes it.
6. Privacy: the word model learns only from what the user sends, never from other players' chat; no chat log is
   saved. **Never commit real screenshots, recordings or other players' names/messages** — the repo is public. Real
   test material lives in `local/` (git-ignored); fixtures use invented names.
7. Files in the game folder: only our DLL's own folder `<addons>\GW2Keyboard\`, and Events_Chat.dll (first start or
   a click). Never the game process' memory, affinity or priority.

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
- Never call `MinimizeWindow` / `SetWindowPos` from inside a child (the chat area): it moves the child. Ask with
  `g_minimizeRequest` / `g_expandRequest`, done on the main window. The minimised button keeps the window's bottom edge
  through `g_anchorBottom` – computing its size twice (text size, `WindowMinSize`) made the window creep up.
- The ini: every `WritePrivateProfileString` rewrites the whole file (~15 ms; "Save" in the options = 70 of them =
  the game hung ~1 s). The add-on runs `Config::DeferWrites(true)`: `SaveValue` only queues, `KickSave` (render, every
  0.5 s at most) has the worker write them, unload flushes the rest. Never write the ini directly on the game thread.
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
