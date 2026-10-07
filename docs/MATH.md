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

⚠ *Offen:* Die Vervollständigung mit Tippfehler (`CompleteFuzzy`) rechnet noch nach der alten Formel
`c₁ + 6·c₂` (rohe Zahlen). Sie mischt sich nicht mit der obigen Liste (eigene Liste, nur wenn nichts passt), sollte
aber dieselben Anteile nutzen.

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

## 3 Lesen (als Nächstes durchrechnen)

- Wortsalat `garbled(w)`: Ziffer/Zeichen zwischen Buchstaben, Zeichen vor dem Wort, Ziffern + unübliche Endung.
- Reparatur: Kandidaten = Kombinationen der Verwechslungen (höchstens 80), das Wörterbuch wählt; mehrere → Rangfolge
  der Rechtschreibprüfung.
- Zweitlesung angenommen, wenn Editierabstand ≤ 1 (≤ 4 Buchstaben), ≤ 2 (5), ≤ 3 (länger) und echtes Wort.
- Bildbereich: nach dem ersten Bild nur Absätze, deren Unterkante in die untersten 6 Textzeilen reicht.
- Doppelte Prüfung: eine neue Zeile zählt erst, wenn das nächste Bild sie wieder zeigt.

⚠ Diese Formeln werden im nächsten Schritt genauso auf Fehler und Abkürzungen geprüft.
