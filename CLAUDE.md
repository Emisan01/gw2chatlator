# CLAUDE.md — GW2 Keyboard for Nexus

A Nexus add-on (C++17, MSVC x64, ImGui 1.80 from Nexus) – the chat of Guild Wars 2 in your language, typing help,
voice input. This repository holds only the add-on. The old desktop programs (GW2 Keyboard exe, GW2 Chat Translator,
the OCR reader, the GW2 Chat Bridge) live in the branch `desktop-archive` and are not part of main (user, 2026-10-14);
an OCR module may come back later as an optional download – not now. Read this before changing anything. Several
agents/LLMs work on this repo: keep changes small, build and run the affected checks before every commit using the
change-scoped policy below, update this file when you learn something the next agent needs. **This file and the code never contradict each other** (user, 2026-10-22): when they
do, report it – never quietly change one of them to match the other. Every change of behaviour updates this file in
the same commit.

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
  src/kb_options.cpp    the options window and the modules list
  src/kb_local_mt.cpp  local translation helper and per-pair model downloads/removal
  src/chat_links.*      chat codes: "[Item name]" in rarity colour + mouse-over details via Decoder Ring (outdated
                        since 2026-10-22: finding the codes is ours – `SegmentLine`; our own resolver replaces it,
                        Phase 2a). A click on a
                        chat code does nothing (2.3.1, user: the click logic – copy, GW2's chat line, the waypoint
                        "quantum macro" with its screen reading and synthetic mouse – crashed the game in 2.3.0 and is
                        gone; copying was in the way too). No add-on can trigger the game's own map jump.
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
                        extras_install (GitHub release assets, SHA-256 checked), local_mt_packages, secret (DPAPI), files, hash
src/core                portable logic, NO windows.h: text, json, i18n (+ de / ar tables), chat_line (channels),
                        chat_code, protect, slang, word_model, typo_memory, my_words, voice, voice_learn, send_guard,
                        chat_source (the rules for lines of Events: Chat), window_layout (the window's place as a
                        formula), snippets (your text blocks), send_route (where a message goes: the footer's
                        "Send", a whisper tab, "Translate what I say" – one pure function), steady (Technical's numbers),
                        translate_policy (whether a chat line goes to the translator – one pure function),
                        translation_cache (the session's chat translations, memory only), translator protocols,
                        `local_mt_protocol` (JSON lines, OPUS-MT routes and output validation)
```

- `core` compiles on any OS: `g++ -std=c++17 -I src tests/core_tests.cpp src/core/*.cpp`.
- Translation backends implement `core/translator.hpp` (thread-safe, blocking, called from worker threads; `keep`
  segments come back unchanged; set `quotaExceeded` when a free contingent is used up).
- MyMemory: its machine translation only (`&onlyprivate=1` without a key – no public memory). Its public memory
  answered with other sentences' translations (misaligned corpora, quality 0: "ah alles klar" → "All these great
  people.", "Wer kommt mit?" → "Seen it?"; user, 2026-10-22).
- **Local OPUS-MT** (`Engine::Local`, INI value `local`): CTranslate2 + SentencePiece in the separate
  `gw2kb_translate.exe` child process; int8 on CPU, two worker threads per model, below-normal process priority,
  at most two loaded pairs (LRU), idle exit after `[Translate] UnloadMinutes` (default 10). The add-on sends newline
  JSON through bounded pipes; a request times out after 5 s, a helper fault becomes a translation error, and the
  existing send guard never sends the untranslated text after an error. The helper uses a kill-on-close Windows Job
  Object when the host permits it; otherwise timeout, removal and normal shutdown still stop it explicitly. Installed
  model-pair discovery is cached for at most 1 s. `local_mt_protocol` selects an installed direct pair first, then at
  most two pairs through English. Marian source input ends with `</s>`; multilingual models receive their required
  source prefix from the installed `pair.json`. Hallucinated music-note artifacts absent from the input are stripped;
  output must still contain a word, while other unexpected decorative symbols remain a validation error. Models load
  only on first use.
- Local helper and each individual model pair are offered in Options → Translator and download only on the user's
  click. `DownloadReleaseAsset` checks GitHub's SHA-256 digest. The helper release asset is
  `GW2Keyboard-LocalMT.zip`; model assets are `GW2Keyboard-OPUS-MT-<pair>.zip` and unpack below
  `<addons>\GW2Keyboard\translate\<pair>`. Model packaging uses `tools/package_local_mt_models.py` against
  converted folders under ignored `local/translation-spike/models`; `--convert` fetches the exact Helsinki-NLP model
  named in the core catalog and converts it to int8. The package builder emits only converted pairs that exist.
  CTranslate2, oneDNN and SentencePiece notices travel with the helper; each OPUS-MT model carries its own model-card
  license, attribution, upstream URL and conversion notice. Do not assume one license covers the model family:
  `en-de` and `ru-en` currently declare CC-BY-4.0; this catalog stores each model's exact declared license.

## How the add-on works

- **Threads:** the render callback only draws and copies (everything shared sits behind `g_mu`). Translations,
  spelling, sending run on `g_worker` (below normal priority). Chat codes are resolved via Decoder Ring, the voice
  helper its own process. `Worker::Stop(last)` runs the cleanup (COM / WinRT objects) on the worker before the DLL goes
  – without it the add-on crashed on unload.
- **Failsafe:** every callback runs behind try/catch + SEH (`SafeRender`, `SafeOptions`); an error sets `g_disabled`
  for the session, never takes the game down. Every thread of ours too (since the 2.3.0 crash came from one outside
  that net): `Shielded(...)` (kb_main `ShieldedCall`: catches faults only – a C++ exception goes on to its catch) around
  every worker job (after a fault the worker runs nothing but the last cleanup job), the voice helper's reader, the
  downloads, the "open link / folder" threads. The add-on DLL is built with `/EHa` (the rest with `/EHsc`): a caught
  fault still unwinds, so `g_mu` and every lock are given back – nothing hangs the game after it. A new thread gets its
  `Shielded` too. The self-test faults a worker job holding `g_mu` on purpose (`Gw2KbTestFault`).
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
  counts only – never a text, a name or a length). 2.1.0's probation (marker file, VerifiedVersion / BadVersion) was taken out again; its
  leftovers are removed at load. Other add-ons' code runs only behind a guard too:
  `ReadAddonDefGuarded` (the module scan calls every add-on's GetAddonDef), `ResolveGuarded` (Decoder Ring: a record
  with room to spare, strings ended, one fault = not asked again this session).
- **The waypoint click is gone** (2.3.1): the opt-in "quantum macro" (2.2.0–2.3.0: one frame of the game read with
  Windows' text recognition, then a synthetic click on the original link in GW2's chat, on its own thread) crashed the
  game on a link click once Decoder Ring named the links – a thread outside `SafeRender`'s net. If it ever comes back:
  every thread of ours behind its own SEH, and in the branch history (`experiment/waypoint-macro`, tag v2.3.0).
- **Chat in:** Events: Chat (`EV_CHAT:Message`) → `OnChatEvents` (the guard above) → `AddLine` (`UnescapeChatText`, sanitised) →
  translated on the worker – into the language you write in (the chat is read in it). Whether a line goes to the
  translator is one pure function, `core/translate_policy` (`ShouldTranslate`, equation E12): automatically (only
  while `[Nexus] TranslateChat=1`, default 0) and "Translate visible chat once" (`TranslateLine(.., once)`, the
  thrifty mode – user, 2026-10-22) by the same rule: only a sure language (`SureLanguage`; "xD", "lf tank" stay as
  they are) and none in the one list "Translated is everything except: [My writing language (DE) x] [English x] [+]"
  (`RenderExceptList`, Translator tab; the same sentence on the dashboard; user, 2026-10-22: one list, the logic clear
  at first sight). Its first entry follows the language you write in (`[Translate] SkipTypingLang`, on by default,
  removable), the others are `[Translate] Understood`; a click on an entry takes it out, "+" adds. A click on the line
  always translates it. `TranslateRecentLines` goes through the lines in view
  (`g_visibleLines`, never an old chat in full) when the translation is switched on, the language you write in or the
  translator changes, or once from the menu; a translation that still fits (into the language you write in now, by
  the translator you use now: `ChatLine::transTarget` / `transBy`) stays. Every chat line goes through the session's
  cache (`core/translation_cache`, `g_transCache`, worker only, equation E13): key = the line as it came + source +
  target + translator name, ≤ 500, least recently used out, memory only – never on disk (invariant 6). A hit is no
  request: "once" twice, a language changed and back, a repeated LFG line cost nothing more; `CountMyMemory` counts
  only real requests. Your own input (`TranslateInput`) does not go through it. The brake: automatic translations at
  most 8 lines per 3 s (`AllowAutoTranslate`) so a flood does not use up a contingent – only for a translator with one
  (`LimitedContingent`: all but LibreTranslate, your own server has no limit). MyMemory's budget left today stands
  beside "Translate visible chat once" (`MyMemoryLeftText`). The worker keeps at most 96 jobs waiting (the oldest go).
  **The chat buffer** (2.3.5): `g_lines` holds at most `kMaxLines` = 2,000 (kb_shared.hpp); `TrimChatLines`
  (kb_chat.cpp) drops the oldest public line first (Map, Say, System …), then the oldest group line (Guild, Party,
  Squad), whispers last. Options → Technical shows the count, the capacity and the RAM. A line is cut at 600
  characters, a name at 64. Trimming the buffer protects the memory, not the translation contingent – that is the cache.
  The detection runs for every line (local, no translator): `g_playerLang` (session only) remembers each player's
  language; their whisper tab then sends in it (`WhisperTab::sendLang` → `g_sendOverride`, "=" = as written; the
  footer's choice in a tab changes only that tab). Nothing of other players is stored (privacy, Raidcore).
- **First use** (user, 2026-10-22): nothing is translated – `[Translate] Writing` 0 (the footer's "Send": as written),
  `[Nexus] TranslateChat` 0; the prediction is on, in the system language (`writeIn` empty); choosing a language at
  "Send" translates with MyMemory (engine "Automatic" without a key). Minimising when not in use, opening with GW2's
  chat line and every focus behaviour are off; reading the chat is on once Events: Chat is there.
- **Languages offered** (user, 2026-10-22: the choice clean, an unsure script left out – stability before features):
  every menu offers only what our window draws (`DrawnInOurWindow`: Latin, Greek, Cyrillic – no Arabic, CJK,
  Devanagari, Thai, Hebrew); a name the font in use cannot draw is shown in English (`NativeName` – never a "?", that
  read as misspelled names). "I write in" offers all of them; "Send" only what GW2's chat shows (`SendableInGw2`:
  Latin script). Written in Cyrillic or Greek a message is never sent as written: `RouteMessage` takes the
  first of yours (EN-GB without any), and "Send" offers no "as written" then (equation E10). "Language of this window"
  lists only the UI languages (`UiLanguages`: English, German, French, Spanish, Italian, Polish, Portuguese, Turkish, Russian). The text font's ranges include Vietnamese
  (1E00–1EFF). **No Arabic anywhere** (user, 2026-10-22): ImGui 1.80 has no shaping and no right-to-left – single
  letters, left to right. Not in the UI, not to write in, not in the font; Arabic lines of others are still detected and
  translated. The options window samples Nexus' effective font size in its `OptionsRender` callback (including the Nexus
  window scale and global Windows/game scaling), then rebuilds our UI font at that size with a 14 pt minimum; while it
  loads, the fallback is scaled to the same minimum. It never reads the chat's `FontPt`. `RequestUiFont`, `kUiFontId`:
  Segoe UI; it has ł, ş, ğ and
  Cyrillic, Nexus' font does not).
- **Writing:** the multi-line input (soft wrap = '\n' in place of a space, `Flat()` turns it back) → `ComputeTyping`
  (word bar, grey completion, what Space writes) → debounce → `TranslateInput` (`MyWords::Expand` first, protected
  segments, back-translation) → Enter → `StartSend` → `SplitForChat` (199) → `SendNow` → `SendToGw2Chat`.
  Nothing goes out half done (user, 2026-10-22): while your own dictation is still being recognized (`g_userRecording`
  and `g_recording`) Enter waits with a note; a send that waits for its translation (`g_sendPending`) is not sent when
  the translation fails – the text comes back into the input and the status names the error (never the original in
  the wrong language). "auto" (hands-free) goes the same way: whisper's final result first (also its English), then
  `StartSend`, then the translator.
- **Tabs** (`WhisperTab`): whisper tabs (session only, ≤5, a click on a name) and tabs of your own (`custom`: right-click
  on the header → "New tab", a copy of the chat; right-click on such a tab = its settings: name, channels shown,
  channel written in; ≤6, kept in `[NexusTabs]` – settings only, never chat). ImGui id per tab = `uid`.
- **Window:** shown only on a map (MumbleLink `inMap`), never over the open world map (`g_mapOpen`). Header: tabs (Chat, your own, whisper tabs),
  mic · V/A (fold to the header) · ≡ · ×; every part can be hidden (`g_layout`: header, chat, preview, word bar,
  footer) – read them through `Lay()`. "Compact view" and "Mini mode" (menu, and the top of Options → Window →
  Window layout) are presets of the parts (`SetProfile`): a profile is on exactly while the parts match it, changing
  a part switches it off, switching it off brings back the layout from before. No preview: the translation stands
  small in the input.
  Without the header a right-click on the window (not a chat line, not the input) opens the menu `kbmenu`
  with the tabs and "Minimise"; the input's learning popup then has "Menu …". Without the chat (the mini mode: nothing
  but the input) the menu holds only "Options …", "Compact view", "Mini mode" (ticked) – in that order (user, 2026-10-22). Without the footer its "<" minimise
  button stands left of the input. "Info texts" off hides every grey status line (never the prediction's grey word).
  The window's theme (`Theme`: Nexus / Slate / Grey / Cba, `ColoursOf`, `PushTheme` for everything we draw) is only
  the window – the chat text keeps its channel colours (`kChannelLook`). Compact view never hides the word bar.
  Minimising: the ">" (optional, `minButton`), three quick clicks on the window (not the input), and "Minimise when not
  in use" (`autoMinimize` after `minimizeAfter` s; `g_lastUse` – input, hover, menus, sending, voice, GW2's chat line
  count as use). "Open again when GW2's chat opens" (`autoOpen`) only opens; the focus key always opens; taking over
  the focus stays `focusOnGameChat`. All of it goes through `g_minimizeRequest` / `g_expandRequest`.
  Text size and font (Options → Window): a new chat starts at 14 pt, or larger if the game's UI scale calls for it
  (`[Window] FontPt=14`; the slider remains adjustable from 8–28 pt). The options window is independent of the chat
  slider and uses at least 14 pt, or Nexus' effective size when larger.
  The fonts: Segoe UI, Segoe UI Semibold, Tahoma (`kFontFaces`, `[Window] FontFace`; each
  has Latin, Greek, Cyrillic); one Nexus font name per face, `ReleaseTextFont` before
  another is asked for (the window scales Nexus' font until it arrives).
  The input grows with its soft-wrapped lines (`g_boxLines`, up to 8) from one line (`inputLines`, default 1), the
  preview the same (`g_previewRows`, what it drew last frame; `previewLines`, default 1), and the window grows upwards when it must
  (bottom kept, shrinks back). The word bar puts a complete valid word before longer completions, corrects ordinary
  lowercase sentence starts, and shows next-word predictions only after a repeated clear context; one-off phrases do
  not produce guesses. Without the word bar the grey word also shows its highlighted completion (what Tab takes;
  Space alone still keeps a valid word). The first Tab takes the highlighted word.
  `GW2KB_SHOT_TYPE=<text>` types into the input before the self-test's picture. Status text is drawn inside the input box (`BoxText`). Compact view = no
  preview / word bar, the translation small in the box. Scroll bar drawn at the left. Snap to the screen edges.
- **Options > General = the dashboard** (user, 2026-10-20; `RenderDashboard`): the modes at a glance, each a chip
  switch (`DashSwitch`: green on, grey off, amber = `edit` differs from the saved config – "Save" applies, as in every
  tab; the chat's translation applies at once like in the Translator tab), states switched elsewhere as chips that
  open their tab (`DashState`), the typing focus (`focusOnGameChat`, `returnFocus` – moved from Writing), then
  `RenderModules(true)` – the empty chat's box. Picture of a tab: `GW2KB_SHOT_OPTIONS=<tab>` (0 General … 6 Technical).
- **Text blocks** (user, 2026-10-20; `core/snippets`, `RenderSnippets`): menu ≡ → "Text blocks …" – save the input,
  a click puts one back after what is in the input (refused with a note when it would not fit the 1024 bytes),
  x deletes; ≤ 30 blocks of ≤ 600 characters, only your own text, `<data>\snippets.txt`. The popup opens at
  the window's top left (`g_openSnippets`, opened in the main window). Picture: `GW2KB_SHOT_SNIPPETS=1`.
- **Right-click** only in the input with a word you wrote (learning mode; empty input without the header: the menu),
  docked to the window's top right corner (1 px), an X closes it: self-corrections noticed while typing (`NoteSelfFix`) →
  `SpellService::TeachFix` (never a dictionary word), and "my words" (meaning → translator). No menu on chat lines.
- **Nothing downloads by itself** (user, 2026-10-18): `FirstStart` only marks the setup done. The modules list (empty
  chat; Options → Technical for Events: Chat and Decoder Ring, Options → Voice for the voice module) offers each on a
  click: Events: Chat first (reading), Decoder Ring (optional and outdated – only in Options → Technical, never in the
  empty chat's box or on the dashboard; 2.3.0: `InstallDecoderRingNow` – the newest release of
  PieOrCake/decoder_ring, `DecoderRing.dll`, SHA-256 checked; `RemoveDecoderRingNow` while Nexus does not hold it;
  live = `g_links.HasDecoder()`, file = `g_decoderFile`), then – only after voice
  input is switched on (off for new users unless the voice module is already there) – the voice module
  (`voice_module.cpp`: release ZIP via `DownloadReleaseAsset`, tar.exe, only gw2kb_voice.exe + voice\*.dll copied)
  and then a voice model. Unload: the voice helper ends after [Voice] UnloadMinutes without use.
- The local translation helper is separate from the voice module and is not built by default. A native release build
  sets `-DGCT_LOCAL_MT=ON`; `tools/pack_release.py` adds `GW2Keyboard-LocalMT.zip` only when the helper was built.
  Converted model packages are built independently with `tools/package_local_mt_models.py` and uploaded as separate
  release assets. No model is downloaded or loaded when the add-on starts. Options → Translator owns install and
  remove controls; Remove everything stops the helper before deleting `GW2Keyboard`.
- **Everything removable** (user: what we brought gets a remove button; each in its place, user 2026-10-20):
  Options → Technical – Events: Chat (only while Nexus does not hold it, else the way via Nexus) and reading it;
  Options → Voice – the voice module, the voice models (each with a second click); once, at the very bottom of
  Options → Technical, "Remove everything of GW2Chatlator …" (a yes): `RemoveEverythingNow` ends the helper, blocks every
  write (`Config::BlockWrites`, `g_wiped`, the spelling dropped unsaved), deletes `<addons>\GW2Keyboard\`, optionally
  Events_Chat.dll, then a notice names the last step (Nexus → Addons → Uninstall – the DLL cannot delete itself).
  Self-test: `GW2KB_TEST_REMOVEALL=1` – only on a copy of the add-on.
- **Translate what I say** (user, 2026-10-20 – kept small: "is voice input used and the switch on, that has priority,
  everything else as before"): `[Voice] Translate` (`voiceTranslate`). A voice result into an empty input marks it
  `g_inputSpoken` (gone when the input is empty again or a history message comes); `VoiceFirst()` = spoken + voice on
  + the switch on – then `SendOriginal()` / `SendLang()` give `VoiceLang()` (the language at "Send", there "as
  written": the first of `writeLangs`) and everything downstream (preview, sending, status) follows. Switch off or
  typed: exactly as before (the footer's choice – "Translate my text" only sets its default). English: whisper itself
  – `[Voice] IntoEnglish` (`voiceIntoEnglish`) is no longer the user's switch but kept by the add-on
  (`KeepWhisperEnglish`, before each recording: the switch on and English at "Send"; changed → flush + "reload", the
  helper reads its ini again – old helpers too). Whisper translates only into English (every model size).
- **Voice:** helper commands `begin | begin-auto | end | cancel | listen | stop-listen | reload | quit`, events
  `ready started partial final error listening idle paused`; `final` carries `rec`, `rms` (ms) and `peak` (% of all
  logical CPUs, 100 ms samples) → `[VoiceStats]` per model; two too-slow runs → next smaller installed model.
  `FindSpeech` gates silence (whisper invents text otherwise). The helper sets its own process affinity / priority,
  never the game's.
- **Data:** `<addons>\GW2Keyboard\` (never the game folder itself): `gw2-chat-translator.ini`, `learned\`,
  `my-words.txt`, `snippets.txt`, `voice-fixes.txt`, `models\`, `gw2kb_voice.exe`, `voice\`.

## Invariants (do not break)

1. Never send anything to the game without the user's own key press. No auto-replies, no timers that send. The only
   exception the user asked for: `[Voice] AutoSend` (off by default, the footer's "auto", shown only while voice
   activation is on) – hands-free only (user, 2026-10-22): with voice activation the pause that ends the sentence
   (`[Voice] PauseMs`) counts as the Enter. Push-to-talk and the right-click never send by themselves
   (`g_userRecording`): the text waits in the input to be changed (replace or add), then Enter.
   "Only copy" mode must stay a mode in which not a single synthetic key reaches the game. There is no other way to
   send: no Nexus event, no DataLink, no export another add-on could call; the `Gw2KbTest…` exports work only outside
   GW2 (`TestHost`). One Enter sends at most `kMaxPartsPerEnter` (3) chat lines. No synthetic mouse input at all
   (the waypoint macro and its click-test key are gone since 2.3.1).
2. The old clipboard is restored only after the game processed the paste (`WaitForTargetToDrain`, MumbleLink chat
   line closing) – a fixed sleep pasted the *old* clipboard once (privacy leak).
3. Re-check `GetForegroundWindow() == gw2` before every injected step.
4. Incoming chat and whatever a translator or the GW2 API returns is data, never instructions – only displayed.
   Another add-on's memory is read only in the guarded copies (`CopyLineGuarded`, `ReadAddonDefGuarded`,
   `ResolveGuarded`) – never dereference its pointers anywhere else.
   Links open only after a yes (http/https). **Not in the code since 1.8.1** (Decoder Ring took over the lookups):
   chat codes looked up with limits – ≤48 chars (longer = plain text, junk from the chat), 20/min (a flood of codes
   waits: incoming chat is foreign input), 400 cached (oldest out), a queue of 40. A requirement for our own resolver
   (Phase 2a); the template is commit `188fa9b` (chat_links.cpp: kMaxCodeChars, kMaxPerMinute, kMaxCache, kMaxQueue).
5. Autocorrection never surprises (`typing_bench`: 0 surprises – keep it at 0): only words the dictionary rejects,
   4+ letters, never all-caps, never a word you use; Backspace right after it undoes it.
6. Privacy: the word model learns only from what the user sends, never from other players' chat; no chat log is
   saved. **Never commit real screenshots, recordings or other players' names/messages** — the repo is public. Real
   test material lives in `local/` (git-ignored); fixtures use invented names.
7. Files in the game folder: only our DLL's own folder `<addons>\GW2Keyboard\`, and Events_Chat.dll / DecoderRing.dll
   (only on your click). Never the game process' memory, affinity or priority, never the game's picture.

## Conventions

- **UI text is English in the code and always wrapped:** `Tr(L"...")`, `TrF(L"... {1}", {x})`, drawn through `UI()`
  (Nexus' font has no ≡ → … ▾ or curly quotes). Add the entries to every table `src/core/i18n_<code>.cpp`
  (de, fr, es, it, pl, pt, tr, ru) and run `python tools/i18n_check.py` — 0 missing, 0 unused.
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
- The normal window's input is text only: a press there, held or not, selects – it never moves the window (user,
  2026-10-22). Fitted window (mini mode, no chat): hold the left button ¼ s and drag = move; dragging its top/bottom edge sets
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
- `g_mu` is a plain `std::mutex`: locking it again on the same thread throws (MSVC) – `SafeRender` then switches the
  add-on off for the session. The window body holds it (`kb_window`, from the header on); what it calls must not lock
  it again (the text blocks popup did – only when open, so only its picture caught it).
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
  a dark halo = 8 copies round the letters, alpha 220 – check it without `chatBg`, on a dark fill it adds little; the
  brightness (`Bright`) is the message's only, a name keeps GW2's palette; `[NexusWindow] Glow / Brightness / GameColors`).
- The self-test poses as Events: Chat itself (it exports GetAddonDef with Vonsh's signature, `g_fakeChatVersion`)
  and as a broken Decoder Ring (`FakeDataLink`): a version new since the last session, a shifted layout, unreadable
  memory, a guard page (it must stay a guard page), endless strings, 3000 random lines, the pause and "Resume", reading
  off, the protocol, the version remembered after the unload. Without the look-first and the SEH the process crashes
  there (checked). The smoke
  test checks that all 500 realistic lines pass the guard (a rule too strict would pause reading for everyone).
- The window's corner against random walks: 400 steps (minimise, open, mini mode, clicks, a press held while the
  shape changes and the mouse moves, fractional frame heights) – the bottom-left corner must never move;
  `GW2KB_TEST_SEED=<n>` walks another way (it found the stale own-drag press on its first run).
- Self-test and smoke test run while you play: the add-on under test reads its own, empty MumbleLink
  (`GW2KB_MUMBLE`, `MumbleLink::DefaultName` – never set in the game), the clipboard stays in the test process,
  `FindGw2Window(pid)` returns only that process' window (never another GW2), so no click can reach the game.
- `build\Release\typing_bench.exe [n]` after every change to the typing help: SURPRISES must stay 0.
- `local_mt_translator_selftest.exe <local_mt_fake_helper.exe>` covers the local translator's real child process:
  protected segments, music-note cleanup, decorative-only output, helper crash + restart, and a five-second timeout.
  It is part of Gate 4.
- The equations (core_tests `TestEquations` E1–E5, `TestEquations2` E6–E13, `TestLocalMtProtocol` E14–E15): thousands of random inputs each – sending,
  protection, ini, soft breaks, chat source; text blocks saved = loaded, protocol counts, no invisible mark reaches the
  window (`WithoutInvisible` in `UI()`), the steady number, where a message goes (`RouteMessage`: typed exactly as
  before 2.3.0, "as written" exactly when there is no language), plain punctuation (E11), what goes to the translator
  (E12: "once" = automatic while on, a click always), the translation cache (E13: a hit asks nobody, LRU against a
  plain list); local MT helper JSON requests preserve UTF-8 text byte for byte (E14), and local model routing prefers
  an installed direct pair before a route of at most two installed pairs through English (E15). Hallucinated musical
  notation is stripped by `CleanLocalMtOutput`; empty/punctuation-only output and other unexpected decorative symbols
  are rejected by `IsPlausibleLocalMtOutput`. A new part gets its
  equation. `i18n_check.py` also
  fails on a translation whose `{1}` placeholders differ from the English text, or on an invisible mark in it.
- **Change-scoped verification is the default for every agent.** Map the edited behavior and files to the narrowest
  checks that can catch regressions, run those checks, and skip unrelated suites. Do not treat a protocol checklist as
  a command to run every listed test: use it as a coverage map, and add a focused check only where the existing checks
  leave a real gap. For model work, check only the changed/available model pair and route; do not benchmark every
  catalog entry. Documentation and license-only edits do not trigger runtime tests. A CMake edit is scoped by the
  targets and sources in its diff; it never turns on every test by itself.
- `python tools/verify_release.py --changed --plan` shows the selected checks without running them;
  `python tools/verify_release.py --changed [base]` runs only those affected checks and is never a release verdict.
  The mapping is maintained in `tools/verify_release.py`. The full release run still executes all eight gates. The
  self-test starts from a fresh ini every run (a half-failed run used to leave its layout for the next one).
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
