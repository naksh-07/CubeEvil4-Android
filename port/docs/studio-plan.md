# CubeEvil4 Studio – Werkzeuge für Mods auf Android

Wunsch von DevJunior (18.11.2026): Werkzeuge auf dem Handy, um Modelle und Animationen mit den Daten des Spiels
anzusehen und umzuwandeln, statt „blind“ Pakete zu bauen. Seine Entscheidungen (18.11.2026):

- **Wo:** in CubeEvil4 selbst (gleiche APK, eigenes Symbol „CubeEvil4 Studio“, nutzt die importierte Disc, Vorschau
  mit der Engine des Spiels).
- **Zuerst:** der Betrachter.
- **Umwandler:** die Python-Werkzeuge (`port/tools/uhd_*`, `re9_finisher_pack.py`, `pmd_weapon_convert.py`) nach C++.
- **Grundlage:** der Bewegungsbetrachter des Debug-Menüs (`src/Tools/t_mv.cpp`, Menüpunkt 17, mit
  `src/tools/db_mod.cpp`) und der Sequenz-Editor (`t_motseq.cpp`).

## Befund (19.11.2026)

Der Bewegungsbetrachter liest alles vom Entwicklungs-PC (SN-Dateiserver, `game/file.cpp`: `PCopen` … unter
`d:\bio4`): zuerst `Room/Em/mot_tbl.txt`, dann je Modell eine BIN- und eine TPL-Datei und je Bewegung eine FCV-Datei
(`DB_MODEL_FILES::read` → `HDReadDebugAlloc`). Im Port waren diese Aufrufe Stubs: der Betrachter brach sofort ab.
Die Tabelle ist Text mit CR LF, fünf Blöcke `_SET`, `_BIN`, `_TPL`, `_FCV`, `_ONTO` aus `name {` … `}`
(`mottblInit`); eine Zeile eines Satzes nennt seine BIN-, TPL- und Bewegungsblöcke, eine `#`-Folge im Dateinamen der
Bewegung nimmt die Bewegungsnummer auf. Die Dateien sind genau die Einträge der Archive der Disc (`em/*.drs`: BIN,
TPL, FCV, SEQ …) nach der Umwandlung der Bytereihenfolge.

## Schritte

| Schritt | Inhalt | Stand |
|---|---|---|
| S1 | Dateiserver auf dem Gerät (`runtime/studio_host.cpp`, Patch 0930): Ordner `game/studio`; virtuelle Einträge `@/<Datei>/eNNN` und Bewegungen `@/<Datei>/mNNN` (umgewandelt wie beim Spiel, Mod-Pakete eingeschlossen); `Room/Em/mot_tbl.txt` aus den Archiven in `em/` (ein Satz je BIN, Palette: nächste TPL danach, sonst die letzte davor; alle FCV des Archivs nummeriert). Symbol „CubeEvil4 Studio“ legt den Ordner an. | Cloud ✅, Gerät offen |
| S2 | Auf dem Gerät prüfen: Debug-Menü → MOTION VIEWER, Modelle und Bewegungen der Disc, Fehler im Protokoll (`studio:`). Paarung BIN/TPL wo nötig verbessern (Tabelle der Figuren wie `CHARACTER_TPL_USERS`). | offen |
| S3 | Studio-Start ohne Umweg: das Symbol startet direkt im Betrachter (Debug-Menü-Werkzeug 17 von der App aus), freie Kamera und Bildschritt mit Touch und Controller. | offen |
| S4 | Mod-Pakete ansehen: Sätze für `mods/<Paket>/melee/*.dat` (unsere Bewegungen: Remake-Gang, Reaktionen, Finisher) auf Leons Modell. | offen |
| S5 | Umwandler in C++: zuerst Bewegungen (FCV aus PC-Dateien), dann Modelle (PMD/UDAS → BIN/TPL); Ausgabe als Mod-Paket in `mods/`. | offen |
| S6 | Sequenz-Editor (`t_motseq.cpp`) speichert über den Dateiserver in `game/studio`. | offen |

Nicht bei EU-/US-Discs: dort ist das Debug-Menü ausgeblendet (R6).
