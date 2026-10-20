#include "ModelPanelWidget.h"
#include "HeatmapWidget.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QLabel>

static const QString kDiffOptionName = "Differenz: Spalte 1 minus Spalte 2";

ModelPanelWidget::ModelPanelWidget(stosis::CTransportUnitRegistry* registry,
    bool offerDiffOption,
    QWidget* parent)
    : QWidget(parent)
    , m_registry(registry)
    , m_offerDiffOption(offerDiffOption)
{
    auto* mainLayout = new QVBoxLayout(this);

    m_modelParamGroup = new QGroupBox("Modell", this);
    m_modelParamLayout = new QFormLayout(m_modelParamGroup);

    m_modelCombo = new QComboBox(m_modelParamGroup);
    for (const auto& name : m_registry->GetAvailableTypeNames())
        m_modelCombo->addItem(QString::fromStdString(name));

    if (m_offerDiffOption)
        m_modelCombo->addItem(kDiffOptionName);

    connect(m_modelCombo, &QComboBox::currentIndexChanged,
        this, &ModelPanelWidget::OnModelChanged);
    m_modelParamLayout->addRow("Typ:", m_modelCombo);

    mainLayout->addWidget(m_modelParamGroup);

    m_heatmap = new HeatmapWidget(this);
    mainLayout->addWidget(m_heatmap, 1);

    RebuildParameterPanel();
}

bool ModelPanelWidget::IsDiffMode() const
{
    return m_offerDiffOption &&
        m_modelCombo->currentText() == kDiffOptionName;
}

void ModelPanelWidget::SetGridConfig(size_t numClasses, double sizeMin, double sizeMax,
    size_t numTimeSteps, double totalTime,
    double d0, double sigma0)
{
    if (numClasses != m_numClasses || sizeMin != m_sizeMin || sizeMax != m_sizeMax ||
        numTimeSteps != m_numTimeSteps || totalTime != m_totalTime ||
        d0 != m_d0 || sigma0 != m_sigma0)
    {
        m_needsRecompute = true;
    }
    m_numClasses = numClasses;
    m_sizeMin = sizeMin;
    m_sizeMax = sizeMax;
    m_numTimeSteps = numTimeSteps;
    m_totalTime = totalTime;
    m_d0 = d0;
    m_sigma0 = sigma0;
}

bool ModelPanelWidget::RunSimulation()
{
    if (IsDiffMode())
        return false;

    if (!m_needsRecompute)
        return true;

    m_flowsheet.ConfigureGrid(m_numClasses, m_sizeMin, m_sizeMax,
        m_numTimeSteps, m_totalTime);
    m_flowsheet.SetInitialGaussian(m_d0, m_sigma0, 1.0);

    for (const auto& entry : m_modelParamSpins)
        m_flowsheet.SetTransportUnitParameter(entry.first, entry.second->value());

    m_flowsheet.RunDistributedSimulation();
    m_heatmap->SetColorMode(EColorMode::Sequential);
    m_heatmap->SetResults(&m_flowsheet.GetDistributedResults());
    m_heatmap->SetValueLabel(QString::fromStdString(m_flowsheet.GetTransportUnitResultLabel()));

    m_needsRecompute = false;
    return true;
}

void ModelPanelWidget::SetExternalResults(const stosis::CSimulationResults* results,
    bool divergingColorScale)
{
    m_heatmap->SetColorMode(divergingColorScale
        ? EColorMode::Diverging
        : EColorMode::Sequential);
    m_heatmap->SetValueLabel("Differenz");
    m_heatmap->SetResults(results);
}

const stosis::CSimulationResults* ModelPanelWidget::GetResults() const
{
    if (IsDiffMode())
        return nullptr;
    return &m_flowsheet.GetDistributedResults();
}

void ModelPanelWidget::OnModelChanged(int /*index*/)
{
    m_heatmap->SetResults(nullptr);
    RebuildParameterPanel();
    emit ModelChanged();
}

void ModelPanelWidget::RebuildParameterPanel()
{
    const std::string selected = m_modelCombo->currentText().toStdString();

    if (!IsDiffMode())
    {
        m_flowsheet.SelectTransportUnit(selected);
        m_needsRecompute = true;
    }

    for (auto& entry : m_modelParamSpins)
        m_modelParamLayout->removeRow(entry.second);
    m_modelParamSpins.clear();

    if (IsDiffMode())
    {
        auto* infoLabel = new QLabel("Wird automatisch berechnet.", m_modelParamGroup);
        m_modelParamLayout->addRow(infoLabel);
        return;
    }

    for (const auto& desc : m_flowsheet.GetTransportUnitParameters())
    {
        auto* spin = new QDoubleSpinBox(m_modelParamGroup);
        spin->setRange(desc.minValue, desc.maxValue);
        spin->setValue(desc.defaultValue);
        m_modelParamLayout->addRow(QString::fromStdString(desc.label) + ":", spin);
        m_modelParamSpins.emplace_back(desc.id, spin);
        connect(spin, &QDoubleSpinBox::valueChanged, this, &ModelPanelWidget::MarkDirty);
    }
}