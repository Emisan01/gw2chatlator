# GW2 Chat Translator

Ein Fenster neben Guild Wars 2, das in beide Richtungen übersetzt: Der ganze GW2-Chat erscheint laufend in deiner Sprache, und was du schreibst – auf Deutsch, Arabisch, Chinesisch oder Latein – geht korrigiert und übersetzt in den Kanal deiner Wahl.

Kein Hook, kein Memory Reading, nichts wird ins Spiel injiziert. Das Tool sieht nur, was ohnehin auf dem Bildschirm steht, und sendet nur auf deinen eigenen Tastendruck.

## Was es kann

**Mitlesen**
- Liest den GW2-Chat vom Bildschirm (Windows-Texterkennung, offline) und zeigt jede neue Nachricht in deiner Lesesprache, das Original klein darunter.
- Erkennt Kanal (an der Textfarbe oder am Kanal-Kürzel), Sprecher, Gilden-Tags, Zeitstempel und umgebrochene Zeilen. Farben wie im Spiel: Sagen grün, Karte rosa, Gruppe blau, Trupp hellgrün, Team rot, Flüstern lila, Gilde gold.
- **Tabs wie im GW2-Chat:** Jeder Tab zeigt nur die Kanäle, die du per **Rechtsklick auf den Tab** ankreuzt. Neue Tabs gibt es aus Vorlagen (Alles, Gruppe, Gilde, Karte, WvW, Flüstern), bis zu 8, mit Zähler für Ungelesenes. Zeigt ein Tab genau einen Kanal, schreibst du dort automatisch in diesen Kanal (im Tab „Gilde“ also mit `/g`).
- **Flüstern** hat ab Werk einen eigenen Tab. Rechtsklick auf eine Flüsternachricht → „Antworten an …“.
- Nachrichten in deiner eigenen Sprache werden nicht übersetzt, Wiederholungen kommen aus dem Speicher, deine eigenen Zeilen erscheinen nicht doppelt.

**Schreiben**
- Tippen in jeder Sprache und Schrift, Arabisch und Hebräisch von rechts nach links. Die Rechtschreibprüfung folgt deinem Tastaturlayout (Alt+Umschalt).
- **Verbesserung beim Tippen:** rote Kringel, Vorschläge per Rechtsklick, Autokorrektur sicherer Tippfehler (Strg+Z macht sie rückgängig). GW2-Slang und offizielle Namen gelten nicht als Fehler.
- **Senden als:** Englisch, Französisch, Arabisch, Chinesisch … oder „Original (nur Korrektur)“. Über „Weitere Sprachen“ sind 38 Sprachen wählbar.
- **Vorschau** dessen, was im Spiel ankommt, dazu die **Rückübersetzung** (≈ …) in deine Sprache. So prüfst du auch eine Nachricht in einer Sprache, die du nicht lesen kannst.
- **Kanal:** „Aktiver Kanal“ (wie in GW2 gewählt) oder fest Sagen, Karte, Gruppe, Trupp, Team, Gilde. Im Flüstern-Tab: `/r` an den letzten Flüsterer oder an einen bestimmten Namen.
- Offizielle Spielnamen aus der GW2-API: „Löwenstein“ wird zu „Lion's Arch“ statt wörtlich übersetzt. Chat-Codes (`[&BDAEAAA=]`) und Kürzel (LFG, WvW, DPS …) bleiben unangetastet.
- Länger als 199 Zeichen? Die Nachricht wird an Satzgrenzen geteilt, jedes Enter sendet einen Teil.
- **Schriften, die GW2 nicht anzeigt:** Kommt die Übersetzung z. B. in arabischer Schrift heraus, warnt die Vorschau. **Strg+U** macht daraus Umschrift in lateinischen Buchstaben (Arabizi, Pinyin, Romaji …), so wie viele Spieler ohnehin schreiben.

**Wie ein Teil des Spiels**
- **Über den GW2-Chat legen** (Menü ≡): Das Fenster setzt sich genau auf das Chat-Panel von GW2 und ersetzt es. Gelesen wird weiter der echte Chat darunter, denn Windows nimmt unser Fenster aus der eigenen Aufnahme heraus (ab Windows 10 Version 2004). Der GW2-Chat muss dafür offen bleiben. Stell in GW2 einen Reiter auf alle Kanäle, die Aufteilung übernehmen unsere Tabs.
- **An GW2 andocken** (Menü ≡): Das Fenster behält seinen Platz relativ zur linken unteren Ecke des Spiels und wandert mit, wenn du GW2 verschiebst oder die Größe änderst. Verschieben oder Größe ändern speichert den neuen Platz.
- Erscheint, wenn GW2 oder das Fenster selbst vorne ist, und verschwindet, wenn du zu einer anderen Anwendung wechselst.
- Die Vorschau nimmt nur Platz ein, während du tippst – sonst gehört die Fläche dem Chat.
- Esc gibt die Tastatur ans Spiel zurück, das Fenster bleibt als Chat-Ansicht stehen.

## Übersetzer – Basis reicht, mehr geht

| | Kosten | Qualität | Hinweise |
|---|---|---|---|
| **Basis** (MyMemory) | kostenlos, ohne Anmeldung | ordentlich | 5.000 Zeichen/Tag, mit E-Mail in der ini 50.000. Für Flüstern und Gruppe genug, für dauernd übersetzten Kartenchat knapp. |
| **DeepL** | eigener API-Key | sehr gut | Key in der ini unter `[DeepL]`. Arabisch und Latein werden unterstützt. |
| **LLM lokal** (Ollama, LM Studio) | kostenlos, unbegrenzt, offline | gut bis sehr gut, je nach Modell | Versteht Slang und Umschrift (Arabizi), kann Strg+U für jede Schrift. Braucht eine Grafikkarte mit genug Speicher. |
| **LLM Cloud** | je nach Anbieter | sehr gut | jede OpenAI-kompatible Adresse plus API-Key |

„Automatisch“ (Standard) nimmt DeepL, wenn ein Key eingetragen ist, sonst ein eingetragenes LLM, sonst Basis. Wechseln geht jederzeit über das Menü ≡ → Übersetzer.

**Lokales LLM in zwei Minuten:** [Ollama](https://ollama.com) installieren, `ollama pull qwen2.5:7b` ausführen, in der ini unter `[LLM]` `Model=qwen2.5:7b` eintragen, Tool neu starten. Die Adresse `http://localhost:11434` ist schon voreingestellt; für LM Studio `http://localhost:1234`.

Das LLM bekommt Chatnachrichten ausdrücklich als Daten: Anweisungen, die jemand in den Chat schreibt, werden übersetzt, nicht ausgeführt. Und was das LLM zurückgibt, wird nur angezeigt – es kann nichts senden.

## Erste Schritte

1. `GW2ChatTranslator.exe` in einen eigenen Ordner legen und starten. Daneben entsteht `gw2-chat-translator.ini` mit allen Einstellungen und Erklärungen.
2. GW2 im **Fenstermodus oder Vollbild-Fenster** spielen. Über exklusivem Vollbild kann kein Fenster liegen.
3. Einmalig auf den Hinweis im Chat-Tab klicken (oder Menü ≡ → „Chat-Bereich festlegen …“) und einen Rahmen um die **Textzeilen** des GW2-Chats ziehen – ohne Eingabezeile und Reiter. Der Bereich wird relativ zur linken unteren Ecke des Spiels gespeichert.
4. Oben rechts „Lesen: …“ auf deine Sprache stellen, unten „→ …“ auf die Sprache, in der du senden willst.
5. Optional: DeepL-Key oder lokales LLM eintragen (siehe oben).

In GW2 muss **Enter** die Chat-Taste sein (Standard). Liegt die exe in einem schreibgeschützten Ordner, landen ini, Wortliste und Cache in `%LOCALAPPDATA%\GW2ChatTranslator`.

## Bedienung

| Taste | Wirkung |
|---|---|
| Enter | senden bzw. im Modus „Nur kopieren“ kopieren (bei geteilten Nachrichten: den nächsten Teil) |
| Strg+Enter | Original senden, ohne Übersetzung |
| Strg+L | „Senden als“-Sprache weiterschalten |
| Strg+U | Umschrift in lateinischen Buchstaben |
| Strg+Tab | zum nächsten Tab |
| Esc | zurück ins Spiel |
| Strg+Alt+T (einstellbar) | Fenster holen / ausblenden, systemweit |
| Rechtsklick auf Kringel | Vorschläge, eigenes Wort, ignorieren |
| Rechtsklick auf eine Chatzeile | kopieren, antworten, Kanalfarbe zuordnen, Verlauf leeren |
| Rechtsklick auf einen Tab | Kanäle des Tabs, neuer Tab, verschieben, schließen |

Klickbar sind außerdem: die Tabs, „Lesen: …“, das Menü ≡, unten der Kanal und „→ Sprache“. Ziehen kannst du das Fenster an Kopf- oder Fußzeile, die Größe änderst du an den Rändern.

## Wenn der Chat nicht richtig gelesen wird

- **Kanal falsch erkannt:** Rechtsklick auf die Zeile → „Diese Zeilenfarbe gehört zu“ → richtigen Kanal wählen. Die Farbe wird gespeichert. Zurück auf Standard: Menü ≡ → „Kanalfarben zurücksetzen“.
- **Zeilen fehlen oder sind verstümmelt:** Rahmen enger um die Textzeilen ziehen. Bei kleiner Interface-Größe in GW2 in der ini `OcrScale=3` probieren. Ein ruhiger, dunkler Hintergrund hinter dem Chat hilft.
- **Diagnose:** Menü ≡ → „Diagnose-Aufnahmen speichern“. Im Ordner `captures` landen dann das aufgenommene Bild, das aufbereitete Bild und der erkannte Text. Diese Dateien bleiben auf deinem PC.
- Die Texterkennung nutzt die Sprachpakete von Windows. Für kyrillische oder chinesische Chatzeilen muss das passende Paket mit Texterkennung installiert sein (`OcrLanguage=` in der ini wählt es aus).

## ArenaNet-Regeln und Kontosicherheit

ArenaNet verbietet Programme, die einen unfairen Vorteil verschaffen, Aktionen im Spiel automatisieren, Spielen ohne Anwesenheit ermöglichen oder anderen schaden. Erlaubt sind beaufsichtigte Makros, die pro Tastendruck genau eine Aktion auslösen. ArenaNet prüft oder genehmigt kein Drittprogramm, die Nutzung ist immer auf eigenes Risiko ([Third-Party-Policy](https://help.guildwars2.com/hc/en-us/articles/360013625034-Policy-Third-Party-Programs), [Makro-Policy](https://help.guildwars2.com/hc/en-us/articles/360013762153)). Das Tool ist deshalb so gebaut:

- **Eine eigenständige exe, keine DLL.** Nichts wird in den GW2-Prozess geladen: kein Hook, kein Speicherlesen, keine veränderten Spieldateien. Auf den Spielprozess wird nur mit der Frage zugegriffen, ob er noch läuft – wie es auch der Task-Manager tut.
- **Gelesen wird nur, was du ohnehin siehst:** der Bildschirmbereich, den du selbst festlegst. Charaktername und „Chatzeile offen“ liefert MumbleLink, die offizielle Schnittstelle von GW2.
- **Es sendet nie von selbst.** Jede Zeile geht nur auf dein Enter raus, eine Nachricht pro Tastendruck. Ein Tool, das Chat liest und automatisch antwortet, wäre ein Bot – das gibt es hier nicht, auch nicht mit LLM.
- **Modus „Nur kopieren“** (Menü ≡ oder `SendMode=copy` in der ini): Enter legt die fertige Zeile nur in die Zwischenablage und holt GW2 nach vorn. Einfügen und Abschicken machst du selbst (Enter, Strg+V, Enter). Dann erreicht keine einzige künstliche Taste das Spiel.
- Kein Tastatur-Hook, nichts, was nach Keylogger aussieht. Eine unsignierte exe kann trotzdem die SmartScreen-Warnung „unbekannter Herausgeber“ auslösen.

## Was deinen PC verlässt

Nur Text, und nur an den Übersetzer, den du gewählt hast: Basis an MyMemory, DeepL an DeepL, ein Cloud-LLM an dessen Anbieter. Mit einem lokalen LLM verlässt nichts den PC. Bildschirmaufnahmen werden nie verschickt. Die offiziellen Spielnamen kommen von der GW2-API (`api.guildwars2.com`).

## Grenzen

- Gelesen wird, was im **aktiven GW2-Chat-Reiter** steht. Was GW2 dort nicht zeigt, sieht auch das Tool nicht. Den GW2-Chat zu minimieren geht deshalb nicht – überdecken schon (siehe „Über den GW2-Chat legen“).
- Ohne Hook gibt es keinen anderen Weg an *alle* Kanäle. arcdps mit „unofficial extras“ liefert exakten Text nur für Gruppe/Trupp und NPCs; eine optionale Nexus-Brücke dafür ist angedacht.
- Unter Windows-Versionen vor 10 Version 2004 kann das Fenster nicht aus der Aufnahme ausgeblendet werden. Dann pausiert das Lesen, solange es den Chat verdeckt, und die Statuszeile sagt es dir.
- Arabische Schrift zeigt der GW2-Chat sehr wahrscheinlich nicht an (kurz testen: `مرحبا` in den Chat einfügen). Deshalb die Warnung und Strg+U.
- Flüster-Namen aus der Texterkennung können falsch gelesen sein – die Statuszeile erinnert daran. `/r` (Antwort an den letzten Flüsterer) ist immer sicher.
- Die Rechtschreibprüfung braucht das Windows-Sprachpaket der jeweiligen Sprache. Ohne Paket bleibt die Prüfung für diese Sprache aus, statt jedes Wort anzustreichen.
- Strg+Alt+T entspricht auf deutschen Tastaturen AltGr+T. Bei Bedarf in der ini ändern.

## Dateien

| Datei | Inhalt |
|---|---|
| `gw2-chat-translator.ini` | Einstellungen (kann deinen DeepL- oder LLM-Key enthalten – nicht weitergeben) |
| `gw2-woerter.txt` | deine eigenen GW2-Wörter, ein Wort pro Zeile |
| `cache/gw2names_<sprache>.tsv` | offizielle Namen aus der GW2-API, alle 14 Tage erneuert |
| `captures/` | nur mit eingeschalteter Diagnose |

## Warum die Zwischenablage so vorsichtig behandelt wird

Wenn das Tool die alte Zwischenablage zu früh zurückschreibt und GW2 gerade einen langsamen Frame hat, fügt GW2 den *alten* Inhalt ein – also das, was du zuletzt kopiert hast. Im Test mit festen Wartezeiten ist genau das passiert. Deshalb wartet das Tool nach jedem Schritt, bis GW2 seine Eingaben abgearbeitet hat, und prüft über MumbleLink, ob die Chatzeile wirklich zu ist. Bestätigt GW2 das nicht, bleibt die Übersetzung in der Zwischenablage stehen und die Statuszeile sagt es dir.

## Bauen

**Visual Studio 2022:** *Datei → Öffnen → Ordner* auf das Projekt, CMake wird automatisch erkannt. Ziel `GW2ChatTranslator.exe` wählen und bauen.

**MinGW (auch von Linux aus):**

```
cmake -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-win
```

**Tests:**
- `core_tests` laufen auf jedem System (die Kernlogik hat kein Win32).
- `fake_gw2.exe` ersetzt das Spiel: Fensterklasse wie GW2, Chat-Panel aus `fake_chat.txt` (`RRGGBB|Text` pro Zeile), MumbleLink-Block, Chatzeile, die nach `received.txt` schreibt.
- `GW2ChatTranslator_fakes.exe` ist das Tool mit fester Test-Rechtschreibung und einer Texterkennung, die `fake_chat.txt` liest.
- `tests/tools/mock_llm.py` spielt ein OpenAI-kompatibles LLM (`Url=http://127.0.0.1:11500`).

## Herkunft

Die Idee eines schwebenden Eingabefensters, das in den GW2-Chat sendet, stammt aus [Grammarly-support-overlay-for-guild-wars-2](https://github.com/hazratali-uydevelopers/Grammarly-support-overlay-for-guild-wars-2) (ISC). Von dort wurde kein Code übernommen: Dieses Projekt ist eine native Neuentwicklung in C++/Win32.
