# Kernlogik als Mathematik

Jede Funktion als geschlossene Formel: Eingabe → Ausgabe, ohne Einstellungs-Parameter. Beim Aufschreiben gefundene
Fehler stehen direkt dabei (✅ behoben, ⚠ offen). Reihenfolge = Markenkern: Schreiben, dann Übersetzen, dann Lesen.

## 1 Lernende Tastatur

**Normalform eines Worts.** `k(w)` = Kleinschreibung + arabische Schreibvarianten zusammengelegt (أإآٱ→ا, ى→ي, ة→ه,
Harakat weg). Alle Zählungen laufen über `k(w)`.

**Gedächtnis.** Drei Zähler, nur aus gesendeten Nachrichten und gewählten Vorschlägen:
- `c₁(w)` – wie oft das Wort, `c₂(b,w)` – wie oft `w` nach `b`, `c₃(a,b,w)` – wie oft `w` nach `a b`.
- Senden: jedes Wort +1, jedes Paar und jede Dreierfolge +1.
- Wählen (Wortleiste, grauer Vorschlag): `+g` auf `c₁`, `+2g` auf `c₂` und `c₃`, mit `g = 1` bewusst (Klick, Tab zu
  einem anderen) und `g = 0,3` beim einfachen Weiterschreiben mit dem ersten Vorschlag.
- Obergrenzen: 20.000 Wörter, 60.000 Paare (+ Dreier); darüber fallen die seltensten weg.

**Vergessen** alle 500 gelernten Nachrichten: `c₁ ← 2 + (c₁ − 2)·0,95` für `c₁ > 2`, sonst unverändert;
`c₂, c₃ ← 0,95·c`, unter 0,2 gestrichen.
Eigenschaft: Gleichmäßiges Verkleinern ändert keine *Anteile* – es wirkt wie ein gleitender Mittelwert: neue
Zählungen wiegen relativ mehr. Ein Wort mit `c₁ ≥ 2` bleibt „bekannt“ (wird nie wegkorrigiert).
Halbwertszeit eines Paars: `0,95ⁿ = 0,5` → n ≈ 13,5 Schritte ≈ 6.750 Nachrichten.

**Vervollständigung** des angefangenen Worts `p` nach den Wörtern `a b`. Kandidaten `W_p` = bekannte Wörter, die mit
`p` beginnen (länger als `p`). Mit `S₁ = Σ c₁`, `S₂ = Σ c₂(b,·)`, `S₃ = Σ c₃(a,b,·)` über `W_p`:

    score(w) = λ₃·c₃(a,b,w)/S₃ + λ₂·c₂(b,w)/S₂ + λ₁·c₁(w)/S₁
    λ₃ = 0,5·S₃/(S₃+1),  λ₂ = 0,3·S₂/(S₂+1),  λ₁ = 1 − λ₃ − λ₂

✅ *Gefunden beim Aufschreiben:* Vorher war `λ₃ = 0,5`, sobald es **irgendeine** passende Dreierfolge gab. Eine einzige
zufällige Dreierfolge (`S₃ = 1`) bekam damit das volle Gewicht und schlug ein Wort, das man hundertmal benutzt.
Jetzt wächst das Gewicht eines Kontexts mit seiner Erfahrung `S/(S+1)` (wie Witten-Bell-Glättung): 1× gesehen →
halbes Gewicht, 10× → 91 %.

**Nächstes Wort** nach `a b` (nichts getippt). `T₃ = Σ c₃(a,b,·)`, `T₂ = Σ c₂(b,·)`:

    next(w) = w₃·c₃(a,b,w)/T₃ + (1 − w₃)·c₂(b,w)/T₂,   w₃ = 0,6·T₃/(T₃+1)

✅ Derselbe Fehler war hier (`w₃ = 0,6` fest); gleich behoben.

**Sicheres nächstes Wort** (grau nach dem Leerzeichen): im genauesten Kontext mit genug Erfahrung
(`T₃ ≥ 2,5`, sonst Paar): bestes `w` mit `c ≥ 2,5` **und** `c ≥ 0,6·T`. Sonst nichts.
**Satzrest:** dasselbe wiederholt, Kontext rückt nach, höchstens 6 Wörter, kein Wort doppelt (sonst Kreis).
Eigenschaft: Jedes Glied ist für sich sicher – die Kette ist nie sicherer als ihr schwächstes Glied.

**Vervollständigung mit Tippfehler** (nur wenn nichts genau passt; Kandidaten = Wörter mit `c₁ ≥ 2`, deren Anfang
`p` mit einem Tippfehler trifft):

    fuzzy(w) = slip(w) · (λ₂·c₂(b,w)/S₂ + λ₁·c₁(w)/S₁),   λ₂ = 0,3·S₂/(S₂+1),  slip = 2 bei Nachbartaste, sonst 1

✅ Rechnete vorher mit rohen Zahlen `c₁ + 6·c₂` – jetzt dieselben Anteile wie oben.

**Was die Leertaste schreibt** (`Choices`): Ist das getippte Wort gültig (Wörterbuch oder `c₁ ≥ 2`), bleibt es –
außer Tab hat ein anderes gewählt. Sonst das erste aus [Vervollständigungen, dann Korrekturen]. Korrekturen kommen nur
für ungültige Wörter ab 3 Buchstaben. (Regel „überrascht nie“.)

**Gesparte Tastendrücke:** `1 − Tasten/Buchstaben` der gesendeten Nachricht (Eingefügtes zählt nicht).

## 2 Übersetzen: wann überhaupt

**Sprache sicher?** `L(t)`:
1. andere Schrift (Arabisch, Kyrillisch …) → die Schrift entscheidet;
2. ≥ 3 Wörter und die Windows-Spracherkennung meldet etwas → deren Sprache;
3. verräterische Buchstaben (ñ, ß, ç …) → diese Sprache;
4. verräterische Wörter → ihre Sprache, aber nur wenn alle gefundenen zur *selben* Sprache gehören;
5. sonst unsicher → keine Übersetzung.

✅ *Gefunden:* Regel 4 braucht **paarweise disjunkte** Wortlisten. Verletzt waren: „je“ (auch Niederländisch),
„mille“ (auch Französisch), „porque“, „amigo“, „vamos“ (auch Portugiesisch). Entfernt; Test prüft die Fälle.

**Übersetzen?** `fremd(t) = Buchstaben(t) ≥ 3 ∧ übersetzbarer Rest nach Schutz ≠ ∅ ∧ L(t) ≠ ∅ ∧ L(t) ≠ Lesesprache ∧
L(t) ∉ verstanden`. Geschützt (nie übersetzt): Namen, Links, Chat-Codes, Smileys, GW2-Abkürzungen (eindeutige immer,
mehrdeutige nur mit ≥ 2 Großbuchstaben).

**Anzeigen?** `zeigen = fremd ∨ ¬„nur Übersetzungen“ ∨ Fenster liegt über dem GW2-Chat`.

## 3 Lesen

**Wortsalat** `garbled(t)` (t ohne Satzzeichen außen) ist wahr, wenn eines gilt:
1. Ziffer oder Zeichen (! | > $ …) **zwischen zwei Buchstaben** („syn!ax“, „g9danken“);
2. **„!“ oder „|“ direkt vor einem Wort** – die Zeichen, die die Erkennung für l, I, t hält („!raining“);
3. **Ziffern, dann Buchstaben mit einem Großbuchstaben mitten zwischen kleinen** („9QEine“).

Ausgenommen: Links, Codes, Kontonamen (`/ @ [ & . : =`), weniger als 2 Buchstaben.
✅ *Gefunden:* Regel 3 lautete „Ziffern + unübliche Endung“ und traf Chat-Slang („2day“, „4ever“, „2nite“) – der
wurde mangels Wörterbuchwort weggeworfen. Regel 2 galt für jedes Zeichen und zerlegte Emotes („\*grins\*“ →
„grins\*“), „#lfg“, „~hi“. Beides enger gefasst, Tests dazu.

**Reparatur** eines Worts `w`: Kandidaten `K(w)` = alle Kombinationen der Zeichen-Verwechslungen (höchstens 80), dann
eine Buchstaben-Verwechslung; gültig = Wörterbuch kennt es. `|gültig| = 1` → nehmen; `> 1` → Rangfolge der
Rechtschreibprüfung; bei normalen Wörtern (kein Salat) nur ab 5 Buchstaben und nur eindeutig.
✅ *Gefunden:* Der **Sprecher** („Marco:“) lief mit durch und hätte zu einem Wörterbuchwort werden können. Das erste
Wort einer Chatzeile mit Doppelpunkt bleibt jetzt unangetastet (außer echter Wortsalat – Namen haben keine Ziffern).

**Zweitlesung** angenommen ⇔ echtes Wort ∧ Editierabstand ≤ 1 (≤ 4 Buchstaben), ≤ 2 (5), ≤ 3 (länger).
**Unreparierbarer Salat** → weggelassen. **Unbekanntes Wort am Rand** des Bildbereichs → weggelassen, außer die
Mehrheit der Zeilen beginnt am linken Rand (dann ist eng gerahmt, nicht abgeschnitten).

**Bildbereich:** nach dem ersten Bild zählen nur Absätze, deren Unterkante in die untersten 6 Textzeilen reicht
(Zeilen gleicher Höhe ±3 px = eine Zeile). Mathe: 6 Zeilen / 0,4 s = 15 Zeilen/s – mehr schreibt kein Chat.
## 4 Glyphen-Leser (exakt statt Annäherung)

**Tinte** eines Pixels: `t = clamp((h − h₆₀)/(h₉₉,₅ − h₆₀)) · clamp((h − min₅ₓ₅ h)/0,35)` mit `h` = hellster Farbkanal,
`h₆₀, h₉₉,₅` = Perzentile der Zeile. Der zweite Faktor ist der **Umriss**: GW2-Buchstaben haben einen schwarzen Rand,
das Spiel hinter dem Panel nicht – Laub und Himmel fallen damit auf 0.
**Buchstabenfenster:** Grundlinie = Median der Unterkanten der Tintenstücke; Höhe `H = asc + desc` (90. / 95. Perzentil,
einmal gelernt). Normiert gegen die hellste Spalte im Umkreis `H/3` (graue Zeitstempel wie heller Text).

**Lesen** (Viterbi über die Spalten `x`):

    R(0) = 0
    R(x+1) ← R(x) + Σ_y n(x,y)²                       (leere Spalte)
    R(x+w_g) ← R(x) + Σ (n − T_g)² + 0,06·H            (Buchstabe g der Breite w_g ab x)

Kein Zerschneiden vorab: klebende Buchstaben brauchen keine Lücke, ein „m“ aus zwei blassen Bögen bleibt ein Bild.
Leerzeichen ⇔ Lücke ≥ 0,2·H. **Sicher** ⇔ jeder Buchstabe `e = Σ(n−T)²/(w·H) ≤ 0,04` ∧ jeder andere Buchstabe gleicher
Breite ≥ `e + 0,01` ∧ keine leere Spalte mit Tinte > 0,2 pro Pixel. Nur sichere Zeilen ersetzen die Erkennung.

**Lernen:** (1) Stücke = Buchstaben und Wortlücken > Lücken im Wort → jedes Stück lehrt seinen Buchstaben;
(2) Zwangsausrichtung: derselbe Viterbi, aber die Buchstabenfolge ist bekannt (≤ 2 unbekannte, deren Breite sich aus
den Nachbarn ergibt); angenommen nur, wenn jeder bekannte Buchstabe `e ≤ 0,06` hat. Im Spiel wortweise: jedes Wort
der Erkennung, das ein echtes Wörterbuchwort ist, an der Stelle, wo die Erkennung es fand.

**Messung** (echte 4K-Bilder, gelernt nur aus den *anderen* Bildern): sichere Zeilen 100 % exakt; exakte Zeilen
Windows-OCR 11 % → zusammen 14 %. Die verbleibenden Fehler sind Zwillinge mit wenig Beispielen (m/rn, I/l, F/E) –
Daten, keine Logik: im Spiel lernt er laufend.

**Doppelte Prüfung:** eine neue Zeile zählt erst, wenn das nächste Bild (200 ms später) sie wieder zeigt.
Messung nach den Korrekturen (ocr_bench, echte 4K-Aufnahmen): unverändert 30,3 % / 30,4 % – kein Schaden.
