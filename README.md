# StoSiS – Stochastic Simulation of Solids

C++-Fließschemasimulation für die Feststoffverfahrenstechnik. Ziel ist eine
stochastische Lösung mit gewichteten Monte-Carlo-Partikeln, beschleunigt auf
GPUs (CUDA, Großrechner amplitUDE mit NVIDIA H100) und kompatibel zu
Dyssol Open.

Methoden, Designentscheidungen und Validierungen stehen ausführlich in
[`docs/StoSiS_Methoden.html`](docs/StoSiS_Methoden.html). Diese Datei gibt nur
einen Überblick.

## Funktionsumfang

Umgesetzt:

- **Flowsheet-Editor:** Block-Palette mit Drag & Drop, Ports, orthogonale
  Kabel, Zoom und Verschieben, Einstellungsfenster je Block, Ergebnisse als
  Kurven und Heatmap (mit Randverteilungen, je Stoff umschaltbar).
- **Units:** Inlet (Zeitprofil; Größenverteilung Log-Normal, RRSB, GGS,
  Tabelle oder Normal; Stoff, T, p), Outlet, Splitter (2 oder 3 Ausgänge,
  deterministisch oder stochastisch nach Ornstein-Uhlenbeck), Merger (2 oder
  3 Eingänge), größenabhängige Verzögerung als Platzhalter-Prozess.
- **Größenraster:** logarithmisch (Standard 1 µm – 1 mm, 300 Klassen) oder
  linear; intern SI-Einheiten, Anzeige in µm.
- **Rechenablauf:** Makroschritte, Units der Reihe nach (Gauß-Seidel),
  Rückführungen werden an einer Verbindung aufgeschnitten.
- **Mehrere Läufe:** globaler Seed mit eigenem Zufallsstrom je Unit,
  Mittelwert und Streuung nach Welford, Bänder und Perzentile; validiert
  gegen eine analytische Lösung.
- **Auswertung schwankender Ströme:** Fenstermittel, Ausreißerfilter,
  Histogramm, Wahrscheinlichkeitsnetz und Zeitverlauf; Vergleich mit
  Messdaten einer Dosierwaage.
- **Materialien:** eigene Materialdatei [`data/Materialien.json`](data/README.md)
  (18 Stoffe, Startinhalt aus Dyssol, jeder Wert mit Quelle und Prüfstatus).
  Ströme tragen je Stoff und Größenklasse eine Massenstromdichte sowie T
  und p; beim Mischen gilt die Energiebilanz.
- **Partikelmodus (im Aufbau):** Ströme tragen gewichtete MC-Partikel
  („Karten“); das Inlet erzeugt sie an Quantilen mit Mindestzahl je
  Größenbereich, für die Anzeige werden sie aufs Raster umgerechnet.

Noch nicht umgesetzt (Planung in Kapitel 13 bis 15 des Methoden-Dokuments):

- Partikelmodus für Merger, Splitter und Units mit Hold-up; Koagulator mit
  Koagulation, Bruch, Wachstum und Keimbildung
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

## Installation ohne Entwicklungsumgebung

Unter [Releases](https://github.com/neilunt/StoSiS_Open/releases/latest) liegt
das Installationsprogramm `StoSiS-<Version>-win64.exe` (Windows 10/11, 64 Bit).
Herunterladen und ausführen: Es installiert StoSiS mit allen Bibliotheken
nach `C:\Program Files\StoSiS` (im Explorer „Programme“) und legt
Verknüpfungen im Startmenü und auf dem Desktop an. Visual Studio, Qt, CUDA
und CMake sind dafür nicht nötig; nur die GPU-Rechnungen im Menü
„Entwicklung“ brauchen eine NVIDIA-Grafikkarte. Entfernen über
Einstellungen → Apps.

Das Installationsprogramm ist nicht signiert. Windows SmartScreen meldet
deshalb „Der Computer wurde durch Windows geschützt“; über „Weitere
Informationen“ → „Trotzdem ausführen“ geht es weiter. Die Installation
braucht Administratorrechte; ohne sie die ZIP-Datei aus demselben Release
entpacken und `StoSiS.exe` im entpackten Ordner starten.

## Voraussetzungen zum Bauen

- Windows, Visual Studio 2022 mit „Desktopentwicklung mit C++“(https://visualstudio.microsoft.com/de/vs/older-downloads/)
- CMake ≥ 3.20 (https://cmake.org/download/)
- Qt 6 (Modul Widgets, Kit msvc2022_64). Der Pfad steht in
  `gui/CMakeLists.txt` (`CMAKE_PREFIX_PATH`, derzeit
  `C:/Qt/6.11.1/msvc2022_64`) und muss bei anderer Installation angepasst
  werden.(https://www.qt.io/development/qt-framework/qt6)
- CUDA Toolkit (11.8 oder 12.x). Die GPU-Architekturen stehen in
  `core/CMakeLists.txt` (`CUDA_ARCHITECTURES`).(https://developer.nvidia.com/cuda-toolkit-archive)

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
