#include "ProcessNodeEditorWidget.h"
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QVBoxLayout>

ProcessNodeEditorWidget::ProcessNodeEditorWidget(QWidget* parent)
    : QWidget(parent)
{
    SetupUi();
}

void ProcessNodeEditorWidget::SetupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    m_paramGroup = new QGroupBox("Modell & Parameter", this);
    m_paramLayout = new QFormLayout(m_paramGroup);

    m_modelCombo = new QComboBox(m_paramGroup);
    connect(m_modelCombo, &QComboBox::currentIndexChanged, this, &ProcessNodeEditorWidget::OnModelTypeChanged);
    m_paramLayout->addRow("Modelltyp:", m_modelCombo);

    mainLayout->addWidget(m_paramGroup);

    m_applyButton = new QPushButton("Uebernehmen", this);
    connect(m_applyButton, &QPushButton::clicked, this, &ProcessNodeEditorWidget::OnApply);
    mainLayout->addWidget(m_applyButton);

    mainLayout->addStretch();
}

void ProcessNodeEditorWidget::SetGraph(stosis::CFlowsheetGraph* graph)
{
    m_graph = graph;
    LoadNode();
}

void ProcessNodeEditorWidget::SetNode(const std::string& nodeId)
{
    m_nodeId = nodeId;
    LoadNode();
}

void ProcessNodeEditorWidget::LoadNode()
{
    m_modelCombo->blockSignals(true);
    m_modelCombo->clear();

    stosis::IStreamProcessor* processor = m_graph ? m_graph->GetProcessor(m_nodeId) : nullptr;
    if (processor)
    {
        for (const std::string& name : m_graph->GetProcessorRegistry().GetAvailableTypeNames())
            m_modelCombo->addItem(QString::fromStdString(name));

        // Aktuell aktiven Typ in der Liste vormarkieren, ohne dass dabei
        // OnModelTypeChanged (und damit ein ungewollter Modellwechsel)
        // ausgeloest wird - deshalb sind die Signale hier blockiert.
        const QString currentType = QString::fromStdString(processor->GetTypeName());
        const int typeIndex = m_modelCombo->findText(currentType);
        if (typeIndex >= 0)
            m_modelCombo->setCurrentIndex(typeIndex);
    }
    m_modelCombo->blockSignals(false);

    RebuildParameterPanel();
}

void ProcessNodeEditorWidget::OnModelTypeChanged(int index)
{
    if (index < 0 || !m_graph || !m_graph->GetProcessor(m_nodeId))
        return;

    const std::string typeName = m_modelCombo->itemText(index).toStdString();

    // Erzeugt eine NEUE Prozessor-Instanz - alte Zeiger auf ihre
    // Ergebnisse werden dabei ungueltig, deshalb muss danach neu
    // simuliert werden (ueber "Simulieren" im Hauptfenster).
    m_graph->SetProcessorType(m_nodeId, typeName);
    RebuildParameterPanel();
    emit ModelTypeChanged();
}

void ProcessNodeEditorWidget::RebuildParameterPanel()
{
    for (auto& entry : m_paramSpins)
        m_paramLayout->removeRow(entry.spin);
    m_paramSpins.clear();

    stosis::IStreamProcessor* processor = m_graph ? m_graph->GetProcessor(m_nodeId) : nullptr;
    if (!processor)
        return;

    for (const stosis::SParameterDescriptor& desc : processor->GetParameterDescriptors())
    {
        // Angezeigt wird Wert * displayFactor (Groessen in um statt m).
        auto* spin = new QDoubleSpinBox(m_paramGroup);
        if (desc.displayFactor != 1.0)
            spin->setDecimals(4);
        spin->setRange(desc.minValue * desc.displayFactor, desc.maxValue * desc.displayFactor);
        spin->setValue(processor->GetParameterValue(desc.id) * desc.displayFactor);
        m_paramLayout->addRow(QString::fromStdString(desc.label) + ":", spin);
        m_paramSpins.push_back({ desc.id, spin, desc.displayFactor });
    }
}

void ProcessNodeEditorWidget::OnApply()
{
    stosis::IStreamProcessor* processor = m_graph ? m_graph->GetProcessor(m_nodeId) : nullptr;
    if (!processor)
        return;

    for (const SParamSpin& entry : m_paramSpins)
        processor->SetParameter(entry.id, entry.spin->value() / entry.displayFactor);

    emit Applied();
}
