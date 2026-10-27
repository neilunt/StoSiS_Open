#pragma once
#include <QWidget>
#include "FlowsheetGraph.h"

class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QTableWidget;

// Fenster "Laufzeitprotokoll" (Menue Flowsheet): was die letzte Simulation
// wann begonnen und beendet hat - Laeufe, Initialisierung und Zeitschritte
// der Units, Statistik (Textprotokoll) - und wie lange jede Unit gerechnet
// hat (Tabelle). Daten aus CFlowsheetGraph::GetSimulationLog und
// GetSimulationTiming (Methoden-Dokument Kap. 12).
class SimulationLogWindow : public QWidget
{
    Q_OBJECT

public:
    explicit SimulationLogWindow(QWidget* parent = nullptr);

    void SetGraph(stosis::CFlowsheetGraph* graph);
    // Nach jeder Simulation; guiSeconds = Zeit fuers Neuzeichnen der
    // Ergebnisfenster (nicht in der Rechenzeit des Rechenkerns enthalten).
    void Refresh(double guiSeconds);

private:
    void ShowLogText();
    // Nachkommastellen der Zeitstempel (s): 3 oder 6 (Haekchen "1 us genau").
    int GetLogDecimals() const;
    void FillTable();
    void SaveLog();
    void SaveTableCsv();

    stosis::CFlowsheetGraph* m_graph = nullptr;
    double m_guiSeconds = 0.0;

    QLabel* m_summary = nullptr;
    QPlainTextEdit* m_text = nullptr;
    QTableWidget* m_table = nullptr;
    QCheckBox* m_detailedCheck = nullptr;
    QCheckBox* m_microsecondsCheck = nullptr;
};
