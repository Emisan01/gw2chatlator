# Nexus Addon Manager Integration Rules

This document is the binding checklist for Gemini and every coding agent working on the Nexus install, update, or load integration of GW2Chatlator. The target is the **Nexus Addon Manager's in-game Library install path**. Successfully loading a DLL that was copied into `addons` by hand does not prove that Library installation works.

## 1. Identity is a single contract

These values must agree across the official Raidcore Library entry, the DLL returned by the GitHub release, the embedded Nexus definition, and the installed filename:

| Field | Required value |
|---|---|
| Raidcore Nexus Addon Manager ID | `3098852796` |
| Nexus signature | `0xB8B4BDBC` (`uint32_t`, decimal `3098852796`) |
| Embedded add-on name | `GW2Chatlator` |
| Installed DLL filename | `GW2Chatlator.dll` |
| GitHub repository / update link | `https://github.com/Emisan01/gw2chatlator` |
| Update provider | `UP_GitHub` |

Do not substitute the internal CMake target name `gw2keyboard_nexus` for the public identity. The target may retain that name for build compatibility; the emitted DLL and every shipped standalone DLL must be named exactly `GW2Chatlator.dll`. Never emit, publish, package, or use `gw2keyboard_nexus.dll` as a fallback.

## 2. Mandatory source locations and values

Before changing integration code, read `CLAUDE.md` and inspect these locations:

- `addons/nexus-keyboard/src/kb_main.cpp`, `GetAddonDef()`:
  - Export must have the exact C ABI symbol `GetAddonDef` (`extern "C" __declspec(dllexport)`).
  - Return a stable, fully initialized `AddonDefinition_t` whose storage remains valid after return.
  - `Signature` must equal `0xB8B4BDBC`.
  - `Name` must be `GW2Chatlator`.
  - `Provider` must be `UP_GitHub` and `UpdateLink` must be the repository URL above.
  - Preserve the valid API version, version fields, callbacks, flags, author and description. Do not leave required fields uninitialized.
- `addons/nexus-keyboard/CMakeLists.txt`:
  - Keep the produced shared library output name `GW2Chatlator` with no `lib` prefix, so the Windows artifact is `GW2Chatlator.dll`.
- `tools/pack_release.py`:
  - Accept only `build/addons/nexus-keyboard/Release/GW2Chatlator.dll` as the add-on binary.
  - The standalone output is `build/dist/GW2Chatlator.dll`.
  - `GW2Keyboard-Nexus.zip` must have exactly `GW2Chatlator.dll` at archive root; optional support files stay under `GW2Keyboard/`.
  - Fail loudly if the correctly named build artifact is absent. Never silently rename an obsolete DLL into apparent validity.
- `tools/verify_release.py` and `README.md`:
  - Keep release metadata and verification aligned to the exact published asset name and repository URL.

## 3. Release and Library download contract

The in-game Library installation consumes the public Raidcore Library metadata and its linked download; it is not the same path as this repository's manual ZIP instructions or an already-installed local DLL. For every candidate release, verify the actual online state rather than inferring it from local source or an old log:

1. The Raidcore Library entry is the intended add-on ID and points at the intended repository/release.
2. The GitHub release has the exact expected tag and a published asset named exactly `GW2Chatlator.dll` (not only a ZIP and not a differently cased or legacy filename).
3. The linked asset downloads successfully as a complete Windows x64 PE DLL. Record its byte size and SHA-256.
4. The downloaded bytes match the release/build artifact intended for that tag; do not overwrite a published asset in place and assume all CDN/cache clients see the same bytes.
5. The embedded `GetAddonDef()` identity agrees with the Library entry. Asset filename, embedded `Name`, and Signature are separate checks; passing one does not imply the others pass.
6. Check the exact manager failure from its log and current install destination. Preserve useful evidence (request URL, status/error, final filename, partial/temp-file behavior) before proposing a cache, CDN, or network explanation.

Never claim that CDN propagation, a stale cache, or a Nexus bug caused failure without evidence from the affected Library download attempt. Do not use a successful self-test or a successful manual copy as proof of Library install success.

## 4. On-disk layout and compatibility

- The manager's add-on DLL belongs directly in `<Guild Wars 2>\\addons\\GW2Chatlator.dll`.
- Persistent add-on data remains in `<Guild Wars 2>\\addons\\GW2Keyboard\\`. Do not rename, delete, or migrate this folder as an installation fix; it protects existing settings and learned data.
- For the optional manual `GW2Keyboard-Nexus.zip`, keep the DLL at archive root and support files under `GW2Keyboard/`. This package layout does not replace the standalone DLL asset consumed by the Library entry.
- Do not copy test binaries, debug DLLs, stale artifacts, or files from another configuration into release assets.

## 5. Required evidence for a fix

Gemini must first identify which contract check fails, with concrete evidence. Then make the smallest appropriate code or release-metadata change. A fix is not complete until all applicable items below have evidence:

### Static and artifact checks

- Inspect the resulting PE export table and confirm exact export `GetAddonDef`.
- Read the returned definition using the repository's Nexus lifecycle harness (or an equally direct ABI check): Signature `0xB8B4BDBC`, name `GW2Chatlator`, provider `UP_GitHub`, correct update URL, valid callbacks/version.
- Confirm CMake output, standalone release asset, and ZIP root entry are all exactly `GW2Chatlator.dll`; confirm there is no legacy DLL in `build/dist` or release/package outputs.
- Compare the public GitHub release asset name, size, and SHA-256 with the intended artifact.
- Use `python tools/verify_release.py --changed --plan` and then `--changed` for source changes, following `AGENTS.md`'s change-scoped verification. Documentation-only changes do not require unrelated runtime suites.

### Manager-path check

- Reproduce through the **in-game Nexus → Library → GW2Chatlator → Install** flow. Do not substitute manual file copy, `Load`, the standalone self-test, or a ZIP extraction.
- Capture the Nexus log covering the exact attempt, including the Library metadata lookup and asset request/result. Report whether the final file appeared at `<Guild Wars 2>\\addons\\GW2Chatlator.dll`, and whether Nexus recognized the expected add-on identity after restart.
- If the environment cannot run GW2/Nexus, state explicitly that the Library-path verification remains unperformed. Do not call the issue fixed or infer success from local tests.

## 6. Change control

- Preserve user settings and never remove or rewrite game files as a diagnostic shortcut.
- Do not push, publish, replace a public release asset, or release without the maintainer's explicit approval. Follow the Pre-Release, eight-gate, and in-game eye-test requirements in `AGENTS.md` and `CLAUDE.md`.
- In the final report, separate what was changed, what was verified locally, what was verified against GitHub/Raidcore, and whether the actual in-game Library install was tested. State remaining evidence gaps plainly.

## 7. Known current repository contract (2026-10-24)

At the time this checklist was written, local `GetAddonDef()` sets Signature `0xB8B4BDBC`, Name `GW2Chatlator`, Provider `UP_GitHub`, and the expected repository URL; CMake sets output name `GW2Chatlator`; README directs manual installation to the game's `addons` folder. These facts do **not** establish that the Raidcore Library metadata or the live GitHub asset currently works. Recheck those live endpoints for each diagnosis.

## References

- Project architecture and release rules: `CLAUDE.md`, `AGENTS.md`.
- Official Addon Manager project: <https://github.com/gw2-addon-loader/GW2-Addon-Manager>.
- Manager wiki: <https://github.com/gw2-addon-loader/GW2-Addon-Manager/wiki>.
- Approved Addons repository description: <https://github.com/gw2-addon-loader/Approved-Addons>.
- This repository's integration points: `addons/nexus-keyboard/src/kb_main.cpp`, `addons/nexus-keyboard/CMakeLists.txt`, `tools/pack_release.py`, `tools/verify_release.py`.
