#pragma once
#include <QWidget>
#include <vector>
#include <utility>
#include <string>
#include "Flowsheet.h"

class QDoubleSpinBox;
class QSpinBox;
class QComboBox;
class QFormLayout;
class QGroupBox;
class HeatmapWidget;

class HeatmapDemoWindow : public QWidget
{
    Q_OBJECT

public:
    explicit HeatmapDemoWindow(QWidget* parent = nullptr);

private slots:
    void OnModelChanged(int index);
    void OnRegenerate();

private:
    void SetupUi();
    void RebuildParameterPanel();

    stosis::CFlowsheet m_flowsheet;
    HeatmapWidget* m_heatmap = nullptr;

    QComboBox* m_modelCombo = nullptr;

    QDoubleSpinBox* m_totalTimeSpin = nullptr;
    QSpinBox* m_numTimeStepsSpin = nullptr;
    QSpinBox* m_numSizeClassesSpin = nullptr;
    QDoubleSpinBox* m_d0Spin = nullptr;
    QDoubleSpinBox* m_sigma0Spin = nullptr;

    QGroupBox* m_modelParamGroup = nullptr;
    QFormLayout* m_modelParamLayout = nullptr;
    std::vector<std::pair<std::string, QDoubleSpinBox*>> m_modelParamSpins;
};