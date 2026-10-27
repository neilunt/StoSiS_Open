# stosis_gui: das eigentliche .exe. Enthaelt NUR Praesentationslogik
# (Fenster, Menues, Darstellung) und linkt gegen stosis_core fuer
# alles Rechenrelevante.

# WICHTIG: Diesen Pfad an die lokale Qt-Installation anpassen, z.B.:
#   set(CMAKE_PREFIX_PATH "C:/Qt/6.7.0/msvc2019_64")
# Ist die Umgebungsvariable Qt6_DIR bereits gesetzt, kann die Zeile
# auch entfallen - dann findet find_package() Qt automatisch.
#
set(CMAKE_PREFIX_PATH "C:/Qt/6.11.1/msvc2022_64")

find_package(Qt6 REQUIRED COMPONENTS Widgets)

# Qt-typisch: MOC (Meta-Object Compiler) muss automatisch fuer alle
# Klassen mit Q_OBJECT-Makro laufen. Diese drei Zeilen erledigen das,
# sonst muesste man das von Hand pflegen.
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

add_executable(stosis_gui
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/HeatmapWidget.cpp
    src/HeatmapWidget.h
    src/HeatmapDemoWindow.cpp
    src/HeatmapDemoWindow.h
    src/ModelPanelWidget.cpp
    src/ModelPanelWidget.h
    src/ComparisonWindow.cpp
    src/ComparisonWindow.h
src/FlowsheetNodeItem.cpp
src/FlowsheetNodeItem.h
src/FlowsheetCanvasWidget.cpp
src/FlowsheetCanvasWidget.h
src/FlowsheetConnectionItem.cpp
src/FlowsheetConnectionItem.h
src/FlowsheetPaletteWidget.cpp
src/FlowsheetPaletteWidget.h
src/FlowsheetScene.h
src/NodeTemplates.h
# Alter Ansatz, nicht mehr benutzt (Zeichenflaeche nutzt QGraphicsView +
# die FlowsheetPortItem-Klasse aus FlowsheetNodeItem.h). Aus dem Build
# genommen, weil es sonst zwei verschiedene Klassen "FlowsheetPortItem"
# im selben Programm gaebe (ODR-Verletzung). Dateien liegen noch im Ordner.
#src/FlowsheetPortItem.cpp
#src/FlowsheetPortItem.h
#src/FlowsheetGraphicsView.cpp
#src/FlowsheetGraphicsView.h
src/InletEditorWidget.cpp
src/InletEditorWidget.h    
src/CurveWidget.cpp
src/CurveWidget.h
src/ResultsCurveWindow.cpp
src/ResultsCurveWindow.h
src/AnalysisWindow.cpp
src/AnalysisWindow.h
src/AnalysisPlotWidget.cpp
src/AnalysisPlotWidget.h
src/SimulationLogWindow.cpp
src/SimulationLogWindow.h
src/CardWindow.cpp
src/CardWindow.h
src/CardPlotWidget.cpp
src/CardPlotWidget.h
src/SieveGridWidget.cpp
src/SieveGridWidget.h
src/ProcessNodeEditorWidget.cpp
src/ProcessNodeEditorWidget.h
src/ResultsHeatmapWindow.cpp
src/ResultsHeatmapWindow.h
src/SettingsWindow.cpp
src/SettingsWindow.h
src/DistributionPreviewWidget.cpp
src/DistributionPreviewWidget.h
src/SplitterEditorWidget.cpp
src/SplitterEditorWidget.h
src/FractionPreviewWidget.cpp
src/FractionPreviewWidget.h
src/ResultsEntryKey.h
src/MaterialsPage.cpp
src/MaterialsPage.h
)

target_link_libraries(stosis_gui PRIVATE
    stosis_core
    Qt6::Widgets
)

set_target_properties(stosis_gui PROPERTIES FOLDER "GUI" OUTPUT_NAME "StoSiS")

# Ort der Materialdatei im Quellordner. Das Programm liest sie direkt von
# dort, damit Aenderungen an data/Materialien.json ohne neues Bauen wirken
# (siehe FindMaterialFile in MaterialsPage.cpp).
target_compile_definitions(stosis_gui PRIVATE STOSIS_DATA_DIR="${CMAKE_SOURCE_DIR}/data")

# Sorgt dafuer, dass beim Debuggen in Visual Studio (F5) das .exe direkt
# als Startprojekt erkannt wird.
set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT stosis_gui)

# Kopiert die benoetigten Qt-DLLs automatisch neben die .exe, damit sich
# das Programm auch ausserhalb von Visual Studio starten laesst (Windows
# findet sonst z.B. Qt6Widgets.dll nicht). Ohne C++-Laufzeit: Die ist mit
# Visual Studio installiert; windeployqt wuerde sonst nur das Installations-
# programm vc_redist danebenlegen und ausserhalb der VS-Eingabeaufforderung
# warnen (VCINSTALLDIR). Fuer die Weitergabe an Rechner ohne Visual Studio
# dort einmal das "Microsoft Visual C++ Redistributable" installieren.
if(WIN32)
    find_program(WINDEPLOYQT_EXE windeployqt HINTS "${Qt6_DIR}/../../../bin")
    if(WINDEPLOYQT_EXE)
        add_custom_command(TARGET stosis_gui POST_BUILD
            COMMAND "${WINDEPLOYQT_EXE}" --no-compiler-runtime "$<TARGET_FILE:stosis_gui>"
        )
    endif()
endif()
