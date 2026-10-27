# AUFTRAG 2.6.3 — Prüfbares Gesamtpaket + Helfer-Update

## ENTSCHEIDUNGEN VON EMI (verbindlich, ergänzen den Auftrag)
1. Zielversion ist 2.6.3. Das Pre-Release v2.6.2 (Tag, Asset GW2Chatlator.dll, SHA-256 f36bbb…6cb1) bleibt
   unberührt: kein Tag verschieben, kein Asset ersetzen, nichts löschen.
2. Versionsanhebung wie Gate 1 sie verlangt (CMakeLists.txt, CHANGELOG.md mit Abschnitt 2.6.3, README-Links).
3. Update des Helfers nur auf Klick (Banner mit einer Schaltfläche), keine automatische Installation.
4. Versionszeile im Dashboard mit Commit-ID und "dirty"-Hinweis ist drin (Punkt M).
5. Keine UI-Tests, keine GitHub Action (K' / Punkte K und L entfallen).
6. Fluss: sauberer Commit -> Quittung einmal (alle Artefakte) -> Emis Clean-Room-Test im Spiel, erster Blick auf
   die Versionszeile, die zur Quittung passen muss -> Emis "ok" -> main pushen, Pre-Release v2.6.3, Helfer in
   extras ersetzen, verify_assets.py. KEINE zweite Runde der Gates nach dem Push.
7. Zu Latest befördert ausschließlich Emi, erst nach seinem Test. Ziel ist: neue Hauptversion mit dem ganzen
   funktionalen Paket (DLL + Helfer + Voice + Modelle) gleichzeitig.
8. Fokus/Tastenpuffer: nichts Neues bauen. Tastenpuffer, Esc und Senden bleiben unangetastet. Falls Emis Test
   zeigt, dass Tasten nach dem Senden zu lange hängen, ist der einzige erlaubte Hebel die feste 3-Sekunden-
   Grenze (kürzen). Das entscheidet Emi nach dem Test.
9. Keine Attributionszeilen, Autor ist Emi.

---

## AUFTRAGSTEXT

### ZIEL (Emi)
Alles, was ein Nutzer über Nexus bekommt, passt zusammen und ist prüfbar: DLL, Übersetzungs-Helfer,
Voice-Modul, Modelle. Eine veraltete Helfer-EXE wird erkannt, entfernt und durch die passende ersetzt.
Alte Versionen und alte Ordner-Layouts müssen nicht migriert werden.

### BEFUND (belegt, Stand 26.10.)
1. extras / GW2Keyboard-LocalMT.zip (hochgeladen 24.10., 8.574.670 B): gw2kb_translate.exe, 31.676.928 B,
   SHA-256 106f5dbb1c6ae283f8f08662c3fdc805214726d4603378fa19fc71358d2047ea, Linker-Zeit 22.10. 14:26 UTC.
   Im Binary fehlen "nllb-200", "sentencepiece.bpe.model", "deu_Latn" und "nllb-200 tokenizer failed to
   load". Vorhanden ist nur "tokenizer files are missing or invalid". Das Binary enthält also keinen NLLB-Code.
2. build\dist\GW2Keyboard-LocalMT.zip (26.10. 16:19, 8.581.159 B): EXE 31.694.336 B, SHA-256 beginnt mit
   fa3787c6e1a04b31, mit NLLB-Code. Gebaut am 25.10. morgens, vor dem Commit der zugehörigen Quellzeilen.
3. Tag v2.6.0, translate_helper_main.cpp: `ExistsFile` und `canUseNllb` werden benutzt (Z. 164, 170) und
   nicht definiert. Erst 6956ac9 (v2.6.1) ergänzt sie. Der Tag-Quelltext entspricht nicht der Binary.
4. Der Helfer meldet nur {"evt":"ready"} (translate_helper_main.cpp:280). Die DLL wartet darauf
   (local_mt_translator.cpp:209). Es gibt keine Version und keine Fähigkeiten. `LocalMtModulePresent` prüft
   nur, ob die EXE existiert (local_mt_packages.cpp:112).
5. Die Modell-Installation prüft nach dem Entpacken nur die Existenz der Dateien (local_mt_packages.cpp,
   validFiles). Der Download selbst ist per GitHub-SHA-256 abgesichert (extras_install.cpp:44-65).
6. Voice: extras/GW2Keyboard-Nexus.zip hat 6.459.721 B (24.10.), lokal in dist 6.519.494 B (26.10.).
   Herkunft und Inhalt sind ungeklärt, bitte inventarisieren.
7. `extras` ist ein veränderbarer Container mit festen Dateinamen. ALLE installierten Versionen (auch
   2.6.0 bis 2.6.2) lesen daraus. Jede Änderung dort trifft sofort alle.

### PHASE 0 - Sofort: Helfer in extras korrigieren
0.1 Helfer aus sauberem Baum bauen (HEAD 6c6ceae oder neuer, `git status` leer). Melde EXE-Größe,
    SHA-256, Linker-Zeit und die Commit-ID. Belege per Binary-Strings, dass "nllb-200 tokenizer failed
    to load" enthalten ist.
0.2 Beweise mit Test, dass die neue EXE mit den DLLs 2.6.0, 2.6.1 und 2.6.2 funktioniert (alle lesen extras).
0.3 Clean-Room-Test durch Emi (siehe E). Erst nach Emis "ok": GW2Keyboard-LocalMT.zip in extras
    ersetzen. Gleicher Dateiname! Danach Größe und SHA-256 des hochgeladenen Assets gegen die lokale Datei
    prüfen.

### PHASE 1 - Paketvertrag und Prüfung
A. tools/pack_release.py baut ALLE Artefakte (DLL, Helfer-Zip, Voice-Zip, Modell-Zips) aus einem Commit
   und bricht bei dirty tree ab. Es schreibt dist/manifest.json: schema, version, commit, built (UTC) und je
   Artefakt name, role, bytes, sha256, Ziel (Tag vX.Y.Z oder extras). Bei Zips zusätzlich die Dateien darin
   (path, bytes, sha256), bei Helfern proto und caps. Commit-ID und dirty-Flag werden beim Build in DLL und
   Helfer eingebaut (generierter Header) und im Manifest geprüft.
B. tools/verify_assets.py prüft nach dem Upload JEDES Asset (DLL im Tag, alles in extras) gegen das Manifest.
   Für große Modelle den `digest` der Release-API nutzen statt 582 MB zu laden. Ausgabe als Tabelle, Exit-Code
   0 oder 1. Manifest als Asset ablegen: manifest.json im Versions-Tag, extras-manifest.json in extras.

### ZUSATZ PHASE 1 - Quittung statt Wiederholung
G. verify_release.py --full: verweigert bei dirty tree, baut alle Artefakte aus HEAD, führt alle Gates aus und
   schreibt dist/verification.json (commit, tree_clean, Artefakt-Hashes, je Gate id/Ergebnis/Exit-Code/Dauer,
   Testzahlen, Zeit, Werkzeugversionen).
H. verify_release.py --check-receipt: prüft HEAD == Quittungs-Commit, Arbeitsbaum sauber, Hashes auf Platte ==
   Quittung. Seit der Quittung nur Doku-Pfade geändert (README.md, CHANGELOG.md, docs/**, AGENTS.md): gilt als
   gültig, dann läuft nur die schnelle Stufe. Sonst "veraltet" mit Exit 1. Dauer unter 5 Sekunden, keine Builds.
I. Claims-Check (tools/claims.json): 5-10 zentrale Aussagen, die in mehreren Dateien stehen (Senden/Hands free,
   Identität, Datenordner, Stringzahl) werden mechanisch abgeglichen. Gate 1 prüft den Link-Vertrag statt eines
   Textmusters.
J. TESTPROTOCOL: jede Testzeile trägt den Kurz-Hash der Quittung, für die sie gilt.
K'. Keine UI-Tests und keine GitHub Action. Nicht bauen.
M. Versionszeile im Dashboard (kb_options.cpp, beide Stellen mit GCT_VERSION_STR) zeigt zusätzlich die
   eingebaute Commit-ID (kurz) und "dirty", wenn der Baum beim Build nicht sauber war. Die Helfer-Build-ID
   (aus "ready") erscheint im Diagnose-Export. Keine neuen Einstellungen, keine neue UI.
   Emis Test beginnt immer mit einem Blick auf diese Zeile; sie muss zum Kurz-Hash der Quittung passen.

### PHASE 2 - Handshake und Update (Code, Version 2.6.3)
C. Handshake: Helfer senden {"evt":"ready","helper":"translate","proto":N,"caps":["opus","nllb"],
   "build":"<commit12>"}. Der Voice-Helfer bekommt das gleiche Schema. `LocalMtMessage` bekommt proto, caps
   und build. Die DLL hat `kMinHelperProto` und verlangt Fähigkeiten je nach gewählter Engine (NLLB braucht
   "nllb"). Ein "ready" ohne proto zählt als proto 0 und damit als veraltet.
D. Update-Logik, Zustände: HelperMissing, HelperOk, HelperOutdated(Grund), Updating, UpdateFailed(Grund).
   Ablauf: (1) Helfer-Prozess beenden und das Ende abwarten, (2) Zip aus extras laden (Prefix und
   SHA-256 wie bisher), (3) entpacken und die innere EXE gegen extras-manifest.json prüfen, (4) alte EXE
   umbenennen (.old), neue per MoveFileEx atomar einsetzen, (5) neuen Helfer starten und auf "ready" mit
   passendem proto warten, (6) bei Erfolg .old löschen, bei Fehler zurückrollen und im Klartext melden.
   Es darf nie ein Halbzustand bleiben. Veraltete Dateien aus bekannten alten Layouts nur per
   ausdrücklicher Namensliste entfernen, nie unbekannte Dateien löschen. Gleiche Logik für den Voice-Helfer.
   Modelle: Beim Install eine Markierung (.package.json mit Zip-SHA-256 und Dateien samt Größe) ablegen.
   Zur Laufzeit nur die Größen prüfen (kein Hashen von 582 MB). Stimmt etwas nicht, meldet die UI
   "Modell beschädigt - neu installieren".
   Banner mit einem Klick (Update auf Klick).

### PHASE 3 - Tests und Doku
E. Level B (automatisch):
   1. local_mt_translator_selftest mit Fake-Helfern: altes "ready" -> HelperOutdated; neu ohne "nllb" bei
      gewähltem NLLB -> Outdated(caps); neu vollständig -> OK.
   2. Portable core_tests für Manifest-Parsing und proto/caps-Vergleich (kaputtes JSON, fehlende Felder).
   3. Austausch-Logik in temporären Ordnern: Erfolg, Download-Fehler (alte EXE bleibt), Handshake-Fehler
      (Rollback), Helfer läuft noch (wird zuerst beendet).
   4. Rückwärtskompatibilität: der Parser aus Tag v2.6.2 (ParseLocalMtMessage) muss die neue "ready"-Zeile
      akzeptieren. Belegen, nicht annehmen.
   Level C (Emi, im Spiel, frischer Datenordner): Helfer installieren, NLLB installieren, de->en
   übersetzen; alter Helfer vorhanden -> Update; Download unterbrochen -> kein Halbzustand; Pfad mit
   Leerzeichen und Umlaut (sentencepiece-Load prüfen). Zeilen ins TESTPROTOCOL (ok / defect / skipped,
   Nicht-Ausgeführtes bleibt skipped).
F. Doku: AGENTS.md Abschnitt "Package Contract" (Artefaktliste, Bau aus einem Commit, Manifest, Prüfung nach
   dem Upload), README kurz "Was wann heruntergeladen wird", CHANGELOG 2.6.3, neue UI-Strings über Tr(L"...") in
   8 Sprachen (tools/i18n_check.py), Version laut Gate 1.

### RELEASE-FLUSS (Emis Regeln)
Gates einmal -> Emis lokaler Clean-Room-Test -> Emis "ok" -> Push -> Pre-Release 2.6.3 -> verify_assets.py
(gehört zum Push-Vorgang, ist keine Wiederholung der Gates) -> Latest nur durch Emi.

### NICHT ANFASSEN
Tastenpuffer, Esc, Senden, Identitätsvertrag, Hands-free.

### ARBEITSREGELN
- Beleg statt Zustimmung: Zitat mit Datei:Zeile, Hash oder Exit-Code. Nicht Ausgeführtes bleibt skipped.
- Jede neue Datei und jeder neue Mechanismus wird Emi im Bericht in zwei Sätzen erklärt.
- Keine Attributionszeilen, Autor ist Emi. Kein Push und kein Release ohne Emis "ok".
