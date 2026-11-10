#pragma once
#include <QMainWindow>
#include "FlowsheetGraph.h"
#include "Materials.h"
#include "FlowsheetFile.h"

class QDoubleSpinBox;
class QSpinBox;
class QCheckBox;
class QComboBox;
class QLabel;
class FlowsheetCanvasWidget;
class ResultsCurveWindow;
class ResultsHeatmapWindow;
class SettingsWindow;
class HeatmapDemoWindow;
class ComparisonWindow;
class AnalysisWindow;
class SimulationLogWindow;
class CardWindow;
class FlowsheetStructureWindow;

// Hauptfenster = Darstellungsebene: links das Flowsheet (mit
// Simulationsdauer + "Simulieren" darunter), rechts Kurven ueber
// Heatmap. Alle Bereiche sind ueber Trennlinien (QSplitter) in der
// Groesse verschiebbar. Einstellungen leben im separaten
// SettingsWindow (Werkzeugleiste oder Doppelklick auf einen Knoten).
// Standard fuer "Anzahl Laeufe" (auch beim Programmstart).
inline constexpr size_t kDefaultNumRuns = 20;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void OnSimulate();
    void OnShowSettings();
    void OnShowMaterials();
    void OnShowAnalysis();
    void OnShowLog();
    void OnShowCards();
    void OnShowStructure();
    void OnSaveFlowsheet();
    void OnSaveFlowsheetAs();
    void OnNewFlowsheet();
    void OnOpenFlowsheet();
    void OnNodeDoubleClicked(const QString& nodeId);
    void OnShowHeatmapDemo();
    void OnShowComparisonWindow();
    void OnRunCudaTest();

private:
    void SetupMenuAndToolBar();
    void SetupCentralArea();
    void BuildTestFlowsheet();
    static void ConfigureDefaultGrid(stosis::CFlowsheetGraph& graph);
    // Ersetzt das Flowsheet (neu oder aus Datei) und setzt alle Ansichten darauf.
    void ReplaceGraph(stosis::CFlowsheetGraph&& graph, int runs);
    void SetCurrentFile(const QString& path);
    // Liest data/Materialien.json; Fehler landen in m_materialLoadError.
    void LoadMaterials();
    SettingsWindow* EnsureSettingsWindow();
    // Dauer und dt aus der Simulationsleiste ins Raster uebernehmen, Hinweis
    // (Rate, Zeitpunkte) aktualisieren und das Auswertungsfenster abgleichen.
    void ApplyTimeGrid();
    QString TimeGridInfo() const;
    void SyncAnalysisSettings();
    // Holt die Ergebnis-Zeiger in Kurven und Heatmap neu.
    void RefreshResultViews();
    // Was ausser dem Graphen in die Flowsheet-Datei gehoert (Anzahl Laeufe).
    stosis::SFlowsheetFileSettings CurrentFileSettings() const;
    // Fenster "Struktur" nach einer Aenderung am Flowsheet neu aufbauen.
    void RefreshStructureWindow();
    // Oeffnet docs/<Datei> im Browser (neben der exe oder im Quellordner).
    void OpenDocument(const QString& fileName);

    stosis::CFlowsheetGraph m_graph;
    stosis::CMaterialDatabase m_materials;
    QString m_materialLoadError;
    // Datei des Flowsheets (geoeffnet oder gespeichert), leer bei neuem
    // Flowsheet; nur ueber SetCurrentFile setzen (Titelleiste).
    QString m_currentFile;

    FlowsheetCanvasWidget* m_canvas = nullptr;
    QDoubleSpinBox* m_durationSpin = nullptr;
    QDoubleSpinBox* m_dtSpin = nullptr;
    QLabel* m_timeInfoLabel = nullptr;
    QSpinBox* m_seedSpin = nullptr;
    QSpinBox* m_runsSpin = nullptr;
    QComboBox* m_representationCombo = nullptr;
    QSpinBox* m_cardsSpin = nullptr;
    QCheckBox* m_limitsCheck = nullptr;
    ResultsCurveWindow* m_resultsCurveView = nullptr;
    ResultsHeatmapWindow* m_resultsHeatmapView = nullptr;

    SettingsWindow* m_settingsWindow = nullptr;
    HeatmapDemoWindow* m_heatmapDemoWindow = nullptr;
    ComparisonWindow* m_comparisonWindow = nullptr;
    AnalysisWindow* m_analysisWindow = nullptr;
    SimulationLogWindow* m_logWindow = nullptr;
    CardWindow* m_cardWindow = nullptr;
    FlowsheetStructureWindow* m_structureWindow = nullptr;
    double m_lastGuiSeconds = 0.0; // Neuzeichnen nach der letzten Simulation
};
