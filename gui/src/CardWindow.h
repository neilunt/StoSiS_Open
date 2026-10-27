#pragma once
#include <QWidget>
#include "FlowsheetGraph.h"

class QComboBox;
class QLabel;
class QSlider;
class QTableWidget;
class CardPlotWidget;

// Fenster "Kartenansicht" (Menue Flowsheet): zeigt im Partikelmodus die
// Karten selbst statt ihres Fotos auf dem Groessenraster - fuer einen Strom
// und einen Zeitschritt von Lauf 1: Kartenwolke (Groesse gegen Massenstrom,
// Siebsatz als Streifen), Karten je Groessenbereich mit Warnschwelle,
// Kartenzahl ueber die Zeit und eine Tabelle aller Karten (CSV-Export).
// Daten aus CFlowsheetGraph::GetCardHistory (Methoden-Dokument 13.23).
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
    void FillTable(const stosis::CCardHistory::SSnapshot& snapshot, size_t numCompounds);
    void ExportCsv();
    const stosis::CCardHistory* CurrentHistory() const;

    stosis::CFlowsheetGraph* m_graph = nullptr;

    QComboBox* m_entryCombo = nullptr;
    QSlider* m_stepSlider = nullptr;
    QLabel* m_stepLabel = nullptr;
    QLabel* m_info = nullptr;
    CardPlotWidget* m_cloud = nullptr;
    CardPlotWidget* m_ranges = nullptr;
    CardPlotWidget* m_counts = nullptr;
    QTableWidget* m_table = nullptr;
};
