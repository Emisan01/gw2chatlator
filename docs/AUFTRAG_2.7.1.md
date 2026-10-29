# ToDo 2.7.1

Regeln: [AGENTS.md](../AGENTS.md) (Evidence bei feat/fix/perf/refactor, messen statt raten), nur erfundene Testzeilen,
keine AI-Attribution, kein Push/Release (nur auf Emis Wort). Claude prüft zuletzt.

## Fehler
0. ~~Flüstern senden kaputt~~ – erledigt in 2.7.1 (f603419: Name getippt, Pausen zurück); im Spiel prüfen (CHAT-13).
1. **Block bleibt unübersetzt:** nach einer Zeile mit Tippfehler, einer Korrekturzeile, einer Zeile nur `\o/` und einer
   kurzen englischen Zeile übersetzt kein Übersetzer mehr die folgenden Zeilen, auch nicht per „einmal übersetzen“.
   Nachstellen (erfundene Zeilen), Ursache finden, Test dazu (`TranslateRecentLines` → `TranslateLine`, kb_worker.cpp).
2. ~~erledigt 2.7.1 (Routenzeile zeigt „roh“)~~ **Rohschalter:** bleibt bis zum Neustart an, auch beim Übersetzerwechsel, und man sieht, ob er an ist.
3. **Initialisieren sauber und sichtbar** (Neubau nur bei Übersetzerwechsel und MyMemory-Ausweichschalter: erledigt 2.7.1; Vorladen mit Meldung: erledigt 2.7.1): nach Wechsel von Übersetzer, Qualitätsstufe oder Eingabesprache das Modell
   vorladen mit Meldung „Modell lädt …“ → „bereit“; Anfragen während des Ladens warten statt abzulaufen. Live-Test: nach
   Wechsel der Eingabesprache „Übersetzungsanfrage abgelaufen (Timeout)“ mit Angebot „> MyMemory“, obwohl NLLB gewählt
   war; nach Hin- und Zurückwechseln lief NLLB „Beste“ gut. Klären, warum ein Wechsel der Eingabesprache den Übersetzer
   neu baut, und dass Dashboard und Übersetzer-Tab denselben Übersetzer zeigen (Dashboard NLLB, Tab MyMemory gesehen).
   Spur: `MakeTranslator` (kb_state.cpp) liefert MyMemory, solange `g_myMemorySessionFallback` gesetzt ist – egal was
   eingestellt ist; das Flag nach dem Wechsel zurück auf einen anderen Übersetzer löschen und anzeigen, wenn es greift.
3b. ~~erledigt 2.7.1~~ **Eigene Nachricht: übersetzt wird, was du wirklich geschrieben hast.** Eine Regel für beide Live-Befunde:
   Quelle = die erkannte Sprache, **wenn die Erkennung sicher ist** (`SureLanguage`), sonst die Eingabesprache. Übersetzt
   wird, sobald diese Quelle nicht die Sendesprache ist – auch wenn „Schreiben“ und „Senden“ gleich stehen (Live-Test:
   „Schreiben: Français, Senden: Français“, englisch getippt → ging englisch raus; kurze deutsche Zeile → unübersetzt).
   „Schreiben“ bleibt die Sprache der Tipphilfe. Die Vorschau zeigt es vor dem Senden. Prüfen, dass Enter vor fertiger
   Übersetzung wartet. Heute: send_route.cpp entscheidet nur nach den Einstellungen, kb_worker.cpp:242 nimmt die
   Erkennung auch unsicher.
3c. ~~erledigt 2.7.1 (gleich/übersprungen → Original)~~ **Alte Übersetzung bleibt stehen:** wechselt das Ziel und ist eine Zeile schon in der neuen Zielsprache (wird
   übersprungen), zeigt sie weiter die alte Übersetzung – sie soll dann das Original zeigen.
   Dazu (Live-Test FR→FR): kurze französische Zeilen mit Tippfehlern wurden nicht als Französisch erkannt und
   „ins Französische“ übersetzt („Je suis un mai (?)“, „où est les nouveaux joueurs“). Regel: hat die Übersetzung fast
   nichts geändert oder ist sie unsicher, während das Ziel eine Sprache ist, die du liest – Original zeigen.
3d. **Minimal übersetzen:** beim Einschalten/Initialisieren nur, was noch keine passende Übersetzung hat (sichtbare
   Zeilen, neueste zuerst) – fertige Zeilen bleiben (Prüfung `translated`/`transTarget`/`transBy` in `TranslateRecentLines`
   und `g_transCache` gibt es schon). Alles neu nur, wenn Lesesprache, Übersetzer oder Qualitätsstufe sich ändern – und
   dann kurz sagen, warum.

## Übersetzung
4. ~~erledigt 2.7.1~~ **Kein-Mist-Filter (so einfach wie möglich):** ist ein Ergebnis schlecht (Score unter `kUnsureScore` oder die
   Prüfung schlägt an), die Zeile einmal mit höherer Qualität neu übersetzen; ist sie dann noch schlecht, das Original
   zeigen statt des schlechten Ergebnisses. Als Option mit Schalter. Mit translation_bench belegen.
4b. **Eigene Nachricht wartet nie hinter dem Chat:** heute ein Helfer, eine Übersetzung nach der anderen – läuft gerade
   eine lange Chatzeile, wartet die eigene. Vorschlag statt zweier Instanzen: ein Helfer mit zwei Replikaten
   (`num_replicas_per_device = 2` in translate_helper_main.cpp; CTranslate2 teilt die Modellgewichte laut Doku – RAM/CPU
   messen) und Anfrage-IDs im Protokoll; einfachere Alternative: automatische Chatzeilen pausieren, solange du tippst.
5. ~~erledigt 2.7.1 (min. Beam 2)~~ **Drossel:** automatische Zeilen unter Last nie unter Beam 2. Vorher je Beam (1/2/4) messen: Bedeutung und ms.
6. ~~erledigt 2.7.1~~ **Grüße** (Hola, Selam, Merhaba, Salut, Cześć, Привет, Olá, Ciao …) vor dem Modell nachschlagen.
7. **Listen füllen** mit Emi: `data/gw2-words.tsv` nur mit gemessenen Fehlern, nichts erfinden. Kandidaten aus Live-Tests: „boss“ (→ „Chef“), „frak“ (Fractals;
   NLLB kennt „frak“ als Science-Fiction-Fluch → „Scheiße“), „riss“ (Rift; „lfg ein t3 riss farm gruppe“ verlor „riss farm“).
   Danach
   ein zweiter, unbekannter Prüfsatz `tests/data/translation_corpus_holdout.tsv`, nie zum Nachbessern.
7b. ~~erledigt 2.7.1~~ **Offizielle Namen auch ohne Apostroph:** „lions arch“ blieb ins Französische „lions arch“ statt „L'Arche du
   Lion“ – der Glossar-Vergleich achtet auf den Apostroph, die Begriffsliste (`TermKey`) nicht. Gleich machen.
7c. **Jeder Live-Befund wird eine erfundene Prüfzeile** im Korpus (gleiches Muster, eigener Text) – vor dem Fix
   messen, danach messen; so wächst der Prüfsatz mit dem echten Chat.
7d. **Kandidaten-Sammler (Vorschlag, Emi entscheidet):** lokal zählen, welche kurzen Wörter in fremden Zeilen in keiner
   unserer Listen stehen und in unsicheren Ergebnissen vorkommen („frak ×3, riss ×2“) – nur Wort und Anzahl, keine Zeilen,
   keine Spieler (Datenschutz: nur Zählungen). Anzeige im Technik-Tab, ein Klick übernimmt ein Wort in die eigenen
   Begriffe; gute Funde wandern von dort in die eingebauten Listen.

## Oberfläche
8. ~~erledigt 2.7.1 (Menü: Klick auf den offenen Chat-Tab / Rechtsklick)~~ **Lesesprache:** eigene Einstellung, Start = Systemsprache, dann frei wählbar, unabhängig von Eingabe und Footer;
   „nicht übersetzen“ filtert dagegen. Ort: „v“-Knopf im Chat-Tab, klappt nach oben auf (wie beim Mikrofon), dazu die
   Tab-Aktionen (Rechtsklick auf den Tab bleibt).
9. ~~erledigt 2.7.1 (ohne sichtbare Markierung der Nachricht)~~ **Einzelne Nachricht übersetzen:** Rechtsklick auf eine Chatnachricht markiert sie ganz (über Umbrüche) und bietet
   „Übersetzen (Lesesprache)“ direkt unter dem Mauszeiger und „Übersetzen in ›“ (alle Sprachen). Ein Klick übersetzt
   immer. Keine Klicklogik auf Chat-Codes. Wörter lernen bleibt im Eingabefeld.
10. **Direktstart in der Kachel:** alles für den Start der Übersetzung in der Kachel; Entwurf vorher mit Emi.
11. **Chat-Darstellung** (Einzug versucht, Selbsttest „Breite nicht sofort stabil“ – zurückgenommen, offen): Umbruchzeilen 1 Zeichen (1 em) eingerückt; enger innerhalb, etwas mehr Abstand zwischen
    Nachrichten; Schriften mit Emi ansehen. Vorher/Nachher-Screenshot an Emi.
12. ~~erledigt 2.7.1 (Zeilen-Menü „Was ist passiert?“)~~ **Zeiten und Ablauf sehen:** ms pro Zeile (Tooltip); im Debug-Fenster für eine Zeile: Sprache, Ziel, geschützte
    Teile, jeder Versuch mit Ausgabe und Score, Ergebnis. Nur im Speicher, nie in eine Datei.
13. ~~erledigt 2.7.1~~ **Original klein und grau unter der Übersetzung** – als Option (aus per Standard), wie in den ersten Versionen.
14. ~~erledigt 2.7.1~~ **Eigene Nachrichten in der Lesesprache:** statt die gesendete Übersetzung zurückzuübersetzen, das zeigen, was du
    getippt hast (liegt beim Senden vor, nur im Speicher) – exakt und ohne Rechenzeit.
15. **Filter zeigt die eigene Sprache:** „Übersetzt wird alles außer:“ zeigt die Lesesprache als eigenen Eintrag
    (`skipTypingLang`, z. B. „English (Lesesprache) ×“) neben dem +; löschbar und über + wiederherstellbar (Emi, Live-Test).
16. **Kurze Zeilen:** ein Wort („herbeiwinken“) wird nie automatisch übersetzt – die Erkennung ist nicht sicher
    (`ShouldTranslate`: `detected` leer → nein). Prüfen, ob eine Zeile mit Wörtern aus einer Wörterliste / einem
    Windows-Wörterbuch als sicher gelten darf; heute hilft nur der Klick auf die Zeile.

## Später (nur auf Emis Wort)
Sendesprache automatisch an den Empfänger anpassen (die erkannte Sprache je Spieler
gibt es schon: `g_playerLang`) · Verben wie
farm/level · Spracheingabe (Emi testet zuerst, Whisper bleibt auf Standard) · Wächter · Aufräumen Schichten 2–3 ·
Fuzzing der Chat-Eingabe.

## Bei Emi
API-Schlüssel auf account.arena.net löschen · GitHub: Private vulnerability reporting einschalten.
