#include "CompoundSeparatorEditorWidget.h"
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QGroupBox>
#include <QHeaderView>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QStringList>
#include <algorithm>

CompoundSeparatorEditorWidget::CompoundSeparatorEditorWidget(QWidget* parent)
    : QWidget(parent)
{
    SetupUi();
}

void CompoundSeparatorEditorWidget::SetupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    auto* group = new QGroupBox("Anteil je Stoff in Ausgang 1 (abgetrennt)", this);
    auto* layout = new QVBoxLayout(group);
    auto* intro = new QLabel("Jeder Stoff geht mit seinem Anteil in Ausgang 1, unabhaengig von der Groesse "
                             "(z. B. Magnet: Eisen 0,95). Der Rest und alle Stoffe mit 0 gehen in Ausgang 2. "
                             "Oben stehen die Stoffe des Flowsheets.", group);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    m_table = new QTableWidget(0, 2, group);
    m_table->setHorizontalHeaderLabels({ "Stoff", "Anteil in Ausgang 1" });
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(m_table, 1);

    m_summaryLabel = new QLabel(group);
    m_summaryLabel->setWordWrap(true);
    layout->addWidget(m_summaryLabel);

    m_errorLabel = new QLabel(group);
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setStyleSheet("color: rgb(200, 40, 40);");
    m_errorLabel->setVisible(false);
    layout->addWidget(m_errorLabel);
    mainLayout->addWidget(group, 1);

    m_applyButton = new QPushButton("Uebernehmen", this);
    mainLayout->addWidget(m_applyButton);
    connect(m_applyButton, &QPushButton::clicked, this, &CompoundSeparatorEditorWidget::OnApply);
}

void CompoundSeparatorEditorWidget::SetGraph(stosis::CFlowsheetGraph* graph)
{
    m_graph = graph;
    LoadNode();
}

void CompoundSeparatorEditorWidget::SetMaterialDatabase(const stosis::CMaterialDatabase* materials)
{
    m_materials = materials;
    LoadNode();
}

void CompoundSeparatorEditorWidget::SetNode(const std::string& nodeId)
{
    m_nodeId = nodeId;
    LoadNode();
}

void CompoundSeparatorEditorWidget::LoadNode()
{
    const stosis::CCompoundSeparatorUnit* separator = m_graph ? m_graph->GetCompoundSeparator(m_nodeId) : nullptr;
    const stosis::SCompoundSeparatorSettings settings = separator ? separator->GetSettings() : stosis::SCompoundSeparatorSettings{};

    // Rows: compounds of the flowsheet, then the material file, then names
    // from the settings that are in neither.
    const std::vector<std::string> active = m_graph ? m_graph->GetActiveCompounds() : std::vector<std::string>{};
    m_rowNames = active;
    const auto add = [this](const std::string& name)
    {
        if (!name.empty() && std::find(m_rowNames.begin(), m_rowNames.end(), name) == m_rowNames.end())
            m_rowNames.push_back(name);
    };
    if (m_materials)
        for (const stosis::SMaterial& material : m_materials->GetMaterials())
            add(material.name);
    for (const auto& entry : settings.shares)
        add(entry.first);

    m_table->setRowCount(static_cast<int>(m_rowNames.size()));
    m_rowSpins.clear();
    for (size_t row = 0; row < m_rowNames.size(); ++row)
    {
        const std::string& name = m_rowNames[row];
        QString text = QString::fromStdString(name);
        const stosis::SMaterial* material = m_materials ? m_materials->FindByName(name) : nullptr;
        if (std::find(active.begin(), active.end(), name) != active.end())
            text += "  (im Flowsheet)";
        if (!material)
            text += "  (nicht in der Materialdatei)";
        else if (material->state == "gas")
            text += "  (Gas)";
        else if (material->state == "liquid")
            text += "  (fluessig)";
        auto* item = new QTableWidgetItem(text);
        if (row < active.size())
        {
            QFont bold = item->font();
            bold.setBold(true);
            item->setFont(bold);
        }
        m_table->setItem(static_cast<int>(row), 0, item);

        auto* spin = new QDoubleSpinBox(m_table);
        spin->setRange(0.0, 1.0);
        spin->setDecimals(3);
        spin->setSingleStep(0.05);
        spin->setValue(settings.ShareOf(name));
        connect(spin, &QDoubleSpinBox::valueChanged, this, &CompoundSeparatorEditorWidget::OnValuesChanged);
        m_table->setCellWidget(static_cast<int>(row), 1, spin);
        m_rowSpins.push_back(spin);
    }
    OnValuesChanged();
}

stosis::SCompoundSeparatorSettings CompoundSeparatorEditorWidget::CurrentSettings() const
{
    stosis::SCompoundSeparatorSettings settings;
    for (size_t row = 0; row < m_rowNames.size() && row < m_rowSpins.size(); ++row)
        if (m_rowSpins[row]->value() > 0.0)
            settings.shares.push_back({ m_rowNames[row], m_rowSpins[row]->value() });
    return settings;
}

void CompoundSeparatorEditorWidget::OnValuesChanged()
{
    const stosis::SCompoundSeparatorSettings settings = CurrentSettings();
    QStringList parts;
    for (const auto& entry : settings.shares)
        parts << QString("%1 %2").arg(QString::fromStdString(entry.first), QString::number(entry.second, 'g', 4).replace('.', ','));
    m_summaryLabel->setText(parts.isEmpty() ? QString("Alle Stoffe gehen in Ausgang 2 (Rest).")
                                            : "In Ausgang 1: " + parts.join(", ") + "; alles andere in Ausgang 2.");
    const QString problems = QString::fromStdString(settings.Check());
    m_errorLabel->setText(problems);
    m_errorLabel->setVisible(!problems.isEmpty());
    m_applyButton->setEnabled(m_graph && m_graph->GetCompoundSeparator(m_nodeId) && problems.isEmpty());
}

void CompoundSeparatorEditorWidget::OnApply()
{
    stosis::CCompoundSeparatorUnit* separator = m_graph ? m_graph->GetCompoundSeparator(m_nodeId) : nullptr;
    if (!separator)
        return;
    const stosis::SCompoundSeparatorSettings settings = CurrentSettings();
    if (!separator->SetSettings(settings))
    {
        m_errorLabel->setText(QString::fromStdString(settings.Check()));
        m_errorLabel->setVisible(true);
        return;
    }
    emit Applied();
}
