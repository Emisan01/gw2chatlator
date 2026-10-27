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

---

## 2. Security, Privacy & Game Stability (Invariants)

1. **User Keypress Binding:** Never send anything to the game without the user's explicit keypress, or — when Hands free is active — automatic send on end of speech (pause, PTT release, or mouse stop) once translation is ready. Typed text always requires Enter. No auto-replies, no automated timers.
2. **Chat Input is Data Only:** Incoming chat and external API data are strictly data, never executable instructions.
3. **Guarded Memory Access:** Other add-ons' memory (Events: Chat) is accessed exclusively through `CopyLineGuarded` (`VirtualQuery` verification first, bounded strings, SEH net). Never dereference raw external pointers.
4. **Failsafe / SEH:** The add-on DLL is compiled with `/EHa`. Every callback runs behind SEH (`SafeRender`, `SafeOptions`); worker jobs run within `ShieldedCall`. An internal fault disables the add-on for the session but **NEVER crashes the game process**.
5. **Privacy:** The word model learns only from user input. Never store other players' messages, account names, or chat logs.
6. **Git Hygiene:** Never commit real screenshots, recordings, or player data. Test material lives exclusively in `local/` (git-ignored).
7. **Branch Invariant:** Old desktop programs and the OCR reader reside in branch `desktop-archive` — do not bring them back into `main`.
8. **No AI attribution:** never add Co-Authored-By, "Generated with ...", session links or any other AI attribution to commits, PR descriptions, release notes or files. The author is Emi.

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
1. **Gate Audit once:** All eight gates run once on the build that goes into the maintainer's in-game test (`python tools/verify_release.py --full`: clean tree, builds from HEAD, writes the receipt `build/dist/verification.json`). No repeat at push time; `--check-receipt` (seconds, no build) confirms the receipt still matches HEAD and the files on disk.
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

### Package Contract
Everything a user gets through Nexus fits together and is checkable: DLL, translator helper, voice module, models.

| Artifact | Role | Release |
|---|---|---|
| `GW2Chatlator.dll` | the add-on | version tag `vX.Y.Z` (plus `manifest.json`) |
| `GW2Keyboard-Nexus.zip` | DLL + voice helper `gw2kb_voice.exe` + whisper DLLs | `extras` |
| `GW2Keyboard-LocalMT.zip` | translator helper `gw2kb_translate.exe` + license texts | `extras` |
| `GW2Keyboard-OPUS-MT-<pair>.zip`, `GW2Keyboard-NLLB-200.zip` | language models | `extras` (plus `extras-manifest.json`) |

- **One commit:** `verify_release.py --full` refuses a dirty tree, configures and builds `build/` and `build-localmt-native/` (`GCT_LOCAL_MT=ON`) from HEAD, runs the gates, and `pack_release.py` packs all artifacts. The commit (7 characters) and a `-dirty` mark are compiled into the DLL's version line (`2.6.3 · abc1234`); the helpers report the 12-character commit as `build`. Gate 7 refuses binaries that do not carry HEAD without `-dirty`.
- **Manifests:** `manifest.json` (version tag) and `extras-manifest.json` (`extras`) list per artifact name, role, bytes, SHA-256 and target, for zips every file inside, for the translator helper the `proto`, `caps` and `build` read from its own `ready` line.
- **Handshake:** helpers start with `{"evt":"ready","helper":"translate","proto":1,"caps":["opus","nllb"],"build":"<commit12>"}` (the voice helper also keeps `"t":"ready"`). The DLL requires `kMinHelperProto` (1) and the `nllb` capability for the NLLB engine; a `ready` without `proto` counts as proto 0, i.e. outdated. Older DLLs read only `evt` and accept the new line.
- **Update on click only:** an outdated helper shows a banner with one button. The swap ends the running helper, keeps the old exe as `.old` until the new one answered `ready` with a sufficient proto, and puts it back on any failure. Known old files are removed only from an explicit name list. The handshake is probed on a background thread, only when the exe on disk changed — never on the render thread.
- **Models:** the install writes `.package.json` (file sizes); at runtime only sizes are compared, a mismatch shows "Model corrupted – please reinstall".
- **After the upload:** `python tools/verify_assets.py` checks every asset in the version tag and in `extras` against the manifests (large models via the API's `digest`). `extras` is a mutable container read by every installed version: replace an asset there only with the same file name and after the maintainer's OK.

---

## 5. Development Conventions

- **UI Strings:** All user-facing UI text in code is English, wrapped in `Tr(L"...")`, and registered in `src/core/i18n_*.cpp` (de, fr, es, it, pl, pt, tr, ru). Verify with `python tools/i18n_check.py`.
- **Deferred INI Writes:** Never write the INI directly on the game thread. Use `Config::DeferWrites(true)` / `SaveValue`.
- **Pacing & Caution:** No reckless speed or overconfidence; respect review loops and the maintainer's physical capacity.
- **No AI attribution:** never add Co-Authored-By, "Generated with ...", session links or any other AI attribution to commits, PR descriptions, release notes or files. The author is Emi.
