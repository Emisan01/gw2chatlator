# CLAUDE.md — GW2 Chat Translator

Native C++17 / Win32 tool, no hooks, no game memory. Read this before changing anything.
Several agents/LLMs work on this repo: keep changes small, build + test before every commit,
and update this file when you learn something the next agent needs.

## Layers (dependencies point downwards only)

```
src/app   window + views (UI thread)   main_window (menus, tray, wait-for-GW2), input_box (spelling, autocorrect,
                                        word bar, Backspace undo), suggestion_bar, chat_log_view, preview_view,
                                        settings_dialog (settings + guided setup, native controls), chat_reader
                                        (worker), region_picker, spell_service, config, theme
src/win   Windows services (no UI)      http (WinHTTP), deepl_translator, online_translators (MyMemory, Google,
                                        Microsoft, LibreTranslate, LLM, model list, LanguageTool call), els (language detection, transliteration),
                                        ocr (Windows.Media.Ocr, raw WinRT ABI), tesseract_ocr (subprocess),
                                        screen_capture (DXGI + GDI), mumble_link, gw2_sender, gw2_api, spellcheck,
                                        gw2_locate (find GW2, install, autostart, add-on scan), folder_cleanup, files,
                                        keyboard_layout (neighbouring keys of the active layout for the word bar)
src/core  portable logic, NO windows.h  text, json, i18n (+ i18n_de / i18n_ar tables), hotkey, langs, languages,
                                        glossary, protect, slang (+ word-bar starter list), chat_line (OCR-tolerant
                                        parsing), chat_stream, chat_tabs, image, chat_geometry (line grid, dynamic
                                        enlargement, snapping the chat frame, words -> lines), names (speakers seen,
                                        protected from translation), gw2_text, mumble, word_model (phone keyboard,
                                        fuzzy completion, Arabic folding, forget), tesseract_tsv,
                                        languagetool_protocol, gw2_install, housekeeping, deepl/mymemory/llm_protocol,
                                        cloud_mt_protocol (Google v2, Microsoft v3, LibreTranslate),
                                        translator.hpp
res/      app.rc (icon id 1, manifest: common controls v6, version info), app.ico, app.manifest
tools/    i18n_check.py (missing/unused translations)
```

- `core` must compile on Linux: `g++ -std=c++17 -I src tests/core_tests.cpp src/core/*.cpp` (run from the repo
  root; `TestRealCapture` reads `tests/data/real_capture_win_ocr.txt`).
- Views know nothing about services except through callbacks / objects handed to them.
- New translation backends implement `core/translator.hpp` (thread-safe, blocking, called from
  worker threads; `keep` segments must come back unchanged; set `quotaExceeded` when a free
  contingent is used up).
- `spellcheck.cpp` and `ocr.cpp` are linked per executable, so the test build swaps in fakes.

## Data flow

Incoming: `ChatReader` (worker: capture → fingerprint → `ChatOcr::Read`: `FindLineGrid` → `OcrScaleFor` (~30 px line
spacing) → `UpscaleForOcr` (bicubic, no contrast tricks) → Tesseract (`FindTesseract`, `ChooseTesseractLangs`, PGM
inverted via stdin with `tessedit_do_invert=0`, TSV via stdout) or Windows OCR → `GroupWordsByRows` (words into the
measured lines) → lines with per-word colours) →
`WM_APP_SNAPSHOT` → `BuildMessages` (timestamps incl. OCR-mangled ones and `MangledStampAndTagLength` for
"117:46J[M]", "tO:13JtPJ"; fuzzy channel tags; "anchored" start
at the first stamped line, tag-only input line dropped, channel by `LeadColor`/tag, speaker, continuation
merge compares with the *text* colour of the line above (`TailColor`), whisper prefixes) → `ChatStream::Feed` (fuzzy
novelty; on the very first picture only the last 3 messages) → speakers into `NameList` → `ClassifyOwn` →
`NeedsTranslation` (ELS) →
`TranslationCache` → batched `TranslateBatch` (or `LlmTranslator::TranslateOcrBatch` = translate + repair OCR
errors) → `ChatLogView::Update`. Tabs are filters (channel mask + the tab id outgoing lines were written in).

Setting the chat area: `PickRegion` takes a still of the game (WGC/DXGI) → the player drags roughly →
`SnapChatArea` (line grid, half lines, tab bar / input line by their wider gap, scroll bar) → traffic light +
preview of the first lines (`ChatOcr` on a worker thread) → Enter takes the snapped frame.

Outgoing: InputBox → `SanitizeChatText` → `SplitChatCommand` → `ProtectForTranslation` (chat codes, glossary, names
of people in the chat, keep-words) → worker `Translate`
→ `WM_APP_TRANSLATED` (generation-checked) → `SplitForChat` (199) → preview + back-translation → Enter →
`SendToGw2Chat` → log entry → `SpellService::Learn` (word model). Optional: debounce → LanguageTool
(`StartGrammarCheck`, rate limited) → blue marks via `InputBox::SetGrammarIssues`.

Typing help: `SpellService::Suggestions` (completion from the learned `WordModel`, then `Gw2StarterWords`, then
`CompleteFuzzy` = one typo in the prefix, neighbouring keys of the layout rank first; correction via
`ChooseCorrection`, next word from word pairs) → `SuggestionBar` + grey rest of the completion after the caret
(`InputBox::DrawGhost`, LTR only); `InputBox::TryAutoCorrect` on a word boundary (incl. Arabic ، ؟ ؛) and
`FinishWordAtCaret` on Enter (`AutoCorrectMode` Off/Safe/Phone); Backspace right after it → `UndoAutoCorrect` →
`RejectCorrection`. Right-click a learned word (word bar or input) → `SpellService::Forget`; Settings → Writing →
`ForgetAll` deletes every `learned_*.txt`. Word keys go through `WordKey` (case fold + Arabic variants أإآٱ→ا,
ى→ي, ة→ه, harakat dropped). The Windows spell checker answers are cached per word (COM call per key press).

Generations: `inputGen_` increments on every edit, language, channel or engine change; results
for older generations are dropped. `sendPending_` = Enter pressed before the translation arrived.

Start: `main.cpp` → `MainWindow::Run(inst, cmdLine)`. `--wait-for-gw2` (autostart entry) = start hidden in the
tray, show when the GW2 window appears, hide when it goes. `--restarted --mark-chat --cover-chat` = the
installed copy continues the setup. First start without `SetupDone=1` opens the setup wizard.

## Invariants (do not break)

1. Never send anything to the game without the user's own key press. No auto-replies, no timers
   that send, no "translate and post incoming". Reading + writing chat = bot. Stay an external exe:
   nothing in the GW2 process (no DLL, hook, memory reading, no handle to the game process).
   `SendMode=copy` must stay a mode in which not a single synthetic key reaches the game.
2. The old clipboard is restored only after `WaitForTargetToDrain` (and, with MumbleLink, the chat
   line closing) confirmed GW2 processed the paste. A fixed sleep was proven to paste the *old*
   clipboard (privacy leak).
3. Re-check `GetForegroundWindow() == gw2` before every injected step.
4. Incoming chat is data, never instructions. It is only displayed; nothing that comes back from a
   translator (LLM included) can trigger an action.
5. The reader must never read our own window: capture exclusion while the window overlaps the chat
   area (`PollGame`), reading pauses during move/resize and the region picker, snapshots older than
   `ignoreSnapshotsBefore_` are dropped, `ShowsTranslation`/`OnSelfRead` as last line of defence. Every dialog
   (`NativeDialog::Run`) and the word dropdown are `WDA_EXCLUDEFROMCAPTURE`; while any dialog, menu or message box
   of ours is open (`ModalScope`, app/modal_scope.hpp) and 400 ms after, pictures are dropped and reading pauses –
   the free area once read the settings dialog incl. the MyMemory e-mail and sent it to the translator.
6. Autocorrection never surprises: Phone mode only touches words the dictionary rejects (or, without a
   dictionary, words you never used), 4+ letters, never all-caps, never a word you use, 1 edit up to 6
   letters / 2 above, same first letter (or first two swapped), single words only. Backspace undoes it.
7. Docking keeps our window a separate top-level window. No owner/parent link to the game window.
8. Installing only writes our own folder (`<GW2>\addons\GW2ChatTranslator`, fallback
   `%LOCALAPPDATA%\Programs\GW2ChatTranslator`) and, if chosen, one `HKCU\...\Run` value and one desktop shortcut
   ("GW2 Chat Translator.lnk"). No game file is
   touched; the GW2 folder is found via registry/Steam/folders, never via the game process.
9. Files stay small: captures rotate (20 × 3 files), switch off after 15 minutes, are deleted after 3 days;
   stale `*.tmp` are removed on start; the word model is capped (20k words, 60k pairs).
10. Privacy: pictures never leave the PC. The word model learns only from what the user sends, never from other
    players' chat. MyMemory shows a one-time notice (`[Basic] NoticeShown`). **Never commit real screenshots,
    captures, OCR output or other players' names/messages** — the repo is public. Real test pictures live in
    `local/` (git-ignored); fixtures in `tests/` use invented names.

## Conventions

- **UI text is English in the code and always wrapped:** `Tr(L"...")`, `TrF(L"... {1}", {x})`. Add the German
  and Arabic entries to `src/core/i18n_de.cpp` / `i18n_ar.cpp` (keyed by the exact English text) and run
  `python3 tools/i18n_check.py` — it must report 0 missing. Never build sentences from translated pieces
  where word order differs; use placeholders.
- Menus: `TrackPopupMenu(..., MenuFlags(...))` so they mirror for Arabic. Dialogs get `WS_EX_LAYOUTRTL` when
  `UiRtl()`. Text that may be RTL: `DT_RTLREADING` when `IsRtlText`.
- Sources are UTF-8 (MSVC `/utf-8`); `\uXXXX` escapes are fine too. Status markers in text controls are ASCII
  (`[OK]`, `[--]`) — symbol glyphs need font fallback that is not guaranteed.
- INI is ASCII only (`AsciiEscape`/`AsciiUnescape` for paths, keys, names).
- All Win32 calls use the explicit `W` variants. Every setting is reachable from the settings dialog;
  `Config::SaveAll` writes what the dialog changes; `MainWindow::ApplySettings` restarts only what changed.

## Pitfalls already hit

- `small` is a macro (rpcndr.h) → theme fonts are `fontSmall` etc.; `FoldString` is a macro → `CaseFold`.
- Multi-line EDIT sends no `EN_CHANGE` for `WM_SETTEXT` → call `OnInputChanged()` after `InputBox::Clear()`.
- `GetPrivateProfileInt` turns negatives into 0 → `Ini::Int` parses strings.
- Per-monitor DPI v2: coordinates are physical everywhere; `WM_DPICHANGED` → `ApplyDpi` recreates the theme
  (also used for the text-size setting).
- Resetting `ChatStream` re-adds every visible line → never reset it on region changes.
- MumbleLink context: `uiState` at offset 48, `processId` at 80 (static_asserts).
- DXGI: `WAIT_TIMEOUT` is "no new frame", not an error; GDI fallback only after 3 real errors.
- Tesseract: one thread (`OMP_THREAD_LIMIT=1`), below-normal priority, 8 s timeout, picture fed on a helper
  thread while stdout is drained (avoids pipe deadlocks). Scale ≥ 2 before OCR.
- `CompareStringOrdinal`/`SHCreateDirectoryExW` need shell32/advapi32/version/uuid (see CMake).
- Wine (tests only): no Arabic/CJK fallback for Segoe UI/Tahoma (boxes), first click on a fresh popup is
  eaten, hotkeys are not delivered, `WDA_EXCLUDEFROMCAPTURE` fails (cover-chat pauses reading), bidi in
  STATIC controls is weak. Not bugs of the app.
- Nexus has no chat event. The only chat callback around is arcdps unofficial extras: party/squad + NPC.
  In-process tools that read all channels exist (CatBridge, Better Chat), but via the game process — not us.
- Text recognition, measured with `ocr_bench` on real 4K captures (2026-10-06), error after the parser:
  Windows OCR ~0.5–3.4 % at ~90 ms, Tesseract ~0.7–13 % at ~1.5–3.5 s (process start + 4 models). The old
  `PrepareForOcr` made both 2–3× worse. Simulated 1080p (downscaled) is bad for Windows OCR — check with real
  1080p captures before building anything for it. Tesseract light-on-dark: invert + `tessedit_do_invert=0`
  (else it tries every line twice).
- Words of several chat lines reported as one line (Tesseract) or lines out of order (Windows OCR): always
  regroup by the measured grid (`GroupWordsByRows`). Most "merged" messages were the parser treating a line
  with a mangled timestamp as a continuation — extend `MangledStampAndTagLength` with real examples.
- Only chat is read: (1) reading runs only while MumbleLink ticks (`MumbleState::inMap`; character selection
  and loading screens show character data where the chat is), (2) `BuildMessages` starts a message only with a
  timestamp, a channel tag or "Name: text" (plain notices only in timestamp-less frames that mostly look like
  chat), (3) speaker-less fragments with < 3 letters or < 50 % letters are dropped, (4) double scan:
  `ChatStream::Feed(..., confirm=true)` takes a new line only when the next read (200 ms later) shows it again.
- The log shows no timestamps and no channel tags, only "Name: text" in the channel colour.
- Smileys (`:D`, `^^`, `<3`, `xD` ...) and names are protected from translation (`ProtectForTranslation`).
- MumbleLink "game closed?" is checked by the game's windows (`EnumWindows`), never `OpenProcess` (invariant 1).
- Sending: every key is held `KeyHoldMs` (30 ms) and Ctrl+V is staggered — GW2 reads the keyboard once per frame;
  down+up in one `SendInput` batch got lost (messages did not arrive).
- Incoming translation: up to 4 requests at once (an LLM: 1), newest line first (`inQueue_` back); the log stays
  chronological. MyMemory gets one line per request. Short lines: `GuessLanguageByLetters` when ELS says nothing.
- OCR "automatic" = Windows OCR (faster and, measured, better at normal size); Tesseract only below
  `ChatOcr::kSmallTextPitch`. Reading interval default 400 ms (OCR runs only when the picture changed).
- The reading area goes ~1/3 below the frame (at most to the window edge) so the newest line is never cut; the
  input line (tag, no timestamp, below the last stamped line) and the number row are dropped by the parser.
- Typing: the dropdown under the word (`ChoicePopup`, `SpellService::Choices`): Space/punctuation take the
  highlight, Tab/arrows move, Esc closes, Backspace undoes. Slips: `SlipDistance` (key next door 0.5, swap 0.7),
  `HandShiftVariants` (whole hand one key off), `KeyLayout` from the active keyboard layout.
- Log: names white, text in the channel colour; click a name → whisper tab for that player (`ChatTab::person`,
  max `kMaxPersonTabs`); click an untranslated line → translated first; links (`FindLinks`) are never translated
  and open only after a yes (`OpenLinkAsking`, http/https only).
- Settings → Translator: local models via Ollama (`LocalModelOffers`, `/api/pull`), with size and VRAM notes.
- First start (0.6): one-page setup; no chat area is guessed. While `regionSet` is false, `PollGame` places the
  window bottom left once and runs `StartChatDetection` every 1.5 s on a map: capture of the game's bottom-left
  corner → `ChatOcr` → `LocateChatLines` (≥2 timestamp lines aligned on the left) → `SnapChatArea` →
  `UseChatArea` + `CoverChat`. Our window is excluded from the capture while it overlaps the search area.
- Capture: WGC only where its yellow frame can be switched off (Windows 11, `BorderlessWindowCapture`), else
  DXGI; `[Reader] Capture=auto|window|screen`.
- Translation scope: everything automatic except `[Translate] Understood=` languages and unticked
  `AutoChannels`; skipped lines get "click to translate". MyMemory characters per day are counted
  (`[Basic] UsedDay/UsedChars`, `CountMyMemory`) and shown in the footer.
- Settings → Technical: `TechnicalStatus()` (parameters named like the ini keys, live `stats_`); "copy
  diagnosis" holds no chat text. Version: `project(... VERSION)` → generated `version.h` → exe resource and UI.
- A chat line half hidden under GW2's own tab bar is not a reading error: it was read when it appeared at the
  bottom; `ChatStream` keeps it from coming again.
- Free screen area (`[Reader] FreeArea=1`, `FreeRect` in screen pixels, menu ≡): no MumbleLink gate, no game
  window needed, capture target = screen, `BuildFreeTextMessages` (lines → paragraphs, ≤ ~450 chars) instead of
  `BuildMessages`, messages carry `freeText` (never "system", always translated unless the language is understood),
  the whole first picture is translated. Chat detection does not run in this mode. `ReaderOptions::freeText`: no
  `GroupWordsByRows` (it merges side-by-side columns), scale ≥ 2; `BuildFreeTextMessages` assigns each line to the
  open block above it in its column; `recentFree_` + `SameFreeParagraph`: a grown paragraph updates its entry.
- Second look (`ChatOcr::Read`, `core/second_look`, `[Reader] SecondLook=1`): words the Windows spell checkers
  (OCR language + read/write/chat languages + English, created on the reader thread) do not know are cropped with
  room around them, enlarged more (`scale + 2`, max 4) and read again with Windows OCR; the new reading replaces the
  old only if it is a real word and close (`PlausibleRereading`: 1 edit ≤ 4 letters, 2 for 5, 3 above). Decided once
  per word-in-line (`decided_`), max 6 looks per picture. Measured on the 4K GW2 bench (2026-10-07): no change in
  error rate (what it looks at there is names/slang), +~0.2 s on the first picture only. Meant for small app fonts
  in the free area; `ocr_bench` shows "app plain" vs "app 2nd look".
- Learned recognition fixes (`<data>\ocr-fixes.txt`, a `MyWords` list "as read = correct"): second-look successes
  arrive as `ReaderSnapshot::newFixes`, are stored by `MainWindow`, and reach `ChatOcr` as `ReaderOptions::ocrFixes`
  (applied before the dictionary check, no re-read).
- RapidOCR (`win/rapid_ocr`, `core/rapid_rec`, `core/rapid_models`, `ReaderOptions::rapidDir/rapidGroups`,
  `OcrChoice::Rapid`): recognition-only (no detection model, no OpenCV): each measured grid row (chat) or each Windows
  OCR line box (free text) is cut out with 25 % margin (measured best; more pulls in neighbour lines), scaled to 48 px
  height, CTC-decoded; word boxes from the CTC steps (colours). `rapidCache_` keys rows by an FNV hash of their pixels:
  a scrolling chat costs 1–11 ms per picture. ONNX Runtime 1.30 comes via CMake FetchContent (SHA-256 checked),
  `/DELAYLOAD:onnxruntime.dll`, `ORT_API_MANUAL_INIT` + `RuntimeAvailable()` loads the DLL from the exe folder first;
  the Latin model is downloaded at configure time and copied to `<exe>apid`; other groups download into
  `<data>apid` (`DownloadRapidGroup`, SHA-256). Measured (ocr_bench, parsed error): 4K Windows 0.5–3.4 % vs Rapid
  5–17 %; simulated 1080p Windows 63–81 % vs Rapid 8–31 %. Auto = Rapid only for small text (pitch < 14 px).
- `FindInk` handles both polarities: bright background (median ≥ 160) = dark text.
- Translators (`Engine`): Auto order = DeepL → Google → Microsoft → own LibreTranslate server → LLM → MyMemory. Google
  key goes in the `X-Goog-Api-Key` header (never the URL). Protected segments: `<span translate="no"
  class="notranslate">` (HTML mode only when something is protected). The settings combo order differs from the enum:
  `kEngineOrder` / `EngineIndex` / `EngineAt` in settings_dialog.cpp.
- LLM presets (`kLlmPresets`, settings_dialog.cpp): all OpenAI-compatible (Anthropic `https://api.anthropic.com/v1`,
  Gemini `…/v1beta/openai`). Cloud URLs (`!IsLocalLlmUrl`) get no `temperature` (reasoning models reject it). The key
  field is cleared when the provider changes. `tools/i18n_check.py` reads preset names and notes.
- The Translator settings page shows one section per engine (`group_` collects controls, `UpdateTranslatorView`
  after every `ShowPage`); sections share the same rows.
- Correction memory (`core/corrections`, `<data>\corrections.txt`): exact line per target language (case and
  spacing ignored) → used instead of any translator (incoming in `HandleIncoming`, outgoing in `StartTranslation`);
  phrases (few changed words, `ChangedPhrase`) → `Apply` on every fresh translation. Right-click "Correct this
  translation…" (`AskCorrection`); not offered for messages split over several chat lines (`splitSend`).
- My words (`core/my_words`, `<data>\my-words.txt`, "word = meaning"): `Expand` runs on the text that goes to the
  translator (outgoing `StartTranslation`, incoming `PumpIncoming`), never on what is shown or used as cache/correction
  key. Explained words also go to `SpellService::AddUserWord` and to `ReaderOptions::knownWords` (second look).
  Source side on purpose: a phrase correction on the target side ("finds" -> "I think") would break "he finds".
- API keys in the ini: `ProtectSecret`/`UnprotectSecret` (win/secret, DPAPI, "dpapi:" + base64); old plain values
  are still read. A key encrypted on another account/PC reads as empty.
- `ocr.cpp` calls `RoInitialize(MTA)`: create `ChatOcr` on a worker thread, not on the UI (STA) thread.
- WGC frames match `DWMWA_EXTENDED_FRAME_BOUNDS`, not `GetWindowRect` (invisible resize borders).

## Testing

- Native Windows (Visual Studio 2022+): `cmake -S . -B build && cmake --build build --config Release`, then
  `build\Release\core_tests.exe` (prints the word-bar time per key press for a full 20k model; must stay < 15 ms).
- `build\Release\ocr_bench.exe local\bench`: real chat crops `name.png` + `name.txt` (what really stands there) →
  error rate before/after the parser and time, old vs new preparation, Tesseract vs Windows OCR. `BENCH_DUMP=1`
  prints recognized/parsed/truth text. Pictures stay in `local\` (never committed).
- `core_tests` (any OS; also as .exe under Wine for 16-bit `wchar_t`); ASan/UBSan on Linux:
  `g++ -std=c++17 -g -fsanitize=address,undefined -I src tests/core_tests.cpp src/core/*.cpp -o t && ./t`
- Windows build from Linux: `cmake -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake
  -DCMAKE_BUILD_TYPE=Release && cmake --build build-win -j8`
- End to end under Wine + Xvfb (`Xvfb :99 &`, `WINEPREFIX=/tmp/wineprefix`, unset proxy variables):
  copy `fake_gw2.exe` + `GW2ChatTranslator_fakes.exe` into one folder with a `fake_chat.txt`
  (`RRGGBB|[19:20] [M] Name: text` per line; `f7f92e` = yellow system line), start fake_gw2, then the app.
  First start → setup wizard. For translations: ini `Engine=llm`, `[LLM] Url=http://127.0.0.1:11500
  Model=mock` and `python3 tests/tools/mock_llm.py 11500` (touch `mock_drop` for short batches). Drive with
  `xdotool`, look with `import -window root shot.png`. Fake spell checker knows komtm/dsa/fraktal/doubled.
- Verified there (v0.5): setup wizard incl. live switch to German/Arabic (mirrored), region picker →
  cover chat, incoming translation via mock LLM (OCR-repair prompt), system line filter, phone autocorrect
  `komtm`→`kommt`, Backspace undo + learning, learned completion in the word bar + Tab, restructured main
  menu, settings pages, connections view.
- Not verifiable without real Windows + GW2: Windows OCR / Tesseract quality on live chat, DXGI, ELS, real
  spell checker, DeepL/MyMemory/LanguageTool live, hotkey, GW2 chat glyph support, autostart entry, install
  into the real GW2 folder (permissions).

## Roadmap

1. ✅ Outgoing: spelling, autocorrect, glossary, translators, safe sending, split, channels
2. ✅ Incoming: OCR reader, channels, whisper tab, cache, own-line handling, LLM option
3. ✅ Tabs with channel sets, docking, cover the GW2 chat
4. ✅ v0.5: English/German/Arabic UI, settings dialog, guided setup, installer + autostart (no hook),
   Tesseract, OCR-tolerant parsing, phone keyboard + learning, LanguageTool, temp-file hygiene
5. ✅ v0.5.1: Smart GW2 path resolution (folder/exe/shortcuts), non-blocking setup wizard when game OFF,
   raid collapse mode (32 px bar with tabs/badges), optional focus transfer on game chat focus
6. ✅ Windows Graphics Capture (WGC) of the GW2 window (DirectX backbuffer capture beneath overlays)
7. ✅ (2026-10-06, see docs/PLAN.md) Fast typing help (forget words, fuzzy mid-word completion with layout
   neighbours, Arabic folding, Enter finishes the last word, grey completion, starter list), privacy (README,
   MyMemory notice), reading rebuilt on measurements (line grid, dynamic enlargement, regrouping, snapping
   frame with traffic light + preview, hardened timestamp parsing, names protected), `ocr_bench`.
8. Next: live test in the game (incl. real 1080p/1440p captures for `ocr_bench`), then decide on an own glyph
   reader. Optional, off by default, user's decision: Nexus add-on that forwards unofficial-extras party/squad
   chat as exact text to the exe (named pipe). It lives in the game process — keep it a separate download.
