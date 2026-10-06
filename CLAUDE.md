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
src/win   Windows services (no UI)      http (WinHTTP), deepl_translator, online_translators (MyMemory, LLM,
                                        model list, LanguageTool call), els (language detection, transliteration),
                                        ocr (Windows.Media.Ocr, raw WinRT ABI), tesseract_ocr (subprocess),
                                        screen_capture (DXGI + GDI), mumble_link, gw2_sender, gw2_api, spellcheck,
                                        gw2_locate (find GW2, install, autostart, add-on scan), folder_cleanup, files
src/core  portable logic, NO windows.h  text, json, i18n (+ i18n_de / i18n_ar tables), hotkey, langs, languages,
                                        glossary, protect, slang, chat_line (OCR-tolerant parsing), chat_stream,
                                        chat_tabs, image, gw2_text, mumble, word_model (phone keyboard),
                                        tesseract_tsv, languagetool_protocol, gw2_install, housekeeping,
                                        deepl/mymemory/llm_protocol, translator.hpp
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

Incoming: `ChatReader` (worker: capture → fingerprint → `PrepareForOcr` → Tesseract (`FindTesseract`,
`ChooseTesseractLangs`, PGM via stdin, TSV via stdout) or Windows OCR → lines with per-word colours) →
`WM_APP_SNAPSHOT` → `BuildMessages` (timestamps incl. OCR-mangled ones, fuzzy channel tags, "anchored" start
at the first stamped line, tag-only input line dropped, channel by `LeadColor`/tag, speaker, continuation
merge, whisper prefixes) → `ChatStream::Feed` (fuzzy novelty) → `ClassifyOwn` → `NeedsTranslation` (ELS) →
`TranslationCache` → batched `TranslateBatch` (or `LlmTranslator::TranslateOcrBatch` = translate + repair OCR
errors) → `ChatLogView::Update`. Tabs are filters (channel mask + the tab id outgoing lines were written in).

Outgoing: InputBox → `SanitizeChatText` → `SplitChatCommand` → `ProtectForTranslation` → worker `Translate`
→ `WM_APP_TRANSLATED` (generation-checked) → `SplitForChat` (199) → preview + back-translation → Enter →
`SendToGw2Chat` → log entry → `SpellService::Learn` (word model). Optional: debounce → LanguageTool
(`StartGrammarCheck`, rate limited) → blue marks via `InputBox::SetGrammarIssues`.

Typing help: `SpellService::Suggestions` (completion from the learned `WordModel`, correction via
`ChooseCorrection`, next word from word pairs) → `SuggestionBar`; `InputBox::TryAutoCorrect` on a word
boundary (`AutoCorrectMode` Off/Safe/Phone); Backspace right after it → `UndoAutoCorrect` → `RejectCorrection`.

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
   `ignoreSnapshotsBefore_` are dropped, `ShowsTranslation`/`OnSelfRead` as last line of defence.
6. Autocorrection never surprises: Phone mode only touches words the dictionary rejects (or, without a
   dictionary, words you never used), 4+ letters, never all-caps, never a word you use, 1 edit up to 6
   letters / 2 above, same first letter (or first two swapped), single words only. Backspace undoes it.
7. Docking keeps our window a separate top-level window. No owner/parent link to the game window.
8. Installing only writes our own folder (`<GW2>\addons\GW2ChatTranslator`, fallback
   `%LOCALAPPDATA%\Programs\GW2ChatTranslator`) and, if chosen, one `HKCU\...\Run` value. No game file is
   touched; the GW2 folder is found via registry/Steam/folders, never via the game process.
9. Files stay small: captures rotate (20 × 3 files), switch off after 15 minutes, are deleted after 3 days;
   stale `*.tmp` are removed on start; the word model is capped (20k words, 60k pairs).

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

## Testing

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
5. Real-GW2 test round with Tesseract: tune `PrepareForOcr`, colours (`DefaultChannelColors`), tags
6. Windows Graphics Capture of the GW2 window (sees the chat under any overlay)
7. Optional, off by default, user's decision: Nexus add-on that forwards unofficial-extras party/squad chat
   as exact text to the exe (named pipe). It lives in the game process — keep it a separate download.
