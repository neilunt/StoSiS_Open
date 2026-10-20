#pragma once
#include <QWidget>
#include "FlowsheetGraph.h"

class QListWidget;
class QComboBox;
class QCheckBox;
class CurveWidget;

// Kurvenansicht: oben die Liste aller Ergebnisse (Mehrfachauswahl per
// Strg/Umschalt), darunter ein gemeinsames Diagramm mit einer Linie je
// markiertem Eintrag ueber die Zeit: Massenstrom (Integral ueber alle
// Groessen; alle Stoffe oder ein Stoff), Temperatur oder Druck. Bei mehreren
// Laeufen zeigt die Linie den Mittelwert, dazu wahlweise ein Streuband
// (+-1 sigma, +-2 sigma, 5-95 %-Perzentil) und die Einzellaeufe als duenne
// Linien.
class ResultsCurveWindow : public QWidget
{
    Q_OBJECT

public:
    explicit ResultsCurveWindow(QWidget* parent = nullptr);

    void SetGraph(stosis::CFlowsheetGraph* graph);
    void RefreshNodeList();
    // Berechnet die Kurven fuer die AKTUELL markierten Eintraege neu,
    // ohne die Liste selbst neu aufzubauen - im Gegensatz zu
    // RefreshNodeList() bleibt die Markierung dabei erhalten.
    void UpdateCurvesPublic() { UpdateCurves(); }

private slots:
    void OnSelectionChanged();

private:
    void SetupUi();
    void UpdateCurves();
    // Stoffliste der letzten Simulation ins Dropdown (Auswahl bleibt).
    void UpdateCompoundList(const std::vector<std::string>& names);

    // Reihenfolge der Eintraege im Streuband-Dropdown.
    enum EBand
    {
        BandNone = 0,
        BandOneSigma,
        BandTwoSigma,
        BandPercentile
    };

    // Reihenfolge der Eintraege im Groessen-Dropdown.
    enum EQuantity
    {
        QuantityMassFlow = 0,
        QuantityTemperature,
        QuantityPressure
    };

    stosis::CFlowsheetGraph* m_graph = nullptr;

    QListWidget* m_nodeList = nullptr;
    QComboBox* m_quantityCombo = nullptr;
    QComboBox* m_compoundCombo = nullptr;
    QComboBox* m_bandCombo = nullptr;
    QCheckBox* m_runsCheck = nullptr;
    CurveWidget* m_curveWidget = nullptr;
};
