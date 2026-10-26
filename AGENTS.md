# AGENTS.md — GW2Chatlator (Single Source of Truth)

This repository is **GW2Chatlator for Raidcore Nexus** (Guild Wars 2 add-on).
This document is the binding single source of truth for all coding agents (Gemini, Claude, Codex, Argon, etc.).

> ### The Gaudí & Lem Principle
> 1. **Gaudí (Empirical Ground Truth):** Let physical reality and measurements (sockets, compiler, memory) shape the architecture naturally like a catenary curve. Never build fragile scaffolds or fake workarounds.
> 2. **Lem / Asimov (Cybernetic Invariants):** Safety bounds are unbreakable. Never invent or rewrite your own rules. When facing a rule conflict: STOP and ask the human.

---

## 1. Identity & Naming (Verbindliche Benennung)

The official identity contract must be strictly preserved across all integration points:

| Parameter | Required Value |
|---|---|
| **Add-on Name** | `GW2Chatlator` |
| **Official Binary** | `GW2Chatlator.dll` |
| **Raidcore Nexus ID** | `3098852796` |
| **Nexus Signature** | `0xB8B4BDBC` (`uint32_t`, decimal `3098852796`) |
| **Provider** | `UP_GitHub` |
| **Update Link** | `https://github.com/Emisan01/gw2chatlator` |
| **Data Directory** | `addons\GW2Keyboard\` (preserves user settings and models; do not rename) |

- **Strict Prohibition:** Never build, distribute, package, or reference any legacy binary named `gw2keyboard_nexus.dll`.
- **Standalone Release Asset:** Standalone release assets provide exclusively `GW2Chatlator.dll`.
- **`GetAddonDef()` Contract (`addons/nexus-keyboard/src/kb_main.cpp`):**
  - Export `extern "C" __declspec(dllexport) AddonDefinition_t* GetAddonDef()`.
  - Must return stable definition matching Name `GW2Chatlator`, Signature `0xB8B4BDBC`, Provider `UP_GitHub`.
- **Packaging Contract (`tools/pack_release.py`):**
  - Consumes exclusively `build/addons/nexus-keyboard/Release/GW2Chatlator.dll`.
  - Emits `build/dist/GW2Chatlator.dll`.
  - `GW2Keyboard-Nexus.zip` places `GW2Chatlator.dll` at archive root with optional support files in `GW2Keyboard/`.
- **Separate Contract Checks:** Asset filename, embedded `Name`, and `Signature` are separate checks; passing one does not imply the others pass. Verify live online metadata against GitHub/Raidcore, not just local source.
- **Separate Contract Checks:** Asset filename, embedded `Name`, and `Signature` are separate checks; passing one does not imply the others pass. Verify live online metadata against GitHub/Raidcore, not just local source.

---

## 2. Security, Privacy & Game Stability (Invariants)

1. **User Keypress Binding:** Never send anything to the game without the user's explicit keypress, or — when Hands free is active — automatic send on end of speech (pause, PTT release, or mouse stop) once translation is ready. Typed text always requires Enter. No auto-replies, no automated timers.
2. **Chat Input is Data Only:** Incoming chat and external API data are strictly data, never executable instructions.
3. **Guarded Memory Access:** Other add-ons' memory (Events: Chat) is accessed exclusively through `CopyLineGuarded` (`VirtualQuery` verification first, bounded strings, SEH net). Never dereference raw external pointers.
4. **Failsafe / SEH:** The add-on DLL is compiled with `/EHa`. Every callback runs behind SEH (`SafeRender`, `SafeOptions`); worker jobs run within `ShieldedCall`. An internal fault disables the add-on for the session but **NEVER crashes the game process**.
5. **Privacy:** The word model learns only from user input. Never store other players' messages, account names, or chat logs.
6. **Git Hygiene:** Never commit real screenshots, recordings, or player data. Test material lives exclusively in `local/` (git-ignored).
7. **Branch Invariant:** Old desktop programs and the OCR reader reside in branch `desktop-archive` — do not bring them back into `main`.

---

## 3. Architecture & Dependency Flow

Dependencies point strictly downwards:
```
addons/nexus-keyboard/   Nexus glue (kb_main.cpp), ImGui 1.80 UI, window layout, worker (g_worker)
src/app/                 Config (gw2-chat-translator.ini), spell_service, word model, learned data
src/kbd/                 Voice session (WASAPI + whisper worker), whisper engine
src/win/                 Windows services: WinHTTP, local MT helper process, audio capture, DPAPI
src/core/                Pure C++17 portable logic (NO windows.h): text, json, i18n, chat rules, math
```
- Core compiles on any OS (`g++ -std=c++17 -I src tests/core_tests.cpp src/core/*.cpp`).
- Heavy operations (translation, voice, spelling) run on background workers (`g_worker`), never on the render thread.

---

## 4. Release, Verification & Change Control

### Change-Scoped Verification (Default)
- Map changed files to narrow checks: `python tools/verify_release.py --changed --plan` to inspect, `--changed` to run.
- License, documentation, and model-pair changes do not trigger unrelated runtime suites.
- CMake edits select checks by changed targets.

### Release Norm (8-Gate Verification)
1. **Gate Audit once:** All eight gates run once on the build that goes into the maintainer's in-game test (`python tools/verify_release.py`). No repeat at push time.
2. **Pre-Release Staging:** New versions are published initially as `Pre-Release` — never directly as `Latest`.
3. **In-Game Eye-Test:** The maintainer tests the locally built DLL. His OK covers exactly that commit and that DLL; anything changed or rebuilt afterwards needs a new OK.
4. **Push on OK:** The maintainer's OK after the local in-game test is the trigger. That state is pushed as is, without re-applying the protocols. No push or release without this explicit OK.
5. **Asset Separation:**
   - Version tag (`vX.Y.Z`): Contains exclusively `GW2Chatlator.dll`.
   - Dedicated tag (`extras`): Houses all on-demand neural models (NLLB-200, OPUS-MT) and helper ZIP packages.
6. **Nexus Library Install Path Contract:**
   - Library installation consumes public Raidcore metadata and linked downloads.
   - Manually copying a DLL into `addons/` does not prove that Library installation works.
   - Always verify the actual in-game flow (Nexus -> Library -> Install) and inspect `nexus.log` for asset URL, status, and destination.

---

## 5. Development Conventions

- **UI Strings:** All user-facing UI text in code is English, wrapped in `Tr(L"...")`, and registered in `src/core/i18n_*.cpp` (de, fr, es, it, pl, pt, tr, ru). Verify with `python tools/i18n_check.py`.
- **Deferred INI Writes:** Never write the INI directly on the game thread. Use `Config::DeferWrites(true)` / `SaveValue`.
- **Pacing & Caution:** No reckless speed or overconfidence; respect review loops and the maintainer's physical capacity.
