# OCR plan (from 2026-10-09) – HDR first, then compare the engines

Hand-over for any coding model (Claude, Gemini, …) working in this repo after a `git pull`.
**Read `CLAUDE.md` first – its invariants and conventions are binding.** This file says *what* to do next and *how*.

## Ground rules (do not skip)

- Small steps. Every step: build (`cmake --build build --config Release`), `build\Release\core_tests.exe` (0 failed),
  `python tools/i18n_check.py` (0 missing) when UI text changed, `ocr_bench` before/after when reading changed, one
  small commit with a clear message. **Only keep what the bench shows is better** – measured, not assumed. Earlier
  "obvious" improvements (contrast stretching, `PrepareForOcr`, pre-enlarging) were measured and made things worse.
- `src/core` stays portable: no `windows.h`, must build on Linux (`g++ -std=c++17 -I src tests/core_tests.cpp src/core/*.cpp`).
- No OpenCV, no shaders, no new big dependencies. The own `Image` type (core/image) is enough for ~0.2 megapixels.
- **Never commit real screenshots, captures, OCR output or other players' names/messages** (the repo is public).
  Real test pictures live in `local\` (git-ignored). Tests in `tests/` use invented names.
- UI text: English in code, always `Tr(L"…")` / `TrF(L"… {1}", {x})`, plus German and Arabic entries in
  `src/core/i18n_de.cpp` / `i18n_ar.cpp`, then `python tools/i18n_check.py`.
- Nothing in the GW2 process, no handle to the game process (invariant 1). The reader never reads our own window
  (invariant 5).
- Do not push or publish a release unless the user says so. Update `CLAUDE.md` (pitfalls / data flow) when you learn
  something the next agent needs, and `CHANGELOG.md` under "## Unreleased".
- Write code like the surrounding code: comment density, naming, idioms.

## Scope

- **OCR reads only the native GW2 chat (and the free screen area).** GW2 itself can only show Latin script (plus
  accents), so the reader needs no Arabic/Cyrillic/CJK models. Other scripts matter only for the *keyboard/typing
  help*, which already handles them – leave that alone.
- The user's PC: Windows 10 (19045), i7-5820K, 8 GB VRAM. On Windows 10 the tool captures with **DXGI**, not WGC (WGC's
  yellow frame cannot be switched off there). GW2 is mostly **CPU-bound** (one main thread) – keep our CPU use low.

## Roadmap (in this order)

### Step 0 – Measure first (prerequisite for everything)

0.1 Settings → Technical → "Compare recognition": show the **raw engine text** (no second look, no garble repair, no
    paragraph building) plus milliseconds per engine.
0.2 `tests/tools/ocr_bench.cpp`: env knobs `BENCH_SCALE` (1–4), `BENCH_PROJECT` (colour projection on/off, step 2),
    `BENCH_WINLANG` (de-DE / en-US). Add a **hybrid row** (step 3) with error rate and time.
0.3 **16-bit test pictures:** a raw format for FP16 captures (e.g. `name.f16` = small header width/height + raw
    `R16G16B16A16_FLOAT` rows) that `ocr_bench` can load next to `name.png` + `name.txt`. Add a hidden/technical
    switch that saves the next capture of the chat area as `.f16` into `local\` style data folder (never into the
    repo). The user then records: their game resolution, HDR on and off, a busy background (WvW / meta event) behind
    the half-transparent chat, and writes the true text into `name.txt`.

### Step 1 – HDR capture (priority)

Background: with Windows HDR on, the desktop is composed as linear FP16 (scRGB, 1.0 = 80 nits). SDR content like GW2
sits in it scaled by the "SDR white level" (the user's SDR brightness slider; 240 nits → white = 3.0). Today all
capture paths (DXGI, WGC, GDI) take 8 bit and let Windows convert – washed-out text, lost edges.

1.4 **(do this first, cheap)** No permanent GDI: today, after 3 DXGI errors the tool stays on GDI until restart
    (errors happen e.g. when HDR is switched or a UAC dialog appears; GDI is the worst path with HDR). Retry
    DXGI (and WGC where used) every 30 s, remember the fallback reason, show it on the technical page.
1.1–1.3 **Done (2026-10-09, DXGI only, `screen_capture.cpp`: `SdrWhiteFactor`, `HdrLut`, FP16 path in
    `CopyArea`). Still to do: measure with real HDR captures (0.3), 1.4, 1.5.**
1.1 Detect HDR: `IDXGIOutput6::GetDesc1`, `ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020`.
1.2 HDR on: DXGI via `IDXGIOutput5::DuplicateOutput1` with formats `{DXGI_FORMAT_R16G16B16A16_FLOAT,
    DXGI_FORMAT_B8G8R8A8_UNORM}`. (WGC: frame pool with `DirectXPixelFormat::R16G16B16A16Float` – only relevant on
    Windows 11.) Extend `CopyArea` / `Dxgi::Grab` in `src/win/screen_capture.cpp` for FP16 – the GPU still copies only
    the chat area into the staging texture (already done right).
1.3 Convert while mapping (the only place where the 16-bit data exists): SDR white level via
    `DisplayConfigGetDeviceInfo(DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL)`, factor = value / 1000.
    A 65 536-entry LUT: half → ÷ factor → clamp 0..1 → sRGB OETF → 8 bit. Rebuild on `WM_DISPLAYCHANGE` and every few
    seconds (the slider can move). Output BGRA8 exactly as today – the rest of the pipeline stays unchanged.
    **Not** Reinhard or any tone mapping (bends the curve and the letter edges), **not** histogram stretching
    (measured worse: Windows OCR 0.5 % → 10 %). The LUT conversion lives in core (portable, testable): a pure function
    `HalfToSrgb8Lut(double factor)` + tests in `core_tests`.
    Auto HDR on GW2 can push highlights above SDR white – they clip after the division; white text stays white. Only
    if the bench shows problems: per-row normalisation.
1.5 Technical page: "HDR on/off · SDR white N nits · capture FP16 / 8 bit / GDI" (+ fallback reason).
1.6 Keep `OcrZoom` (default 0 = automatic `OcrScaleFor`) as a manual emergency setting; nothing to remove.

### Step 2 – Preparation per row (after HDR; bench-gated)

2.1 Colour projection per row after `FindLineGrid`: per pixel the closeness to the nearest palette colour against the
    background → dark text on white. A GW2 row has several colours (name white, text in the channel colour, item links
    in rarity colours): **project onto the whole palette, never onto one colour.** Palette = the user's calibrated
    channel colours (the existing "this colour is …" calibration / `ChannelColor` palette – players can change channel
    colours in GW2) + white + system yellow + item rarity colours. The black outline of the GW2 font helps (see the
    glyph reader's edge factor in CLAUDE.md). Portable in core, tests in `core_tests`. Keep only if the bench improves
    for both engines.
2.2 RapidOCR row cache (`rapidCache_`): hash the **projected, quantised** row instead of raw pixels – the game world
    moves behind the half-transparent chat, so raw hashes never hit in the game.
2.3 Reader thread `THREAD_PRIORITY_BELOW_NORMAL`; ONNX Runtime session option
    `AddConfigEntry("session.intra_op.allow_spinning", "0")` (`src/win/rapid_ocr.cpp`, 2 intra-op threads already set) –
    otherwise ORT threads busy-wait and burn CPU the game needs.

### Step 3 – Compare engines, then hybrid (bench decides)

Known numbers (ocr_bench, parsed error rate): GW2 chat 4K – Windows 0.5–3.4 %, Rapid 5–17 %; simulated 1080p /
small text – Windows 63–81 %, Rapid 8–31 %. Windows ~90 ms per picture; Rapid 1–11 ms only with cache hits.
Re-measure after steps 1–2 on the user's real captures before deciding.

3.1 If Rapid wins as base: Rapid every picture; Windows OCR only for **new rows** (cache misses), on their projected crop.
3.2 Merge per word via x boxes: equal → take it. Different → a *sure* glyph-reader word wins → else the one that is a
    dictionary word / name / keep-word / GW2 term → else Rapid, word marked unsure.
3.3 Tesseract no longer part of "automatic", only selectable by hand.

### Step 4 – Post-processing

4.1 `IsWord`: also try with a capital first letter.
4.2 `LooksGarbled`: gamer abbreviations are no reading errors (LF1M, LF2M, f2p, p2w, b2b, 1v1, …) – a list in
    `core/slang`.
4.3 Dictionary repair only when the second engine confirms it.
4.4 `ocr-fixes.txt` separated by mode (chat / free area) and row height; applied only after 3 confirmations.
4.5 Hyphen at the end of a line (`chat_line.cpp` ~482): keep it when a digit precedes it or a capital follows.
    Do **not** tighten the "< 3 letters" fragment rule – "gg", "ty", "np" are real messages.

### Step 5 – Glyph reader (GW2 chat only)

5.1 Learn from own messages: when an own line is recognised (`recentSent_` in main_window), learn the exact sent text
    (not normalised) word by word with `GlyphReader::LearnWord` at the OCR boxes – only with the same word count and an
    OCR reading ≥ 80 % similar. Exact ground truth, no guessing.
5.2 Calibration sentence in the menu (all letters, umlauts, digits, punctuation) that the player sends themselves
    (their own key press – invariant 1).
5.3 A sure glyph reading has priority in 3.2.

### Step 6 – Display

6.1 Free area: order paragraphs by screen position instead of by appearance.
6.2 Display scheme: the rules come from the user later. Count what is dropped and make it showable.

### Step 7 – Local LLM on CPU (`src/win/online_translators.cpp`)

7.1 Local Ollama via its own `/api/chat` instead of `/v1/chat/completions` (only there `options` work):
    `options: {num_gpu: 0, num_thread: 2}`, `keep_alive`. Keep `/v1` for every other OpenAI-compatible server.
7.2 Model offers (`LocalModelOffers`) with RAM/CPU notes instead of VRAM; default ~1B class (e.g. gemma3:1b),
    instruct models only (no R1 / "thinking" models – measured: DeepSeek-R1-Distill 1.5B returned the German text
    unchanged after 165 thinking tokens). Cloud stays the default (Arabic and quality); local stays optional.
7.3 Settings → Translator → LLM: a "Find local server" button: Ollama (11434), LM Studio (1234), a running
    `llama-server` (e.g. Unsloth Studio starts one on a random port; its OpenAI API needs no key, Unsloth's own port
    8888 needs auth) – fills in URL and model (`/v1/models`).

## Also open (typing help, small)

- "ohje" is an interjection (sigh) – never a typo; "micih" → "mich" is a letter one place too early/late – add it as a
  slip kind in `LooksLikeSlip` (`src/core/word_model.cpp`) with tests. Run `build\Release\typing_bench.exe` after
  every typing change: SURPRISES must stay 0.

## Things that are wrong in older notes – do not do them

- OpenCV / `cv::Mat`; Reinhard tone mapping; histogram stretching; "just check the colour space" (you must capture in
  16 bit); Google API key in the URL (`?key=` – the repo sends it in the `X-Goog-Api-Key` header on purpose); rotating
  several free accounts' keys to stretch free tiers (against the providers' terms).
