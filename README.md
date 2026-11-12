# StoSiS – Stochastic Simulation of Solids

C++ flowsheet simulation for solids process engineering. The goal is a
stochastic solution with weighted Monte Carlo particles, accelerated on GPUs
(CUDA, amplitUDE cluster with NVIDIA H100) and compatible with Dyssol Open.

The model, assumptions and methods are described in the methods handbook
[`docs/StoSiS_Methodenhandbuch.html`](docs/StoSiS_Methodenhandbuch.html), as PDF
[`docs/StoSiS_Methodenhandbuch.pdf`](docs/StoSiS_Methodenhandbuch.pdf) (in German). This file only gives
an overview.

## Features

Implemented:

- **Flowsheet editor:** block palette with drag & drop, ports, orthogonal
  wires, zoom and pan, a settings window per block, results as curves and
  heatmap (with marginal distributions, switchable per compound).
- **Units:** inlet (time profile; size distribution log-normal, RRSB, GGS,
  table or normal; compound, T, p), outlet, splitter (2 or 3 outputs,
  deterministic or stochastic after Ornstein-Uhlenbeck), merger (2 or 3
  inputs), vessel (ideally mixed, plug flow, cascade, core flow or batch;
  initial filling empty, steady-state or given), size-dependent delay as a
  placeholder process.
- **Size grid:** logarithmic (default 1 µm – 1 mm, 300 classes) or linear;
  SI units internally, display in µm.
- **Calculation sequence:** macro steps, units one after another
  (Gauss-Seidel), recycles are torn at one connection. The "Struktur"
  window shows order, cycles and tear streams as a diagram and as a tree.
- **Flowsheet as a file:** JSON with all inputs (simulation, units with all
  settings, connections); new (Ctrl+N), open and save in the user interface,
  secured by a round-trip test. Examples in `Flowsheets/Validierungs-Flowsheets`:
  compartment models after Levenspiel and a blender, checked against their
  exact residence time distribution, and a dry granulation line after
  Martinetz et al. (2018) at four throughputs, checked against the line model
  and the measured step response of the paper (`test_levenspiel`).
- **Multiple runs:** global seed with a separate random stream per unit,
  mean and spread after Welford, bands and percentiles; validated against an
  analytical solution.
- **Analysis of fluctuating streams:** window means, outlier filter,
  histogram, probability plot and time series; comparison with measured data
  of a loss-in-weight feeder.
- **Materials:** material file [`data/Materialien.json`](data/README.md)
  (18 compounds, initial content from Dyssol, every value with source and
  check status). Streams carry a mass flow density per compound and size
  class as well as T and p; mixing follows the energy balance.
- **Particle mode:** streams carry weighted Monte Carlo particles ("cards");
  all units compute with them. The inlet places the cards according to an
  adjustable card density from mass and number; splitter, vessel and
  size-dependent delay distribute whole cards with an account method; limits
  per stream slice keep the number of cards small; for display the cards are
  projected onto the grid.

Not yet implemented:

- Vessel content as a result; coagulator with coagulation, breakage, growth
  and nucleation
- Running a flowsheet file without the user interface; translator from
  Dyssol flowsheets (`.dflw`, HDF5 format) into this file
- Editing materials in the user interface, export to the Dyssol format
- 3D display of the heatmap (Qt Graphs)

The isolated transport models (tanks-in-series, diffusion/drift, first
passage time, also on the GPU) are available in the "Entwicklung" menu and
not yet part of the flowsheet.

## Structure

```
StoSiS/
├── core/          stosis_core: computational core, plain C++17, no Qt dependency
│   ├── include/   units, flowsheet graph, streams, statistics, materials
│   ├── src/       among others CUDA kernels (library stosis_cuda)
│   └── third_party/nlohmann_json   JSON library (MIT)
├── gui/src/       stosis_gui: user interface (Qt 6 Widgets), program StoSiS.exe
├── data/          material file with instructions and Dyssol license notice
├── docs/          methods handbook (HTML and PDF), program help, figures
├── tests/         automatic tests of the computational core (CTest)
├── Flowsheets/    flowsheet files; inside, the validation flowsheets (to open in StoSiS)
├── tools/         dmdb_to_json.pl (conversion of the Dyssol material database)
└── CMakeLists.txt
```

## Installation without a development environment

[Releases](https://github.com/neilunt/StoSiS_Open/releases/latest) contains the
installer `StoSiS-<version>-win64.exe` (Windows 10/11, 64 bit). Download and
run it: it installs StoSiS with all libraries to `C:\Program Files\StoSiS`
and creates shortcuts in the start menu and on the desktop. Visual Studio,
Qt, CUDA and CMake are not required; only the GPU calculations in the
"Entwicklung" menu need an NVIDIA graphics card. Uninstall via
Settings → Apps.

The installer is not signed. Windows SmartScreen therefore reports "Windows
protected your PC"; continue via "More info" → "Run anyway". Installing needs
administrator rights; without them, unpack the ZIP file from the same release
and start `StoSiS.exe` in the unpacked folder.

The user interface and the documentation are currently in German.

## Requirements for building

- Windows, Visual Studio 2022 with "Desktop development with C++" (https://visualstudio.microsoft.com/vs/older-downloads/)
- CMake ≥ 3.20 (https://cmake.org/download/)
- Qt 6 (module Widgets, kit msvc2022_64). The path is set in
  `gui/CMakeLists.txt` (`CMAKE_PREFIX_PATH`, currently
  `C:/Qt/6.11.1/msvc2022_64`) and has to be adapted for a different
  installation (https://www.qt.io/development/qt-framework/qt6)
- CUDA Toolkit (11.8 or 12.x). The GPU architectures are set in
  `core/CMakeLists.txt` (`CUDA_ARCHITECTURES`) (https://developer.nvidia.com/cuda-toolkit-archive)

## Building

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
```

Then open `build/StoSiS.sln` in Visual Studio and start with F5
(`stosis_gui` is the startup project). After building, `windeployqt` copies
the Qt DLLs next to `StoSiS.exe`, so the program also runs without Visual
Studio.

The material file is read directly from `data/` in the source folder. After
changes, the "Neu laden" button in the "Materialien" tab is enough; no
rebuild is needed.

If CMake does not find Qt (`Could not find a package configuration file
provided by "Qt6"`): check that `CMAKE_PREFIX_PATH` points to the folder
containing `lib/cmake/Qt6`, and that the kit matching the compiler
(msvc2022_64) is installed in the Qt installer.

## Tests

The checks from the methods handbook are test programs in `tests/` (size
distributions, graph and recycles, OU splitter, multiple runs against the
analytical solution, materials, streams with compound/T/p, cards, vessel
against the closed-form residence time distributions, and a regression test
over all results). Run them after building:

```bat
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Stochastic tests use fixed seeds and are reproducible. If the calculation
changes on purpose, `test_regression --update` prints new reference values.
`analysis_convergence` regenerates the convergence data over the number of
runs. With `-DSTOSIS_BUILD_TESTS=OFF` the tests can be switched
off.

## Licenses

- `core/third_party/nlohmann_json`: MIT license
- Initial content of `data/Materialien.json`: from Dyssol (BSD-3 license), see
  `data/LICENSE_Dyssol.txt`
