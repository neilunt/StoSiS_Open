#pragma once
#include <QWidget>
#include "FlowsheetGraph.h"
#include "CurveWidget.h"

class QListWidget;
class QComboBox;
class QCheckBox;
class QPushButton;

// Kurvenansicht: oben die Liste aller Ergebnisse und die Liste der Stoffe
// (je Mehrfachauswahl per Strg/Umschalt), darunter ein gemeinsames Diagramm
// mit einer Linie je markiertem Eintrag und Stoff ueber die Zeit: Massenstrom
// (Integral ueber alle Groessen; alle Stoffe zusammen oder einzelne Stoffe),
// Temperatur oder Druck. Bei mehreren Laeufen zeigt die Linie den
// Mittelwert, dazu wahlweise ein Streuband (+-1 sigma, +-2 sigma,
// 5-95 %-Perzentil) und die Einzellaeufe als duenne Linien. The plot can be
// zoomed and panned with the mouse (see CurveWidget); the shown curves can
// be saved as CSV.
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
    // Compound list of the last simulation ("alle Stoffe" plus one entry per
    // compound when there are several); the selection is kept by name.
    void UpdateCompoundList(const std::vector<std::string>& names);
    // Writes the shown curves (all time steps) as CSV.
    void ExportCsv();

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
    QListWidget* m_compoundList = nullptr;
    QComboBox* m_quantityCombo = nullptr;
    QComboBox* m_bandCombo = nullptr;
    QCheckBox* m_runsCheck = nullptr;
    QPushButton* m_exportButton = nullptr;
    CurveWidget* m_curveWidget = nullptr;

    // What is shown, for the CSV export.
    std::vector<SCurveSeries> m_shownSeries;
    QString m_shownQuantity;
    double m_shownDt = 0.0;
    size_t m_shownRuns = 0;
};
