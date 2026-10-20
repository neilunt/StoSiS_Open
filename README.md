# StoSiS – Stochastic Simulation of Solids

C++-Fließschemasimulation für die Feststoffverfahrenstechnik. Ziel ist eine
stochastische Lösung mit gewichteten Monte-Carlo-Partikeln, beschleunigt auf
GPUs (CUDA, Großrechner amplitUDE mit NVIDIA H100) und kompatibel zu
Dyssol Open.

Methoden, Designentscheidungen und Validierungen stehen ausführlich in
[`docs/StoSiS_Methoden.html`](docs/StoSiS_Methoden.html). Diese Datei gibt nur
einen Überblick.

## Stand (14. Oktober 2026)

Umgesetzt:

- **Flowsheet-Editor:** Block-Palette mit Drag & Drop, Ports, orthogonale
  Kabel, Einstellungsfenster je Block, Ergebnisse als Kurven und Heatmap
  (mit Randverteilungen, je Stoff umschaltbar).
- **Units:** Inlet (Zeitprofil, Größenverteilung Normal/Log-Normal/RRSB,
  Stoff, T, p), Outlet, Splitter (2 oder 3 Ausgänge, deterministisch oder
  stochastisch nach Ornstein-Uhlenbeck), Merger (2 oder 3 Eingänge),
  größenabhängige Verzögerung als Platzhalter-Prozess.
- **Rechenablauf:** Makroschritte, Units der Reihe nach (Gauß-Seidel),
  Rückführungen werden an einer Verbindung aufgeschnitten.
- **Mehrere Läufe:** globaler Seed mit eigenem Zufallsstrom je Unit,
  Mittelwert und Streuung nach Welford, Bänder und Perzentile; validiert
  gegen eine analytische Lösung.
- **Materialien:** eigene Materialdatei [`data/Materialien.json`](data/README.md)
  (18 Stoffe, Startinhalt aus Dyssol, jeder Wert mit Quelle und Prüfstatus).
  Ströme tragen je Stoff und Größenklasse eine Massenstromdichte sowie T
  und p; beim Mischen gilt die Energiebilanz.

Noch nicht umgesetzt (Planung in Kapitel 13 bis 15 des Methoden-Dokuments):

- logarithmisches Größenraster mit SI-Einheiten (heute linear, 150 Punkte
  von 0 bis 200)
- gewichtete MC-Partikel im Flowsheet; Koagulator mit Koagulation, Bruch,
  Wachstum und Keimbildung
- Flowsheets speichern und laden (geplant JSON); Import von Dyssol-Dateien
  (`.dflw`, Format HDF5)
- Materialien bearbeiten in der Oberfläche, Export ins Dyssol-Format
- 3D-Darstellung der Heatmap (Qt Graphs)

Die isolierten Transportmodelle (Tanks-in-Series, Diffusion/Drift, First
Passage Time, auch auf der GPU) sind über das Menü „Entwicklung“ erreichbar
und noch nicht ins Flowsheet eingebunden.

## Struktur

```
StoSiS/
├── core/          stosis_core: Rechenkern, reines C++17, keine Qt-Abhängigkeit
│   ├── include/   Units, Flowsheet-Graph, Ströme, Statistik, Materialien
│   ├── src/       u. a. CUDA-Kernel (Bibliothek stosis_cuda)
│   └── third_party/nlohmann_json   JSON-Bibliothek (MIT)
├── gui/src/       stosis_gui: Oberfläche (Qt 6 Widgets), Programm StoSiS.exe
├── data/          Materialdatei mit Anleitung und Dyssol-Lizenzhinweis
├── docs/          Methoden-Dokument, Abbildungen
├── tests/         automatische Tests des Rechenkerns (CTest)
├── tools/         dmdb_to_json.pl (Umwandlung der Dyssol-Materialdatenbank)
└── CMakeLists.txt
```

## Voraussetzungen

- Windows, Visual Studio 2022 mit „Desktopentwicklung mit C++“
- CMake ≥ 3.20
- Qt 6 (Modul Widgets, Kit msvc2022_64). Der Pfad steht in
  `gui/CMakeLists.txt` (`CMAKE_PREFIX_PATH`, derzeit
  `C:/Qt/6.11.1/msvc2022_64`) und muss bei anderer Installation angepasst
  werden.
- CUDA Toolkit (11.8 oder 12.x). Die GPU-Architekturen stehen in
  `core/CMakeLists.txt` (`CUDA_ARCHITECTURES`).

## Bauen

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
```

Danach `build/StoSiS.sln` in Visual Studio öffnen und mit F5 starten
(`stosis_gui` ist Startprojekt). Nach dem Bauen kopiert `windeployqt` die
Qt-DLLs neben `StoSiS.exe`, sodass das Programm auch ohne Visual Studio
läuft.

Die Materialdatei wird direkt aus `data/` im Quellordner gelesen. Nach
Änderungen genügt im Reiter „Materialien“ der Knopf „Neu laden“, ohne neu
zu bauen.

Findet CMake Qt nicht (`Could not find a package configuration file
provided by "Qt6"`): prüfen, ob `CMAKE_PREFIX_PATH` auf den Ordner zeigt, der
`lib/cmake/Qt6` enthält, und ob im Qt-Installer das zum Compiler passende
Kit (msvc2022_64) installiert ist.

## Tests

Die Prüfungen aus dem Methoden-Dokument liegen als Testprogramme in
`tests/` (Größenverteilungen, Graph und Rückführungen, OU-Splitter,
Mehrfachläufe gegen die analytische Lösung, Materialien, Ströme mit Stoff/T/p
und ein Regressionstest über alle Ergebnisse). Ausführen nach dem Bauen:

```bat
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Stochastische Tests nutzen feste Seeds und sind reproduzierbar. Ändert sich
die Rechnung bewusst, gibt `test_regression --update` neue Referenzwerte aus.
`analysis_convergence` erzeugt die Daten der Konvergenz-Abbildung im
Methoden-Dokument neu. Mit `-DSTOSIS_BUILD_TESTS=OFF` lassen sich die Tests
abschalten.

## Lizenzen

- `core/third_party/nlohmann_json`: MIT-Lizenz
- Startinhalt von `data/Materialien.json`: aus Dyssol (BSD-3-Lizenz), siehe
  `data/LICENSE_Dyssol.txt`
