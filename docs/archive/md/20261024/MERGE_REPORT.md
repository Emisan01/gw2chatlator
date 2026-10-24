# Markdown Documentation Consolidation Report (2026-10-24)

## MOVED TO ARCHIVE
Preserved with full history in `docs/archive/md/20261024/`:
- `CLAUDE.md` (44.3 KB, 425 lines of historical development logs) -> `docs/archive/md/20261024/claude_legacy_notes.md`
- `GEMINI.md` (2.2 KB, 100% duplicate of AGENTS.md) -> `docs/archive/md/20261024/GEMINI.md`
- `nexusint.md` (8.2 KB, integration checklist) -> `docs/archive/md/20261024/nexusint.md`

## KEPT AT REPO ROOT
- `AGENTS.md` (Consolidated, minimal Single Source of Truth, 90 lines)
- `CLAUDE.md` (Lightweight pointer redirecting all agents and contributors to `AGENTS.md`)
- `README.md` (User-facing documentation & project presentation; untouched except updated link)
- `CHANGELOG.md` (Release history; completely untouched)
- `LICENSE` (MIT; completely untouched)

## MERGED FROM NEXUSINT (`nexusint.md`)
All critical requirements from `nexusint.md` were merged into `AGENTS.md`:
1. **Identity Contract Table:** Strict match of ID `3098852796`, signature `0xB8B4BDBC`, DLL name `GW2Chatlator.dll`, `UP_GitHub`, `https://github.com/Emisan01/gw2chatlator`.
2. **Data Directory Invariant:** `addons\GW2Keyboard\` must remain to protect existing player settings/models; renaming strictly forbidden.
3. **Separate Contract Checks:** Explicit rule that asset filename, embedded `Name`, and `Signature` are separate checks (passing one does not imply others pass).
4. **Nexus Library Install Path Contract:** Distinction between in-game Library install path and manual DLL copying (manual copy does not prove library install success).
5. **GetAddonDef ABI Contract:** Export signature `extern "C" __declspec(dllexport) AddonDefinition_t* GetAddonDef()`.
6. **pack_release.py Artifact Contract:** Input strictly `build/addons/nexus-keyboard/Release/GW2Chatlator.dll`, output strictly `GW2Chatlator.dll` at archive root.

## UPDATED REFERENCES
- `CLAUDE.md` in root: Converted to a clean pointer directing to `AGENTS.md`.
- `README.md` line 166: Updated `More in [CLAUDE.md](CLAUDE.md)` -> `More in [AGENTS.md](AGENTS.md)`.
- `AGENTS.md`: Stripped self-contradictory "Read CLAUDE.md before changing anything" line; now stands as self-contained Single Source of Truth.
- `addons/nexus-keyboard/src/kb_options.cpp`: Diagnostic message references `(CLAUDE.md / AGENTS.md invariant)` (intact).

## RISKS & SANITY CHECKS
- **Risk of lost technical details:** Eliminated. Full unedited original text of `CLAUDE.md` is preserved in `docs/archive/md/20261024/claude_legacy_notes.md`.
- **Cross-Verification with Code:**
  - `addons/nexus-keyboard/src/kb_main.cpp`: Verified `g_def.Signature = 0xB8B4BDBC`, `g_def.Name = "GW2Chatlator"`.
  - `addons/nexus-keyboard/CMakeLists.txt`: Verified `OUTPUT_NAME "GW2Chatlator"`.
  - `tools/pack_release.py`: Verified `EXPECTED_DLL_NAME = "GW2Chatlator.dll"`.
- **Gate 1 Sanity:** `python tools/verify_release.py --changed` executed and passed Gate 1 (Version consistency).
- **Rollback Safety:** Backup branch `backup/md-cleanup-20261024-1808` exists locally.
