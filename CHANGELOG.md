# Changelog

The version lives in `CMakeLists.txt` (`project(... VERSION x.y.z)`); the exe, the technical page and the
diagnosis show it.

## 2.6.7 — 2026-10-27 (GW2Chatlator for Nexus)

- **Chat Module State Follows Nexus:** after Events: Chat was removed (or unloaded in Nexus), the dashboard kept saying "ready – Events: Chat is loaded" until the game ended – the first chat line of the session had marked it live for good. Removing it or Nexus unloading it now resets that; a line still coming says it is loaded after all.
- **Chat Module Where It Belongs:** while Events: Chat is missing it stands big below the tiles, with its install button and the hint – easy to find at the first start. Once it is loaded only its chip is left, right of "Translation & Languages" (it opens the Technical tab).
- **Tiles Grow With Their Text:** the dashboard tiles were 180 pixels high whatever the font: with Nexus' large font the lines ran under the choosers and out of the tile. They are now as high as their text needs, the choosers as high as the font – also for languages with longer words.

## 2.6.6 — 2026-10-27 (GW2Chatlator for Nexus)

- **One Base List of GW2 Terms:** all built-in terms now live in one file, `data/gw2-terms.tsv` – term, kind and what it means – compiled into the add-on (`tools/gen_terms.py`). It holds the whole community glossary: about 560 terms, each of one kind – never translated ("LFG"), never translated in capitals ("AB", but German "ab"), or translated like any word but never marked as a spelling error ("stack", "Kommi"). New terms come with the next version.
- **Official Names of Several Words Stay Whole:** "Wizard's Vault" became "Assistent" or "Zauberschrank", "Lion's Arch" became "Löwenbogen". About 70 official names – cities, maps, raid wings and bosses, strikes, fractals, dungeons, expansions – are kept as one term, also written in lower case or without the apostrophe ("wizards vault").
- **A Third Try Before Pieces:** when the local model translates a protected part anyway, the sentence goes once more with placeholders the model keeps ("Wir treffen uns am Wegpunkt in X1") – after the try with the words written like names. Each try is checked the same way; only when all fail the sentence goes in pieces. A protected part is checked by itself, not with the marks around it ("Tequatl ?" made a whole sentence fall back to pieces). Measured on 42 made-up chat lines: no protected part lost, all 93 sentences arrive; "Meet at the waypoint in Lion's Arch, then we go to Verdant Brink for the meta." → "Wir treffen uns am Wegpunkt in Lion's Arch, dann gehen wir zu Verdant Brink für die meta.".
- **Names in Brackets Stay Whole:** a name written the way the game shows a link – "[Shipwreck Peak Waypoint]", typed as text – is kept as one part (it became "zum Schiffbruch"); before, only short tags like "[DS]" were.
- **No Invented Dash:** the local model opened some lines as dialogue ("hot area it is" → "- Das ist heiß"); a dash the original did not start with goes.
- **The Quality Step Works and Shows:** "Translate visible chat once" kept the lines it had already translated, so switching to Best changed nothing; and with several lines at once the speed throttle used the fastest step anyway. A local translation now carries its beam ("NLLB-200 (local) · Beam 4" in the status), a line you ask for gets the quality you chose, and another quality translates again.
- **Raw Translation for Comparing:** Technical tab → "Raw translation (until restart)": the translator gets each line exactly as written and its result is shown as it comes – no GW2 terms, names, tags, sentence split, extra tries, clean-up or quality check; chat codes and links stay protected. The visible chat is translated again in the other way, so our logic can be compared with the bare model. Not saved.
- **Your Own Text Says When the Model Guessed:** a local translation of your text the model was unsure of is said in the status, as in the chat ("kkkbl" became "¿Qué es esto?").
- **Your Terms of Several Words:** a guild's name or any term of up to 5 words can be taught (right-click in the input → "A GW2 term"); in `gw2-terms.txt` a note may follow after a tab, and the built-in list there shows what each term means.

## 2.6.5 — 2026-10-27 (GW2Chatlator for Nexus)

- **No More Lost Sentences (local translation):** NLLB-200 dropped sentences when a line held several – measured on 20 made-up chat lines, only 26 of 47 sentences arrived. The local models now translate sentence by sentence, the whole sentence with its protected parts first: 47 of 47 arrive (`translation_bench`).
- **Protected Words Belong to Their Sentence:** a GW2 word at the start or end of a sentence goes to the local model with it – "Gracias por el rez!" became "Danke für ihn. rez!", now "Danke für den rez!". When the model translates a protected word anyway, the sentence is tried once more with the word written like a name, which the model keeps. Protected words come back exactly as written ("wtf", not "WTF"); no full stop the original did not have, no space before a comma or question mark ("Hey, was ist los?") – French keeps its space before ! ? ; :. Measured on 40 made-up chat lines: all protected parts and all 90 sentences arrive.
- **Tags in Brackets Stay Whole:** "[DS]", "[LFG]" and other tags are protected as one part – a keep-word inside a tag no longer tears the sentence apart.
- **One Quality Check for Every Translator:** protected parts present, no sentence lost, not cut short. A doubtful chat translation is shown marked (?) with the reason on hover; a doubtful outgoing translation is said in the status before you send.
- **The Local Model Says How Sure It Is:** the translation helper reports the model's score; a translation it was unsure of is marked (helper capability "score"; older helpers keep working).
- **MyMemory Keeps Protected Parts:** chat codes, names and keep-words (LFG, DPS …) no longer go out raw – when MyMemory changes one, only the parts around them are translated; long texts are split at sentence ends.
- **Names Stay Names:** the names of the people in the chat are left alone inside a translated line (this session, memory only).
- **Chat Reactions in Sentences:** "wtf", "lmao", "rofl" and similar stay as they are inside a sentence too; a comma after a keep-word no longer glues the words together ("WvWWir").
- **Repetitions Follow the Translator:** after switching the translator, a repeated line is no longer shown in the old translator's wording.
- **No Silent Switch-Off:** after an internal error the add-on still switches itself off for the session (the game goes on), and now one Nexus alert says so.
- **Chat Commands Go as They Are:** "/wiki Tequatl", "/dance *", "/sqjoin Name", "/age" and every emote – also ones bought in the gem store or added later – are typed whole into the game and never translated; before, "/wiki …" lost its command. Only the channel commands (/s, /m, /p, /g1 … /g5, /d, /t, /w, /r, /yell, subgroups, the German /karte, /sagen …) switch the channel and translate the message after them. The preview shows the line exactly as it will go. A command alone ("/wiki", "/dance") is sent at once – it was never sent before, and with a send language set it hung on "translating and sending …"; commands are not learned as words.
- **More GW2 Short Forms:** about 90 abbreviations from a community glossary (W1–W8, EBG/SMC/NWT, qharb, alacren, LFT, EotN …) are never translated; the ones that are ordinary words elsewhere ("AB", "SI", "POI", "SEC") only in capitals. "rez" stays "rez" (NLLB made it "Boden"), and so do role short forms like "qheal", "ahealer", "hscourge", "qrev" (NLLB made "qheal" into "Queal").
- **Your Own GW2 Terms:** right-click in the input → "A GW2 term – never translated" teaches a word (a guild tag, a nickname for a boss) that is never translated from then on. Your terms live in `gw2-terms.txt` in the GW2Keyboard folder – yours on top (kept across versions, editable), the built-in GW2 list below (rewritten by every version, to look up what is already known).
- **Languages:** all 8 languages complete (608 strings each).

## 2.6.4 — 2026-10-27 (GW2Chatlator for Nexus)

- **Translation Tile:** The dashboard tile shows which translator runs, how (offline with its beam, free online with today's MyMemory quota, or with your API key) and whether the chat and your text are translated. A chooser (opens upwards) lists the translators – ready right away, on this PC, with your own key – and applies the choice at once; NLLB-200 asks for its download size first and becomes the translator once installed. A local translator gets its quality (Fast · Balanced · Best) right on the tile.
- **Voice Tile:** Shows the Whisper model and its size, the microphone mode in the accent colour (push-to-talk or voice activation, Hands free), how well the model recognizes your language and how fast it was the last time (CPU peak, seconds for seconds of speech). Its model chooser (opens upwards, with size, quality for your language, installed and recommended) is there from the start and applies at once; beside it a button only when something is to do: install, switch on, load the model. The mode stays in the microphone control at the top.
- **Tiles:** The title sits in the top row next to the symbol, the values right below it.
- **Events: Chat on the Dashboard:** Its state is always shown below the tiles; while it is missing, with its install button and a hint – easy to miss in the compact view otherwise.
- **First Start:** The window's info texts are on by default.
- **"Translate visible chat once" Says What It Does:** The status names how many lines are in view and which translator works on them – a local one first loads its model, which can take a moment.
- **Hands free (voice only):** Named for what it does, usable only once the voice module is installed (dashboard, Microphone tab, microphone menu); works with push-to-talk and voice activation alike. Speech translation on the dashboard waits for the voice module too.
- **Technical Tab:** Modules first, then the diagnostics and the technical details; the send timing is folded up under "Send timing (advanced)".
- **Decoder Ring Removed:** The optional, outdated add-on is no longer offered, installed or asked; chat codes show their kind and code on hover, as they did without it.
- **Local Quality Is Kept:** The quality mode (Fast · Balanced · Best) was never written to the settings and fell back to Balanced on every start; it is saved now.
- **Languages:** All 8 languages complete (597 strings each); hints that name Nexus' own menus use Nexus' words in that language (e.g. "Laden", "Tastenkürzel"); "NLLB-200 (local)", "OPUS-MT (local)", "MyMemory (free)" and the quality steps are translated; the dashboard's status line (English only, cut off) is gone – the tiles show it.
- **Microphone Menu vs. Save:** Voice activation and speech translation switched in the microphone menu are no longer reset by a later "Save" in the options.

## 2.6.3 — 2026-10-27 (GW2Chatlator for Nexus)

- **Helper Handshake & Capability Negotiation:** Local MT and voice helper processes announce helper identity, protocol version (`proto`), capabilities (`caps`), and build commit (12 characters) upon startup. Old helpers lacking a protocol version are identified as outdated; the NLLB engine requires the `nllb` capability. DLLs 2.6.0–2.6.2 read only `evt` and keep working with the new helper.
- **Click-to-Update Helper Workflow:** When an outdated helper is detected, a banner with one button offers the update. The old helper keeps running during the download; the swap then ends it, keeps the old exe as `.old` until the new one answered its handshake, and puts it back on any failure. The handshake is probed once on a background thread and again only when the exe on disk changed — never on the frame thread.
- **Build Commit & State Telemetry:** The dashboard's version line shows the built-in commit and a dirty mark (e.g. `2.6.3 · 6c6ceae`), matching the release receipt. The helper's build ID is part of the diagnostic protocol.
- **Model Package Integrity Marker:** Model installations write `.package.json` with the file sizes; a size mismatch shows "Model corrupted – please reinstall" without hashing hundreds of megabytes.
- **Package Contract & Asset Verification:** `verify_release.py --full` builds every artifact from a clean commit, runs all gates and writes the receipt `verification.json`; `--check-receipt` confirms it in seconds. `pack_release.py` writes `manifest.json` and `extras-manifest.json` (helper proto/caps read from the helper itself); `verify_assets.py` checks the uploaded assets by size and GitHub's SHA-256 digest.

## 2.6.2 — 2026-10-26 (GW2Chatlator for Nexus)

- **Hands-Free Auto-Enter:** Automatically sends spoken messages after end of speech (1500 ms pause, PTT release, or mouse stop) once translation is ready.
- **Key Buffering During Send:** Buffers user keystrokes during the send cycle and replays them into the chat input field without losing characters or triggering unwanted game movement.
- **Configurable Focus Return:** After auto-sending, focus follows standard chat focus settings ("Keep typing after send" / returnFocus).
- **Unified Hands-Free Controls:** Direct FocusSwitch toggles on Dashboard, Microphone tab, and Microphone menu; visual accent indicator on the microphone when Hands-Free is active.
- **Precise Microphone Visual States:** Red indicator reserved exclusively for active speech recording; clicking red stops recording without changing mute status.
- **Clear Text Button (<):** Dedicated `<` button beside the chat input field clears text and cancels waiting auto-send while leaving active recording uninterrupted.
- **Safe Esc Handling:** Esc closes dialogs, cancels recordings, and cancels waiting auto-send layer-by-layer without clearing user input, passing through to the game menu when idle.
- **One-Click Translation Error Recovery:** Hard translation errors display the preview banner with plain-text explanation and a direct `→ MyMemory` button to continue the session immediately.
- **Targeted Voice Model Recommendations:** Dynamically recommends suitable Whisper models in the Microphone settings tab based on selected language and memory capacity.
- **Synchronous INI Flushes:** Settings are flushed to disk before starting the voice helper process and synchronously during addon unload.
- **Complete 8-Language Localization:** Synchronized 573 translation strings across German, French, Spanish, Italian, Polish, Portuguese, Turkish, and Russian.

## 2.6.1 — 2026-10-25 (GW2Chatlator for Nexus)

- **Discord Voice Quick Menu:** Converted voice options submenu to a dedicated upwards-floating overlay window anchored right above the mic chevron button. Prevents background click-through and completely resolves recursive mutex deadlock.
- **Microphone Infotext & Silence Timeout:** Cleaned right-click listening status text to "Ich höre zu …" without key-release instructions. Reduced silence timeout to exactly 3.0 seconds.
- **Dashboard Voice Tile Polish:** Moved Discord mic control into the top-left slot of the Voice module card, eliminating duplicate icons. Expanded the bottom navigation button to full width and vertically centered all card button text.
- **Local MT Model Management:** Restored active engine fallback safeguard allowing uninstallation of local translation models without blocking the UI.
- **Complete 8-Language Localization:** Synchronized 546 translation strings across German, French, Spanish, Italian, Polish, Portuguese, Turkish, and Russian.

## 2.6.0 — 2026-10-24 (GW2Chatlator for Nexus)

- **Author and Addon Naming Consistency:** Addon author set to `Emi` matching Raidcore Nexus Addon Manager library entry. Binary output strictly unified to `GW2Chatlator.dll`.
- **Raidcore Nexus Direct Installation:** Verified automated single-click install through in-game Nexus Addon Library (ID `3098852796`, Signature `0xB8B4BDBC`).
- **Pristine CDN Release Endpoints:** Fresh release distribution assets with zero legacy fallbacks.
- **Airlock Loader Guard:** Added sterile loader verification with timestamp and attribute logging of `d3d11.dll` to ensure clean and transparent Nexus injection.

## 2.5.9 — 2026-10-24 (GW2Chatlator for Nexus)

- **Unified add-on naming to `GW2Chatlator.dll`:** The compiled binary is now directly and exclusively named `GW2Chatlator.dll`, aligning with Raidcore Nexus Addon Manager (ID `3098852796`) and fixing silent install failures when adding via the Nexus manager. Legacy `gw2keyboard_nexus.dll` downloads are completely retired.
- **NLLB-200 local translation engine:** Added explicit selection of Meta's NLLB-200 translation model (`NLLB-200 (local)`) in the Options → Translator engine dropdown and translation helper protocol.
- **Instant UI language switching:** Selecting "Language of this window" in Options → General now applies immediately in real time, updating window title, tabs, options, and status.
- **Footer chip language consistency:** Fixed cache invalidation in the chat window footer so language chips (`[Active]`, `[as written]`, `[Write: ...]`) update immediately upon UI language change.
- **Smoke benchmark stability:** Set process and thread priority during micro-benchmarking to prevent Windows OS thread preemption jitter.

## 2.5.8 — 2026-10-23 (GW2Chatlator for Nexus)

- **Smart-Sieve & Telemetry Pipeline:** Added high-performance filtering for gaming slang and shorthand (0 ms overhead), repetition suppression cache (64 entries), and dynamic beam size adaptation (throttling to Beam 1 when queue > 2, nominal Beam 2/4).
- **Dynamic Inference Throttle:** Real-time beam size adaptation (throttling to Beam 1 under burst queue loads, nominal Beam 2/4) keeping render thread latency strictly sub-millisecond.
- **Diagnostics & Debug Suite:** Dedicated "Debug & Diagnostik" window with live engine profiler metrics, telemetry counters, and one-click Markdown diagnostic protocol export (strictly zero player messages or account names logged).
- **Eliminated stray Debug window:** Fixed Dear ImGui implicit fallback window (`Debug##Default`) popping up on startup by guarding `RenderNexusOptions` and enforcing fallback suppression across all render frames.
- **Voice UI ergonomics:** "Meine Stimme übersetzen mit Voice-Modell" placed directly under model download with inline language dropdown defaulting to "Automatisch erkennen" (`auto`), routing directly to the active footer send language.
- **Complete 8-language localization:** 100% synchronized across German, French, Spanish, Italian, Polish, Portuguese, Turkish, and Russian (523 verified entries per language).
- **Stability:** the chat and options windows give their font back before they close (preventing hidden memory faults).

## 2.5.6 — 2026-10-22 (GW2Chatlator for Nexus)

- **Crash fix:** back to the stable code of 2.5.2. The changes of 2.5.3 to 2.5.5 (guild channels G1-G6, language bundles) are withdrawn until the crash is found.

## 2.5.5 — 2026-10-22 (GW2Chatlator for Nexus)

- **Stable again:** no stray "Debug" window, no crash in the send-language menu (the 2.5.4 test builds had both); every module and language package is in the release again.

## 2.5.4 — 2026-10-22 (GW2Chatlator for Nexus)

- **Consistent primary output language:** Selected send language in the main window immediately becomes primary, persists to configuration, and is preserved when clicking Save in Options (no unwanted fallback to earlier languages).
- **Unified UI and hover tooltip scaling:** The main window, mini mode, and collapsed button now share a single font-driven scaling logic, ensuring hover tooltips and controls are uniformly sized across all window modes.

## 2.5.3 — 2026-10-22 (GW2Chatlator for Nexus)

- **Guild subchannels G1–G6 & active channel detection:** Added dedicated G1–G6 guild chat channels matching the native Guild Wars 2 chat layout and commands (`/g1` to `/g6`), with unified guild coloring and dynamic active channel indicator (`Active (G1)`, `Active (Map)`, etc.).
- **Local MT packages and bundle installer:** Added logical packages in Options → Translator ("My language", "Europe package", "West & Central Europe", "Eastern Europe") and folded single pairs view with sort order (`en -> X`, `X -> en`), progress bar, and 5-second completion status feedback.
- **Main window module info guide:** Local OPUS-MT translation module is now directly offered in the chat's empty status/module guide with one-click installation.
- **Double-download guard:** Atomic `compare_exchange` on local MT jobs prevents duplicate downloads, buttons hide during active downloads, and already installed pairs are safely skipped.
- **Full OPUS-MT package suite:** All 16 language pairs built, verified, and exact package sizes recorded in catalog.

## 2.5.2 — 2026-10-22 (GW2Chatlator for Nexus)

- **A chat line that cannot be translated says why:** e.g. a missing local language pack (French needs fr-en) now shows in the status line instead of nothing happening.
- **Dashboard:** "Translate the chat - into" names your language also when it is "like Windows".

## 2.5.1 — 2026-10-22 (GW2Chatlator for Nexus)

- **Local model list readable:** no more broken characters in the language-pair list; each pair shows its size, where it is installed, and the installed pairs with their total.

## 2.5.0 — 2026-10-22 (GW2Chatlator for Nexus)

- **Local OPUS-MT translation:** optional CTranslate2 + SentencePiece CPU helper runs outside GW2. The add-on downloads the helper and selected model packages only on the user's click; installs are SHA-256 checked and removable. The initial release includes the `de-en` and `en-de` model packages.
- The helper is isolated from the game, uses bounded JSON-line IPC, request timeouts and restart handling, and a kill-on-close Windows Job Object where supported. Local translations stay on the PC.
- Music-note artifacts absent from the source are cleaned while unusable output is rejected; translated text is never silently sent after a failure.
- **Typing suggestions:** a clear learned next word is moved to the top, and a valid full word with corrected sentence casing is offered explicitly.
- **Readable first use:** the options and chat UI start at a minimum of 14 pt; the chat size remains adjustable.
- **Compact send choice:** “as written” leads the send menu and its footer label no longer clips.

## 2.4.1 — 2026-10-22 (GW2Chatlator for Nexus)

- **The options window keeps Nexus' size:** it follows Nexus' own UI size in our UI font, independently of the chat text size.
- **The chat follows the game's size:** until you set a text size of your own, the chat window follows GW2's interface size (small, normal, large, larger).

## 2.4.0 — 2026-10-22 (GW2Chatlator for Nexus)

- **No more wrong translations from MyMemory's public memory:** MyMemory now answers with its machine translation
  only. Its public memory sometimes returned the translation of a different sentence ("ah alles klar" became "All
  these great people.").
- **First use translates nothing by itself:** your text goes out as written until you choose a language at "Send"
  (then MyMemory translates it); the input and the preview start with one line and grow with what you write.
- **Mini mode's right-click menu:** only Options, Compact view and Mini mode – nothing that needs the chat.
- **Clean language menus:** no language names with "?" any more (a name the font cannot draw shows in English);
  only languages the window can actually draw are offered – Chinese, Japanese, Korean, Hindi, Thai and Hebrew are
  gone from the menus. "Language of this window" lists only the languages it has texts in.
- **Russian, Ukrainian, Bulgarian, Greek to write in:** choose them at "I write in"; since GW2's chat cannot show them,
  your message is always translated (into the first language at "Send") – never sent as written.
- **Arabic is gone:** ImGui draws Arabic letters unjoined and left to right, so the Arabic window language and Arabic as
  a language to write in were taken out. Arabic lines of other players are still recognised and translated.
- **Nothing is sent half done:** if the translation fails (quota used up, no connection), the message is not sent in
  the wrong language – it comes back into the input and the status line says why. Enter waits while what you said
  is still being recognized.
- **"auto" is hands-free only:** with voice activation, the pause after you speak ends the sentence and sends it.
  Push-to-talk and the right-click never send by themselves any more – what you said waits in the input, where you
  can still change it. The "auto" switch shows in the footer only while voice activation is on.
- **Seven new window languages:** French, Spanish, Italian, Polish, Portuguese (Brazil), Turkish and Russian, besides
  English and German. The options window is drawn with the same font as the chat, so every letter of them shows.
- **A font to choose:** Options → Window offers Segoe UI, Segoe UI Semibold and Tahoma beside the text size.
- **The mouse in the input selects text:** holding the left button in the input no longer moves the window (only the
  mini mode's small window still moves that way).
- **"Translate visible chat once" is the thrifty mode:** it now follows the same rule as the automatic translation –
  lines in your own language, in languages you understand, or too short to tell ("xD", "lf tank") are left as they
  are. A click on a single line still translates it, whatever its language.
- **Real quota protection – a translation cache:** every chat line translated in this session is kept in memory (500
  lines, never written to disk). Pressing "once" twice, switching your language back and forth, or the same LFG line
  posted again costs no new request, and the MyMemory counter counts only real requests. (2.3.5 called its chat
  buffer trimming "quota protection" – that protects memory; this is the part that protects your contingent.)
- **One list instead of two settings:** Options → Translator now reads "Translated is everything except: [My writing
  language] [English] [+]". The first entry follows your writing language (on by default, a click removes it); the
  same sentence stands on the General page. A click on a line always translates it.
- **The brake only where it saves something:** the limit of 8 automatic translations per 3 seconds now applies only to
  translators with a contingent – not to your own LibreTranslate server. With MyMemory, the menu shows next to
  "Translate visible chat once" how much of today's budget is left.
- **Decoder Ring is outdated:** it no longer appears in the empty chat's module box or on the General page; it stays
  in Options → Technical as optional. GW2Chatlator finds chat codes itself; names and details on mouse-over still
  come from Decoder Ring until our own lookup replaces it.
- **Names keep GW2's colours:** the brightness slider changes only the message text; the map channel's salmon name no
  longer turns pinkish white.
- **Stronger halo:** the dark outline around chat text now closes on all sides (8 directions instead of 4 diagonals).

## 2.3.5 — 2026-10-21 (GW2Chatlator for Nexus)

- **Intelligent chat buffer & quota protection:** chat history capacity expanded from 500 to 2,000 lines (~1.4 MB RAM) with intelligent channel pruning; high-volume public channels (Map, Say, System) are pruned first during traffic bursts so private Whispers and Group discussions (Guild, Party, Squad) are protected from being purged.
- **Buffer diagnostic indicator:** the Technical options tab now features a real-time buffer monitor displaying current line count, capacity, and estimated RAM usage.

## 2.3.4 — 2026-10-21 (GW2Chatlator for Nexus)

- **Hardened chat links:** hovering item and waypoint links in the chat window can no longer crash the game when textures load; texture requests are shielded with an SEH guard and check readiness before rendering.
- **Clearer UI & options:** "Translate visible chat once" in the window menu now clearly states its scope with a tooltip; the options title and Nexus menu button are unified to "GW2Chatlator"; the General tab shows a compact overview of your configured hotkeys; chat focus options are gathered under a single "Chat focus & behavior" section; the dashboard is cleanly grouped into active switches and module status; obsolete and redundant settings are removed.

## 2.3.3 — 2026-10-20 (GW2Chatlator for Nexus)

- **No more "?" in the chat window:** its font has no dashes, curly quotes or "…" – "ready – Events: Chat is loaded"
  showed "ready ? Events: Chat is loaded". Our texts now use the plain forms (-, ", ...), and chat lines and
  translations that bring such characters (DeepL's German quotes, players writing from a phone) are drawn with the
  plain forms whenever the font lacks them – the text itself, and what the translator gets, stays as it is.

## 2.3.2 — 2026-10-20 (GW2Chatlator for Nexus)

- **The game never goes down with us – also not from a background task.** Translating, spelling, downloads and the
  voice module's answers run on threads of their own; an error there could take the game with it (that is what
  happened in 2.3.0). Now each of them runs behind the same net as the window: an error switches GW2Chatlator off for
  the session, the game goes on – and nothing stays locked behind it.
- What the waypoint click left in the settings file is removed on load.
- **New equations, true for every input** (thousands of random ones in every build): text blocks come back exactly as
  saved (they did not when one held a carriage return – fixed), the chat protocol counts every line and every rule,
  no invisible direction mark ever reaches the window (one sat in an Arabic text – it would have shown as "?"), the
  numbers in Technical stay within what came in and change at most four times a second, and where a message goes:
  typed exactly as before, spoken with "Translate what I say" on never "as written" while you have a language.
  Every translation keeps the {1} placeholders of its English text – checked in every build.

## 2.3.1 — 2026-10-20 (GW2Chatlator for Nexus)

- **Stable again: a click on a chat code does nothing.** In 2.3.0 a click on a link could crash the game. Everything
  that hung on that click is gone: the Waypoint/POI click (it read a picture of the game and clicked the original in
  GW2's chat), its test key and its settings, opening GW2's chat line, and copying the code (it was in the way).
  Chat codes still show their name and their details when you point at them; everything else stays as it was.

## 2.3.0 — 2026-10-20 (GW2Chatlator for Nexus)

- **Nothing of other players' messages leaves your PC:** "Copy protocol" (Options > Technical, for a GitHub issue)
  listed every recent line with its channel and the lengths of name, account and message. Now it only counts which
  rules the last lines broke, and how often; the guard itself keeps nothing else of a line.
- **Decoder Ring as a module:** next to Events: Chat (Options > Technical, "What is ready", the empty chat) and as a
  tile on the dashboard – loaded or missing, "Install Decoder Ring" (its newest release, checked like Events: Chat),
  remove; "Remove everything" may take it too. Nothing is installed by itself – only on your click.
- **A waypoint link no longer opens GW2's chat line or the map:** in game a click on a waypoint here opened GW2's
  chat line – typing mode, where a click on a link does not fly the map – and older ones the map without a target,
  with the Waypoint/POI click on or off alike (it fell back to the old "every link opens GW2's chat" way whenever
  Decoder Ring had not named the waypoint yet). Now: the Waypoint/POI click clicks the original; otherwise the code is
  copied and the status line says why and where to click. Items and other links as before.
- **Waypoint/POI click, slower by default:** its pauses now start at 40 – 70 ms before the click, 40 – 70 ms held
  and 10 – 25 ms after it (was 3 – 25 ms each) – at least two frames, GW2 reads the mouse once a frame. Values you
  set yourself stay.
- **Text blocks:** menu ≡ → "Text blocks …". Save what is in the input as a block, put it back with a click, delete
  it with x – up to 30 blocks, only your own text, kept in GW2Keyboard\snippets.txt ("Remove everything" takes them
  too).

## 2.2.0 — 2026-10-20 (GW2Chatlator for Nexus)

- **The chat draws only what you see.** Every line keeps its height; out of view it costs nothing but its place. Our
  share of a frame on GW2's render thread went from 0.22 ms to 0.06 ms – with 500 lines of history instead of 200.
- **The window's place is one small formula.** Four numbers – the bottom-left corner, the width, the big window's
  height – give its rect in every mode (big, mini, minimised). Growing for a long input and shrinking back, the mini
  mode, minimising and opening again cannot move it any more, by construction. Same behaviour as before.
- **For the developer:** Options > Technical shows what our frame costs on GW2's render thread, by part (window,
  header, chat, input, footer, options), and what a chat line costs – smoothed, updated four times a second, like
  the game's frame rate there.
- **Options > General is a dashboard:** what is on at a glance – translating the chat, your text, what you say, voice
  input, the translator, reading the chat – each switched right there (green on, grey off, amber not saved yet); the
  typing focus, named plainly (Enter in the game opens our input; keep typing after sending); and what is ready, the
  same box as in the empty chat.
- **Translate what I say:** a new switch (Options > Voice and the dashboard). When you speak and it is on, what you
  said goes into the language at "Send" below the input – before "Translate my text"; into English whisper does it
  on your PC, other languages go through your translator. Off, or typed: everything as before. (It replaces "Into
  English": that setting carries over.)
- **Everything in its place:** Events: Chat (loaded, install, remove) and "Read the chat" are in Options >
  Technical, the voice module and its models in Options > Voice, "Remove everything of GW2Chatlator" once, at the
  very bottom of Technical. The empty chat still shows what is ready and what is missing.
- **The window stays where you put it:** a click on a link no longer lets it creep upwards, the minimised button
  no longer slides up, and opening it with the mouse held no longer drags it along – checked with thousands of
  random steps in every build.
- **Waypoint/POI click (experimental, off by default):** Options > Window > More > "Waypoint/POI click". Only
  waypoints and points of interest: a click on one here – while its line still stands at the bottom of GW2's chat,
  at most 8 lines below it, however old – finds the very same line in GW2's chat – its name, who wrote it, what it
  says – and clicks the original once, so the game flies the map there as if you had clicked it yourself. GW2's
  chat line is never opened for it (there a click on a link does not fly the map), our window stays where it is,
  no map is opened by us. One picture of the game, read only in memory with Windows' own text recognition; nothing
  downloaded, nothing stored. Not found: the code is copied and you click it in GW2's chat yourself. While it
  clicks, our window lets the mouse through. Its three pauses (before the click, held, after it) can be set under
  the option, from – to in milliseconds (3 – 25 by default), each thrown anew. A test key in Nexus' keybinds
  ("KB_GW2KEYBOARD_CLICKTEST") makes one click where the mouse is – to see whether GW2 takes it.
- Under the hood: equations that must hold for every input – the window's place, sending (no character lost, a chat
  code never cut), translation protection, the settings file, the soft line breaks, the chat source (every line
  counted exactly once) – checked with thousands of random inputs in every build.

## 2.1.1 — 2026-10-19 (GW2Chatlator for Nexus)

- **Leaner: protection instead of switching on and off.** Updates and compatibility are the business of Nexus and
  Events: Chat; ours is that GW2Chatlator never crashes the game and never shows garbage. So the probation of every
  new version is gone again (no more reading switched off, no marker file); what stays: every line is copied behind
  a guard and checked, and reading pauses for the session when Events: Chat hands over unreadable data or many lines
  stop fitting – "Resume" reads again.
- **The guard looks before it reads:** the memory is checked first (committed, readable, no guard page), so not even
  a caught fault is left behind for another thread.
- **For the developer:** Options > Technical shows the version of Events: Chat, says when it is new since the last
  session (a note once), counts shown, dropped and unreadable lines, and lists the last lines as lengths and rules –
  "Copy protocol", never text or names.
- What 2.1.0's probation left (a marker file, three ini keys) is removed on load.

## 2.1.0 — 2026-10-19 (GW2Chatlator for Nexus)

- **An update of Events: Chat can no longer take the game down.** Nexus updates Events: Chat over night, unasked; if
  its layout ever changed, reading it the old way could have crashed the game. Now one guarded piece of code copies
  every line (pointers checked, texts bounded; unreadable memory only pauses reading), every line is checked before
  it is shown, and a new version of Events: Chat is on probation for its first five lines. If the game still ended
  during that check, the next start keeps reading off for that version. Writing and sending always keep working.
- **Reading the chat by hand** (Options → General → Modules): switch it off, "Read anyway", "Check again", and
  "Copy protocol" – versions and the rule each line failed, no chat text and no names, ready for a GitHub issue.
  When reading pauses, the chat area (or the input, in the mini mode) says why.
- **Decoder Ring** (the names of chat codes) runs behind a guard as well: a broken update leaves the codes plain.
- The self-test poses as Events: Chat and as a broken Decoder Ring: a changed layout, unreadable memory, endless
  texts, 3000 random lines – nothing crashes, garbage is never shown.

## 2.0.5 — 2026-10-18 (GW2Chatlator for Nexus)

- **The window no longer stays too tall after the mini mode.** A long input had made the big window taller; you
  switched to the mini mode, sent there, and back the big window kept the extra height for good. It shrinks back now.
- **Minimised in the mini mode, the button moved, the mini mode switched off in the options:** the big window opens
  at the button, not where it stood long before.
- **Learning mode right after sending:** a word you corrected yourself in the next message was not noticed (the
  state of the sent message was still around) – until the new one was longer than the old. Sending starts it fresh.
- The self-test checks all three (long input → mini → sent → back; the correction after a send).

## 2.0.4 — 2026-10-18 (GW2Chatlator for Nexus)

- **Mini mode and back like a Nexus window: the window comes back exactly where it was.** 2.0.3 still let it slide
  down slowly in the game – each time it took over a pixel of the small window's fitting as a move. Now the window
  remembers its whole rect when the mini mode goes on and gets exactly that rect back when it goes off. It follows
  only when you really moved the small window (hold and drag, or the edge drag) – then by just as much. A game started
  in the mini mode opens the big window right above where the small one stands.

## 2.0.3 — 2026-10-18 (GW2Chatlator for Nexus)

- **The window no longer crawls when the mini mode goes on and off.** Leaving the mini mode (or switching the chat
  back on), ImGui's minimum height for the chat grew the window downwards before it could keep its bottom edge – on,
  off, on … it crawled down the screen like a caterpillar. Now the window remembers its size when it becomes the small
  input and gets exactly that size back, its bottom edge where the small window stood. The self-test switches the mini
  mode on and off four times and runs five minimise / open cycles near the screen edges – the window must stay put.

## 2.0.2 — 2026-10-18 (GW2Chatlator for Nexus)

- **Everything it brought can be removed again** (Options → General → Modules): a remove button for Events: Chat, the
  voice module and the voice models (a second click confirms), and "Remove everything of GW2Chatlator …" with a yes –
  it deletes the folder GW2Keyboard (settings, learned words, your words, voice module and models), Events: Chat too
  if you tick it, and writes nothing any more afterwards (nothing comes back on unload). The last step it names:
  Nexus → Addons → Uninstall – a running add-on cannot delete its own DLL. Checked by the self-test on a copy.
- **The settings file of a new player no longer grows on every start:** an old upgrade path from the desktop program
  appended the translation settings again each time (about 1.4 KB a start). Gone, and the copies it left are removed
  on load (only exact copies – what Windows wrote real values into stays).
- **A new player's settings file holds only what the add-on reads** – no window position, docking, glossary, LLM or
  LanguageTool of the old desktop program any more.
- The README is short again: one "New in 2.0" block instead of 25; the history is in this file.

## 2.0.1 — 2026-10-18 (GW2Chatlator for Nexus)

- **Nothing downloads by itself.** The first start used to load Events: Chat, the voice module (a helper program and
  the speech engine) and a voice model on its own. Now the modules list – in the empty chat and under Options →
  General → Modules – offers each with a button: Events: Chat first (it reads the chat; the typing help works without
  it), the voice module only after you switch voice input on (it explains what it downloads: about 6 MB from this
  release on GitHub, SHA-256 checked, its own process outside the game), and the voice model after that. Voice input
  is off for new players; whoever already has the voice module keeps it on.
- **Sending hardened:** the add-on's test functions work only outside GW2, so no other add-on can reach our input
  through them; one Enter sends at most 3 chat lines (a longer text stays in the input – a translation that comes out
  longer is refused as well, Up brings it back). There is no other way to send than your Enter.
- The microphone without the voice module says where to load it.

## 2.0.0 — 2026-10-18 (GW2Chatlator for Nexus)

The big one: rebuilt inside, and everything of the 1.9 line in one – a window made of building blocks with two
profiles (compact view, mini mode), sharp text in pt, themes (Slate, Grey, CBA with automatic contrast), the
prediction without the word bar, translation that applies at once, a mouse logic without dead ends, no hangs, no
creeping windows. What is new in 2.0.0 itself:


- **Split into modules.** The one kb_main.cpp (4,700 lines) is nine files now, each with one job: kb_main (only the
  Nexus glue), kb_state, kb_theme, kb_input, kb_worker, kb_voice, kb_chat, kb_window, kb_options, and kb_shared.hpp for
  what they share. The code moved byte for byte (a script); every self-test value stayed the same. A change in one
  part now compiles only that part.
- **Lean when not in use:** the voice helper holds its model in memory (hundreds of MB for the larger ones) – after
  10 minutes without recording or listening it ends and gives that memory back ([Voice] UnloadMinutes); the next press
  of the microphone or the key starts it again. Voice activation keeps it.
- **Old settings gone:** about 25 settings of the old desktop program (window position, docking, glossary,
  LanguageTool, "read in", a native-chat switch from an earlier attempt …) are no longer read or written.
- **"Compact view" and "Mini mode" are profiles** – at the top of Options → Window → Window layout and in the menu. A
  profile sets the parts below to what it means (compact: header, chat, input, footer; mini: nothing but the input); you
  can add or remove parts from there – then it is your own layout and the profile's check goes off. Switching a profile
  off brings back the layout you had before. Without the preview the translation stands small in the input.
- **The learning window** opens at the window's top right corner, beside it (left of it when there is no room), and can
  be dragged.
- **The normal window moves from the input too:** held down for a moment and dragged (a quick drag still selects text).
- The CBA theme's contrast readout is labelled ("contrast 4.5:1"), quiet when fine, gold when too weak, with a tooltip.

## 1.9.13 — 2026-10-18 (GW2Chatlator for Nexus)

- **Mini mode without dead ends.** A right-click on a word you wrote opens the learning mode for exactly that word; a
  right-click anywhere else (between words, an empty input) opens the menu – so the mini mode can always be left
  again. Held down for a quarter second and dragged, the whole window moves (a quick drag still selects text). Its top
  or bottom edge sets the lines of the input in whole steps – "Lines of the input" in the options follows at once; the
  mini mode uses your number of lines now (it was always one). Checked by the self-test (hold and drag).
- **Long words break in the line too:** a code or a link longer than the line used to run out of the box; it is broken
  inside the word now (an invisible mark that is not sent – the self-test checks a broken code arrives whole), the
  caret keeps its place, Backspace at the start of such a line takes the last letter above.

## 1.9.12 — 2026-10-18 (GW2Chatlator for Nexus)

- **"Learn your corrections" stays put:** it docks to our window's top right corner, 1 px above it (below it when the
  window stands in the top third of the screen) – in every mode. It used to open where the mouse was, jumped and was
  pushed to the screen edge.
- **Only with a word you wrote:** a right-click in an empty input does not open it (without the header the right-click
  opens the menu there).
- **An X at its top right** closes it.

## 1.9.11 — 2026-10-18 (GW2Chatlator for Nexus)

- **"Translate the chat once" stands on its own:** what you see of the chat in our window goes into the language you
  write in – whatever "Translate the chat", "Languages I understand" or "Do not translate the language I write in"
  say; only lines already in the language you write in are left as they are. Every click starts afresh: change the
  language you write in, click again, and it is the new language.
- **Only what is in view:** translating what is already there (switching "Translate the chat" on, a new language you
  write in, a new translator, or once) takes only the lines you can see in our window – an old chat is never
  translated in full (each line counts against a free contingent).
- **The CBA theme** (Window → Theme of the window → CBA): your own colours in eight roles (window, fields, buttons,
  active buttons, active tab, text, quiet text, accent), starting from Slate, Grey or CBA blue. "Automatic contrast"
  moves text just as far as needed to stay readable on every surface (WCAG 4.5:1, quiet text and accents 3:1) and
  shows the contrast each one reaches. A theme code (GW2C1:…) to copy and paste – a pasted code is only read as
  colours. The contrast math comes from cba4gw2, here the exact WCAG formula, unit-tested.

## 1.9.10 — 2026-10-18 (GW2Chatlator for Nexus)

- **Two views in the menu (≡ / right-click):** "Mini mode" (instead of "Header") – nothing but the grey input with all
  its help, one line that grows with longer text; three quick clicks on it minimise it, right-click in it → "Menu …"
  brings the rest back. "Compact view" now means header, chat, input and footer – no word bar, no preview (the
  translation stands small in the input while something is translated). Both sit over your settings of the parts
  (Options → Window) and never change them.

## 1.9.9 — 2026-10-18 (GW2Chatlator for Nexus)

- **What you set applies at once.** Switching on "Translate the chat" translates the lines that are already there (the
  newest 20 of other players – each one counts against a free contingent). "Save" in the options sends the text you
  have written through the translator as it is set now (switching on "Translate my text" showed the old text before).
  A new language you write in, or a new translator, translates the chat again.
- **Three languages at once:** the chat is read in the language you write in (as before, now also redone when it
  changes), what you send goes into the language at "Send" – "Languages I understand" and "Do not translate the
  language I write in" still apply.
- **"Translate the chat once"** in the menu: the newest lines into the language you write in, whatever "Translate the
  chat" is set to.
- **Three quick clicks minimise the mini mode too** – there the input counts, there is nothing else to click. Checked
  by the self-test in the input-only, input + word bar and full layouts.

## 1.9.8 — 2026-10-18 (GW2Chatlator for Nexus)

- **A real mini mode.** Without the chat the window is exactly as high as its parts, with a thin edge – down to
  nothing but the grey input with all its help (the grey word, Tab, Space, growing with longer text). With the word bar
  on, its top is the window's top. The bottom-left corner stays where you put it.
- **No more creeping when the window was a little too small** for the chat and all parts: it grew by a fraction of a
  pixel, ImGui rounded the size back down while the position moved – up by almost a pixel every frame. Sizes are whole
  pixels now and the position moves only by what the size really changed. The self-test checks a too small window
  stands still, and runs its window checks in the minimal layouts too.
- The self-test no longer keeps an imgui.ini (a size from an earlier run changed the next one).

## 1.9.7 — 2026-10-18 (GW2Chatlator for Nexus)

- **The prediction without the word bar.** The prediction never depended on the bar (Tab and Space wrote the
  suggestion either way – the self-test shows the same with and without it), but without it you could not see what
  Tab takes. Now the grey word finishes the word with it (Tab takes it, then Space writes it); Space alone still keeps a
  valid word as you typed it. The first Tab takes the highlighted word (the bar's orange one), not always the first.
- **The input grows with your text:** past the lines you set it grows up to 8 lines and falls back once the text is
  sent; when the window is too small it grows upwards (its bottom stays) and shrinks back afterwards.
- **The window steps aside on the world map** like GW2's own chat (it already did at character select and on loading
  screens) – except while it names a waypoint's spot on the map (the emergency way for older waypoints).
- **The Cyan theme is gone** (it becomes Slate).
- Options: the "Info texts" tooltip sat on "Open again …", the line sliders stood under "When not in use".
- The self-test can type into the input before its picture (`GW2KB_SHOT_TYPE`).

## 1.9.6 — 2026-10-18 (GW2Chatlator for Nexus)

- **Minimise when not in use** (Window → When not in use, off by default): nobody writes, points at the window, talks
  or has GW2's chat line open for the seconds you set (2–120) – the window goes down to the small button.
- **Open again when GW2's chat opens (Enter)** (off by default): only opens it. The focus key (Nexus → Keybinds)
  always opens it; taking over the focus stays its own setting under Writing. Checked by the self-test (minimise
  after the time, the focus key opens it again).
- **Three quick clicks on the window minimise it** (Fallen's idea) – not in the input, where clicks place the caret.
  Checked by the self-test (two clicks do nothing, three minimise, it opens again at the same size).
- **The minimise button can be hidden** (Window → Minimize button) – in the footer and, without the footer, next to
  the input.
- **The Cyan theme only colours what is ours to press and read:** buttons, tabs, check marks and the window's text;
  backgrounds and fields stay grey (1.9.5 filled the whole window with teal).

## 1.9.5 — 2026-10-18 (GW2Chatlator for Nexus)

- **A window made of building blocks.** The options have a new tab "Window": the parts from top to bottom, each one to
  switch off – header, chat, translation preview, word bar, footer – and "Info texts": off, every grey status line
  goes (the status in the empty input such as "Translation off", the module lines in the empty chat, "translating …",
  the counter while you do not write). The grey word of the prediction always stays, orange messages too. Text size,
  transparency, snapping, the lines and the chat text's look moved there as well; General keeps language and modules.
- **"Prediction only":** with the chat and your text not translated and voice input off, the input says so.
- **Themes for the window itself** (Window → Theme of the window): Slate (dark greys, slate accent – the default),
  Grey (black, white and grey only), Cyan (deep teal, cyan where something is active) or Nexus (whatever theme is
  chosen in Nexus – its "Classic" one made tabs and fields blue / purple). Only frames, tabs, buttons and fields – the
  chat keeps its channel colours.
- **Without the footer the minimise button stays:** it stands left of the input, in the input's line.
- **Compact view no longer hides the word bar** (the input slid up into its place) **and no longer adds a line** to the
  input when nothing is translated – one line set is one line.
- The tooltip of "GW2 colours" showed on the wrong checkbox.

## 1.9.4 — 2026-10-18 (GW2Chatlator for Nexus)

- **The header can be hidden** like the other parts (Options → General → Window layout → Header): no tabs, microphone,
  minimise, menu and close button – the window starts with the chat. A right-click on the window then opens the menu,
  with what the header offered: the tabs to switch (Chat, whisper tabs, your own), "Minimise", the options, compact
  view, a new whisper tab, hide – and "Header" to bring it back. A right-click on a chat line still translates it, in
  the input it is still the learning mode, which then offers "Menu …" as well (for a window with nothing but the
  input). Push-to-talk keeps working by its key.

## 1.9.3 — 2026-10-18 (GW2Chatlator for Nexus)

- **Sharp text in pt.** The text size scaled Nexus' font up, which made it blurry. Now it is set in pt (8–28; the old
  percent is taken over, 100 % = 11 pt) and Nexus builds the font at exactly that size (Segoe UI from Windows: Latin,
  Greek, Cyrillic, Arabic). While you drag the slider the text is scaled, the sharp font comes when you let go. Without
  Nexus' font functions or the font file the window scales as before. The self-test stands in for Nexus' font
  functions and checks the font is built and used.
- **The input stays at the bottom.** With the chat hidden, everything slid up and left an empty block under it (with
  the footer hidden too it looked broken). Now the input is the lowest line, the space above stays empty and
  see-through, and without the chat the window may be as small as the input (it was held at 240 px, below that it even
  jumped back to 360).

## 1.9.2 — 2026-10-18 (GW2Chatlator for Nexus)

- **No more hang on "Save".** The options wrote the ini with about 70 single writes on the game thread, each one
  rewriting the whole file – the game froze for about a second (a dragged slider wrote every frame). Settings now wait
  in memory and the background thread writes them, at most every half second; the rest when the add-on unloads. The
  voice helper reloads only after they are written.
- **"Save" kept what you changed elsewhere.** With the options open, switching "auto" in the footer, adding a language
  or an automatic switch to a smaller voice model was turned back by "Save". Now the newer value stays.
- **Every slider acts at once:** text size and transparency no longer wait for "Save". Brightness above 100 % lightens
  towards white (the light GW2 tints were cut off before, so it seemed to do nothing). Brightness also reaches the
  preview and the input, the dark outline the preview.
- **See-through up to 100 %:** the chat field fades with the window's transparency; "Background behind the chat" off
  leaves only the text over the game. The small minimised button stays visible. "Glow like GW2's chat" is now called
  "Dark outline (readable when see-through)".
- **The preview has the same light field as the input**, so the two read as one.

## 1.9.1 — 2026-10-18 (GW2Chatlator for Nexus)

- **The window no longer creeps up.** Minimising measured the small button differently (text size, ImGui's minimum
  window size) than the minimised window itself, so every minimise / open moved the window – upwards with larger
  text. A click on a chat code even minimised the chat area instead of the window. Now the button sits exactly on the
  window's bottom edge and the window comes back to exactly the same place – also at the screen edge, where the small
  button used to snap onto the native chat icon every frame and pull the window away from the edge (it snaps only when
  you drag it now). Checked by the self-test.
- **Web links like Discord:** blue and clickable; a click asks "This link goes to … – open it?". No background check
  (no Cloudflare lookups), no warning labels, no link filter option.
- **Chat codes open the original:** a click on a chat code moves the window out of the way and opens GW2's own chat line
  (Nexus presses the game's chat key) – you click the link there and the game does its own thing (the map jump for a
  waypoint, its tooltip for an item). The window comes back when the map closed again (waypoint) or GW2's chat line
  closes, at the latest after 45 s. The code is copied as well.
- **Emergency way for waypoints:** a waypoint older than 15 s (its line has probably scrolled away in GW2's chat): the
  game map opens instead and the window stays, saying where to look ("On the map: Commodore's Quarter Waypoint – Lion's
  Arch", from Decoder Ring).

## 1.9.0 — 2026-10-17 (GW2Chatlator for Nexus)

- **Chat Scroll Fix:**
  - Resolved mouse wheel scroll trap: releasing bottom anchor immediately on upward wheel scroll so past chat messages can be scrolled smoothly without snapping back to bottom.
  - Auto-scroll to bottom only triggers on actual new messages or line expansion from item link resolution.
- **Bottom Snap & Mini-Chat Alignment:**
  - Dynamic bottom bar offset matching native GW2 speech bubble bottom edge across all UI scales and resolutions.
  - Magnetic snap for collapsed `<` button directly over native chat icon.

## 1.8.9 — 2026-10-17 (GW2Chatlator for Nexus)

- **Release Hardening & Automated Gate Protocol:**
  - Automated 8-gate release verification (`tools/verify_release.py`) covering version consistency, Nexus definition, core unit tests, lifecycle selftest, typing determinism, localization completeness, binary inspection, and real-life smoke stress testing.
- **Real-Life Smoke & Stress Benchmark (`nexus_kb_smoketest.exe`):**
  - Stress-tested under live combat conditions: 500 high-intensity chat messages ingested in ~13ms (>38,000 lines/sec) across all channels (Map, Squad, Party, TeamWvW, Whisper, Local).
  - Profiling over 1,000 render frames confirms an average frame time of ~0.34 ms and 99th percentile latency of ~0.75 ms, satisfying the zero-stutter SLA.
- **Window Geometry Clamp Polish:**
  - Consolidated runtime geometry clamp ensuring an atomic reset to 520 × 360 px if `imgui.ini` is corrupted or squished.
- **Safe Release Staging Norm:**
  - Formalized multi-stage release protocol in documentation and repository guidelines.

## 1.8.8 — 2026-10-16 (GW2Chatlator for Nexus)

- **Window Geometry Failsafe (Anti-Stale Cache):**
  - Enforced a hard layout reset if ImGui loads corrupted or squished dimensions from a legacy `imgui.ini` (height < 240px or width < 380px resets cleanly to 520 × 360).
  - Raised window minimum sizing constraints to 380 × 240 to prevent accidental vertical squishing.
- **Fail-Fast Background Network Timeouts:**
  - Added a strict 2000ms receive/connect timeout to the asynchronous Cloudflare DoH security check (`HttpsRequest`).
  - Ensures the worker thread is never stalled by spotty internet connections or network latency, keeping chat translation completely instantaneous.

## 1.8.7 — 2026-10-16 (GW2Chatlator for Nexus)

- **Nexus Autoupdate Integration:**
  - Configured `UP_GitHub` update provider and repository update link (`https://github.com/Emisan01/gw2chatlator`) in `GetAddonDef()`.
  - Enables Nexus's built-in update manager to detect new releases and automatically keep the add-on updated for all players.
- **Security Hardening (Defense-in-Depth):**
  - **Unicode Spoofing Protection:** Strips bidirectional overrides (`\u202A`–`\u202E`, `\u2066`–`\u2069`), directional isolates, zero-width spaces (`\u200B`–`\u200D`, `\uFEFF`), and soft hyphens in `SanitizeChatText` to prevent text reversal and filter evasion attacks.
  - **Punycode & IDN Homoglyph Detection:** Flags internationalized domains (`xn--...`) in chat links as `Dangerous` phishing candidates to block visual homoglyph spoofing attacks.
  - **Hardened URL Execution:** `SafeOpenUrl` strips quotes, control characters, and disallows dangerous URI schemes before browser invocation to eliminate injection risks.
  - **Anti-Flood & Rate Limiting:** Capped the domain safety cache to 500 entries to prevent memory exhaustion from chat floods, and added a token-bucket rate limiter to incoming chat translation requests (max 8 per 3s) to prevent thread/network saturation during chat spam.

## 1.8.6 — 2026-10-16 (GW2Chatlator for Nexus)

- **uBlock-style scam & phishing filter:**
  - Real web links (HTTP/HTTPS) are directly clickable in our chat window.
  - Local domain analyzer detects GW2/ArenaNet impersonation (`guildwars2-gems.xyz`, `arenanet-verification.com`, fake logins) and raw IP addresses immediately $\rightarrow$ marked in red with strikethrough and a `[Phishing]` badge.
  - Shorteners hiding their true destination (`bit.ly`, `tinyurl`, `is.gd` etc.) are tagged with an amber `[Shortener]` badge.
  - Asynchronous Level-2 Cloudflare Security DoH (1.1.1.2) check queries `security.cloudflare-dns.com` for unclassified domains without network lag or API keys.
  - Safe confirmation popup with risk assessment, cancellation as primary action, and optional inspection via Cloudflare Radar.
  - Toggleable via Options ≡ → Chat text → "Filter suspicious links (phishing & shorteners)".
- **Native chat link redirection:**
  - Clicking in-game links (waypoints, items, skills, traits, builds) checks expiration (map changes, 90s age).
  - Automatically minimizes our window and invokes native chat focus so the game engine displays full native interactions (waypoint map panning, 3D dressing room preview).
  - Cleaned up obsolete legacy "Shift+click to copy/paste" behavior.
- **Pixel-perfect bottom bar snapping:**
  - Screen edge snapping aligns exactly with the upper edge of the native minimal chat button above the bottom bar across all 4 UI scales (Small, Normal, Large, Larger) and resolutions.
  - Footer dropdown menus (channels, languages) open upwards to prevent clipping below the screen.
  - Native-style `<` minimize button sits flush at the bottom-left corner.
- **0-allocation idle performance:**
  - Pre-cached chat line segment rendering, status text, footer chips, and language lists eliminate all memory allocations per idle frame for rock-solid framerates.

## 1.8.5 — 2026-10-16 (GW2Chatlator for Nexus)

- **FPS-adapted automatic sending timing:** Key hold and inter-key step delays dynamically adapt to the game's measured framerate, ensuring keys reliably span frame boundaries without lag.
- **Manual timing override:** Editing key hold or pause between keys in Options → Technical automatically disables the automatic timing mode.
- **Optimized chat send path:** In-process foreground focus checks now skip redundant foreground switches and pre-send sleeps when Guild Wars 2 is already the foreground window.
- **Cleanup of legacy desktop OCR & tabs:** Cleaned unused desktop OCR batch logic, dead tab stream files, and redundant translation strings.

## 1.8.4 — 2026-10-15 (GW2Chatlator for Nexus)

- **Name alignment:** updated in-game Nexus add-on definition name and window titles to **GW2Chatlator** to match the official Raidcore listing.
- **Standalone DLL release:** verified standalone DLL build with version display 1.8.4 in Technical tab.

## 1.8.3 — 2026-10-15 (GW2 Keyboard for Nexus)

- **Standalone DLL release & version display:** verified standalone DLL release with version display aligned cleanly to 1.8.3 in the Technical tab.
- **Flexible language translation controls:** all language checkboxes in "Languages I understand" can now be checked
  or unchecked freely, including English.
- **Auto-skip typing language option:** added "Do not translate the language I write in" checkbox (Options → Translator),
  which automatically skips chat messages matching your active typing language without requiring it to be checked in
  the understood list.

## 1.8.1 — 2026-10-15 (GW2 Keyboard for Nexus)

- **Chat codes via Decoder Ring & SegmentLine:** chat messages are parsed into plain text, web links, and interactive
  chat links using `ChatLinks::SegmentLine`. Metadata (item/skill/trait/recipe/waypoint names, rarity colors, icons,
  descriptions, stats/facts) is resolved cleanly via Decoder Ring without manual HTTP scraping.
- **Removed mouse-drag map focus:** removed synthetic mouse dragging and map toggling.
- **Chat link click:** clicking a chat link copies its code (`[&...]`) to the clipboard (honest fallback); Shift+click
  inserts the code into the chat input.

## 1.8.0 — 2026-10-15 (GW2 Keyboard for Nexus)

- **Translation in the background:** tick the languages you understand (Options → Translator) – only the others are
  translated by themselves, so the keyboard stays quiet until someone writes French, Spanish … A click (left or right)
  on a line translates just that one.
- **Answer in their language:** the language each player writes in is noted for the session (never stored); a click on
  their name opens a whisper tab that sends in it. Changing "Send" in a whisper tab changes only that tab.
- **Item tooltips like the game's:** the description, the effect of food and other consumables (icon, name, duration,
  text), weapon strength / defence / attributes, rune bonuses, sigil and infusion effects, "Double-click to use.", the
  type in the game's words, the required level and the crafting discipline. Item and place names in the game's language
  (Options → General → Chat text).
- **Tabs of your own:** right-click on the header → a new tab (a copy of the chat); right-click on it: its name, the
  channels it shows and the channel you write into. Kept across sessions (settings only). New lines mark tabs not in
  front.

## 1.7.6 — 2026-10-14 (GW2 Keyboard for Nexus)

- **Chat text like GW2's chat:** the name in the channel colour, the message in a light tint of it, a dark glow around
  the letters (readable on any background), a brightness of your choice – Options → General → Chat text.
- **Waypoints like in the game:** their name and map on mouse-over; a click opens the world map and moves it onto the
  spot (Shift+click puts the code into your message). The level / instance stays as it is, as with the game's links.
  The table of every waypoint comes once from the GW2 API and is kept for 14 days.
- Item details read better ("Unlimited Consumable"); the footer shortens its chips instead of being cut off; a quiet
  preview while nothing is typed; the chat stays at the bottom when a line grows (an item name arrives).
- main holds only the add-on now – the old desktop programs and the OCR reader are in the branch `desktop-archive`.

## 1.7.5 — 2026-10-14 (GW2 Keyboard for Nexus only; the desktop programs stay at 1.7.1 / 1.4.0)

- **The window:** tabs in the header – the chat and up to five whisper tabs (a click on a name opens one, a right-click
  on the header makes one yourself); V folds the window up to its header, A opens it again; a minimum size; snaps to
  the screen edges. Status messages appear inside the input box. The compact view is built like GW2's chat (no preview,
  no word bar – the translation small in the input). The chat's scroll bar is at the left.
- **Chat codes** show as the game links them – "[Item name]" in its rarity colour; on mouse-over the icon, type, level
  and description (public GW2 API, with limits against misused codes); a click puts the code into your message.
  Web links are clickable and open only after a yes.
- **Translation on your terms:** "Translate the chat" can be switched off (then a click on a line translates just that
  one); without any translation the status says so instead of counting MyMemory. A hint that any made-up e-mail works.
- **Learning mode:** a right-click in the input lists the words you corrected yourself while typing – learned once,
  autocorrection writes them right next time; abbreviations get their meaning for the translator ("finds = I think").
- **Voice:** when only the DLL was copied, the voice module loads itself from this release (checksum checked). The first
  start loads Events: Chat, the voice module and the base model by itself. Per model: the CPU peak and time of the last
  recognition, "Use" and "Remove", the folders; a model too slow twice in a row hands over to the next smaller one.

## 1.7.1 — 2026-10-14 (GW2 Keyboard for Nexus – fixes 1.7.0; desktop GW2 Keyboard 1.7.1; GW2 Chat Translator stays 1.4.0)

- **GW2 Keyboard for Nexus:**
  - **Enter sends again** from the multi-line input (in 1.7.0 it only broke the line without translation); a message
    longer than a chat line goes out in several parts. Ctrl+Enter breaks the line.
  - **Tab / Shift+Tab** pick a word of the word bar (orange), **Space** writes it; without a pick Space writes the grey
    prediction. Backspace right after it brings the typed word back.
  - **Up / Down** in the empty input bring back what you sent (this session).
  - The counter shows into how many chat lines a long message is split ("(2x)").
  - **The window is modular:** chat, translation preview, word bar and footer can be switched on / off, the heights of
    the input and the preview chosen (Options → General → Window layout).
- 1.7.0 is marked as broken on GitHub – please update.

## 1.7.0 — 2026-10-14 (GW2 Keyboard for Nexus – the new main version; desktop GW2 Keyboard 1.7.0; GW2 Chat Translator stays 1.4.0)

- **GW2 Keyboard for Nexus:** the typing help as a Nexus add-on inside the game (`GW2Keyboard-Nexus.zip`, unpacked into
  `Guild Wars 2\addons`).
  - **The chat:** every channel comes as exact text from Vonsh's **Events: Chat** (Nexus library), foreign lines are
    translated; hovering a line shows the original; a click on a name whispers back. Events: Chat is installed with
    one click when missing; "installed but not loaded" is told apart.
  - **Writing:** the translation and its back-translation before Enter sends; grey completion from Windows' word
    prediction; three learned suggestions in the word bar; autocorrection with Backspace undo; learning from what you
    send; a four-line input that wraps.
  - **Voice:** whisper in a helper process outside the game, on the cores GW2 does not use. Hold the microphone, or
    right-click it to speak until a pause, or use voice activation (it pauses while Discord & co. use the microphone).
    An own push-to-talk key that GW2 does not get. Optional: send right after speaking.
  - **Its window like the desktop version:** status line, microphone / menu / close, the chips channel / write / send
    · translator, options in tabs. It appears only on a map. Failsafe: an error switches the add-on off, never the game.
- **Privacy:** the chat can no longer be saved to a file in GW2 Chat Translator (other players' messages).
- **Desktop:**
  - Several senders on the keyboard's pipe; the reader mode of GW2 Chat Translator (`--reader`); the bridge also hears
    Events: Chat.
  - Settings → General: Events: Chat to load / remove; one switch for the whole OCR module.

## 1.6.0 — 2026-10-14 (GW2 Keyboard + the optional Nexus add-on GW2 Chat Bridge; GW2 Chat Translator stays 1.4.0)

- **Party and squad chat as exact text, translated, in GW2 Keyboard.** The optional Nexus add-on **GW2 Chat Bridge**
  (in the package under `nexus\`) takes what arcdps' Unofficial Extras reports and hands it to the keyboard; the newest
  lines appear in a slim band above the preview, translated into your language (a click shows the original). It only
  reads – it never sends anything to the game, and it reads nothing back from the program. Map chat, whispers and the
  rest stay with the OCR reader of GW2 Chat Translator.
- **Settings → General → extensions:** Unofficial Extras and the GW2 Chat Bridge with their status – install, update
  and remove with one click each (Unofficial Extras like the Nexus library would do it: the newest GitHub release,
  checksum checked, into `<GW2>\addons`). Switch for reading in Settings → Game & start.
- **The add-on's page in Nexus** shows what is there (ArcDPS, Unofficial Extras, the program) and installs Unofficial
  Extras on a click; party and squad can be switched off separately.
- **Settings → General tidied:** the font next to the size, narrower sliders, keys set in a small window (two keys,
  a third optional, with a check whether Windows has it free) – also the push-to-talk key.
- **"Show my windows on screenshots"** (Settings → General): our dialogs are hidden from screenshots by default.
- **Focus behaviour** in one row on the Writing page: back here after sending / here when the GW2 chat opens.
- **× puts the window into the tray; Quit (menu ≡ or the tray icon) really ends it** – it used to keep running
  invisibly when you quit while a dialog was open.
- **Learned misheard words per language:** "Red = Raid" learned while speaking German no longer touches an English
  "red". The list (Settings → Voice → Learned…) has sections [de], [en] …; lines above them (and older lists) count for
  every language. The recognizer's hint words follow the language you speak.
- GW2 Keyboard settings without leftovers of the full tool: no game folder, no "Guided setup" button that did nothing.
- The tour knows right-click on the microphone and the key that works while GW2 is in front.
- **Which translator is active, always visible:** the send chip reads e.g. "Send: English · MyMemory" – or
  "· Whisper" when whisper already wrote the chat's language.
- **No MyMemory when whisper translates:** text that is already in the chat's language is not translated back either
  (no request at all).
- **Settings → Translator:** with "Automatic" (MyMemory while no key is set up) the e-mail field for MyMemory's larger
  contingent is there at once.
- **Translation of what you write can be switched off** (Settings → Translator → "Translate my text"): then only the
  typing help runs and the text goes out as written (corrected). Choosing a language on the send chip switches it on
  again.
- **Compact view** (menu ≡): slim bars, a one-line input, only the line that goes into the chat; the window takes the
  height it needs and gets the old one back.
- VirusTotal link in the README.

## 1.5.0 — 2026-10-13 (GW2 Keyboard; GW2 Chat Translator stays 1.4.0)

GW2 Keyboard as the main download: the typing help with voice input, everything of 1.4.0 and 1.4.1 polished.

- README: GW2 Keyboard first, with its own download link and what it does.
- Settings → Microphone tidied: start mode, keys, device, pause first; everything only for voice activation in its own
  section, greyed unless voice activation is chosen; the voice-chat list and "Find…" are gone (one checkbox).

## 1.4.1 — 2026-10-13 (GW2 Keyboard only; GW2 Chat Translator stays 1.4.0)

- **GW2 Keyboard: the status sits at the top left** instead of the title (down in the footer it was often cut off).
  "Waiting for GW2" instead of "GW2 not found"; no "Esc: back to the game" any more.
- **After speaking, quietly in the status:** which model ran, whether "into English" is on, how long the recognition
  took and the real-time factor.
- **Voice recognition keeps off the busy cores:** the load of every core is measured (system-wide, no access to the
  game); whisper runs on the least busy physical cores – as many threads as help (2–6) – and below normal priority, its
  helper threads included, so GW2 wins whenever both want a core.
- Settings → Game & start (GW2 Keyboard): the connections box is gone – the same status is on the Technical page.
- **Who translates is clear:** dictated text keeps its language (whisper's English), text that is already in the chat
  language is not sent to the translator again (saves MyMemory), the grammar check uses that language, and the preview
  says who did what ("MyMemory: DE → EN", "Whisper translated into English · already EN – not translated").
- **Right-click the microphone: on / off** – speak with the mouse free; a pause of about 1.2 s (or the next
  right-click) ends the recording. Mouse-over explains the button. The text still waits for your Enter.
- **Settings → Microphone (new page):** how a recording starts – hold the button or key / press once, a pause ends
  it / voice activation; a key in this window and a **key anywhere** (a Windows hotkey, also while GW2 is in front:
  it starts, a pause ends it, then this window takes the focus for your Enter – no hook); pause length, pre-roll (the
  first word is not lost), sensitivity and shortest speech for voice activation; device and boost (+6 / +12 dB /
  automatic).
- **Voice activation:** listens and starts by itself when you speak, keeping the moment before it; a pause ends the
  message, then it listens again. It listens only after the key anywhere switched it on, while this window is in
  front, or always (with a warning). A click or right-click on the microphone switches it off and on. Sending stays your Enter.
- **Voice activation and voice chats:** voice activation is not exclusive – it cannot tell what was meant for Discord,
  and Discord still hears you. So it pauses by itself while a voice chat (Discord, TeamSpeak, Mumble, Teams …;
  "Find…" lists who uses the microphone now) has the microphone open – the key or a click still work, and a click
  switches it on anyway until the voice chat closes. Nothing in those programs is touched (Windows' audio sessions).
- **Live view** (Settings → Technical): its own window with every technical value, refreshed every second – CPU per
  core, memory, this program, GW2 window / MumbleLink / frame rate, translator requests and times, voice input.
- **What you sent, as a circle after the character counter:** this session's messages (yours only, in memory) – save them as text,
  or learn from them later: Settings → Writing → "Learn what I send – later, when I choose" collects instead of
  learning while you play; the circle's menu then learns them in one go (sentences, words, your typical typos).

## 1.4.0 — 2026-10-13

- **Voice input in GW2 Keyboard (new):** hold the microphone button in the header (or F8 in the window), speak,
  release – the text appears in the input box, you check it, Enter sends. Recognition runs on this PC (whisper.cpp,
  CPU only, no audio leaves the PC, nothing is recorded to disk); the text then goes through the same chain as typed
  text – spelling, your words, corrections and your translator (also offline with a local model). Optional live text
  while speaking (grey). Measured: a 6 s German sentence in about 1.2 s with the "small" model.
- **Settings → Voice:** choose a model (base, small – recommended, medium, large-v3-turbo; size, memory and how well it
  knows your language), download it (checked by SHA-256) or delete it, language, key and hold / press-to-toggle,
  microphone, threads, a 4 s test, hints for weaker PCs. Hidden behind the voice module: GW2 Keyboard still starts and
  types without it.
- **It learns the words it misheard:** correct a recognized word before sending ("Red" → "Raid") and the next time it
  writes it right; the words also steer the recognizer. Only from what you send, stays on this PC, editable
  (Settings → Voice → Learned…).
- **Send guard:** nothing goes to the game from an Enter that another program injected, from text another program
  wrote into the input box, or as spam (the same line more than 4 times in 2 minutes, more than 20 lines a minute).
  The second hit switches sending off for the session.
- **GW2 Keyboard installs and starts with Windows by itself** (Settings → Game & start): it goes into the same
  install folder, gets its own start-menu entry "GW2 Keyboard" and its own autostart entry. Only one of the two
  programs starts with Windows (both would type into the same chat): switching it on for one switches it off for the
  other.
- **Load extension = the normal setup:** the downloaded GW2 Chat Translator now starts like a fresh download and
  installs itself with its own setup, as always; your settings and learned words go along. As it has the whole keyboard
  too, it takes GW2 Keyboard's place: the autostart goes over to it, GW2 Keyboard's start-menu entry goes (the file
  stays – both programs keep working on their own).

## 1.3.1 — 2026-10-12

- **GW2 Keyboard cleaned up, standalone:** built without everything only reading the chat needs – no text
  recognition, no RapidOCR/onnxruntime, no Tesseract, no screen capture (it needs no DLL next to it). Settings: no
  "Reading the chat" page, no chat-only options. All of the writing and translating stays.
- **"Load extension"** (GW2 Keyboard, Settings → General and menu ≡): the chat translation by OCR is GW2 Chat
  Translator itself – when it is not there, one click (and a yes) downloads it from the official GitHub release and
  installs it for your Windows user; then it can start right away.
- **"What can this tool do?"** moved from the menu to Settings → Technical (both programs). GW2 Keyboard's menu is
  short too: chat translation (or load it), dock, settings, quit.

## 1.3.0 — 2026-10-12

- **A short menu that adapts:** ≡ shows "Keyboard only" (ticked) – untick it and it reads "Keyboard + chat reader" with
  **Detect GW2 chat**, **Set read area …** and **Translate chat once now** below. Then Dock, Show on screens (1 min),
  "What can this tool do?", Settings, Quit. Everything else about reading (what is read: the GW2 chat or a screen
  area, only translations, laying over the chat) moved to Settings → Reading the chat; the setup is there as "Guided
  setup", the language under General.
- **Detect GW2 chat** finds the chat in a picture of the game and shows the found frame at once (traffic light and the
  first lines); "Set read area" shows the frame set so far. **Drag an edge of the frame** to adjust it – no new frame.
- **"What can this tool do?"**: the whole tool on one page (writing, learning, the chips below the input, reading, the
  handy keys, nothing in the game). It shows once by itself after the setup (first start of GW2 Keyboard), then stays
  in the menu.

- **Chat like in GW2:** a map change (MumbleLink map id) takes the say and map chat and the notices of that map out of
  the log – the party, squad, guild and whispers stay. Settings → Game: "Keep the chat when GW2 clears it".
- **Save the chat** (the memory circle): the last 1 / 5 / 10 / 20 / 60 minutes or everything as a text file (translation and
  original) – valuable things said over a boss fight kept for good. **Clear the chat** next to it.
- **The chat memory as a small circle in the header** that fills up (500 messages; when full the oldest are
  overwritten, like GW2 – no notice, no popup). Mouse-over explains it, a click offers save or clear.
- **Faster chat log:** every new or translated line measured all up to 500 entries again (~0.25 ms a line, measured:
  up to ~250 ms on the UI thread per message with a full log). Heights are now kept per entry – only new or changed
  lines are measured. Clearing automatically when full would bring nothing more.
- **GW2 Keyboard (new, `GW2Keyboard.exe`):** the typing help as its own small program, written anew – no chat
  reading, no OCR, no chat log or tabs: spelling, learned words and typo memory, Windows' prediction, every translator
  (MyMemory, Google, Microsoft, DeepL, own server, AI model local or cloud), your words, corrections, GW2 names, the
  back-translation in the language you write in, the grammar offer, sending timed by the game's fps, docking, tray,
  hotkey, the light autostart. Same settings and learned words as the full tool (one data folder). Menu ≡ →
  "Activate chat translation" starts the full GW2 Chat Translator with reading on.

- **Reading check with your own lines:** every line you send that comes back through the text recognition is compared
  letter by letter with what was really sent – an exact hit rate of the reading on your screen ("read exactly 48/50"
  in the footer while reading, details incl. character errors on Settings → Technical). The gate for taking over the
  chat later: only when this stays at 100 %.

## 1.2.1 — 2026-10-11

- **Fixed: the GW2 input line glued to the last message** ("… O tüldeJCABCJ" – "[Gilde][ABC]" with misread
  brackets): glued tags below the last stamped line are the input line, never text. A guild tag in front of a name
  ("[ABC] Nia Stone", also "tABCJ …") is removed from the name.

## 1.2.0 — 2026-10-11

- **Two translators:** Settings -> Translator -> "For the chat": the chat can use another translator than your own
  messages (e.g. MyMemory or a local model for reading, DeepL for writing). Default: the same one.
- **Keyboard only:** Settings -> Reading -> "Keyboard only": nothing of the chat is read, the tool is just the typing
  help (spelling, suggestions, learning, translating what you write). Also in the menu ≡ (first entry); the dot in
  the header is only the chat translation on/off.
- The grey back-translation in the preview comes in the language you **write** in (before: the reading language);
  switching the reading language no longer touches the preview, switching the writing language translates again.
- Writing and sending in the same language: nothing is translated, only the typing help runs.
- Glyph reader removed (it no longer found sure rows after 1.1.0 and only patched the OCR), its "Calibrate font
  recognition" menu entry too; "Hybrid" is no own choice any more – re-reading misread words is the second look's job (old "hybrid"
  settings read as automatic).
- **Keyboard only is the start mode** and a compact typing window: just preview, word bar and input; the window grows
  upwards to its full size (with the chat) only when the chat translation is switched on (dot or menu).
- **Lighter start with Windows:** the autostart only creates the hidden window and the tray icon; spell checker,
  Windows' prediction, the learned words, GW2 name lists and the file clean-up are built when GW2 reaches the 3D world
  (or the tray opens the window).
- **Sending follows the game's frame rate:** measured from MumbleLink's frame counter (no access to the game) – keys
  every action (holding a key, the pause between steps, the moment after the chat line opened) gets one measured frame
  + 10 ms; opening the chat is waited for live (MumbleLink). Settings → Technical shows the fps, the timing and the
  last send: chat line open after … ms, message out after … ms, confirmed n of m. A KeyHoldMs set by hand stays.
- **The sentence from the first letter:** the grey suggestion shows the word *and* the rest of a phrase you usually
  write after it ("w" -> "wie geht's dir"); Space writes the word, Tab then takes the rest. Only what was learned.
- **Grammar check offered when it helps:** translating and the check is off → once the sentence stands the preview
  offers it; in the warning colour when the back-translation differs a lot from what you typed. One click switches it on.
- **Local AI (Ollama) no longer hangs:** requests set a small context (4096 tokens – Ollama was loading models with a
  128k context, which makes a CPU model crawl), no thinking phase (qwen3 & co.), 4 threads, the model stays loaded
  30 minutes; while one translation of your text is on its way, newer text waits instead of piling up requests.
- The grammar check (LanguageTool, optional) only looks at sentence structure; spelling is the typing help's job.

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
