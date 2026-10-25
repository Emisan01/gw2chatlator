# GW2Chatlator — Quality Test Protocol

Status: Active Quality Protocol  
Reference: [AGENTS.md](file:///C:/Users/Emi/Documents/github/gw2chatlator/AGENTS.md) (Single Source of Truth)  
Target: Guild Wars 2 / Raidcore Nexus Add-on (`GW2Chatlator.dll`)

---

## 1. Pre-Build Findings & Settle Status

| # | Finding from Code Review | Risk / Context | Resolution Status |
|---|---|---|---|
| **1** | `ImAnimate/` untracked in git | Build failure on other checkouts / CI | **Resolved:** Removed `ImAnimate` entirely. Replaced card hover glow with a lightweight 2-line frame-rate independent lerp in `kb_options.cpp`. Zero untracked dependencies. |
| **2** | Local MT / NLLB removal side-effects | Worker concurrency or config bypass | **Resolved:** Restored `if (inUse)` safety guard in `kb_local_mt.cpp`. Model/module removal is disabled while actively chosen; user is prompted to select another engine and save first. |
| **3** | `SpellService` snapshots & C++ standard | Uninitialized reads or unnecessary copies | **Resolved:** `gameSnap_` initialized to non-null empty set; `IgnoreForSession` only reallocates snapshot if new word was actually inserted (`.second == true`); C++17 `std::atomic_store`/`load` on `shared_ptr` standard compliant. |
| **4** | Worker lanes non-blocking audit | Micro-stuttering during live typing | **Resolved:** `PostInteractive` strictly isolated to `ComputeTyping`. All blocking operations (translation, network, disk, Windows send) assert `AssertNotInteractiveLane` and run on background lane. |
| **5** | Color-blind accessibility & Mic mute | Color-only state distinction | **Resolved:** Implemented `Icon::MicMuted` with a diagonal strike-through slash across capsule and stand; removed red tint on mute (red reserved exclusively for recording activity); text badges for all tile states. |
| **6** | i18n diff bloat & line endings | Messy git history and newline churn | **Resolved:** Preserved native `\r\n` (CRLF) line endings; removed 20 dead keys and appended 39 new keys in place without reordering; `i18n_check.py` reports 0 missing, 0 unused. |
| **7** | UI Simplicity & Clean Layout | Visual clutter and redundant controls | **Resolved:** Removed redundant "Show the window" checkbox from dashboard; added generous vertical spacing; consolidated Translation & Languages section below tiles. |

---

## 2. Quality Architecture (Three Levels)

Quality verification is strictly stratified into three distinct levels:

| Level | Execution | Focus | Result |
|---|---|---|---|
| **A. Automatic** | Automated test suite / script | Compilation, unit suites (`core_tests`), i18n coverage, binary contracts, typing/voice benches | `pass` / `fail` |
| **B. Semi-automatic** | Standalone headless harnesses | `nexus_kb_selftest`, `nexus_kb_smoketest`, `local_mt_translator_selftest`: lifecycle, worker lanes, config roundtrip outside GW2 | `pass` / `fail` |
| **C. Manual In-Game** | Maintainer eye-test in GW2 | Real visual aesthetics, feel, latency under heavy chat load, Nexus interaction, live audio hardware | `ok` / `defect` / `skipped` |

---

## 3. Rules Enforced by This Protocol

1. **Version on Every Push:** Every push to main represents a distinct version and release. Model and package assets are carried along.
2. **Privacy Invariant:** Never store, log, or export other players' messages, account names, or chat content. Counts only. The word model learns exclusively from the user's own typing.
3. **Translation Quality:** Translation accuracy is evaluated against fixed reference datasets; changes must never degrade hit rates. No cascades of approximate fixes; exact methods only.
4. **Window Logic & Automatics:** Automatic window behaviors remain strictly optional and switchable in settings. ImGui internals must be balanced (`PushFont`/`PopFont`, `Begin`/`End`).
5. **No Faked Results:** Any test row not executed during a session remains marked as `skipped`, never assumed or faked as `ok`.
6. **No Unprompted Data Transmission:** Nothing is sent to chat without the user's explicit keypress or active hands-free pause.

---

## 4. Test Matrix

| ID | Area | Step | Expected | Level | Touches (files) | Last run (version, date, result) |
|---|---|---|---|:---:|---|---|
| **START-01** | START | Add-on loads in headless harness and game | `nexus.log` free of errors, warnings, and SEH faults | A/B | `src/`, `addons/nexus-keyboard/` | 2.6.1, 2026-10-25, pass |
| **START-02** | START | Add-on discovery in Nexus Addon Manager | Appears with Name `GW2Chatlator`, opens on keybind or menu | C | `addons/nexus-keyboard/` | 2.6.1, 2026-10-25, skipped |
| **START-03** | START | Unload and reload add-on during active session | Memory freed, helper terminated cleanly, reload without crash | B | `addons/nexus-keyboard/src/kb_main.cpp` | 2.6.1, 2026-10-25, pass |
| **START-04** | START | Binary contract verification | Asset `GW2Chatlator.dll`, signature `0xB8B4BDBC`, ID `3098852796` | A | `tools/verify_release.py`, `CMakeLists.txt` | 2.6.1, 2026-10-25, pass |
| **CFG-01** | CFG | Save options and reload from disk | Exact parameter preservation across restarts | B | `src/app/config.*` | 2.6.1, 2026-10-25, pass |
| **CFG-02** | CFG | Load empty or corrupted INI file | Graceful fallback to factory defaults, zero crashes | B | `src/app/config.*` | 2.6.1, 2026-10-25, pass |
| **CFG-03** | CFG | Atomic swap with retry loop and temp file cleanup | Locked target file retries cleanly; stale temp files purged | B | `src/app/config.cpp` | 2.6.1, 2026-10-25, pass |
| **CFG-04** | CFG | Amber "changed" marker on option edits | Visual indicator appears on edit and clears upon Save | C | `addons/nexus-keyboard/src/kb_options.cpp` | 2.6.1, 2026-10-25, skipped |
| **CFG-05** | CFG | Custom chat tab name and theme persistence | New values survive complete game restart | C | `addons/nexus-keyboard/src/kb_window.cpp` | 2.6.1, 2026-10-25, skipped |
| **TYPE-01** | TYPE | Run `typing_bench` | Correction hit rate not below last release baseline (0 surprises) | A | `src/core/word_model.*`, `tests/` | 2.6.1, 2026-10-25, pass |
| **TYPE-02** | TYPE | Rapid typing in game during heavy chat traffic | No visible stuttering or frame drops on render thread | C | `addons/nexus-keyboard/src/kb_input.cpp` | 2.6.1, 2026-10-25, skipped |
| **TYPE-03** | TYPE | Snapshot stores: word, name, typo, session ignore | Immediate lock-free visibility and persistence after restart | B | `src/app/spell_service.*` | 2.6.1, 2026-10-25, pass |
| **TYPE-04** | TYPE | Dictionary vs typo learning | Real words not learned as typos; slang and Denglish preserved | A | `src/app/spell_service.*` | 2.6.1, 2026-10-25, pass |
| **TYPE-05** | TYPE | Worker flood protection on interactive queue | Queue drops stale requests; Interactive lane never hangs | B | `addons/nexus-keyboard/src/kb_worker.*` | 2.6.1, 2026-10-25, pass |
| **MT-01** | MT | Core translation name & keep-word masking | Character names, GW2 chat codes, and keep-words untouched | A | `src/core/translate_policy.*` | 2.6.1, 2026-10-25, pass |
| **MT-02** | MT | Extended language code mapping (ca, sr, bs, mk, sq, etc.) | Maps correctly to NLLB codes; unknown codes fallback safely | A | `src/core/local_mt_protocol.*` | 2.6.1, 2026-10-25, pass |
| **MT-03** | MT | Single-line translation per engine (Basic, Local, NLLB) | Proper language translation in requested direction | B | `addons/nexus-keyboard/src/kb_local_mt.cpp` | 2.6.1, 2026-10-25, pass |
| **MT-04** | MT | Helper crash, timeout, or invalid output recovery | Add-on survives cleanly, displays friendly error status | B | `src/win/local_mt_process.*` | 2.6.1, 2026-10-25, pass |
| **MT-05** | MT | Install local translation module via Quick Setup | Progress percentage displayed; clean transition to Active | C | `addons/nexus-keyboard/src/kb_local_mt.cpp` | 2.6.1, 2026-10-25, skipped |
| **MT-06** | MT | Remove translation module while active | Disabled with warning; requires explicit engine switch & save | C | `addons/nexus-keyboard/src/kb_local_mt.cpp` | 2.6.1, 2026-10-25, skipped |
| **MT-07** | MT | Offline network error during model download | User-friendly error message, incomplete files cleaned up | C | `addons/nexus-keyboard/src/kb_options.cpp` | 2.6.1, 2026-10-25, skipped |
| **MT-08** | MT | Translation quality evaluation | Hit rate against fixed sample set not degraded | A | `src/core/translate_policy.*` | 2.6.1, 2026-10-25, pass |
| **CHAT-01** | CHAT | Read incoming chat lines from Events: Chat | Incoming lines parsed; status clear if companion missing | C | `addons/nexus-keyboard/src/kb_chatsource.cpp` | 2.6.1, 2026-10-25, skipped |
| **CHAT-02** | CHAT | External chat pointer memory validation | `VirtualQuery` guard, SEH protective net, bounded strings | A | `addons/nexus-keyboard/src/kb_chatsource.cpp` | 2.6.1, 2026-10-25, pass |
| **CHAT-03** | CHAT | Privacy invariant check | No external player chat stored or transmitted (counts only) | A | `addons/nexus-keyboard/src/kb_chat.cpp` | 2.6.1, 2026-10-25, pass |
| **CHAT-04** | CHAT | Click on waypoint or item link in chat | Tooltip displays info; no unwanted automatic clipboard copy | C | `addons/nexus-keyboard/src/kb_chat.cpp` | 2.6.1, 2026-10-25, skipped |
| **VOICE-01** | VOICE | Run `voice_bench` audio recognition test | Accuracy and latency match or exceed baseline | B | `src/kbd/whisper_engine.*` | 2.6.1, 2026-10-25, pass |
| **VOICE-02** | VOICE | Push-to-Talk vs Voice Activation in game | Speech captured cleanly; recognizes text; mic lights red on activity | C | `addons/nexus-keyboard/src/kb_voice.cpp` | 2.6.1, 2026-10-25, skipped |
| **VOICE-03** | VOICE | Discord-safe Mute toggle | Diagonal strike-through slash; mic NOT red; click unmutes | C | `addons/nexus-keyboard/src/kb_theme.cpp` | 2.6.1, 2026-10-25, skipped |
| **VOICE-04** | VOICE | Voice module deactivated in settings | Dashboard shows "Disabled" badge; chat mic hidden; enable button | C | `addons/nexus-keyboard/src/kb_options.cpp` | 2.6.1, 2026-10-25, skipped |
| **UI-01** | UI | Tab navigation & responsive layout | All 6 tabs open cleanly; minimum and maximum scale legible | C | `addons/nexus-keyboard/src/kb_options.cpp` | 2.6.1, 2026-10-25, skipped |
| **UI-02** | UI | Live UI language switching across 8 languages | 0 missing, 0 unused keys; instant text refresh | A/C | `src/core/i18n_*.*` | 2.6.1, 2026-10-25, pass |
| **UI-03** | UI | Dashboard module cards state and action links | Active / Disabled / Optional badges correct; buttons navigate | C | `addons/nexus-keyboard/src/kb_options.cpp` | 2.6.1, 2026-10-25, skipped |
| **UI-04** | UI | Right-click chat tab rename | Rename popup saves; resets to default; survives game restart | C | `addons/nexus-keyboard/src/kb_window.cpp` | 2.6.1, 2026-10-25, skipped |
| **UI-05** | UI | Theme: accent, CBA, auto contrast | Pills and badges follow theme; text legible on snow and dark caves | C | `addons/nexus-keyboard/src/kb_theme.cpp` | 2.6.1, 2026-10-25, skipped |
| **UI-06** | UI | Animations: card glow and hover effects | Smooth frame-rate independent lerp without third-party bloat | C | `addons/nexus-keyboard/src/kb_options.cpp` | 2.6.1, 2026-10-25, skipped |
| **UI-07** | UI | Window logic & automations | Automatic window behaviors remain switchable; nothing forced | C | `addons/nexus-keyboard/src/kb_options.cpp` | 2.6.1, 2026-10-25, skipped |
| **UI-08** | UI | ImGui internals clean | PushFont/PopFont and Begin/End balanced; zero stray windows | C | `addons/nexus-keyboard/src/kb_window.cpp` | 2.6.1, 2026-10-25, skipped |
| **REL-01** | REL | 8-Gate verification (`python tools/verify_release.py`) | All 8 quality gates pass without errors | A | Entire codebase | 2.6.1, 2026-10-25, pass |
| **REL-02** | REL | Version number and documentation synchronization | Version tags, CHANGELOG, and README align | A | `CMakeLists.txt`, `README.md` | 2.6.1, 2026-10-25, pass |
| **REL-03** | REL | Nexus Library install path contract | Download, install, and load via Nexus library | C | Release package | 2.6.1, 2026-10-25, skipped |
| **REL-04** | REL | Extras packages and module assets | Assets carried along from dedicated extras release | A | `tools/pack_release.py` | 2.6.1, 2026-10-25, pass |
| **REL-05** | REL | Maintainer personal in-game eye-test | Physical maintainer validation before public release | C | Entire add-on | 2.6.1, 2026-10-25, skipped |

---

## 5. Change-Scoped Execution Mapping

When running change-scoped validation (`python tools/verify_release.py --changed`):
- `src/core/`: Evaluates `MT-01`, `MT-02`, `MT-08`, `TYPE-01`, `TYPE-04`, `UI-02`.
- `src/app/`: Evaluates `CFG-01`, `CFG-02`, `CFG-03`, `TYPE-03`, `TYPE-04`.
- `addons/nexus-keyboard/`: Evaluates `START-01`, `START-03`, `VOICE-03`, `VOICE-04`, `UI-01`, `UI-03`, `UI-04`, `UI-06`, `UI-07`, `UI-08`.
- `tools/`: Evaluates `START-04`, `REL-01`, `REL-02`, `REL-04`.
