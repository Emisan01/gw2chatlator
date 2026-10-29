# AGENTS.md — GW2Chatlator (Single Source of Truth)

This repository is **GW2Chatlator for Raidcore Nexus** (Guild Wars 2 add-on).
This document is the binding single source of truth for all coding agents (Gemini, Claude, Codex, Argon, etc.).

> ### The Gaudí & Lem Principle
> 1. **Gaudí (Empirical Ground Truth):** Let physical reality and measurements (sockets, compiler, memory) shape the architecture naturally like a catenary curve. Never build fragile scaffolds or fake workarounds.
> 2. **Lem / Asimov (Cybernetic Invariants):** Safety bounds are unbreakable. Never invent or rewrite your own rules. When facing a rule conflict: STOP and ask the human.

### Working Modes – every change answers them with evidence
The names are this project's fingerprint; what counts is the check. A behaviour change states its evidence in the commit
message body (`Evidence: …`), in its `docs/TESTPROTOCOL.md` row and, for measured gains, in the CHANGELOG. Docs-, i18n-
and data-only changes answer Lem and Better only.

| Mode | Question | Evidence the change brings | Practice it follows |
|---|---|---|---|
| **Gaudí – measure** | What do the measurements say? | Baseline and result in numbers (`translation_bench`, `typing_bench`, timings, test counts) – measured before the change, not after the fact | Measurement-driven development |
| **Formula – exact** | Is it an exact rule? | An exact rule; a heuristic only with its measured gain and a fallback that keeps the old behaviour | Design by Contract: invariants, pre- and postconditions as tests |
| **Speed lane – hot path** | What does it cost where time matters? | Cost per chat line, per frame, per key press, per model call – before and after; the numbers the gates measure (smoke test frame times, `typing_bench` ms per key) never get worse without a reason in the commit | Profiling, Amdahl's law; make it work, make it right, make it fast |
| **Better – simplify** | Is there a simpler or more exact way? | One sentence on the alternative considered and why not; a fault is fixed at its cause, never by a patch on a patch | Kaizen / PDCA; root cause (5 Whys) |
| **Lem – invariants** | Does it touch privacy, the send guard, crash safety, attribution? | The invariant named (section 2) and the test that guards it; a conflict means STOP and ask the maintainer | Safety invariants, escalation |
| **Proof – test** | Would a test fail without the change? | The test that fails without it (`core_tests`, a self-test step, a bench line) | Regression testing |

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

### Continuous Checks
- **CI on GitHub** (`.github/workflows/ci.yml`, every push to main and every pull request): term table, translations and
  claims checked, core tests and the add-on DLL built, the local translator's self-test, the workflows scanned with
  zizmor. Actions pinned to full commit SHAs with the tag as a comment, the token only reads, checkout keeps no
  credentials; Dependabot proposes new pins monthly – merged by the maintainer only. CI is a safety net, not the release
  audit: that stays `verify_release.py --full` on the maintainer's PC.
- **Security checks on GitHub:** CodeQL analyses the C++ code that ships (`.github/workflows/codeql.yml`, every push,
  pull request and weekly); the OpenSSF Scorecard rates the repository's security practices (`scorecard.yml`, every
  push to main and weekly); both report under Security → Code scanning. Vulnerabilities are reported privately as
  `SECURITY.md` says – never in a public issue.
- **Evidence trailer:** a commit of type `feat`, `fix`, `perf` or `refactor` carries `Evidence:` in its message (Working
  Modes, top of this file). Gate 1 checks every commit since the rule came (`tools/check_evidence.py`); the same check
  runs before each commit once `git config core.hooksPath tools/hooks` is set in a clone.
- **Translation quality gate:** with `GCT_BENCH_DATA` set to a GW2Keyboard folder holding NLLB-200, `--full` runs
  `translation_bench` on the made-up corpus and fails on anything protected lost, a sentence lost, a line cut short or
  failed; without a model it says "skipped" in the receipt.

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
- **Handshake:** helpers start with `{"evt":"ready","helper":"translate","proto":1,"caps":["opus","nllb","score"],"build":"<commit12>"}` (the voice helper also keeps `"t":"ready"`). The DLL requires `kMinHelperProto` (1) and the `nllb` capability for the NLLB engine; a `ready` without `proto` counts as proto 0, i.e. outdated. Older DLLs read only `evt` and accept the new line. With `score` (2.6.5) each `result` may carry `"score"` (the model's mean log probability per token); older DLLs ignore it, newer DLLs work with helpers that send none.

### Translation Quality Contract
- **One check after every translator** (`src/core/translation_check.*`): protected parts present, no sentence lost, not cut short; a local model's score below `kUnsureScore` counts as unsure. A doubtful chat translation is shown marked `(?)` with the reason on hover; a doubtful outgoing one is said in the status – sending stays the user's.
- **Local models sentence by sentence** (NLLB drops sentences when it gets several at once): the whole sentence with its protected words first (also one at its start or end), then once more with lowercase keep-words written like names ("Rez"), then with placeholders X1, X2 … ("Lion's Arch" became "Löwenbogen"), pieces around them only when every try changed one – each try checked against the protected parts themselves; protected words come back the way they were written, a full stop the source did not end with is dropped, spacing follows the target language (`TidyPunctuation`). MyMemory: whole text first, the parts around protected ones only when it changed one. Cloud translators: `translate="no"` spans.
- **Terms:** the built-in terms live in `data/gw2-terms.tsv` (term, kind `keep`/`caps`/`word`, meaning; several words are one `keep` term: "Wizard's Vault"), compiled in through `python tools/gen_terms.py` → `src/core/gw2_terms.inc` – core_tests fails when the two differ; `gw2-terms.txt` in the data folder holds the user's terms on top (kept across versions) and the built-in list below (rewritten by every version). Chat lines starting with "/" are a channel command (its message translated, the command never) or any other command (typed whole, never translated) – no emote list, so new emotes work by themselves.
- **Measured, not guessed:** `translation_bench <helper> <data dir> tests/data/translation_corpus.tsv [mode]` (made-up chat lines only – never a real player's message in the repo). Each line carries meaning words – must hold / must never hold – so the bench counts "meaning ok" next to protected parts, sentences and length. Modes compare the same lines: `nllb` (our logic), `nllb-raw` (the bare model), `opus`, `mymemory`, `deepl`/`google` (keys from `GCT_DEEPL_KEY`/`GCT_GOOGLE_KEY`). Changes to protection, splitting or the check are measured before and after. Baseline 2026-10-29: ours 30/45 meaning ok, bare NLLB 7/45, MyMemory 32/45.
- **Update on click only:** an outdated helper shows a banner with one button. The swap ends the running helper, keeps the old exe as `.old` until the new one answered `ready` with a sufficient proto, and puts it back on any failure. Known old files are removed only from an explicit name list. The handshake is probed on a background thread, only when the exe on disk changed — never on the render thread.
- **Models:** the install writes `.package.json` (file sizes); at runtime only sizes are compared, a mismatch shows "Model corrupted – please reinstall".
- **After the upload:** `python tools/verify_assets.py` checks every asset in the version tag and in `extras` against the manifests (large models via the API's `digest`). `extras` is a mutable container read by every installed version: replace an asset there only with the same file name and after the maintainer's OK.

---

## 5. Development Conventions

- **UI Strings:** All user-facing UI text in code is English, wrapped in `Tr(L"...")`, and registered in `src/core/i18n_*.cpp` (de, fr, es, it, pl, pt, tr, ru). Verify with `python tools/i18n_check.py`.
- **Deferred INI Writes:** Never write the INI directly on the game thread. Use `Config::DeferWrites(true)` / `SaveValue`.
- **Pacing & Caution:** No reckless speed or overconfidence; respect review loops and the maintainer's physical capacity.
- **No AI attribution:** never add Co-Authored-By, "Generated with ...", session links or any other AI attribution to commits, PR descriptions, release notes or files. The author is Emi.
