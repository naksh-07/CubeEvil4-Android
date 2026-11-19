# CubeEvil4 Studio – Werkzeuge für Mods auf Android

Wunsch von DevJunior (18.11.2026): Werkzeuge auf dem Handy, um Modelle und Animationen mit den Daten des Spiels
anzusehen und umzuwandeln, statt „blind“ Pakete zu bauen. Seine Entscheidungen (18.11.2026):

- **Wo:** in CubeEvil4 selbst (gleiche APK, eigenes Symbol „CubeEvil4 Studio“, nutzt die importierte Disc, Vorschau
  mit der Engine des Spiels).
- **Zuerst:** der Betrachter.
- **Umwandler:** die Python-Werkzeuge (`port/tools/uhd_*`, `re9_finisher_pack.py`, `pmd_weapon_convert.py`) nach C++.
- **Grundlage (18.11.):** der Bewegungsbetrachter des Debug-Menüs (`src/Tools/t_mv.cpp`, Menüpunkt 17, mit
  `src/tools/db_mod.cpp`) und der Sequenz-Editor (`t_motseq.cpp`).
- **Neu (19.11.2026, nach dem Gerätetest):** der Debug-Betrachter lief auf dem Gerät aus dem Speicher (Debug-Heap, die
  Tabelle mit 1824 Modellen) und stürzte ab (`cLightMgr::setEsp`). DevJunior will eine moderne Android-Oberfläche
  („solamente se ven los modelos, pestañas para animaciones, conversiones … cuadrícula con los puntos X/Z“); seine
  Wahl: **eigene App-Seite ohne das Spiel darunter**, zuerst **Betrachter + Animationen**.

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
| S1 | Dateiserver auf dem Gerät (`runtime/studio_host.cpp`, Patch 0930): Ordner `game/studio`; virtuelle Einträge `@/<Datei>/eNNN` und Bewegungen `@/<Datei>/mNNN` (umgewandelt wie beim Spiel, Mod-Pakete eingeschlossen); `Room/Em/mot_tbl.txt` aus den Archiven in `em/` (ein Satz je BIN, Palette: nächste TPL danach, sonst die letzte davor; alle FCV des Archivs nummeriert). Symbol „CubeEvil4 Studio“ legt den Ordner an. | Cloud ✅, Gerät: Debug-Menü zeigte MOT VIEWER nicht (Patch 0116 kürzte es auf zwei Werkzeuge) → S3 |
| S2 | Auf dem Gerät prüfen: Debug-Menü → MOTION VIEWER, Modelle und Bewegungen der Disc, Fehler im Protokoll (`studio:`). Paarung BIN/TPL wo nötig verbessern (Tabelle der Figuren wie `CHARACTER_TPL_USERS`). | offen |
| S3 | Direkt in den Betrachter (DevJunior, 19.11.2026: „debería entrar directo, para nada más seleccionar personaje“): das Studio-Symbol legt `studio/open-viewer` an und startet ein neues Spiel in r100 (Testskript START, dann A bis zum Raum); steht Leon frei im Raum, öffnet `runtime/studio_game.cpp` über Patch 0061 das Debug-Menü mit MOT VIEWER (DbMenuSetExecTool). MOT VIEWER auch im SELECT-Menü (Patch 0116). Touch und freie Kamera folgen. | Cloud ✅ (Build), Gerät offen |
| N1 | Eigene Studio-Seite (`StudioActivity`, Symbol „CubeEvil4 Studio“): links Reiter Models / Animations / Convert / Edit, rechts 3D-Ansicht (GLES 3, `studio_gl.cpp`) mit Boden-Gitter X/Z (250 mm, alle 1000 mm heller), Achsen X rot / Y grün / Z blau, Knochen, Texturen; ein Finger dreht, zwei verschieben, Zwicken zoomt; Zeitleiste mit Abspielen, Schieber, Tempo, „in place“. Modelle aus `game/disc1|disc2/files/em/*.drs` und Mod-Paketen (`mods/<Paket>/em`), BIN-Eintrag als Hauptmodell, weitere als Teile auf dessen Skelett (Kopf, Hände). Animationen: die FCV des Archivs, Leon (pl00), Ganado (em10), die `melee/*.dat` der Pakete; gespielt mit dem originalen MotionMove (`AnimationRig`), auf der CPU gehäutet (`studio_model.cpp`). Convert/Edit: Hinweis, was kommt. | Cloud ✅ (Host-Bild von Leon pl08 mit Kopf und Animation), Gerät offen |
| N2 | Eigene APK (Entscheidung von DevJunior, 19.11.2026: „el studio una apk aparte“; nur für ihn, die Tester-APK ohne Studio): Flavour `studio` (`com.ast.cubeevil4.studio`, nur `libre4studio`), Daten über `StudioDataProvider` der Spiel-App (gleicher Schlüssel). Ganze Modelle wie das Spiel sie zusammensetzt (`GameParts`, je Teil die eigene TPL), Teile hervorheben, Animationen mit Flag-Bits und ohne passende SEQ. | Cloud ✅ (Host-Bilder, Link-Prüfung), CI-Build und Gerät offen |
| S4 | Mod-Pakete ansehen: Sätze für `mods/<Paket>/melee/*.dat` (unsere Bewegungen: Remake-Gang, Reaktionen, Finisher) auf Leons Modell. | offen |
| S5 | Umwandler in C++: zuerst Bewegungen (FCV aus PC-Dateien), dann Modelle (PMD/UDAS → BIN/TPL); Ausgabe als Mod-Paket in `mods/`. | offen |
| S6 | Sequenz-Editor (`t_motseq.cpp`) speichert über den Dateiserver in `game/studio`. | offen |

Nicht bei EU-/US-Discs: dort ist das Debug-Menü ausgeblendet (R6).
