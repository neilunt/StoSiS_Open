#pragma once
#include <QWidget>
#include <vector>
#include "FlowsheetGraph.h"

class QComboBox;
class QDoubleSpinBox;
class QCheckBox;
class QLabel;
class QPushButton;
class QSpinBox;
class AnalysisPlotWidget;

// Fenster "Auswertung" (Werkzeugleiste "Auswertung oeffnen"): wertet den
// Massenstrom eines Ausgangs so aus, wie man eine Messung auswertet -
// Mittel ueber Zeitfenster (z.B. 1 s wie eine Auffangwaage), optional der
// Ausreisserfilter nach Engisch & Muzzio (2012), dann Kennzahlen,
// Histogramm (Lauf 1 und Band ueber alle Laeufe), Wahrscheinlichkeitsnetz
// und Zeitverlauf; Export der Fenstermittel als CSV. Oben eine eigene
// Simulationsleiste (Dauer, dt, Laeufe, Simulieren) mit denselben Werten
// wie im Hauptfenster, damit man ohne Fensterwechsel neu rechnen kann.
// Rechnet mit SignalAnalysis.h wie das Auswerteprogramm
// tests/analysis_dosierwaage.cpp (Methoden-Dokument Kapitel 12 und 15).
class AnalysisWindow : public QWidget
{
    Q_OBJECT

public:
    explicit AnalysisWindow(QWidget* parent = nullptr);

    void SetGraph(stosis::CFlowsheetGraph* graph);
    // Ausgaenge neu einlesen (Knoten hinzugefuegt/entfernt); Auswahl bleibt.
    void RefreshNodeList();
    // Nach einer Simulation oder geaenderten Einstellungen neu auswerten.
    void Recompute();
    // Simulationsleiste oben mit den Werten des Hauptfensters fuellen
    // (ohne SimulationSettingsEdited auszuloesen); info = Zeitpunkte/Rate,
    // representation = stosis::ERepresentation als int.
    void SetSimulationSettings(double duration, double dt, int runs, int representation, const QString& info);

signals:
    // Dauer, dt oder Laeufe hier geaendert - das Hauptfenster uebernimmt sie.
    void SimulationSettingsEdited(double duration, double dt, int runs, int representation);
    void SimulateRequested();

private:
    void SetupUi();
    void ExportCsv();

    stosis::CFlowsheetGraph* m_graph = nullptr;

    QDoubleSpinBox* m_durationSpin = nullptr;
    QDoubleSpinBox* m_dtSpin = nullptr;
    QLabel* m_timeInfo = nullptr;
    QSpinBox* m_runsSpin = nullptr;
    QComboBox* m_representationCombo = nullptr;
    QComboBox* m_entryCombo = nullptr;
    QDoubleSpinBox* m_windowSpin = nullptr;
    QLabel* m_windowInfo = nullptr;
    QDoubleSpinBox* m_startSpin = nullptr;
    QCheckBox* m_filterCheck = nullptr;
    QDoubleSpinBox* m_setpointSpin = nullptr;
    QDoubleSpinBox* m_bandSpin = nullptr;
    QDoubleSpinBox* m_sigmaSpin = nullptr;
    QDoubleSpinBox* m_classWidthSpin = nullptr;
    QDoubleSpinBox* m_shownTimeSpin = nullptr;
    QLabel* m_statsLabel = nullptr;
    QPushButton* m_exportButton = nullptr;
    AnalysisPlotWidget* m_densityPlot = nullptr;
    AnalysisPlotWidget* m_probabilityPlot = nullptr;
    AnalysisPlotWidget* m_timePlot = nullptr;

    // Letzte Auswertung fuer den Export: Fenstermittel je Lauf (ungefiltert)
    // und Startzeit jedes Fensters.
    std::vector<std::vector<double>> m_windowMeans;
    std::vector<double> m_windowStarts;
    QString m_exportHeader;
};
