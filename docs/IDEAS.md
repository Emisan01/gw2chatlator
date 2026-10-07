# Ideensammlung: Probleme genau benennen, bei anderen abschauen (2026-10-07)

Grundsatz: Ein sauber definiertes Problem trägt die Lösung oft schon in sich. Jede Ziffer unten ist ein Problem,
danach folgen, wer es woanders schon gelöst hat, und was wir daraus machen. Sortiert nach dem Markenkern (Eingabehilfe vor
gezieltem Übersetzen). Nichts davon ist beschlossen.

---

## P1 – Das Fenster drängt sich auf

**Genau definiert:** Mehrwert pro Bildschirmfläche und Sekunde ist meistens null. Das Fenster ist immer da, auch wenn
niemand fremdsprachig schreibt und du nicht tippst. Es nimmt Platz, Aufmerksamkeit und – über die Erfassung – Rechenzeit.

**Abgeschaut:**
- *Grammarly:* sichtbar ist nur ein kleiner Knopf, und erst wenn ein Textfeld den Fokus hat. Hilfe erscheint am Ort
  der Arbeit, nicht in einem eigenen Fenster.
- *Blish HUD (GW2):* läuft als eigenes Programm, durchsichtig über dem Spiel, ohne Injektion; liest nur MumbleLink.
  Wirkt wie ein Teil des Spiels.
- *Google Lens / Kamera-Übersetzer:* die Übersetzung wird **an die Stelle des Originals** gemalt (AR), kein zweites
  Fenster.

**Ideen:**
1. **Geistermodus (Standard):** Unser Fenster ist unsichtbar bzw. eine dünne Leiste. Es erscheint, wenn du in GW2
   Enter drückst (MumbleLink `uiState` „Textbox hat Fokus“ – das lesen wir schon für die Fokusübergabe) und
   verschwindet nach dem Senden. Schreiben = Fenster da. Sonst nicht.
2. **Übersetzung als Untertitel statt Fensterzeile:** eine fremde Zeile → kleine Blase direkt über dem GW2-Chat, die
   nach ein paar Sekunden verblasst. Klick = anheften.
3. **Die Krone – „anmerken statt ersetzen“ (Punkt 3 neu gedacht):** ein durchsichtiges, klick-durchlässiges
   Fenster (`WS_EX_LAYERED | WS_EX_TRANSPARENT`, eigenes Top-Level-Fenster, kein Owner → Invariante 7) genau über
   dem Chatbereich. Wir kennen die Zeilenpositionen schon (Zeilenraster). Über eine fremde Zeile wird ihre Übersetzung
   in GW2-ähnlicher Schrift auf dunklem Grund **an Ort und Stelle** gemalt – wie Google Lens. Der echte Chat bleibt
   darunter voll funktionsfähig (Scrollen, Links, Kanäle), wir legen nur Text darauf. Damit wirkt der native Chat
   „übersetzt“, ohne ihn zu ersetzen. Schutz vor Selbstlesen: `WDA_EXCLUDEFROMCAPTURE` (Invariante 5).
   Taste halten (z. B. Alt) = Original kurz zeigen.
4. **Wertanzeige statt Dauerpräsenz:** kein Text = keine Fläche. Das Fenster wächst mit Inhalt und schrumpft wieder.

---

## P2 – Erkennungsfehler (Wortsalat, „putput“)

**Genau definiert:** Ein Fehler, den wir *anzeigen*, kostet Vertrauen; ein Fehler, den wir *weglassen*, kostet fast
nichts. Wir haben drei Leser, mehrere Bilder pro Zeile und ein Wörterbuch – nutzen es aber nur einzeln.

**Abgeschaut:**
- *Translumo:* lässt mehrere OCR-Engines gleichzeitig laufen und wählt per gelerntem Bewertungsmodell das beste
  Ergebnis.
- *Kamera-Apps:* Mehrbildmittelung – ein Wort, das in 3 Bildern gleich gelesen wird, stimmt fast sicher.

**Ideen:**
1. **Abstimmung pro Zeile:** Windows-OCR + RapidOCR lesen dieselbe Zeile; gewonnen hat die Lesung mit den meisten
   Wörterbuch-Wörtern (+ Namen aus der Namensliste). Kostet nur bei *neuen* Zeilen Zeit (Cache existiert).
2. **Abstimmung über die Zeit:** Der Doppelscan prüft heute „gleiche Zeile?“. Erweitern auf „gleiches Wort?“: pro
   Wort die Mehrheit aus 2–3 Lesungen.
3. **Selbstlernender Glyphen-Leser:** GW2 hat *eine* Chatschrift. Jedes bestätigte Wort liefert Muster für jeden
   Buchstaben – nach einer Spielstunde kennt das Tool die Schrift besser als jede allgemeine OCR (Phase 2 im Plan,
   jetzt mit Trainingsdaten frei Haus).
4. **Gemeinsame Fehlerliste:** `ocr-fixes.txt` ist schon teilbar – eine kuratierte Liste im Repo („rnain = main“)
   hilft allen ab dem ersten Start.

---

## P3 – Vertrauen und die exe

**Genau definiert:** Der Bogen: „kein Hook“ → Vertrauen ↑; „unbekannte exe, liest den Bildschirm, tippt ins Spiel“ →
Vertrauen ↓; „ich verstehe, was sie tut und was nicht“ → Vertrauen ↑↑. Wir müssen Schritt 2 kurz und Schritt 3
schnell machen.

**Abgeschaut:**
- *SmartScreen:* warnt bei jeder unsignierten, seltenen exe – das ist der erste Vertrauensverlust.
  *SignPath Foundation* signiert Open-Source-Projekte kostenlos (z. B. Super Productivity).
- *Blish HUD:* eine eigene FAQ „Ist es sicher?“ mit dem Verweis auf die ArenaNet-Richtlinie zu Fremdprogrammen
  (Overlays ohne Vorteil werden toleriert, aber nicht offiziell abgesegnet).
- *GitHub:* Build in Actions + „artifact attestation“ beweist, dass die exe aus genau diesem Quellcode stammt.

**Ideen:**
1. Signierte Releases über SignPath (beantragen), gebaut nur in GitHub Actions, mit Prüfsumme und VirusTotal-Link.
2. README-Abschnitt „Ist das sicher / erlaubt?“ – ehrlich: was wir tun, was nicht, ArenaNet-Richtlinie verlinkt,
   keine Garantie.
3. **Live-Transparenz:** ein kleines Protokoll „was ging wohin“ (Dienst, Zeit, Zeichenanzahl – ohne Inhalt) und ein
   Schalter „nichts ins Netz“ (nur lokale Übersetzer/OCR). Vertrauen durch Sichtbarkeit.

---

## P4 – Die Eingabehilfe soll sich wie Muttersprache anfühlen (Markenkern)

**Genau definiert:** Ziel ist nicht „weniger Tippfehler“, sondern „weniger Tastendrücke pro gemeintem Wort“ und
„nie löschen müssen“. Messbar: Tastendrücke ÷ gesendete Zeichen, Anteil gelöschter Zeichen.

**Abgeschaut:**
- *Gboard / SwiftKey:* N-Gramme (Wortfolgen) + persönliches Modell, das beobachtet, welche Wörter du nebeneinander
  benutzt; neuronale Modelle erkennen ähnliche Wörter („Flughafen“ ↔ „Hotel“).
- *Grammarly:* Vorschlag am Ort, eine Taste zum Annehmen (haben wir).

**Ideen:**
1. **Antwortbewusste Vorschläge:** Fragt jemand „anyone for Tequatl?“, stehen „Tequatl“, „yes“, „omw“ oben – die
   Wörter der letzten Chatzeilen (übersetzt in deine Schreibsprache) als Kontext. Lernt weiterhin nur aus *deinen*
   gesendeten Texten (Invariante 10), nutzt den Chat nur für die Reihenfolge.
2. **Dreierfolgen statt Paare** (Trigramme) – „kommst du mit“ nach „kommst du“.
3. **Ganze Satzstücke als Geist:** häufige eigene Phrasen („bin gleich da“, „ty all gg“) erscheinen komplett grau,
   Tab nimmt alles.
4. **Eigene Messzahl in den Einstellungen:** „heute 38 % Tastendrücke gespart“ – Motivation und ehrliche Kontrolle.
5. **Für den Freund:** Arabizi-Eingabe („kifak“ → كيفك) und Sprache (P5).

---

## P5 – Sprechen statt tippen

**Genau definiert:** Tippen + Löschen ist der größte Zeitfresser (deine eigene Beobachtung). Sprechen ist 3–4× schneller.

**Abgeschaut:** *LocalWhisper, OmniDictate, Koe* – lokale Diktat-Tools für Windows mit faster-whisper,
Push-to-Talk-Taste, schreiben an die Cursorposition, nichts geht in die Cloud.

**Ideen:**
1. Sofort: Win+H in unserem Eingabefeld (geht schon, nur erklären).
2. Später: Push-to-Talk-Taste im Tool, whisper.cpp (kleines Modell, CPU) → Text ins Eingabefeld → wie getippt
   übersetzen und vorschauen. Gesendet wird weiterhin nur auf Enter (Invariante 1).
3. Vorlesen eingehender Flüsternachrichten (TTS), abschaltbar.

---

## P6 – Spaß („lauter Blödsinn“)

1. **Stil-Knopf (LLM):** „als Pirat“, „wie Shakespeare“, „übertrieben höflich“ – vor dem Senden.
2. **Aussprachehilfe:** Lautschrift der Übersetzung in kleiner Schrift, damit man es im Voice-Chat sagen kann.
3. **Lernmodus:** Wörter, die du oft übersetzen lässt, werden zu einer kleinen Vokabelliste – irgendwann brauchst du
   das Tool für sie nicht mehr.

---

## P7 – Zu viel manuell bedienen

**Genau definiert:** Jede Einstellung, die man im Spiel umschalten muss, ist eine zu viel.

**Ideen:** Profile („Raid“: nur Flüstern/Gruppe, Geistermodus; „Handel“: Karte + Übersetzung; „Entspannt“: alles),
automatisch nach Kartentyp aus MumbleLink (Raid-/WvW-/Stadtkarte).

---

## Vorgeschlagene Reihenfolge

1. P1.1 Geistermodus + P5.1 Win+H erklären (klein, sofort spürbar).
2. P4.1 antwortbewusste Vorschläge + P4.4 Messzahl (Markenkern).
3. P1.3 „anmerken statt ersetzen“ als Experiment (das Aha-Erlebnis).
4. P3.1/P3.2 Vertrauen (vor jeder breiteren Veröffentlichung).
5. P2.1/P2.2 Abstimmung, dann P2.3 Glyphen-Leser.

Quellen: [Blish HUD – How does it work](https://blishhud.com/docs/user/faqs/how-does-bhud-work),
[Blish HUD – Is it safe](https://blishhud.com/docs/user/faqs/is-bhud-safe),
[ArenaNet – Policy: Third-Party Programs](https://help.guildwars2.com/hc/en-us/articles/360013625034),
[Translumo](https://github.com/MixeroTN/Translumo),
[Gboard-Techniken (IndiaAI)](https://indiaai.gov.in/case-study/google-s-mind-reading-keyboard-analyzing-the-ai-techniques-behind-text-prediction),
[SwiftKey Neural](https://venturebeat.com/business/swiftkey-mobile-keyboard-neural-networks),
[Super Productivity – Code Signing (SignPath)](https://super-productivity.com/code-signing/),
[LocalWhisper](https://pypi.org/project/localwhisper/),
[OmniDictate](https://dev.to/gurjar1/omnidictate-free-local-real-time-ai-dictation-for-windows-3oo5).
