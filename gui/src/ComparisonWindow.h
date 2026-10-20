#pragma once
#include <QWidget>
#include "TransportUnitRegistry.h"
#include "SimulationResults.h"

class QDoubleSpinBox;
class QSpinBox;
class ModelPanelWidget;

// Vergleichsfenster: gemeinsames Zeit-/Groessengitter + Startverteilung
// oben, drei ModelPanelWidget nebeneinander darunter. Spalte drei kann
// entweder ein echtes Modell zeigen oder den Differenz-Modus, dann
// wird sie automatisch aus Spalte eins minus Spalte zwei berechnet.
class ComparisonWindow : public QWidget
{
    Q_OBJECT

public:
    explicit ComparisonWindow(QWidget* parent = nullptr);

private slots:
    void OnRunAll();

private:
    void SetupUi();
    void UpdateGridConfig();

    stosis::CTransportUnitRegistry m_registry;

    QDoubleSpinBox* m_totalTimeSpin = nullptr;
    QSpinBox* m_numTimeStepsSpin = nullptr;
    QSpinBox* m_numSizeClassesSpin = nullptr;
    QDoubleSpinBox* m_d0Spin = nullptr;
    QDoubleSpinBox* m_sigma0Spin = nullptr;

    ModelPanelWidget* m_panel1 = nullptr;
    ModelPanelWidget* m_panel2 = nullptr;
    ModelPanelWidget* m_panel3 = nullptr;

    // Haelt die berechnete Differenzmatrix - m_panel3 zeigt nur einen
    // Zeiger darauf an, der Speicher muss also so lange leben wie das
    // Fenster selbst.
    stosis::CSimulationResults m_diffResults;
};