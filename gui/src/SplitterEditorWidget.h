#pragma once
#include <QWidget>
#include <string>
#include <vector>
#include "FlowsheetGraph.h"

class QDoubleSpinBox;
class QComboBox;
class QLabel;
class QPushButton;
class QGridLayout;
class FractionPreviewWidget;

// Editor-Seite fuer EINEN Splitter (im Einstellungsfenster, Reiter
// "Knoten"):
//  - Modus: deterministisch (feste Anteile) oder stochastisch
//    (Anteile schwanken nach einem Ornstein-Uhlenbeck-Prozess)
//  - Tabelle: je Ausgang mittlerer Anteil mu, im stochastischen Modus
//    zusaetzlich Schwankung sigma und Korrelationszeit tau. Der LETZTE
//    Ausgang bekommt den Rest - ein negativer Rest sperrt "Uebernehmen".
//  - Vorschau: genau der Verlauf der Anteile, der bei der naechsten
//    Simulation entsteht (gleicher Seed, gleiches dt).
class SplitterEditorWidget : public QWidget
{
    Q_OBJECT

public:
    explicit SplitterEditorWidget(QWidget* parent = nullptr);

    void SetGraph(stosis::CFlowsheetGraph* graph);
    void SetNode(const std::string& nodeId);
    // Seed, Dauer oder Raster haben sich geaendert -> Vorschau neu rechnen.
    void UpdatePreview();

signals:
    // Werte wurden in den Splitter geschrieben (simuliert NICHT).
    void Applied();

private slots:
    void OnValuesChanged();
    void OnApply();

private:
    void SetupUi();
    void LoadNode();
    double ComputeRest() const;
    bool IsStochasticSelected() const;
    std::vector<double> CurrentMeans() const; // inkl. Rest
    std::vector<stosis::SSplitterFluctuation> CurrentFluctuations() const;

    stosis::CFlowsheetGraph* m_graph = nullptr;
    std::string m_nodeId;

    QComboBox* m_modeCombo = nullptr;
    QGridLayout* m_table = nullptr;
    QWidget* m_tableWidget = nullptr;
    // Je Ausgang ausser dem letzten:
    std::vector<QDoubleSpinBox*> m_meanSpins;
    std::vector<QDoubleSpinBox*> m_sigmaSpins;
    std::vector<QDoubleSpinBox*> m_tauSpins;
    QLabel* m_restLabel = nullptr;
    QLabel* m_warningLabel = nullptr;
    QLabel* m_previewCaption = nullptr;
    FractionPreviewWidget* m_preview = nullptr;
    QPushButton* m_applyButton = nullptr;
};
