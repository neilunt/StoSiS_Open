#pragma once
#include <QWidget>
#include "FlowsheetGraph.h"

class QComboBox;
class QCheckBox;
class QSpinBox;
class QLabel;
class HeatmapWidget;

// Ergebnisfenster als Heatmap: ein Dropdown waehlt EINEN beliebigen
// Flowsheet-Knoten (Inlet, Prozess, Outlet), darunter dessen Ergebnis
// als Zeit x Groesse x Massenstromdichte. Gegenstueck zum
// Kurvenvergleich, der ueber die Groesse integriert. Spricht nur ueber
// IResultsSource mit dem Graphen, kennt also keine konkreten Unit-Typen.
// Bei mehreren Laeufen umschaltbar: Einzellauf (Lauf 1), Mittelwert oder
// Standardabweichung je Zelle. Liefern Inlets verschiedene Stoffe, laesst
// sich ausserdem ein einzelner Stoff zeigen.
class ResultsHeatmapWindow : public QWidget
{
    Q_OBJECT

public:
    explicit ResultsHeatmapWindow(QWidget* parent = nullptr);

    void SetGraph(stosis::CFlowsheetGraph* graph);
    // Baut die Knotenliste neu auf (Knoten hinzugefuegt/entfernt). Die
    // aktuelle Auswahl bleibt ueber die Knoten-ID erhalten, falls moeglich.
    void RefreshNodeList();
    // Holt den Ergebnis-Zeiger frisch aus dem Graphen. Nach JEDER
    // Simulation und nach jedem Modellwechsel aufrufen - ein
    // ausgetauschter Prozessor macht alte Zeiger ungueltig.
    void UpdateHeatmap();

private slots:
    void OnNodeSelectionChanged(int index);

private:
    void SetupUi();
    // Stoffliste der letzten Simulation ins Dropdown (Auswahl bleibt).
    void UpdateCompoundList(const std::vector<std::string>& names);

    // Reihenfolge der Eintraege im Anzeige-Dropdown.
    enum EView
    {
        ViewFirstRun = 0,
        ViewMean,
        ViewStdDev
    };

    stosis::CFlowsheetGraph* m_graph = nullptr;

    QComboBox* m_nodeCombo = nullptr;
    QComboBox* m_viewCombo = nullptr;
    QLabel* m_compoundLabel = nullptr;
    QComboBox* m_compoundCombo = nullptr;
    QCheckBox* m_timeOnXCheck = nullptr;
    QCheckBox* m_logCheck = nullptr;
    QSpinBox* m_decadesSpin = nullptr;
    QCheckBox* m_marginalsCheck = nullptr;
    HeatmapWidget* m_heatmap = nullptr;
    QLabel* m_totalLabel = nullptr;
};
