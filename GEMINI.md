# AGENTS.md – for every coding model (Gemini, Codex, …)

This repo keeps its whole context in two files. Read both before changing anything:

1. `CLAUDE.md` – architecture, data flow, **invariants (binding)**, conventions, pitfalls already hit, how to test.
2. `docs/OCR_PLAN.md` – the current roadmap and the open review points.

Hard rules, short:
- Small steps; each one: build, `build\Release\core_tests.exe` (0 failed), `python tools/i18n_check.py` (0 missing) when
  UI text changed, `typing_bench` (0 surprises) for typing changes, `ocr_bench` before/after for reading changes. Keep
  only what the measurement shows is better.
- Nothing in the GW2 process; never send without the user's key press; never read our own window.
- Never commit real screenshots, captures, OCR output or other players' names/messages (public repo; use `local\`).
- **No push and no release without the user's explicit OK.** Another model reviews before anything is published.
- Update `CLAUDE.md` / `CHANGELOG.md` ("## Unreleased") when you learn or change something the next model needs.
