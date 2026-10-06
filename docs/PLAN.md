# Plan: GW2 Chat Translator, von v0.5.1 zum verlässlichen Chat-Klon

Stand 2026-10-06. Alle Agents lesen zuerst `CLAUDE.md`, dann diese Datei.

## Ziel

Ein Fenster über dem GW2-Chat, das den nativen Chat **überflüssig** macht. Es zeigt jede Nachricht in deiner Sprache und schickt deine Nachrichten blitzschnell korrigiert und übersetzt zurück. Kein Hook, nichts im Spielprozess, nichts wird ohne deinen Tastendruck gesendet.

**Leitlinie: einfach, aber funktional.** Keine Logik, die der Nutzer nicht versteht.

## Fortschritt (2026-10-06)

| Punkt | Stand |
|---|---|
| Spur S: Wort vergessen, alles löschen | ✅ Rechtsklick in Wortleiste und Eingabe; Einstellungen → Schreiben |
| Spur S: Live-Vorschläge mitten im Wort, 1 Tippfehler, Nachbartasten des Layouts | ✅ `CompleteFuzzy`, `keyboard_layout` |
| Spur S: grauer Rest der Vervollständigung hinter dem Cursor | ✅ nur bei Links-nach-rechts-Text |
| Spur S: Enter korrigiert das letzte Wort | ✅ |
| Spur S: Arabisch (Varianten, Harakat, Satzzeichen ، ؟ ؛) | ✅ `WordKey` |
| Spur S: Tempo | ✅ gemessen: ~2 ms pro Taste bei vollem Modell (20 000 Wörter); Windows-Rechtschreibprüfung wird zwischengespeichert |
| Phase 0: Testdaten anonymisiert, `local/` ignoriert, Datenschutz in README, MyMemory-Hinweis | ✅ (Git-Historie noch nicht bereinigt) |
| Phase 1: Messwerkzeug `ocr_bench` | ✅ |
| Phase 1: dynamische Vergrößerung, keine Kontrast-Tricks, Tesseract invertiert ohne Doppelversuch | ✅ |
| Phase 1: Wörter nach gemessenem Zeilenraster ordnen | ✅ |
| Phase 1: einrastender Rahmen mit Ampel und Vorschau | ✅ (im echten Spiel noch zu testen) |
| Phase 1: Zeitstempel- und Kanal-Parser gehärtet, Fortsetzungszeilen nach Textfarbe | ✅ |
| Phase 1: Namensliste, Namen werden nicht übersetzt | ✅ eingehend und ausgehend |
| Phase 1: WGC-Versatz im Fenstermodus | ✅ (`DWMWA_EXTENDED_FRAME_BOUNDS`, im Spiel prüfen) |
| Phase 1: beim Start nur die letzten 3 Nachrichten | ✅ |
| Phase 1: Einrichtungs-Tipp große Chatschrift | ✅ |
| Feinschliff: Startliste Wortleiste | ✅ ca. 100 GW2-Begriffe |
| Feinschliff: Warnungen, ungenutzte Übersetzungen | ✅ MSVC ohne Warnungen, 0 fehlende, 0 ungenutzte Übersetzungen |
| Phase 2: eigener Glyphen-Leser | ⏸ Die Messung zeigt: bei 4K liest Windows-OCR jetzt mit 0,5–3,4 % Fehlern nach dem Parser in ~90 ms. Entscheidung erst mit echten 1080p/1440p-Aufnahmen. |
| Lokal und klein übersetzen (Firefox/Bergamot) | ⏸ Arabisch → Englisch ist dort noch in Entwicklung, die C++-Engine ist inaktiv |
| Phase 3: Live-Test im Spiel, Verzögerung beim Senden messen | offen, braucht das Spiel |
| `main_window.cpp` aufteilen | offen |

### Runde 2 (nach dem ersten Spieltest, 2026-10-06 abends)

| Punkt | Stand |
|---|---|
| Senden kam nicht an | ✅ Tasten werden jetzt gehalten (`KeyHoldMs`, Standard 30 ms), Strg+V sauber gestaffelt |
| Nur Chat lesen (Charakterauswahl lieferte Unsinn) | ✅ nur auf einer Karte (MumbleLink), Strukturfilter, Bruchstück-Filter, Doppelscan |
| Anzeige ohne Zeitstempel und Kanal-Kürzel, Name neutral, Text in Kanalfarbe | ✅ |
| Klick auf Namen → eigener Flüster-Tab mit Spielernamen (max. 5) | ✅ |
| Klick auf nicht übersetzte Zeile → gezielt nachübersetzen | ✅ |
| Übersetzung bei viel Chat zu langsam, Reihenfolge | ✅ bis zu 4 Aufträge parallel (LLM: 1), das Neueste zuerst; Anzeige bleibt chronologisch |
| Türkisch u. a. nicht übersetzt (kurze Zeilen) | ✅ Sprache zusätzlich an typischen Buchstaben erkannt |
| Lesen schneller | ✅ alle 0,4 s statt 0,9 s; „Automatisch“ nimmt Windows-OCR (~90 ms), Tesseract nur bei sehr kleiner Schrift |
| Eingabehilfe aggressiver: Dropdown unter dem Wort, Leertaste übernimmt, Tab blättert | ✅ inkl. verrutschter Hand (`jsööp` → `hallo`) und Nachbartasten-Fehlern |
| Smileys nicht übersetzen | ✅ |
| Verstoß gegen Regel 1 (Handle auf den GW2-Prozess) | ✅ entfernt |
| **Offen:** lokale Übersetzung prominent anbieten, 2 Wege (kleines LLM über Ollama; spezialisiertes Übersetzungsmodell wie Opus-MT/NLLB über CTranslate2) | offen |
| **Offen:** Technikdaten für Interessierte (Erkennungsrate, Zeiten, Filterquote, Leserate einstellbar) | offen (Leserate ist schon einstellbar) |
| **Offen:** ausfallsichere Erkennung des Chatfensters (z. B. am Tab-Leisten-Muster), unterste Zeile sicher erfassen | offen |
| **Offen:** Arabizi und Translit-Russisch (lateinisch geschriebenes Arabisch/Russisch) erkennen und übersetzen | offen; ein LLM kann das, MyMemory nicht |
| **Offen:** Menüs aufräumen | offen, niedrige Priorität |

### Runde 3: Erlebnis bei der Erstinstallation (beschlossen 2026-10-06)

Messlatte: **In unter einer Minute nach dem ersten Start liest man den ersten übersetzten Satz, ohne eine Einstellung zu öffnen.** Vorbilder: Discord- und Steam-Overlay (erscheint einfach), Google Lens (draufhalten, geht), Gboard/SwiftKey (Eingabehilfe).

| Punkt | Stand |
|---|---|
| Gelber WGC-Rahmen (Windows 10): Farbe nicht änderbar → unter Windows 10 Bildschirmaufnahme (DXGI) ohne Rahmen, WGC nur unter Windows 11 (Rahmen dort abschaltbar); Auswahl in den Einstellungen | ✅ |
| MyMemory sparsam → **geändert:** alles wird automatisch übersetzt, Ausnahmen per Häkchen (Kanäle, Sprachen); per Klick trotzdem | ✅ |
| Sprachen, die man versteht (z. B. DE, EN), werden nicht übersetzt; per Klick trotzdem | ✅ Häkchen „Nicht übersetzen“ für EN/DE/FR/ES plus weitere Codes |
| Kleine Anzeige, wie viel vom MyMemory-Kontingent heute übrig ist | ✅ Fußzeile |
| Technik-Seite: alle Parameter an einer Stelle, Live-Werte (Lesedauer, Zeilen, verworfene Zeilen, Übersetzungsdauer), „Diagnose kopieren“ | ✅ Einstellungen → Technik |
| Version im Fenster, CHANGELOG, später GitHub-Releases mit exe und freiwillige Update-Prüfung | ✅ 0.6.0 aus CMake, CHANGELOG; Releases und Update-Prüfung offen |
| Einrichtung minimal: Sprache wählen → unser Fenster liegt unten links → „Öffne den GW2-Chat (Enter)“ → Chat wird an Zeitstempel-Zeilen erkannt → Rahmen setzt sich selbst, Fenster legt sich darüber. Manuelles Ziehen bleibt als Option | ✅ im Spiel zu testen |
| Unser Fenster an den nativen Chat koppeln (öffnen/minimieren) | **zurückgestellt** (GW2 hat keine Taste zum Minimieren; simulierte Klicks widersprechen Regel 1) |
| Modus „Beliebiges Fenster übersetzen“ (Browser, andere Spiele): Kern ist allgemein (Aufnahme, Erkennung, Übersetzung); GW2-spezifisch sind nur MumbleLink, Parser, Farben, Senden | später, in der Architektur mitdenken |

### Was noch aussteht (Stand 0.6.0)

**Im Spiel testen (braucht den Nutzer):**
1. Senden: kommen Nachrichten jetzt an? Sonst `KeyHoldMs` (Technik-Seite) erhöhen.
2. Erstinstallation: Einrichtung (eine Seite) → GW2-Chat öffnen → wird er gefunden, legt sich das Fenster darüber?
3. Windows 10: ist der gelbe Rahmen weg (Aufnahme „Automatisch“ = Bildschirm)?
4. Dropdown unter dem Wort, Flüster-Tabs per Namensklick, Links mit Abfrage, Klick zum Übersetzen.
5. Lokales Modell per „Installieren“ (Ollama) und Übersetzungsgeschwindigkeit damit.
6. Echte Aufnahmen bei 1080p/1440p in `local/bench` (Diagnosebilder einschalten) → Entscheidung über den Glyphen-Leser.

**Bauen:**
- Modus „Beliebiges Fenster übersetzen“ (Browser, andere Spiele, Videos): Fenster/Bereich wählen, mitlesen, übersetzen, nichts senden; pausiert, wenn das Fenster minimiert oder verdeckt ist. Parser im „Klartext“-Modus ohne GW2-Struktur.
- GitHub-Releases mit fertiger exe, freiwillige Update-Prüfung (Releases-API), Version im Hauptmenü.
- Kalibrierung erweitern: Kanalfarben einmessen per Klick (gibt es per Rechtsklick), Prüfbild/Testlauf auf der Technik-Seite („jetzt messen“: Erkennungsrate, Zeilen, Zeit).
- Arabizi und lateinisch geschriebenes Russisch erkennen und übersetzen (LLM kann es, MyMemory nicht).
- Kleines spezialisiertes Übersetzungsmodell (Opus-MT/NLLB über CTranslate2) als zweiter lokaler Weg neben Ollama.
- Menüs aufräumen; `main_window.cpp` aufteilen (Leser, Docking, Menüs, Senden).

**Zurückgestellt (Entscheidung des Nutzers):**
- Unser Fenster an den nativen Chat koppeln (öffnen/minimieren gemeinsam).
- Bereinigen der Git-Historie von alten Testnamen (nur mit ausdrücklichem OK, Force-Push).

**Messung (4K, Fehler nach dem Parser, alt → neu):** Windows-OCR 15,8 / 48,5 / 21,8 % → **0,5 / 3,2 / 3,4 %**;
Tesseract 12,3 / 23,9 / 16,6 % → **2,3 / 12,9 / 0,7 %**.

## Entscheidungen

| Thema | Entscheidung |
|---|---|
| Quelle | Bildpunkte des GW2-Fensters über WGC, externe exe. Exakter Text aus dem Spielprozess (Nexus) höchstens später als freiwilliger, separater Download. |
| Texterkenner | **Ziel: eigener GW2-Leser (Glyphen)**, eingebaut, ohne Download. Bis dahin Windows-OCR als Standard und **Tesseract als optionaler Download** für eine bessere Erkennungsquote (Basis ca. 90 MB, mit allen Sprachen ca. 400 MB). Der eigene Leser soll den Tesseract-Download überflüssig machen. |
| Bildvorbereitung | Die feste Aufbereitung (2× + Schwarz-Weiß + Kontrast) macht Tesseract nachweislich 2–3× schlechter und fliegt raus. Stattdessen wird **dynamisch** auf ca. 30 px Zeilenhöhe vergrößert. |
| MyMemory | Bleibt **voll funktionsfähig**. **Einmaliger Hinweis**, dass Texte an einen fremden Dienst gehen, mit einem Schalter zum Abschalten. |
| Gelernte Wörter | Nur lokal. Zum Verwalten nur **„Wort vergessen“** und **„Alles Gelernte löschen“** (Neustart). Keine weitere Lernlogik. |
| Testdaten | Keine echten Namen, Bilder oder Chats im Repo. Anonymisiert am 2026-10-06 (Namen aus einem Testlauf). |
| Repo | Nichts wird gepusht, ohne dass vorher auf fremde Daten geprüft wurde. |

## Messung vom 2026-10-06 (Grundlage)

Drei echte 4K-Screenshots, handgeschriebene Wahrheit, Zeichenfehler:

| | Windows-OCR | Tesseract (fast-Modelle) |
|---|---|---|
| Rohbild | 8–14 % | **5–6 %** |
| heutige App-Aufbereitung | 12–19 % | 10–17 % |
| 1080p simuliert, roh | – | 41–60 % |
| 1080p, sauber 3× vergrößert | – | 11–17 % |

Die verbleibenden Fehler stecken überwiegend in Klammern und Zeitstempeln. Der Nachrichtentext wird fast fehlerfrei gelesen.

## Spur S: Schreiben mit blitzschneller Autokorrektur (zuerst, braucht keine Texterkennung)

Anwendungsfall: Ein Freund tippt schnell und fehlerreich auf Arabisch. Er will sofort korrigiert und vervollständigt werden und das Ergebnis als Englisch in den Chat schicken. Vorgänger war [Grammarly-support-overlay](https://github.com/hazratali-uydevelopers/Grammarly-support-overlay-for-guild-wars-2) (Electron + AutoHotkey + externes Grammarly, keine Übersetzung).

1. **Wort vergessen** (Rechtsklick in Wortleiste oder Eingabe) und **alles Gelernte löschen** (Einstellungen).
2. **Live-Vorschlag ab dem ersten Buchstaben**: Der beste Vorschlag ist sofort hervorgehoben und wird bei Leerzeichen, Satzzeichen und Enter übernommen. Prüfen, ob Enter das letzte Wort heute schon korrigiert.
3. **Fehlertolerante Vervollständigung**: 1 Tippfehler im Präfix erlaubt, Nachbartasten des aktiven Layouts zählen weniger (`helo` → `hello`).
4. **Arabisch**:
   - Beim Vergleichen gelten أ إ آ ا, ى/ي, ة/ه, Hamza, Tatweel und Diakritika als gleich.
   - Rechts-nach-links in der Wortleiste.
   - Wörterbuch: Windows-Rechtschreibprüfung, falls das Sprachpaket installiert ist, sonst nur Gelerntes.
   - *Offen: arabische Schrift oder Arabizi?*
5. **Unter 15 ms pro Taste**: messen. Vorschläge zuerst aus dem Gelernten im Speicher, die Windows-Rechtschreibprüfung nachreichen und cachen.
6. Die Vorschau zeigt Englisch plus Rückübersetzung, damit der Nutzer vor Enter sieht, ob die Bedeutung stimmt.

## Phase 1: Lesen solide machen

1. **Messwerkzeug** `tools/ocr_bench`: Echte Bilder und die Wahrheit liegen in `local/` (gitignored), ins Repo kommen nur synthetische Bilder.
2. **Dynamische Vorbereitung**: Zeilenraster messen, auf ca. 30 px vergrößern, keine Kontrast-Tricks. Der MumbleLink-Wert `uisz` hilft als Hinweis.
3. **Rahmen, der einrastet und bewertet**:
   - Grob ziehen, dann rastet der Rahmen auf das Zeilenraster ein, ohne Tabs und Eingabezeile.
   - Eine Ampel und eine Vorschau der ersten erkannten Zeilen erscheinen.
4. **Struktur über Farbe und Position**: Zeitstempel, Kanal und Sprecher (eigene Farbe vor dem ersten `:`). Die Texterkennung liest nur noch den Nachrichtentext.
5. **Namensliste**: Erkannte Sprecher werden im Text nicht übersetzt; ähnliche Schreibweisen gelten ebenfalls als Name.
6. **WGC prüfen**:
   - Vermuteter Versatz im Fenstermodus (`GetWindowRect` vs. `DWMWA_EXTENDED_FRAME_BOUNDS`).
   - Gelber Rahmen unter Windows 10.
   - Vollbild-Modi.
7. **Start**: nur neue Zeilen übersetzen, höchstens die letzten 3.
8. **Einrichtungs-Tipp**: Chatschrift in GW2 auf groß stellen (unser Fenster liegt ohnehin darüber), Zeitstempel an.

## Phase 2: Eigener GW2-Leser (Glyphen), macht Tesseract überflüssig

Warum: Tesseract bleibt optional, also lesen die meisten Nutzer mit Windows-OCR (8–14 % Fehler). Ein eingebauter Leser, der nur die GW2-Chatschrift kennt, bringt allen die beste Qualität ohne Download.

- **Kein großes Modell, kein LLM.** Der GW2-Chat kann nur ca. 150–250 Zeichen in einer Schrift darstellen. Dafür reicht Mustervergleich.
- **Ablauf**:
  1. Farbmaske über die festen Kanalfarben.
  2. Zeilenraster bestimmen.
  3. Jedes Zeichen mit den gelernten Mustern vergleichen.
  4. Eine optimale Pfadsuche wählt die beste Zeichenfolge der Zeile.
  5. Unbekannte Zeichen kommen als `?` heraus, nur diese gehen an Windows-OCR oder Tesseract.
- **Lernt selbst**:
  - aus den eigenen gesendeten Nachrichten (der Text ist exakt bekannt),
  - aus einem Kalibriersatz, der in der GW2-Eingabezeile getippt, aber nicht gesendet wird,
  - am Anfang aus sicheren Treffern von Windows-OCR/Tesseract.
- **Erwartung**: Millisekunden pro Bild, Muster wenige KB pro Schriftgröße, liegen in `learned\glyphs_<höhe>.bin`.
- **Optional, falls Mustervergleich nicht reicht**: ein winziges, auf GW2-Schrift trainiertes Erkennungsmodell (ONNX, ca. 1 MB). Das ist der eigentliche Sinn der „LoRA-für-GW2“-Idee.
- Erst bauen, wenn Phase 1 steht. Das Messwerkzeug entscheidet.

## Phase 3: Live-Test im echten Spiel

Gemessen werden:
- Erkennungsquote
- Verzögerung
- CPU-Last im Raid
- Scrollen und Ausblenden
- Kartenwechsel
- Fenster- und Vollbildmodus
- 125 % Windows-Skalierung zusammen mit der GW2-Interface-Größe

## Übersetzen: lokal, aber klein

- **Ollama mit qwen 7B** ist für Chatzeilen zu schwer, weil es neben dem Spiel um die Grafikkarte konkurriert. Es bleibt als Option für starke Rechner.
- **Kandidat für lokal und klein: Firefox-Translations-Modelle (Projekt Bergamot)**:
  - ca. 37 MB pro Richtung, nur CPU, MPL-2.0.
  - Gebaut genau für „klein, schnell, offline“.
  - *Prüfen: welche Sprachpaare es gibt (Arabisch?), wie schnell sie in C++ einzubinden sind, Qualität bei Chat-Slang.*
- **Kleinere LLMs** (qwen2.5 1.5B/3B) als Mittelweg prüfen.
- **GW2-Begriffe und Lore** (nur am Rande): Das Glossar mit offiziellen Namen aus der GW2-API gibt es schon. Dafür braucht es kein eigenes Modell, nur ein gutes Glossar und Schutz vor Übersetzung.

## Feinschliff

- Startliste für die Wortleiste (LFG, Tequatl, Fraktal …).
- Compiler-Warnungen (WGC) und 25 ungenutzte Übersetzungen aufräumen.
- `main_window.cpp` (119 KB) aufteilen.
- Verzögerung beim Senden messen.

## Was bedacht werden muss

- **Auslieferung**: Ohne Code-Signatur warnt Windows SmartScreen. Fehlalarme von Virenscannern sind wahrscheinlich (Bildschirmaufnahme + Tastatureingaben). Ein Update-Weg fehlt.
- **Spielsprache**: System- und Ereigniszeilen und Kanal-Kürzel unterscheiden sich in DE/EN/FR/ES.
- **Links und Chat-Codes** nie übersetzen.
- **Eigene Nachrichten** über den bekannten gesendeten Text abgleichen.
- **Tests**: `core_tests` laufen nativ (`cmake --build build --config Release --target core_tests`, 304/304 am 2026-10-06). GitHub Actions nur mit synthetischen Daten.

## Offen

- Schreibt der Freund arabische Schrift oder Arabizi?
- Git-Historie von den alten Testnamen bereinigen (Force-Push)? Nur mit ausdrücklichem OK.
