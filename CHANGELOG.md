# Changelog

The version lives in `CMakeLists.txt` (`project(... VERSION x.y.z)`); the exe, the technical page and the
diagnosis show it.

## 1.1.0 — 2026-10-09

- **HDR screens:** with Windows HDR on, the chat is now captured in 16 bit and converted exactly (SDR white level,
  sRGB curve) instead of Windows' own 8-bit squeeze that washed the letters out. Switched on only when the screen is in
  HDR mode (DXGI); SDR screens keep the plain path with no extra work.
- **Hybrid OCR engine:** combines RapidOCR speed and caching with Windows OCR word-by-word verification on new lines.
- **Second-look confirmation:** dictionary repairs now require confirmation from the secondary engine before accepting suggestions.
- **Scoped OCR fixes:** persistent OCR repairs in `ocr-fixes.txt` are scoped by reading mode and row height, and require 3 confirmations before saving.
- **Trigram context (Dreierfolgen):** typing help, fuzzy completion and autocorrection now consider the sentence context (`prev2` and `prev`) to rank context-fitting words first (e.g. "seid ihr ..." -> "bereit").
- **Glyph reader learning:** learns the GW2 chat font from sent messages and font calibration sentences.
- **Local AI discovery & CPU inference:** Ollama `/api/chat` support and automatic discovery of running `llama-server` instances on dynamic ports.
- **Wrapped line hyphen joining:** wrapped words ending with a hyphen are joined cleanly unless preceded by a digit or followed by a capital.
- **Gamer abbreviations:** common gamer terms (LF1M, f2p, 1v1, etc.) are recognized and no longer treated as garbled text.

## 1.0.1 — 2026-10-08

- **Screenshots of the tool:** menu ≡ -> "Show this tool on screenshots (1 minute)". Our windows are normally hidden
  from every screen capture (also Win+Print and the Snipping Tool) so the reader never reads itself; for one minute
  they are visible, reading pauses meanwhile, then everything is as before.

## 1.0.0 — 2026-10-08 (Release 1)

- **Everyday words from the first letter, like a phone keyboard:** the typing help now also uses Windows' own word
  prediction (the one of the touch keyboard, offline, for every installed input language): "Sch" offers "schon",
  "schön"; after "wie" a "g" offers "geht's"; after "bin" "gl" offers "gleich". Your own words and names still come
  first; from 4 letters a GW2 word wins ("Tequ" -> "Tequatl"). Space writes Windows' best guess only while what you
  typed is not a word yet – still 0 surprises in `typing_bench`. Languages without the Windows input language
  installed get no such suggestions (Windows Settings -> Time & language -> Language: add the language).
- The grey suggestion shows before anything is learned (starter and chat words for clearly unfinished words).
- Footer: "Write: <language>" (what is corrected and suggested) and "Send: <language>" always side by side.
- `typing_bench --probe de-DE "Sch" "wie g"` prints what a fresh tool offers for these inputs.
- **Typing profile:** Settings -> Writing -> "Learn from my texts…" learns your own texts (.txt) – your words, word
  pairs and your typical typos with what you meant ("shon" -> "schon"); stays on this PC. Typos are also remembered
  from every correction you keep while typing (Backspace forgets it again) and are then corrected without guessing.
- **More freedom:** Space only fixes real finger slips (key next door, swapped, doubled or left-out letter inside the
  word, and only the dictionary's first idea). Changed endings, spoken German ("habs", "gehts") and common English
  chat words ("with", "good", "thanks") stay as you wrote them.
- Spoken German contractions go to the translator written out ("habs" -> "hab es"); a German chat gets them as written.
- Switching the reading language translates the lines already shown (newest 50) again into the new language.
- The "Send" menu offers only languages the GW2 chat can show (Latin script).

## 0.9.1 — 2026-10-08

- **The typing help, measured and tuned with a simulated player** (`tests/tools/typing_bench`: hundreds of typical
  chat messages typed letter by letter with the real logic and the Windows spell checker):
  - **Space never changes a word you typed correctly any more** – before, 42 times in 1200 messages ("abend" ->
    "Abend", "tequatl" -> "gequält", "me" -> "Meta"); now 0. Space writes only your own words (learned, names) or a
    clear slip on the keyboard (a key next door, two letters swapped); the dictionary's ideas, the starter list and
    words of the chat are offered with Tab. Writing small is never an error ("abend" is fine in a chat).
  - Without a dictionary for the language: completion of your own words from 3 letters, no autocorrection (every new
    word would look like a slip).
  - The grey suggestion starts with the first letter.
  - Result: about **50 % fewer key presses** over the first 600 messages, **60 %** once it has learned.
- **Whispers arrive as whispers:** a reply in a whisper tab ("/w Name, text") is now sent the way you do it by hand –
  "/w " typed, the name pasted as the address and confirmed with Tab, then the message. Pasted in one go, GW2 took the
  name as part of the text.
- **Up / Down** in the input brings back what you sent before (like a command line, this session).
- **Teach names:** right-click a word (input or word bar) → "“Fallen” is a name". From then on typing "fallen" offers
  **Fallen** first (Space) and **fallen** right after (Tab), and the name is **never translated** ("hi Fallen" no
  longer arrives as "hi fall"). Kept exactly as taught – the verb "fallen" is still translated. Stored in my-names.txt.
- The word memory writes a word the way you write it **most of the time**: one name "Fallen" no longer capitalises
  the verb "fallen"; a noun you always write with a capital keeps it.
- **Fixed: messages glued together and old ones coming back.** Timestamps the recognition mangled ("CO•.54JCWJ" for
  "[10:54][W]", "CIIOOJCWJ" for "[11:00][W]") were not seen as the start of a message, so several lines became one –
  and every picture glued them a little differently, so they came again as "new". The shape of a timestamp is now
  recognised however its characters were misread.

## 0.9.0 — 2026-10-07

**The glyph reader** – the GW2 chat has one fixed font; the tool now learns its letters and reads them exactly,
instead of only guessing with a general text recognition:
- **Learns while you play:** every word the normal recognition reads in the chat that is a real word teaches its
  letters, right where it stands in the line (names and slang in the same line do not matter). A letter is only taken
  when it also fits the letters already known. Stored per text size in `glyphs\` (letter shapes only, no text).
- **Reads exactly:** a chat line is read as the best sequence of known letters that explains all of its ink – letters
  that touch need no gap, a letter in two faint parts stays one letter. GW2's black outline around every letter tells
  text from the game shining through the panel. A line replaces the normal reading **only when every letter is
  clearly that letter**; everything else is read as before – it can only get better.
- Measured on real 4K chat pictures, learned from other pictures than the one read: every line it was sure of was
  exactly right; the normal recognition reads only 11 % of the lines exactly, together it is 14 % – with letters learned
  from two pictures. In the game it learns from hundreds of lines every evening. ~7 ms per line, lines already seen
  come from a cache.
- Settings → Technical shows how many letters it knows and how many lines it read itself.

**Logic, checked as mathematics**
- **Core logic written down as mathematics** (`docs/MATH.md`), and two mistakes found that way are fixed:
  - one accidental word triple no longer outweighs a word you use all the time – a context now weighs by how often
    it has been seen (S/(S+1)), for completions and the next word;
  - the telltale words that recognize short foreign lines overlapped between languages ("je" is Dutch too, "mille"
    French, "porque", "amigo", "vamos" Portuguese) – removed.
  - chat slang like "2day", "4ever" was taken for word salad and left out; emotes like "*grins*" lost their first
    star; a speaker's name ("Marco:") could be "repaired" into a dictionary word – all fixed;
  - completions with a typo use the same weights as the others.

## 0.8.3 — 2026-10-07

**Writing**
- **Phrase memory:** a phrase you write again and again is offered whole – after "gute nacht " the grey rest
  "bis morgen mit micro" appears, **Tab takes all of it**, → one word at a time, typing on ignores it. Only when every
  word of the rest is sure on its own (seen 3+ times, 60 %+), at most 6 words, never going round in circles.
- **Learning for the long run:** old habits fade much more slowly now (every 500 messages by 5 %): a phrase you wrote
  three times stays offered for over a month (at ~50 messages a day), even if you do not use it in between.

**Translating**
- **GW2 abbreviations, kept apart from ordinary words:** many more short forms from the GW2 wiki's list stay
  untranslated and are never marked wrong ("use tp", "cc", "lfg fotm t4", "cdps", "vindi" …). Letters that are an
  ordinary word somewhere are kept only when written like an abbreviation: "LA", "HoT", "DE", "CM" stay, "la casa",
  "it's hot", "de" are translated.
- Fixed: "might", "hot", "mes" and "ele" were never translated, even as ordinary words ("I might come", "mes amis",
  Portuguese "ele").

**Kassiopeia** 🐢
- The tool is dedicated to Kassiopeia, the tortoise from Michael Ende's *Momo* (see the README).
- A new program icon as a first sketch: her shell seen from above is the icon's edge, glyphs glow on it. A finished
  icon will follow.

## 0.8.2 — 2026-10-07

- **Translate again:** click the text of a message (or right-click → "Translate again") and it is translated anew –
  also when it was already translated and did not fit. The name still opens the whisper tab; a message with a link
  offers "Translate again" next to opening the link.
- The grey suggestion is back to Latin script only (the languages GW2 itself shows). Arabic, Cyrillic, Chinese keep
  the word bar and the spell checker as before – the right-to-left grey suggestion of 0.8.1 is removed.

## 0.8.1 — 2026-10-07

**The learning keyboard, fine-tuned**
- **Learns when you choose:** a word you take from the word bar, or a grey suggestion you take (Space, →, or Tab to
  another one), gets stronger right there – at exactly this place in the sentence. Picking on purpose (a click, Tab to
  the second suggestion) counts more than going on with the first. What you do not take simply falls back.
- **The next word before you type it:** when your last words are (almost) always followed by the same word – seen 3+
  times and at least 60 % of the time – it appears grey after the space ("kommst du |mit"). Tab or → takes it; just
  type on to ignore it.
- **Context outweighs frequency:** suggestions mix how often a word follows your last two words, your last word and
  how often you use it at all as shares (as keyboards' n-gram models do) – "kommst du m" offers "mit" even if you
  write "mal" much more often.
- **Old habits fade:** every 200 messages the learned counts shrink a little (forgetting by use – nothing fades while
  you do not write), so new habits win quickly. A word you used twice stays known and is never corrected away.
- **Grey suggestion for Arabic and Hebrew** too (right to left: it appears left of the last letter).

**Tidied up**
- Resetting the channel colours is reachable again: right-click a line → "This line colour is" → "Reset all to the GW2
  colours".
- README: a table of the five helpers (completion, next word, autocorrection, grammar, translator) – what each looks
  at, does and never does.
- Removed: main-menu commands that were no longer in the menu and their texts, an unused function, a Python cache
  file that had slipped into the repository.

## 0.8.0 — 2026-10-07

**Writing**
- **The sentence so far counts:** the next word and completions look at the last *two* words you typed, not only
  the last one ("kommst du" -> "mit", while "du" alone would offer "da"). Learned from your messages like before;
  older word files keep working.
- **Suggestions know the conversation:** names of the people in the chat and the words of the last lines (foreign
  lines through their translation, in your language) are offered right after your own words – "Ki" -> "Kiro",
  "Teq" -> "Tequatl" when someone just asked for it. Only offered, never learned.
- **Key presses saved** (Settings → Writing): the word help is measured – keys you press compared with the letters
  you send, today and in total ("Today 38 % (120 keys for 195 letters)"). Pasted text is not counted.

**Translating**
- **"Translate once now"** (menu ≡): one look at the chat or screen area, what is foreign is translated, then reading
  stops again – scroll the GW2 chat up and translate something older. It ends as soon as the pictures are read. The
  first menu entry is now called "Automatic translation (permanent)".
- **Only what needs translating:** a line is translated only when its language is clear (another script, at least
  three words the Windows language detection is sure about, telltale letters like ñ, ß, ç, or words of one language
  only like "merci", "gracias", "grazie mille"). Short or unclear lines ("ok np", names) cost nothing.
- **"Show only translations"** (menu ≡ and Settings → Reading, on by default): lines in your languages, unclear ones
  and system lines no longer appear. While the window lies over the GW2 chat it shows every line – it is your chat then.
- **No more word salad:** words nobody types ("syn!ax", "g9danken", "!raining", "9QEine") are always checked:
  characters the recognition confuses are tried (! -> t, 9 -> g/e, 0 -> o, p <-> o, rn <-> m …) and the Windows
  dictionaries pick the real word ("syntax", "gedanken", "training", "putput" -> "output"); else the word is read once
  more, enlarged, or the dictionary's suggestion is taken. What is still garbage is left out instead of shown; repairs
  are learned. Measured on real GW2 captures: no loss.
- **Screen area: only the active part.** The whole area is read once; after that only the lowest 6 lines count (where
  new text appears), so typing below no longer brings back old fragments from further up. A word cut off at the edge
  of the area is left out – but a frame drawn tightly around a text column keeps the first word of each line.

**Installing and updating**
- **Installing like other programs:** the setup installs for your Windows user (`%LOCALAPPDATA%\Programs`, no admin
  rights; ticked by default), then the downloaded copy ends and the installed one starts. Autostart and shortcuts
  always point to the installed copy, so settings and learned words never end up in the download folder.
- Setup: desktop shortcut, **start-menu entry** (also found by Windows search) and autostart are three free choices;
  starting by hand is the default. The start-menu entry comes back by itself if it goes missing, unless you untick it.
- **Updating:** start a newer downloaded version and it offers to update the installed copy (a running old copy is
  closed; settings, learned words and corrections stay); the same or an older download simply starts the installed one.
- Fixed: a folded, docked window jumped to the top of its place several times a second.
- README rewritten for the current state (install, keys, translators, files).

## 0.7.0 — 2026-10-07

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
- **Correction memory**: right-click a translated line → "Correct this translation…". That text gets your
  translation from then on (with every translator); a few changed words are also corrected in later translations.
  Export / import in Settings → Writing (share it with friends). Stays on this PC.
- **Safety** on the technical page: what the tool does and does not do, and where texts go right now. API keys are
  stored encrypted for your Windows account (DPAPI) instead of plain text.
- **Compare translators** (technical page): the same invented chat lines through every translator that is set up,
  with the time – judge yourself which reads naturally.
- Free screen area reads better: dark text on light backgrounds is measured too, the text recognition keeps its own
  lines and side-by-side columns (sidebar, text, picture) become separate paragraphs; at least double enlargement for
  small app fonts; a paragraph that grows (someone types, text streams in) updates its entry instead of adding new
  ones, a slightly different reading of the same text is ignored.
- Settings: no text is cut off any more (all 480 texts measured in English, German and Arabic with the dialog
  font); opened lists are as wide as their longest entry.
- **Word suggestions like Grammarly**: the likely word is written grey after the cursor and narrows with every
  letter. Space writes it and goes on, Tab shows the next suggestion (Shift+Tab back), → at the end takes it
  without a space, Esc hides it. The dropdown at the cursor is gone; the word bar above stays.
- **"I write in"** (Settings → Writing): spelling, learned words and suggestions follow the language you type in
  (default: the keyboard layout's). The GW2 starter words are split by language – no English suggestions while
  writing German.
- Grammar check: choice of provider (LanguageTool free without an account, or your own server), a "Test" button
  that shows whether it is connected, and what it does (whole message, blue marks).
- MyMemory e-mail: says clearly that any address of yours works, no registration.
- **RapidOCR**, a third text recognition (open source: PaddleOCR models, Apache-2.0, on ONNX Runtime, MIT),
  running only on this PC. The Latin model (EN, DE, FR, ES, IT, PT, NL, PL, TR …) ships with the program; other
  scripts (Cyrillic, Arabic, Korean, Chinese/Japanese) are downloaded for your languages in Settings → Reading
  (official files, checksum checked). Measured: clearly better than Windows OCR with small text (1080p: 8–31 %
  errors instead of 63–81 %), Windows OCR stays better at 4K. Lines that did not change are not read again: after the
  first picture (~1–2 s) it needs only a few milliseconds. "Automatic" uses it for small text.
- **Compare recognition** (Settings → Technical): one picture of your chat read by every recognition, with the
  time – see yourself which reads your chat best.
- The installed copy takes your words, corrections, learned fixes and the RapidOCR files along.
- Settings → Reading the chat tidied up: reading on/off is a small dot in the window header (grey = on, black =
  off) instead of a checkbox here; text recognition and picture modes explained ("?"), with your resolution and
  which recognition fits it; Tesseract shows only whether it is found; Chinese for Tesseract follows your languages;
  reading interval 200–2000 ms with a hint; "filter system messages" (on by default) instead of "show".
- **Smart artifact correction** (was "second look") now **learns**: a recognition error it fixed once
  ("rnain" -> "main") is fixed at once next time, also after a restart (`ocr-fixes.txt`, editable and shareable
  under "Learned…").
- Settings → General tidied up: both language lists in one row; "do not translate" gets a language list instead of
  a text field; the channel choice ("translate in") and the "send as" list are gone (everything read is translated;
  the window's write menu offers every language and shows "Chat: …" by itself for scripts GW2 cannot show). Text
  size in points (8–32, slider or typed) and a choice of readable Windows fonts. The hotkey is picked with a key
  field, can be "None", and says whether Windows already gives it to another program; new default
  Ctrl+Alt+Shift+T.
- **Fixed (privacy):** the free screen area could read our own settings dialog (including the MyMemory e-mail field)
  and send it to the translator; on Windows 10 a dialog over the GW2 chat could be read too. All our dialogs and the
  word dropdown are now excluded from screen capture, and reading pauses while any dialog, menu or message box of
  ours is open.
- Free area: stricter noise filter (UI icons and half-hidden letters are no longer translated).
- **My words**: teach the tool your slang ("finds = finde es"): right-click a word in the input box → "Explain …",
  or the list in Settings → Writing → "My words…". Before translating (your messages and incoming lines) the word is
  replaced by its meaning, so every translator gets plain language; it is never underlined or autocorrected and the
  second look leaves it alone. Stays on this PC (`my-words.txt`).
- **Second look** at words that make no sense: a word the dictionaries do not know is read once more, cut out and
  enlarged more; the new reading is taken only if it is then a real word close to the first ("*ain" -> "main"),
  otherwise the text stays as written (names, slang). Counters on the technical page; switch in Settings → Reading.
- Setup: "Language of this window" offers the same full list as "Translate the chat into", both starting with
  "Windows language" (the window itself shows English, German or Arabic; other choices show English for now).
  New: shortcut on the desktop. The two text paragraphs are gone.
- **Small local translator with only your languages**: "Set up on this PC…" (own server) prepares LibreTranslate with
  `--load-only` for your reading, writing and chat languages (about 100 MB each, CPU only).

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
