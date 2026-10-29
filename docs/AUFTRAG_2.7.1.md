# Auftrag 2.7.1 – gesammelt nach Emis Spieltest von 2.7.0 (2026-10-29)

Für das schreibende Modell. Claude prüft zuletzt. **Kein Push, kein Release** – das macht nur Emi bzw. Claude auf
Emis Wort. Bindend: [AGENTS.md](../AGENTS.md) (Arbeitsmodi, `Evidence:` bei feat/fix/perf/refactor, Messen statt
Raten). Testzeilen nur **erfunden** – nie echte Chatzeilen von Spielern ins Repo. Keine AI-Attribution.

## A. Fehler aus dem Spieltest (zuerst)

**A1. Zeilen bleiben unübersetzt („Sichtbaren Chat einmal übersetzen“).** Beobachtet: eine Zeile mit Tippfehler,
die nächste Zeile korrigiert ein Wort („or“), dann eine Zeile nur mit `\o/`, dann eine kurze englische Zeile
(„i dance - cool“). Ab diesem Block übersetzte **kein** Übersetzer mehr die letzten Zeilen, auch nicht per Knopf.
- Nachstellen mit erfundenen gleichartigen Zeilen über den Weg `TranslateRecentLines` → `TranslateLine`
  ([kb_worker.cpp](../addons/nexus-keyboard/src/kb_worker.cpp)): `ShouldTranslate`, `clearPending`,
  `HasTranslatableText`, Interjektions-Sieb im Orchestrator, Übersetzungs-Cache.
- Verdacht: eine Zeile bleibt `transPending`/`transAttempted` hängen; eine Zeile nur aus Emote; der Helfer hängt.
- Abnahme: ein Test (core_tests oder nexus_kb_selftest) mit genau dieser Folge zeigt, dass jede Zeile danach
  übersetzt wird; die Ursache steht im Commit.

**A2. Rohschalter: Zustand nach Übersetzerwechsel unklar.** `g_rawTranslate` wird nur in
[kb_options.cpp:2015](../addons/nexus-keyboard/src/kb_options.cpp:2015) gesetzt – prüfen, was beim Wechsel
zurückspringt (Anzeige, `SetInterjectionSieve`, neuer Übersetzer ohne `SetLocalMtRaw`). Abnahme: der Schalter gilt bis
zum Neustart, egal welcher Übersetzer; sichtbar, ob er an ist (die Routenzeile/`TranslatorTag` kennt „ · raw“).

**A3. NLLB „schnell“ ist schlecht („dance“ → „TANCE“), „hoch“ deutlich besser.** Wichtig: die Drossel
`ComputeBeamSize` setzt automatische Zeilen unter Last auf Beam 1 – auch wenn „hoch“ gewählt ist. Messen:
`translation_bench` je Beam (1/2/4) auf dem Korpus – Bedeutung und ms pro Zeile. Ergebnis als Tabelle an Emi; er
entscheidet (Standard anheben, Drossel nicht unter 2, Stufe „schnell“ entfernen).

**A4. „Zweitblick“ zurückholen – schlechte Ausgabe erkennen, bevor sie gezeigt wird.** (Emis Idee aus der OCR-Zeit:
ein Ergebnis mit einem zweiten Weg gegenprüfen.) Vorschlag, lokal zuerst:
- Signale: Score unter `kUnsureScore` (gibt es) **und/oder** ein erfundenes Wort – ein Wort der Ausgabe, das weder in
  der Eingabe noch im Wörterbuch der Zielsprache noch in unseren Listen steht („TANCE“). Das Wörterbuch-Signal nur, wo
  Windows ein Wörterbuch für die Zielsprache hat (nicht überall – auf Emis PC nur Deutsch).
- Reaktion: **diese eine Zeile** noch einmal mit höherer Qualität (Beam der gewählten Stufe bzw. 4) – die Einstellung
  des Nutzers bleibt, kein globales Umschalten. Erst wenn auch das unsicher ist: Hinweis „(?)“ wie heute; ein Online-
  Übersetzer (MyMemory) nur, wenn der Nutzer ihn eingeschaltet hat.
- Abnahme: translation_bench mit Beam 1 + Zweitblick gegen Beam 1 und Beam 4 – Bedeutung und ms; als Option mit
  Schalter (Emi: jede Automatik als Option).

**A5. Direktstart in der Kachel.** Alles, was für den Start der Übersetzung nötig ist, in der Kachel bedienbar – ohne in
den Tab zu müssen. Entwurf vorher mit Emi abstimmen (er gibt die Richtung vor).

**A6. Ziel beim Chat-Lesen ist an die Eingabesprache gebunden – Emi: soll es nicht sein.** Heute übersetzt
`TranslateLine` jede Chatzeile in `TypingLang()` (kb_worker.cpp, `what.target`), und `skipTypingLang` überspringt
Zeilen in dieser Sprache. Folge: Eingabe Deutsch, Footer Englisch, Deutsch aus „nicht übersetzen“ gelöscht – eine
deutsche Zeile wird trotzdem nicht übersetzt (sie wäre „ins Deutsche“). Der Footer ist die Sendesprache.
**Entschieden (Emi, 2026-10-29):**
- **Lesesprache** als eigene Einstellung, beim ersten Start automatisch = Systemsprache (wie die Eingabesprache), danach
  frei wählbar und von Eingabe und Footer entkoppelt; die Liste „nicht übersetzen“ filtert dagegen. Der Footer bleibt
  Sendesprache (sonst bekäme ein Deutscher, der in den englischen Chat schreibt, englische Zeilen nicht mehr auf Deutsch).
- **Ort:** im Chat-Tab ein „v“-Knopf, der nach oben aufklappt (wie beim Mikrofon-Feld). Darin die Lesesprache und die
  Tab-Aktionen, die heute nur per Rechtsklick auf den Tab gehen (umbenennen usw.; Rechtsklick bleibt zusätzlich).
- **Rechtsklick auf eine Chatnachricht:** markiert die ganze Nachricht sichtbar (über Zeilenumbrüche hinweg) und öffnet
  ein kleines Feld: oben „Übersetzen (Lesesprache)“ – direkt unter dem Mauszeiger, damit der häufige Weg ein schneller
  zweiter Klick bleibt –, darunter „Übersetzen in ›“ mit allen Zielsprachen. Ein ausdrücklicher Klick übersetzt immer,
  auch Sprachen aus „nicht übersetzen“. Chat-Codes bleiben ohne Klicklogik (siehe 2.3.1). Linksklick auf einen Namen
  (Flüster-Tab) bleibt wie er ist.

**A7. Chat-Darstellung (Emi, Screenshot 2026-10-29).** Heute stehen umbrochene Zeilen bündig unter dem Textanfang
hinter dem Namen – bei langen Namen ein großer leerer Block links. Entschieden: Umbruchzeilen **ein Zeichen** (1 em,
schriftgrößenabhängig) eingerückt statt unter dem Text. Dazu, damit Nachrichten trotzdem klar getrennt bleiben: etwas
engerer Abstand zwischen den Zeilen einer Nachricht, etwas mehr Abstand zwischen zwei Nachrichten. Schriftarten und
Zeilenabstände insgesamt mit Emi ansehen (Vorbild: eine Mischung mit dem nativen GW2-Chat). Vorher/Nachher-Screenshot
an Emi, keine echten Chatzeilen ins Repo.

**A8. Übersetzungszeit sichtbar.** Emi möchte die Zeiten sehen: pro Zeile die ms (z. B. im Tooltip der Zeile oder im
Debug-Fenster), ohne Chattext zu speichern.

**A9. „Was ist passiert?“ für eine Zeile (Emi möchte sehen, was der String in dem Moment war).** Ein Eintrag im
Rechtsklick-Feld einer Nachricht (A6) oder im Debug-Fenster zeigt für genau diese Zeile: erkannte Sprache und Ziel, die
geschützten Teile (Tag, Name, Begriff, offizieller Name → Ziel), jeden Versuch (ganz / Namen-Schreibweise / Platzhalter /
Stücke) mit Modellausgabe, Score und Urteil der Prüfung, die Aufräum-Schritte, das Ergebnis und die ms. Nur im Speicher,
nur auf Klick, nie in eine Datei (Datenschutz: kein Chat-Export).

## B. Listen füllen (mit Emi, jeder Eintrag gemessen)

- **B1.** [data/gw2-words.tsv](../data/gw2-words.tsv): nur was ein Übersetzer nachweislich falsch macht; nie
  erfinden (Sprachen ohne GW2-Übersetzung: englisches Wort zuerst); Bench vorher/nachher.
  Kandidaten aus Emis Spieltest: „boss“ (fr/en → de wurde „Chef“), „LF 2 more“ (more → „Mehr“).
- **B1b.** Gildennamen vor einem Tag schützen: großgeschriebene Wörter direkt vor `[TAG]` sind der Name („Death
  Systems [DS]“ blieb, „La Ruche [RUCH]“ wurde „[RUCH]-Hügel“). **Gemessen (erfundene Zeile, 2026-10-29):** mit unserer
  Logik „La Ruche [RUCH] cherche des joueurs pour les raids.“ → „Die [RUCH]-Halle sucht Spieler für die Raid.“, roh →
  „Die Ruche sucht Spieler für die Razzia.“, ohne Tag mit unserer Logik → „Die Ruche sucht Spieler für die Raids.“ – der
  einzeln geschützte Tag neben dem unbekannten Namen bringt NLLB dazu, ein Kompositum zu bauen und den Namen als Wort zu
  übersetzen. Also unsere Schutzform, nicht nur das Modell.
  **Emi, 2026-10-29: erst einen Schritt zurück, keine Namens-Raterei** (Gildennamen können klein geschrieben sein, Tags
  fehlen oft). Reihenfolge: (1) mit A9 ansehen, welcher Versuch „[RUCH]-Halle“ erzeugt hat; (2) exakte Prüfung: ein
  geschützter Teil, an den in der Ausgabe ein neues Wort angehängt wurde („[RUCH]-Halle“, in der Eingabe kein
  „[RUCH]-“), lässt diesen Versuch durchfallen → nächster Versuch; (3) eigene Gildennamen gehören in die eigenen
  Begriffe (gibt es schon); (4) eine Regel „Wörter neben dem Tag sind der Name“ nur, wenn Messungen sie tragen.
- **B2.** Grüße vor dem Modell nachschlagen (Hola, Selam, Merhaba, Salut, Cześć, Привет, Olá, Ciao …): genau diese
  Zeilen erfindet das Modell („das ist alles“, „Ich weiß nicht“), und ihr Score liegt unter `kUnsureScore`.
- **B3.** Erst wenn die Listen stehen: zweiter, **unbekannter** Prüfsatz
  (`tests/data/translation_corpus_holdout.tsv`), nie zum Nachbessern benutzt – er zeigt, ob die Listen allgemein tragen.

## C. Später – nur auf Emis Wort

- C1. Unsichere, sehr kurze Zeile → Original statt erfundener Übersetzung (erst mehr Messung).
- C2. Ganzer fremdsprachiger Satz bei gleicher Ein-/Ausgabesprache: über die Spracherkennung (`SureLanguage`) – vorher
  messen, wie oft sie bei englischen Chatzeilen danebenliegt.
- C3. Verben (farm, level): ob Schutz als Wort trägt, messen.
- C4. Spracheingabe: Emi testet zuerst; Whisper bleibt auf seinen Standardwerten.
- C5. Wächter (Modul-Gesundheit), Aufräumen Schichten 2–3, Fuzzing der Chat-Eingabe.

## D. Bei Emi

- Neuinstallation über Nexus testen (läuft).
- Den GW2-API-Schlüssel auf account.arena.net löschen (wird nicht gebraucht).
- GitHub → Settings → Security → Private vulnerability reporting einschalten.

Stand: 2.7.0 Latest auf `d790a6b`, CI/CodeQL/Scorecard grün.
