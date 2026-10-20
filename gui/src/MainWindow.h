#pragma once
#include <QMainWindow>
#include "FlowsheetGraph.h"
#include "Materials.h"

class QDoubleSpinBox;
class QSpinBox;
class FlowsheetCanvasWidget;
class ResultsCurveWindow;
class ResultsHeatmapWindow;
class SettingsWindow;
class HeatmapDemoWindow;
class ComparisonWindow;

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
    void OnNodeDoubleClicked(const QString& nodeId);
    void OnShowHeatmapDemo();
    void OnShowComparisonWindow();
    void OnRunCudaTest();

private:
    void SetupMenuAndToolBar();
    void SetupCentralArea();
    void BuildTestFlowsheet();
    // Liest data/Materialien.json; Fehler landen in m_materialLoadError.
    void LoadMaterials();
    SettingsWindow* EnsureSettingsWindow();
    // Holt die Ergebnis-Zeiger in Kurven und Heatmap neu.
    void RefreshResultViews();

    stosis::CFlowsheetGraph m_graph;
    stosis::CMaterialDatabase m_materials;
    QString m_materialLoadError;

    FlowsheetCanvasWidget* m_canvas = nullptr;
    QDoubleSpinBox* m_durationSpin = nullptr;
    QSpinBox* m_seedSpin = nullptr;
    QSpinBox* m_runsSpin = nullptr;
    ResultsCurveWindow* m_resultsCurveView = nullptr;
    ResultsHeatmapWindow* m_resultsHeatmapView = nullptr;

    SettingsWindow* m_settingsWindow = nullptr;
    HeatmapDemoWindow* m_heatmapDemoWindow = nullptr;
    ComparisonWindow* m_comparisonWindow = nullptr;
};
