# Changelog

The version lives in `CMakeLists.txt` (`project(... VERSION x.y.z)`); the exe, the technical page and the
diagnosis show it.

## Unreleased

- **Translate any screen area** (menu ≡): frame any text – a website, a document, another game – and everything in
  it is translated; no chat rules, works without GW2. "Read the GW2 chat" switches back.
- **More translators**, each with a "get key" button: Google Translate and Microsoft Translator (free monthly
  contingent with a key), your own LibreTranslate-compatible server (any address). DeepL stays.
- **AI models as presets**: Claude, Gemini (free key), GPT, Mistral, Groq, OpenRouter, local (Ollama) or any other
  address. The key is cleared when the provider changes. Cloud models no longer get a `temperature` they reject.
- The translator page shows only the chosen translator; the AI model and "repairs recognition errors" are one section.
- MyMemory: shorter, clearer privacy notice; the e-mail field explains 50,000 instead of 5,000 characters a day.
- The UI language starts in the Windows language (German, Arabic), else English.
- Fixed: the hint under "Delete everything learned" ran into the next line.

## 0.6.0 — 2026-10-06

**First start**
- Setup is one page (languages, optional install and autostart). The GW2 chat is then found by itself: open it,
  this window recognizes the timestamp lines in the game's bottom-left corner, sets the area and lies over the
  chat. Drawing the frame yourself stays possible.
- On Windows 10 the chat is captured from the screen, so there is no yellow capture frame around GW2 (it cannot
  be switched off there); the game window is captured on Windows 11. Selectable in Settings → Reading.

**Reading**
- Rebuilt on measurements (`tests/tools/ocr_bench`): line grid, dynamic enlargement, words sorted into lines,
  snapping frame with traffic light and preview, hardened timestamp parsing. Windows OCR now reads 4K chat with
  0.5–3.4 % errors after parsing in ~90 ms; "automatic" uses it (Tesseract only for very small text).
- Only chat is read: only on a map (MumbleLink), lines need a timestamp, tag or "Name:", noise and the input line
  are dropped, new lines are confirmed by a second read (double scan). Reading every 0.4 s.

**Translating**
- Everything is translated automatically; exceptions: languages not to translate (e.g. EN, DE) and unticked
  channels. A click on a line translates it anyway.
- MyMemory's free daily contingent is counted and shown small in the footer.
- Newest line first, up to 4 requests at once; letter-based language guess for short lines; names, links and
  smileys are never translated.
- Local models via Ollama with an install button and size/VRAM notes.

**Writing**
- Dropdown under the word: Space takes the highlight, Tab moves through it; typing slips (key next door, whole
  hand one key off) are recognized for the active keyboard layout; Arabic spelling variants fold together.
- Forget a word (right-click), delete everything learned (settings).

**Chat window**
- Names in white, text in the channel colour; no timestamps or tags. Click a name: a whisper tab for that player
  (up to 5). Links open after a confirmation.

**Tools and safety**
- Settings → Technical: every parameter, live numbers, "copy diagnosis" (no chat text).
- Keys are held a few frames when sending (messages got lost). MumbleLink no longer opens a handle to the game.
- Privacy section in the README; test data anonymized; real pictures stay in `local/` (git-ignored).

## 0.5.2 — 2026-10-06
Clean chat output (double read, `LooksLikeChatText`), writing language vs. chat language for scripts GW2 cannot
show, shorter main menu, sliders for text size and transparency.

## 0.5.1
Windows Graphics Capture, raid collapse mode, smart GW2 paths, focus transfer.

## 0.5
English/German/Arabic UI, settings dialog, guided setup, installer and autostart, Tesseract, OCR-tolerant parsing,
phone keyboard with learning, LanguageTool.
