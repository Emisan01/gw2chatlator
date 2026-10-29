# ToDo 2.7.1

Regeln: [AGENTS.md](../AGENTS.md) (Evidence bei feat/fix/perf/refactor, messen statt raten), nur erfundene Testzeilen,
keine AI-Attribution, kein Push/Release (nur auf Emis Wort). Claude prüft zuletzt.

## Fehler
1. **Block bleibt unübersetzt:** nach einer Zeile mit Tippfehler, einer Korrekturzeile, einer Zeile nur `\o/` und einer
   kurzen englischen Zeile übersetzt kein Übersetzer mehr die folgenden Zeilen, auch nicht per „einmal übersetzen“.
   Nachstellen (erfundene Zeilen), Ursache finden, Test dazu (`TranslateRecentLines` → `TranslateLine`, kb_worker.cpp).
2. **Rohschalter:** bleibt bis zum Neustart an, auch beim Übersetzerwechsel, und man sieht, ob er an ist.
3. **NLLB-Qualitätsstufe wechseln:** neu initialisieren und melden „Qualitätsstufe geladen“.
3b. **Eigene Nachricht ging unübersetzt raus** (Live-Test: kurze deutsche Zeile, gemischte Zeile „ich habe J'ai …“).
   Verdacht: kb_worker.cpp:242 nimmt die erkannte Sprache vor der Eingabesprache – erkennt die Erkennung eine kurze
   Zeile falsch (z. B. als Zielsprache), wird nichts übersetzt. Fix: für eigenen Text die Eingabesprache, die Erkennung
   nur wenn sicher (`SureLanguage`). Prüfen, dass Enter vor fertiger Übersetzung wartet („translating and sending …“).
3c. **Alte Übersetzung bleibt stehen:** wechselt das Ziel und ist eine Zeile schon in der neuen Zielsprache (wird
   übersprungen), zeigt sie weiter die alte Übersetzung – sie soll dann das Original zeigen.
   Dazu (Live-Test FR→FR): kurze französische Zeilen mit Tippfehlern wurden nicht als Französisch erkannt und
   „ins Französische“ übersetzt („Je suis un mai (?)“, „où est les nouveaux joueurs“). Regel: hat die Übersetzung fast
   nichts geändert oder ist sie unsicher, während das Ziel eine Sprache ist, die du liest – Original zeigen.
3d. **Kein unnötiges Neu-Übersetzen:** nur wenn Lesesprache, Übersetzer oder Qualitätsstufe sich ändern – und dann
   kurz sagen, warum.

## Übersetzung
4. **Kein-Mist-Filter (so einfach wie möglich):** ist ein Ergebnis schlecht (Score unter `kUnsureScore` oder die
   Prüfung schlägt an), die Zeile einmal mit höherer Qualität neu übersetzen; ist sie dann noch schlecht, das Original
   zeigen statt des schlechten Ergebnisses. Als Option mit Schalter. Mit translation_bench belegen.
4b. **Eigene Nachricht wartet nie hinter dem Chat:** heute ein Helfer, eine Übersetzung nach der anderen – läuft gerade
   eine lange Chatzeile, wartet die eigene. Vorschlag statt zweier Instanzen: ein Helfer mit zwei Replikaten
   (`num_replicas_per_device = 2` in translate_helper_main.cpp; CTranslate2 teilt die Modellgewichte laut Doku – RAM/CPU
   messen) und Anfrage-IDs im Protokoll; einfachere Alternative: automatische Chatzeilen pausieren, solange du tippst.
5. **Drossel:** automatische Zeilen unter Last nie unter Beam 2. Vorher je Beam (1/2/4) messen: Bedeutung und ms.
6. **Grüße** (Hola, Selam, Merhaba, Salut, Cześć, Привет, Olá, Ciao …) vor dem Modell nachschlagen.
7. **Listen füllen** mit Emi: `data/gw2-words.tsv` nur mit gemessenen Fehlern, nichts erfinden (z. B. „boss“). Danach
   ein zweiter, unbekannter Prüfsatz `tests/data/translation_corpus_holdout.tsv`, nie zum Nachbessern.

## Oberfläche
8. **Lesesprache:** eigene Einstellung, Start = Systemsprache, dann frei wählbar, unabhängig von Eingabe und Footer;
   „nicht übersetzen“ filtert dagegen. Ort: „v“-Knopf im Chat-Tab, klappt nach oben auf (wie beim Mikrofon), dazu die
   Tab-Aktionen (Rechtsklick auf den Tab bleibt).
9. **Einzelne Nachricht übersetzen:** Rechtsklick auf eine Chatnachricht markiert sie ganz (über Umbrüche) und bietet
   „Übersetzen (Lesesprache)“ direkt unter dem Mauszeiger und „Übersetzen in ›“ (alle Sprachen). Ein Klick übersetzt
   immer. Keine Klicklogik auf Chat-Codes. Wörter lernen bleibt im Eingabefeld.
10. **Direktstart in der Kachel:** alles für den Start der Übersetzung in der Kachel; Entwurf vorher mit Emi.
11. **Chat-Darstellung:** Umbruchzeilen 1 Zeichen (1 em) eingerückt; enger innerhalb, etwas mehr Abstand zwischen
    Nachrichten; Schriften mit Emi ansehen. Vorher/Nachher-Screenshot an Emi.
12. **Zeiten und Ablauf sehen:** ms pro Zeile (Tooltip); im Debug-Fenster für eine Zeile: Sprache, Ziel, geschützte
    Teile, jeder Versuch mit Ausgabe und Score, Ergebnis. Nur im Speicher, nie in eine Datei.
13. **Original klein und grau unter der Übersetzung** – als Option (aus per Standard), wie in den ersten Versionen.
14. **Eigene Nachrichten in der Lesesprache:** statt die gesendete Übersetzung zurückzuübersetzen, das zeigen, was du
    getippt hast (liegt beim Senden vor, nur im Speicher) – exakt und ohne Rechenzeit.

## Später (nur auf Emis Wort)
Sendesprache automatisch an den Empfänger anpassen (die erkannte Sprache je Spieler
gibt es schon: `g_playerLang`) · Ganzer fremdsprachiger Satz bei gleicher Ein-/Ausgabesprache (Spracherkennung, vorher Fehlrate messen) · Verben wie
farm/level · Spracheingabe (Emi testet zuerst, Whisper bleibt auf Standard) · Wächter · Aufräumen Schichten 2–3 ·
Fuzzing der Chat-Eingabe.

## Bei Emi
API-Schlüssel auf account.arena.net löschen · GitHub: Private vulnerability reporting einschalten.
