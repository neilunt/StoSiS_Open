#include "HeatmapDemoWindow.h"
#include "HeatmapWidget.h"
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QComboBox>
#include <QPushButton>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QGroupBox>

HeatmapDemoWindow::HeatmapDemoWindow(QWidget* parent)
    : QWidget(parent)
{
    setWindowTitle("Heatmap: austauschbares Transport-Modell");
    resize(850, 650);

    SetupUi();
    RebuildParameterPanel();
    OnRegenerate();
}

void HeatmapDemoWindow::SetupUi()
{
    auto* mainLayout = new QVBoxLayout(this);

    auto* gridGroup = new QGroupBox("Zeit-/Groessengitter & Startverteilung", this);
    auto* gridForm = new QFormLayout(gridGroup);

    m_totalTimeSpin = new QDoubleSpinBox(gridGroup);
    m_totalTimeSpin->setRange(0.1, 1000.0);
    m_totalTimeSpin->setValue(10.0);
    gridForm->addRow("Gesamtzeit:", m_totalTimeSpin);

    m_numTimeStepsSpin = new QSpinBox(gridGroup);
    m_numTimeStepsSpin->setRange(2, 5000);
    m_numTimeStepsSpin->setValue(200);
    gridForm->addRow("Anzahl Zeitschritte:", m_numTimeStepsSpin);

    m_numSizeClassesSpin = new QSpinBox(gridGroup);
    m_numSizeClassesSpin->setRange(5, 2000);
    m_numSizeClassesSpin->setValue(150);
    gridForm->addRow("Anzahl Groessenklassen:", m_numSizeClassesSpin);

    m_d0Spin = new QDoubleSpinBox(gridGroup);
    m_d0Spin->setRange(0.0, 200.0);
    m_d0Spin->setValue(60.0);
    gridForm->addRow("Startgroesse d0:", m_d0Spin);

    m_sigma0Spin = new QDoubleSpinBox(gridGroup);
    m_sigma0Spin->setRange(0.01, 100.0);
    m_sigma0Spin->setValue(5.0);
    gridForm->addRow("Anfangsbreite sigma0:", m_sigma0Spin);

    mainLayout->addWidget(gridGroup);

    m_modelParamGroup = new QGroupBox("Transport-Modell", this);
    m_modelParamLayout = new QFormLayout(m_modelParamGroup);

    m_modelCombo = new QComboBox(m_modelParamGroup);
    for (const auto& name : m_flowsheet.GetTransportUnitRegistry().GetAvailableTypeNames())
        m_modelCombo->addItem(QString::fromStdString(name));
    connect(m_modelCombo, &QComboBox::currentIndexChanged, this, &HeatmapDemoWindow::OnModelChanged);
    m_modelParamLayout->addRow("Modelltyp:", m_modelCombo);

    mainLayout->addWidget(m_modelParamGroup);

    auto* runButton = new QPushButton("Simulation ausfuehren", this);
    connect(runButton, &QPushButton::clicked, this, &HeatmapDemoWindow::OnRegenerate);
    mainLayout->addWidget(runButton);

    m_heatmap = new HeatmapWidget(this);
    mainLayout->addWidget(m_heatmap, 1);
}

void HeatmapDemoWindow::RebuildParameterPanel()
{
    const std::string selected = m_modelCombo->currentText().toStdString();
    m_flowsheet.SelectTransportUnit(selected);

    for (auto& entry : m_modelParamSpins)
        m_modelParamLayout->removeRow(entry.second);
    m_modelParamSpins.clear();

    for (const auto& desc : m_flowsheet.GetTransportUnitParameters())
    {
        auto* spin = new QDoubleSpinBox(m_modelParamGroup);
        spin->setRange(desc.minValue, desc.maxValue);
        spin->setValue(desc.defaultValue);
        m_modelParamLayout->addRow(QString::fromStdString(desc.label) + ":", spin);
        m_modelParamSpins.emplace_back(desc.id, spin);
    }
}

void HeatmapDemoWindow::OnModelChanged(int /*index*/)
{
    m_heatmap->SetResults(nullptr);
    RebuildParameterPanel();
    OnRegenerate();
}

void HeatmapDemoWindow::OnRegenerate()
{
    const double totalTime = m_totalTimeSpin->value();
    const size_t numTimeSteps = static_cast<size_t>(m_numTimeStepsSpin->value());
    const size_t numSizeClasses = static_cast<size_t>(m_numSizeClassesSpin->value());

    m_flowsheet.ConfigureGrid(numSizeClasses, 0.0, 200.0, numTimeSteps, totalTime);
    m_flowsheet.SetInitialGaussian(m_d0Spin->value(), m_sigma0Spin->value(), 1.0);

    for (const auto& entry : m_modelParamSpins)
        m_flowsheet.SetTransportUnitParameter(entry.first, entry.second->value());

    m_flowsheet.RunDistributedSimulation();

    m_heatmap->SetResults(&m_flowsheet.GetDistributedResults());
}