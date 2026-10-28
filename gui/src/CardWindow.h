#pragma once
#include <QWidget>
#include <vector>
#include "FlowsheetGraph.h"

class QComboBox;
class QLabel;
class QRadioButton;
class QSlider;
class QTableWidget;
class CardPlotWidget;

// Fenster "Kartenansicht" (Menue Flowsheet): zeigt im Partikelmodus die
// Karten selbst statt ihres Fotos auf dem Groessenraster - fuer einen Strom
// und einen Zeitpunkt von Lauf 1. Reiter "Verteilung": Dichte q3* oder
// Summenkurve Q3 aus den Karten mit Vergleichskurve (A/B), Kartenteppich (D)
// und Karten je Klasse (C) auf gemeinsamer d-Achse; "Zeitverlauf":
// Verteilung ueber der Zeit (E) und Kartenzahl je Zeitschritt;
// "Gewichte": Groesse gegen Massenstrom je Karte (F); "Tabelle": alle
// Karten mit CSV-Export. Daten aus CFlowsheetGraph::GetCardHistory
// (Methoden-Dokument 13.23).
class CardWindow : public QWidget
{
    Q_OBJECT

public:
    explicit CardWindow(QWidget* parent = nullptr);

    void SetGraph(stosis::CFlowsheetGraph* graph);
    // Stroeme neu einlesen (nach einer Simulation oder geaendertem
    // Flowsheet); Auswahl und Zeitpunkt bleiben, soweit moeglich.
    void RefreshNodeList();

private:
    void UpdateViews();
    // d-Achse und Zeitkarte (E) haengen nur vom Strom und der Klassenzahl
    // ab, nicht vom Zeitpunkt; neu gerechnet wird nur, wenn sich das aendert.
    void UpdateAxisAndTimeMap(const stosis::CCardHistory& history);
    // Vergleichskurve: Mittel aller Laeufe auf dem Groessenraster als
    // Dichte q3*(ln d) zum Zeitschritt step.
    void FillReference(size_t step, std::vector<double>& sizes, std::vector<double>& density, QString& label) const;
    void FillTable(const stosis::CCardHistory::SSnapshot& snapshot, size_t numCompounds);
    void ExportCsv();
    const stosis::CCardHistory* CurrentHistory() const;
    int GetClassesPerRange() const;

    stosis::CFlowsheetGraph* m_graph = nullptr;

    QComboBox* m_entryCombo = nullptr;
    QSlider* m_stepSlider = nullptr;
    QLabel* m_stepLabel = nullptr;
    QComboBox* m_classCombo = nullptr;
    QRadioButton* m_cumulativeRadio = nullptr;
    QLabel* m_info = nullptr;
    CardPlotWidget* m_distribution = nullptr;
    CardPlotWidget* m_rug = nullptr;
    CardPlotWidget* m_classes = nullptr;
    CardPlotWidget* m_timeMap = nullptr;
    CardPlotWidget* m_counts = nullptr;
    CardPlotWidget* m_cloud = nullptr;
    QTableWidget* m_table = nullptr;

    // Stand von UpdateAxisAndTimeMap (nullptr = neu rechnen).
    const stosis::CCardHistory* m_axisHistory = nullptr;
    int m_axisPerRange = 0;
    size_t m_axisSnapshots = 0;
    int m_firstRange = 0;
    int m_lastRange = 0;
};
