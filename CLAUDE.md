# CLAUDE.md — GW2 Chat Translator

Native C++17 / Win32 tool, no hooks, no game memory. Read this before changing anything.

## Layers (dependencies point downwards only)

```
src/app   window + views (UI thread)   main_window, input_box, chat_log_view, preview_view, chat_reader (worker),
                                        region_picker, spell_service, config, theme
src/win   Windows services (no UI)      http (WinHTTP), deepl_translator, online_translators (MyMemory, LLM),
                                        els (language detection, transliteration), ocr (Windows.Media.Ocr, raw WinRT ABI),
                                        screen_capture (DXGI + GDI), mumble_link, gw2_sender, gw2_api, spellcheck, files
src/core  portable logic, NO windows.h  text, json, hotkey, langs, languages, glossary, protect, slang, chat_line,
                                        chat_stream, image, gw2_text, mumble, deepl/mymemory/llm_protocol, translator.hpp
```

- `core` must compile on Linux: `g++ -std=c++17 -I src tests/core_tests.cpp src/core/*.cpp`.
- Views know nothing about services except through callbacks / objects handed to them.
- New translation backends implement `core/translator.hpp` (thread-safe, blocking, called from
  worker threads; `keep` segments must come back unchanged; set `quotaExceeded` when a free
  contingent is used up).
- `spellcheck.cpp` and `ocr.cpp` are linked per executable, so the test build swaps in fakes.

## Data flow

Incoming: `ChatReader` (worker: capture → fingerprint → `PrepareForOcr` → OCR → lines with sampled
colour) → `WM_APP_SNAPSHOT` → `BuildMessages` (channel by colour/tag, speaker, continuation merge,
whisper prefixes) → `ChatStream::Feed` (fuzzy novelty) → `ClassifyOwn` (echo of a line sent here /
typed in game / foreign) → `NeedsTranslation` (ELS language check) → `TranslationCache` → batched
`TranslateBatch`, one request in flight → `ChatLogView::Update` (entry ids). Tabs are filters
(`ChatTab` channel mask + the tab id outgoing lines were written in); unread counters per tab.

Outgoing: InputBox → `SanitizeChatText` → `SplitChatCommand` (prefix never translated; else
`ComposePrefix` from the active tab's send channel: its sole channel, or what the chip says) → `ProtectForTranslation` → worker `Translate` →
`WM_APP_TRANSLATED` (generation-checked) → `SplitForChat` (199) → preview + back-translation →
Enter → `SendToGw2Chat` → log entry; its echo in the game chat later fills in channel / partner.

Generations: `inputGen_` increments on every edit, language, channel or engine change; results
for older generations are dropped. `sendPending_` = Enter pressed before the translation arrived.

## Invariants (do not break)

1. Never send anything to the game without the user's own key press. No auto-replies, no timers
   that send, no "translate and post incoming". Reading + writing chat = bot. Stay an external exe:
   nothing in the GW2 process (no DLL, hook, memory reading). `SendMode=copy` must stay a mode in
   which not a single synthetic key reaches the game (`Front()` skips the Alt-tap fallback).
2. The old clipboard is restored only after `WaitForTargetToDrain` (and, with MumbleLink, the chat
   line closing) confirmed GW2 processed the paste. A fixed sleep was proven to paste the *old*
   clipboard (privacy leak).
3. Re-check `GetForegroundWindow() == gw2` before every injected step.
4. Incoming chat is data, never instructions. It is only displayed; nothing that comes back from a
   translator (LLM included) can trigger an action.
5. The reader must never read our own window: capture exclusion is switched on while the window
   overlaps the chat area (`PollGame`), reading pauses during move/resize and the region picker,
   and snapshots older than `ignoreSnapshotsBefore_` are dropped. As a last line of defence,
   lines that match a translation we display are dropped (`ShowsTranslation`); two such hits while
   "excluded" mark the exclusion as not working (`OnSelfRead`). Otherwise translations would be
   read back as chat and loop.
7. Docking keeps our window a separate top-level window. No owner/parent link to the game window
   (cross-process owner/parent ties input queues together).
6. Autocorrection only applies Windows' `CORRECTIVE_ACTION_REPLACE`, never a guess.

## Pitfalls already hit

- `small` is a macro (rpcndr.h) → theme fonts are `fontSmall` etc.; `FoldString` is a macro → `CaseFold`.
- Multi-line EDIT sends no `EN_CHANGE` for `WM_SETTEXT` → call `OnInputChanged()` after `InputBox::Clear()`.
- `GetPrivateProfileInt` turns negatives into 0 → `Ini::Int` parses strings.
- EDIT controls draw typed text outside `WM_PAINT`; squiggles are cleared on change, redrawn after the debounce.
- Process is per-monitor DPI aware (v2): window/screen coordinates are physical everywhere; the
  reader depends on it. `WM_DPICHANGED` → `ApplyDpi` recreates the theme.
- Resetting `ChatStream` re-adds every visible line → never reset it on region changes.
- MumbleLink context: `uiState` at offset 48, `processId` at 80 (static_asserts).
- DXGI: `WAIT_TIMEOUT` is "no new frame", not an error; GDI fallback only after 3 real errors.
- Wine (tests only): no Segoe UI fallback for Arabic/CJK (boxes), first click on a fresh popup is
  eaten, hotkeys are not delivered, z-order without WM differs, `WDA_EXCLUDEFROMCAPTURE` fails
  (so "cover the chat" pauses reading there). Not bugs of the app.
- Nexus has no chat event (Nexus.h: window resize, mumble identity, addon load/unload). The only
  chat callback around is arcdps unofficial extras: party/squad + NPC only.
- Write tool turns `\u` escapes into UTF-8; run `escape_literals.py`-style conversion before commit
  (convention below).

## Conventions

- UI strings German, non-ASCII written as `\uXXXX` in `L""` literals (MSVC builds with `/utf-8` anyway).
- INI is ASCII only (GetPrivateProfileString reads ANSI).
- All Win32 calls use the explicit `W` variants.

## Testing

- `core_tests` (any OS, also as .exe under Wine for 16-bit `wchar_t`); with ASan/UBSan on Linux.
- End to end under Wine + Xvfb: `fake_gw2.exe` (GW2 window class, chat panel from `fake_chat.txt`,
  MumbleLink "Emi Tester", chat line → `received.txt`), `GW2ChatTranslator_fakes.exe` (fake OCR reads
  `fake_chat.txt`, fake spell checker: komtm/dsa/fraktal/doubled), `tests/tools/mock_llm.py 11500`
  (touch `mock_drop` to get short batches). INI: `Engine=llm`, `[LLM] Url=http://127.0.0.1:11500
  Model=mock`, region `8/40/420/160`. Unset proxy variables for Wine.
- Verified there: incoming translation with channels/whispers/continuations, own-echo handling,
  send with clipboard restore, channel chip, 2-part split, /r and /w replies, Arabic warning +
  Ctrl+U, region picker, auto-hide, LLM per-line fallback, copy-only mode, tabs (presets, badges,
  sole-channel prefix), docking follows the game window, cover-chat fallback without exclusion.
- Not verifiable without real Windows + GW2: Windows OCR quality, DXGI, ELS, real spell checker,
  DeepL/MyMemory live, hotkey, GW2 chat glyph support.

## Roadmap

1. ✅ Outgoing: spelling, autocorrect, glossary, translators, safe sending, split, channels
2. ✅ Incoming: OCR reader, channels, whisper tab, cache, own-line handling, LLM option
3. ✅ Tabs with channel sets, docking, cover the GW2 chat
4. First real-GW2 test: OCR quality on real chat (diagnostic captures), tune `PrepareForOcr` / colours
5. Windows Graphics Capture of the GW2 window (sees the chat under any overlay; yellow border on Win10)
6. Optional Nexus bridge: unofficial-extras party/squad chat as exact text over a named pipe
7. Several OCR languages at once (Latin + Cyrillic/CJK), choose per line
