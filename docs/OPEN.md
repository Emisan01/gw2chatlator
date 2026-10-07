# Was noch nicht fertig oder nicht perfekt ist (Stand 2026-10-07)

Ehrliche Durchsicht. Jede Zeile: **Logik** (wo der Kreis nicht schließt), **Mathe** (was die Zahlen sagen),
**Erlebnis** (was der Nutzer spürt). Status egal – Sammlung. ★ = wichtig.

## Die Grundrechnung (warum Erkennung alles entscheidet)

Bei einer Zeichenfehlerquote p ist eine Zeile mit 40 Zeichen fehlerfrei mit Wahrscheinlichkeit (1 − p)^40:

| p | fehlerfreie Zeile |
|---|---|
| 0,5 % (4K, Windows-OCR) | 82 % |
| 3 % | 30 % |
| 10 % (1080p, RapidOCR gut) | 1,5 % |
| 30 % (1080p, Windows-OCR) | ~0 % |

Folge: Bei 1080p ist *jede* Zeile fehlerhaft – Reparatur (Wörterbuch, Abstimmung, LLM) ist dort kein Extra, sondern
Pflicht. Bei 4K reicht fast der Rohtext.

## A – Lesen

1. ★ **1080p-Erkennung** (8–31 % Fehler mit RapidOCR). Gemessen nur mit *simuliertem* 1080p. Echte Aufnahmen fehlen.
   Größter Qualitätshebel.
2. ✅ (Kurzwort-Liste) **Kurze fremde Zeilen gingen verloren.** `SureLanguage` braucht 3 Wörter oder verräterische Buchstaben → „merci“,
   „gracias“, „grazie mille“ werden nie übersetzt. Logik: Wir sparen Arbeit, verlieren aber genau die Höflichkeiten.
   Lösung: kleine Liste typischer Kurzwörter je Sprache (merci, gracias, bitte, obrigado …) als sicheres Merkmal.
3. ✅ (beim Überdecken alles zeigen) **„Nur Übersetzungen“ + „über den Chat legen“ widersprachen sich.** Liegt unser Fenster über dem GW2-Chat und
   zeigt nur Übersetzungen, sieht man deutsche/englische Zeilen gar nicht mehr. Logik-Kreis offen. Lösung: beim
   Überdecken zählt „alles zeigen“ (oder Hinweis beim Einschalten).
4. ✅ (eng gerahmt = kein Schnitt) **Rand-Filter konnte zu viel wegwerfen.** Unbekannte Wörter ≤ 3 px am Rand des Bildbereichs fallen weg – rahmt man
   eng, steht das erste Wort *jeder* Zeile am Rand (Namen, Slang). Lösung: nur wegwerfen, wenn andere Zeilen nicht an
   derselben Kante beginnen (dann ist es abgeschnitten, nicht nur eng gerahmt).
5. ✅ **„Jetzt einmal übersetzen“ endete nach festen 3,5 s.** Mathe: Doppelscan braucht 2 Bilder; Tesseract 1,5–3,5 s pro
   Bild, RapidOCR erstes Bild 1–2 s → kann zu früh enden. Lösung: Ende nach 2 erfolgreichen Bildern, 10 s als Grenze.
6. **„Nur die untersten 6 Zeilen“** stimmt für Chats, nicht für Seiten, die man scrollt (neuer Text oben). Bei einem
   Dokument: „Jetzt einmal übersetzen“ benutzen. Mathe: 6 Zeilen / 0,4 s = 15 Zeilen/s – reicht für jeden Chat.
7. **Wortsalat-Filter ungemessen auf App-Schriften** (nur GW2-Bench: neutral). Weggelassene Wörter können den Sinn
   ändern („nicht“ fehlt). Bis zu 80 Wörterbuchfragen pro Salatwort – nur beim ersten Bild spürbar.
8. Abstimmung mehrerer Leser / mehrerer Bilder pro Wort (Translumo-Idee) fehlt noch.

## B – Schreiben (Markenkern)

1. ✅ (Einstellungen → Schreiben) **Kein Beweis für den Nutzen.** Es fehlt die Zahl „gesparte Tastendrücke“: 1 − Tastendrücke ÷ gesendete
   Zeichen. Ohne sie wissen wir nicht, ob die Engine hilft.
2. ✅ (gemessen: RTL-Zeichen melden ihre rechte Kante; live mit Arabisch prüfen) **Graue Vervollständigung nur links-nach-rechts** → der arabische Freund (Hauptnutzer 2!) sieht sie nicht; er hat
   nur die Wortleiste. Größte Lücke für ihn.
3. ✅ (Dreierfolgen) **Nächstes Wort nur aus Wortpaaren**; Dreierfolgen („kommst du *mit*“) fehlen. Mathe: Paare kennen nur das letzte
   Wort – bei häufigen Wörtern („du“, „ich“) ist das fast Raten.
4. **Arabizi** („kifak“ → كيفك) fehlt – so schreiben viele Araber im Spiel tatsächlich.
5. **Ganze Phrasen** („bin gleich da“) als grauer Vorschlag fehlen.
6. Sprache: nur Win+H; Push-to-Talk mit lokalem Whisper wäre der nächste Schritt (3–4× schneller als Tippen).
8. ★ **Vorwissen fehlt (Kaltstart):** Gboard kennt die Sprache ab Werk, wir starten fast leer. Plan (0.8.1): pro Sprache
   die häufigsten Wörter und Wortpaare als kleine mitgelieferte Liste (~1 MB), Gelerntes zählt mehr. Quellen geprüft:
   Tatoeba-Sätze (CC-BY 2.0 FR, teils CC0 – gesprächsnah, gut für Paare), FrequencyWords aus OpenSubtitles
   (Inhalt CC-BY-SA 4.0 – die abgeleitete Liste bleibt CC-BY-SA, Quellenangabe in THIRD_PARTY_NOTICES). Danach:
   Satzgedächtnis (ganzer Rest eines oft geschriebenen Satzes als Vorschlag). ✅ Lernen bei der Wahl (Wortleiste und
   grauer Vorschlag) – „negatives Lernen“ ist unnötig: das Gewählte zu stärken reiht den Rest von selbst nach hinten.
7. Lernen ist pro Sprache getrennt – wer Deutsch und Englisch mischt, lernt doppelt langsam.

## C – Übersetzen

1. **Standard ist MyMemory** (öffentlich, mittlere Qualität, 50.000 Zeichen/Tag ≈ 800 Chatzeilen). Ein freier
   Gemini-Schlüssel wäre deutlich besser – die Einrichtung könnte das aktiver anbieten.
2. LLM-Reparatur von Erkennungsfehlern nur mit LLM; mit klassischen Übersetzern gehen Fehler mit durch.
3. Rückübersetzung zeigt, *ob* etwas falsch ist, aber nicht *was* man ändern soll.

## D – Benutzer-Erlebnis

1. ★ **Alte installierte Kopie läuft weiter**, man testet die falsche Version (heute passiert). Kein Hinweis „neuere
   Version verfügbar“. Lösung (klein): beim Start Version gegen GitHub-Release prüfen, nur Hinweis, nichts automatisch.
2. **Zu viel Handarbeit** im Spiel (Menü, Haken). Profile wären die Lösung – aber bewusst *nicht* zu viel Fensterlogik.
3. **Unsignierte exe** → SmartScreen-Warnung beim ersten Start = erster Vertrauensverlust. SignPath (kostenlos) beantragen.
4. Bedienung nicht entdeckbar: Leertaste/Tab/→ für Vorschläge, Klick auf Namen, Rechtsklick „korrigieren“. Ein
   kurzes „Wusstest du?“ in der leeren Fensterfläche würde reichen.
5. Das Menü bleibt bewusst wie es ist (Entscheidung 2026-10-07).

## E – Technik

1. `main_window.cpp` hat ~3.700 Zeilen → aufteilen (Leser, Senden, Menü, Docking).
2. Leser und Senden sind nur unter Wine end-to-end getestet, nie automatisiert unter echtem Windows.
3. Lokal 10+ Commits ungepusht; Release 0.7.1 wartet auf OK.

## Vorschlag: Reihenfolge nach Nutzen ÷ Aufwand

1. A2 Kurzwörter, A3 Überdecken-Widerspruch, A4 Rand-Filter, A5 Einmal-Übersetzen-Ende (je klein, schließen Logik-Kreise)
2. B1 Tastendruck-Zahl (macht den Markenkern messbar)
3. B2 Vervollständigung für Arabisch (Hauptnutzer 2)
4. D1 Versionshinweis
5. B3 Dreierfolgen, A1 echte 1080p-Aufnahmen, B4 Arabizi
