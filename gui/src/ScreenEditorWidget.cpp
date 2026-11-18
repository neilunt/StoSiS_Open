#include "ScreenEditorWidget.h"
#include "GradeEfficiencyPreview.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QGroupBox>
#include <QFormLayout>
#include <QVBoxLayout>
#include <algorithm>

ScreenEditorWidget::ScreenEditorWidget(QWidget* parent)
    : QWidget(parent)
{
    SetupUi();
}

void ScreenEditorWidget::SetupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    auto* group = new QGroupBox("Trennkurve", this);
    auto* form = new QFormLayout(group);

    m_modelCombo = new QComboBox(group);
    m_modelCombo->addItem("Plitt", static_cast<int>(stosis::EScreenModel::Plitt));
    m_modelCombo->addItem("Molerus-Hoffmann", static_cast<int>(stosis::EScreenModel::MolerusHoffmann));
    m_modelCombo->addItem("Teipel-Hennig", static_cast<int>(stosis::EScreenModel::TeipelHennig));
    form->addRow("Modell:", m_modelCombo);

    m_cutSizeSpin = new QDoubleSpinBox(group);
    m_cutSizeSpin->setRange(1e-3, 1e7);
    m_cutSizeSpin->setDecimals(3);
    m_cutSizeSpin->setSuffix(QString(" ") + QString::fromUtf8(stosis::kSizeDisplayUnit));
    form->addRow("Trenngroesse x_cut:", m_cutSizeSpin);

    m_alphaSpin = new QDoubleSpinBox(group);
    m_alphaSpin->setRange(0.0, 100.0);
    m_alphaSpin->setDecimals(3);
    m_alphaSpin->setSingleStep(0.5);
    form->addRow("Trennschaerfe alpha:", m_alphaSpin);

    m_betaLabel = new QLabel("beta:", group);
    m_betaSpin = new QDoubleSpinBox(group);
    m_betaSpin->setRange(0.001, 100.0);
    m_betaSpin->setDecimals(3);
    m_betaSpin->setSingleStep(0.1);
    form->addRow(m_betaLabel, m_betaSpin);

    m_offsetLabel = new QLabel("Offset (Feingut im Grobgut):", group);
    m_offsetSpin = new QDoubleSpinBox(group);
    m_offsetSpin->setRange(0.0, 1.0);
    m_offsetSpin->setDecimals(3);
    m_offsetSpin->setSingleStep(0.01);
    form->addRow(m_offsetLabel, m_offsetSpin);

    m_formulaLabel = new QLabel(group);
    m_formulaLabel->setTextFormat(Qt::RichText);
    m_formulaLabel->setWordWrap(true);
    m_formulaLabel->setStyleSheet(kFormulaLabelStyle);
    form->addRow(m_formulaLabel);

    m_errorLabel = new QLabel(group);
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setStyleSheet("color: rgb(200, 40, 40);");
    m_errorLabel->setVisible(false);
    form->addRow(m_errorLabel);
    mainLayout->addWidget(group);

    auto* previewGroup = new QGroupBox("Vorschau", this);
    auto* previewLayout = new QVBoxLayout(previewGroup);
    m_preview = new GradeEfficiencyPreview("T(d) ins Grobgut", previewGroup);
    previewLayout->addWidget(m_preview, 1);
    mainLayout->addWidget(previewGroup, 1);

    m_applyButton = new QPushButton("Uebernehmen", this);
    mainLayout->addWidget(m_applyButton);

    connect(m_modelCombo, &QComboBox::currentIndexChanged, this, &ScreenEditorWidget::OnValuesChanged);
    for (QDoubleSpinBox* spin : { m_cutSizeSpin, m_alphaSpin, m_betaSpin, m_offsetSpin })
        connect(spin, &QDoubleSpinBox::valueChanged, this, &ScreenEditorWidget::OnValuesChanged);
    connect(m_applyButton, &QPushButton::clicked, this, &ScreenEditorWidget::OnApply);
}

void ScreenEditorWidget::SetGraph(stosis::CFlowsheetGraph* graph)
{
    m_graph = graph;
    LoadNode();
}

void ScreenEditorWidget::SetNode(const std::string& nodeId)
{
    m_nodeId = nodeId;
    LoadNode();
}

void ScreenEditorWidget::LoadNode()
{
    const stosis::CScreenUnit* screen = m_graph ? m_graph->GetScreen(m_nodeId) : nullptr;
    const stosis::SScreenSettings settings = screen ? screen->GetSettings() : stosis::SScreenSettings{};
    const QSignalBlocker b0(m_modelCombo), b1(m_cutSizeSpin), b2(m_alphaSpin), b3(m_betaSpin), b4(m_offsetSpin);
    m_modelCombo->setCurrentIndex(std::max(0, m_modelCombo->findData(static_cast<int>(settings.model))));
    m_cutSizeSpin->setValue(settings.cutSize * stosis::kSizeDisplayFactor);
    m_alphaSpin->setValue(settings.alpha);
    m_betaSpin->setValue(settings.beta);
    m_offsetSpin->setValue(settings.offset);
    OnValuesChanged();
}

stosis::SScreenSettings ScreenEditorWidget::CurrentSettings() const
{
    stosis::SScreenSettings settings;
    settings.model = static_cast<stosis::EScreenModel>(m_modelCombo->currentData().toInt());
    settings.cutSize = m_cutSizeSpin->value() / stosis::kSizeDisplayFactor;
    settings.alpha = m_alphaSpin->value();
    settings.beta = m_betaSpin->value();
    settings.offset = m_offsetSpin->value();
    return settings;
}

void ScreenEditorWidget::OnValuesChanged()
{
    const stosis::SScreenSettings settings = CurrentSettings();
    const bool teipelHennig = settings.model == stosis::EScreenModel::TeipelHennig;
    m_betaLabel->setVisible(teipelHennig);
    m_betaSpin->setVisible(teipelHennig);
    m_offsetLabel->setVisible(teipelHennig);
    m_offsetSpin->setVisible(teipelHennig);
    m_formulaLabel->setText(GradeEfficiencyFormulaHtml(settings.model,
        "<i>x</i> = <i>d</i> / <i>x</i><sub>cut</sub>", "Partikelgroesse durch Trenngroesse"));

    const QString problems = QString::fromStdString(CurrentSettings().Check());
    m_errorLabel->setText(problems);
    m_errorLabel->setVisible(!problems.isEmpty());
    m_applyButton->setEnabled(m_graph && m_graph->GetScreen(m_nodeId) && problems.isEmpty());
    UpdatePreview();
}

void ScreenEditorWidget::UpdatePreview()
{
    if (!m_graph)
    {
        m_preview->SetCurves({}, 0.0, 0.0);
        return;
    }
    const stosis::SSimulationGrid& grid = m_graph->GetGrid();
    const stosis::SScreenSettings settings = CurrentSettings();
    std::vector<GradeEfficiencyPreview::SCurve> curves;
    if (settings.Check().empty())
        curves.push_back({ QString(), [settings](double size) { return settings.GradeEfficiency(size); },
            settings.cutSize, "x_cut" });
    m_preview->SetCurves(std::move(curves), grid.sizeMin, grid.sizeMax);
}

void ScreenEditorWidget::OnApply()
{
    stosis::CScreenUnit* screen = m_graph ? m_graph->GetScreen(m_nodeId) : nullptr;
    if (!screen)
        return;
    if (!screen->SetSettings(CurrentSettings()))
    {
        m_errorLabel->setText(QString::fromStdString(CurrentSettings().Check()));
        m_errorLabel->setVisible(true);
        return;
    }
    emit Applied();
}
