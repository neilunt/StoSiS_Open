#pragma once
#include <QWidget>
#include <vector>
#include <utility>
#include <string>
#include "TransportUnitRegistry.h"
#include "SimulationResults.h"
#include "Flowsheet.h"
#include "HeatmapWidget.h"

class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QGroupBox;
class HeatmapWidget;

// Eigenstaendiges Panel: Modellauswahl + dynamisches Parameter-Panel
// + Heatmap. Wird im Vergleichsfenster dreimal nebeneinander eingesetzt.
// Bekommt von aussen das gemeinsame Gitter und die Startverteilung
// mitgeteilt, simuliert selbst, und stellt das Ergebnis zur Abholung
// bereit (z.B. fuer die Differenzberechnung).
class ModelPanelWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ModelPanelWidget(stosis::CTransportUnitRegistry* registry,
        bool offerDiffOption,
        QWidget* parent = nullptr);

    // Wird vom Vergleichsfenster aufgerufen, bevor RunSimulation()
    // aufgerufen wird, damit alle Panels dasselbe Gitter benutzen.
    void SetGridConfig(size_t numClasses, double sizeMin, double sizeMax,
        size_t numTimeSteps, double totalTime,
        double d0, double sigma0);

    // Fuehrt die Simulation fuer das aktuell ausgewaehlte Modell aus.
    // Gibt false zurueck, wenn dieses Panel den Differenz-Modus
    // ausgewaehlt hat (dann rechnet es selbst nichts).
    bool RunSimulation();

    void MarkDirty() { m_needsRecompute = true; }

    // Setzt das Ergebnis von aussen (nur fuer den Differenz-Modus).
    void SetExternalResults(const stosis::CSimulationResults* results,
        bool divergingColorScale);

    const stosis::CSimulationResults* GetResults() const;

    bool IsDiffMode() const;

signals:
    void ModelChanged();

private slots:
    void OnModelChanged(int index);

private:
    void RebuildParameterPanel();

    stosis::CFlowsheet m_flowsheet;
    stosis::CTransportUnitRegistry* m_registry = nullptr;
    bool m_offerDiffOption = false;

    QComboBox* m_modelCombo = nullptr;
    QGroupBox* m_modelParamGroup = nullptr;
    QFormLayout* m_modelParamLayout = nullptr;
    std::vector<std::pair<std::string, QDoubleSpinBox*>> m_modelParamSpins;

    HeatmapWidget* m_heatmap = nullptr;

    size_t m_numClasses = 150;
    double m_sizeMin = 0.0;
    double m_sizeMax = 200.0;
    size_t m_numTimeSteps = 200;
    double m_totalTime = 10.0;
    double m_d0 = 60.0;
    double m_sigma0 = 5.0;
    bool m_needsRecompute = true;
};