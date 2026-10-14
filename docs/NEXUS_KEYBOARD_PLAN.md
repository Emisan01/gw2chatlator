# NEXUS_KEYBOARD_PLAN — GW2 Keyboard as a pure Nexus add-on

Status 2026-10-14: decided by the user ("we split and build a pure DLL version").
- Stages 1–5 are built.
- Tested outside the game: `nexus_kb_selftest` loads the add-on with a real ImGui context, draws frames, raises a
  chat line and unloads it. The voice helper's protocol was tested by hand.
- Not tested in the game yet.

Layout in `<GW2>\addons`:
- `gw2keyboard_nexus.dll`
- `GW2Keyboard\` (settings, learned words, models) with `gw2kb_voice.exe` and its `voice\` DLLs
- The build puts the same layout under `build\addons\nexus-keyboard\Release\`.

## Why

- Delta (Raidcore) said the whole tool can live in one Nexus DLL.
- In the Nexus library (112 add-ons, checked 2026-10-14) there is **no voice input and no typing / translation help**.
  GW2 Keyboard would be the first.
- Nexus reaches many more players than a separate exe.
- Every chat channel comes exactly from Vonsh's "Events: Chat" (docs/READER_PLAN.md), so the add-on needs no OCR.

## The split

| | GW2 Keyboard (exe) | GW2 Keyboard for Nexus (DLL) |
|---|---|---|
| UI | Win32 (today's windows) | ImGui inside the game (Nexus' ImGui 1.80) |
| Chat in | the bridge add-on + optional OCR reader | "Events: Chat" / Unofficial Extras directly (no pipe) |
| Core | `src/core` (portable) | the same `src/core` |
| Services | `src/win` | the same `src/win` (translators, spell checker, models, hashing …) |
| Arabic | yes | later (ImGui 1.80 does not join Arabic letters – see below) |

The core and the services stay one code base. Only the UI is new. Both products ship from this repository.

## Rules inside the game process

1. **Failsafe** (as in cba4gw2, `NexusGuard.h`):
   - Every render callback checks the ImGui context first (`IsRenderContextSafe`).
   - Every callback and every thread is wrapped in `try/catch` and an SEH `__try/__except`, so an error ends our
     feature, never the game.
   - After an exception the add-on switches itself off for the session and says so in the Nexus log.
2. **No work on the game's threads.** Render callbacks only draw and copy. Translation, spelling, prediction and voice
   run on our own worker threads with below-normal priority.
3. **Never touch the game process' affinity or priority class.** Built: the helper process `gw2kb_voice.exe` runs the exe's
   `VoiceSession` (thread count to choose; automatic = the least busy cores) with its own affinity, below normal. The exe's `CpuBudget` sets the *process* – inside GW2
   that would hit the game.
   - In the DLL, the voice recognizer runs either on its own threads, each bound to the least busy cores (`CoreLoad` +
     `PickVoiceCores`, per thread), or in a small helper process (`gw2kb-voice.exe`, started hidden by the add-on).
   - The helper process is preferred: a whisper crash or its memory (a model of several hundred MB) stays out of
     the game, and process affinity and priority work as today.
   - Decide with measurements (FPS with and without dictation).
4. **Sending stays the user's** (built: Enter → ImGui lets go of the keyboard → the exe's `SendToGw2Chat` on the
   worker, key timing after the frame rate). The same send path as the exe: Enter in our window, then the line goes into the GW2
   chat with clipboard and keys and the foreground checks (invariants 1–3).
   - The user's idea of an optional automatic send after dictating (a timer) collides with invariant 1 and with
     ArenaNet's "one key press = one action". Talk about it before building anything.
   - A rule-safe variant: releasing the push-to-talk key *is* the key press that sends.
   - Built that way as an option (user, 2026-10-14): a small box next to the microphone, `[Voice] AutoSend`, off by
     default. The details are to be settled later with the user.
5. **Privacy** (Delta: translating is fine, collecting chat data is not): nothing of other players is stored; the word
   model learns only from what the user sends (invariant 10).

## Arabic later

ImGui 1.80 renders glyphs one by one, left to right, with no shaping. Possible ways:
- Pre-shaping: Arabic presentation forms (U+FE70–FEFF) plus reversed order per line. This is a known ImGui trick.
- Shaping with Windows (Uniscribe / DirectWrite) into a texture.
- ideka's "Japanese Text" shows that own glyph rendering inside GW2 add-ons is done.

## Stages

1. **Skeleton:**
   - CMake target `gw2keyboard_nexus`, `GetAddonDef`, failsafe, an ImGui window (toggled by a Nexus keybind and a
     quick-access icon).
   - Input line with the translation preview from the existing translators.
   - The chat from "Events: Chat" in a list, translated.
2. **Sending:**
   - Enter sends the translation into the GW2 chat (focus handling between ImGui and the game, `gw2_sender`).
   - Whisper replies (`/w Name`).
3. **Typing help:** word suggestions and grey completion, autocorrection, learned words (`SpellService` without
   Win32 controls).
4. **Voice:** push-to-talk with a Nexus keybind, the helper process or threads bound to free cores, the live text.
5. **Settings** in the Nexus options page (languages, translator, keys, voice model).
6. **Nexus library:** ask Raidcore for listing, with a VirusTotal link and the source.
