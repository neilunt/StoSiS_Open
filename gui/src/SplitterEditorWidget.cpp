#include "SplitterEditorWidget.h"
#include "FractionPreviewWidget.h"
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QGroupBox>
#include <QGridLayout>
#include <QFormLayout>
#include <QVBoxLayout>
#include <algorithm>

SplitterEditorWidget::SplitterEditorWidget(QWidget* parent)
    : QWidget(parent)
{
    SetupUi();
}

void SplitterEditorWidget::SetupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    // ---- Aufteilung ----------------------------------------------------
    auto* group = new QGroupBox("Aufteilung", this);
    auto* groupLayout = new QVBoxLayout(group);

    auto* modeForm = new QFormLayout();
    m_modeCombo = new QComboBox(group);
    m_modeCombo->addItem("Deterministisch (feste Anteile)");
    m_modeCombo->addItem("Stochastisch (schwankende Anteile)");
    connect(m_modeCombo, &QComboBox::currentIndexChanged, this, &SplitterEditorWidget::OnValuesChanged);
    modeForm->addRow("Modus:", m_modeCombo);
    groupLayout->addLayout(modeForm);

    m_tableWidget = new QWidget(group);
    m_table = new QGridLayout(m_tableWidget);
    m_table->setContentsMargins(0, 0, 0, 0);
    groupLayout->addWidget(m_tableWidget);

    auto* info = new QLabel(
        "Der Anteil gilt fuer alle Partikelgroessen gleich (reiner Mengenteiler). "
        "Stochastisch schwankt jeder Anteil um seinen Mittelwert mu: sigma = typische "
        "Abweichung (z.B. 0,05 = +-5 %-Punkte), tau = wie lange eine Abweichung anhaelt "
        "(0 = jeder Zeitschritt unabhaengig). Der letzte Ausgang bekommt den Rest.", group);
    info->setWordWrap(true);
    info->setStyleSheet("color: gray;");
    groupLayout->addWidget(info);

    m_warningLabel = new QLabel(group);
    m_warningLabel->setWordWrap(true);
    m_warningLabel->setStyleSheet("color: rgb(200, 40, 40);");
    groupLayout->addWidget(m_warningLabel);
    mainLayout->addWidget(group);

    // ---- Vorschau --------------------------------------------------------
    auto* previewGroup = new QGroupBox("Vorschau der Anteile", this);
    auto* previewLayout = new QVBoxLayout(previewGroup);
    m_preview = new FractionPreviewWidget(previewGroup);
    previewLayout->addWidget(m_preview, 1);
    m_previewCaption = new QLabel(previewGroup);
    m_previewCaption->setWordWrap(true);
    m_previewCaption->setStyleSheet("color: gray;");
    previewLayout->addWidget(m_previewCaption);
    mainLayout->addWidget(previewGroup, 1);

    m_applyButton = new QPushButton("Uebernehmen", this);
    connect(m_applyButton, &QPushButton::clicked, this, &SplitterEditorWidget::OnApply);
    mainLayout->addWidget(m_applyButton);
}

void SplitterEditorWidget::SetGraph(stosis::CFlowsheetGraph* graph)
{
    m_graph = graph;
    LoadNode();
}

void SplitterEditorWidget::SetNode(const std::string& nodeId)
{
    m_nodeId = nodeId;
    LoadNode();
}

void SplitterEditorWidget::LoadNode()
{
    // Tabelle komplett neu aufbauen - Splitter haben 2 oder 3 Ausgaenge.
    while (QLayoutItem* item = m_table->takeAt(0))
    {
        delete item->widget();
        delete item;
    }
    m_meanSpins.clear();
    m_sigmaSpins.clear();
    m_tauSpins.clear();
    m_restLabel = nullptr;

    stosis::CSplitterUnit* splitter = m_graph ? m_graph->GetSplitter(m_nodeId) : nullptr;
    if (!splitter || splitter->GetNumOutputs() == 0)
    {
        m_preview->Clear();
        return;
    }

    m_modeCombo->blockSignals(true);
    m_modeCombo->setCurrentIndex(splitter->IsStochastic() ? 1 : 0);
    m_modeCombo->blockSignals(false);

    m_table->addWidget(new QLabel("<b>mu</b> (Anteil)", m_tableWidget), 0, 1);
    m_table->addWidget(new QLabel("<b>sigma</b>", m_tableWidget), 0, 2);
    m_table->addWidget(new QLabel("<b>tau</b> [s]", m_tableWidget), 0, 3);

    const std::vector<double>& means = splitter->GetFractions();
    const std::vector<stosis::SSplitterFluctuation>& fluctuations = splitter->GetFluctuations();
    const size_t numOutputs = splitter->GetNumOutputs();
    for (size_t i = 0; i + 1 < numOutputs; ++i)
    {
        const int row = static_cast<int>(i) + 1;
        m_table->addWidget(new QLabel(QString("Ausgang %1").arg(i + 1), m_tableWidget), row, 0);

        auto* meanSpin = new QDoubleSpinBox(m_tableWidget);
        meanSpin->setRange(0.0, 1.0);
        meanSpin->setDecimals(4);
        meanSpin->setSingleStep(0.05);
        meanSpin->setValue(means[i]);
        m_table->addWidget(meanSpin, row, 1);
        m_meanSpins.push_back(meanSpin);

        auto* sigmaSpin = new QDoubleSpinBox(m_tableWidget);
        sigmaSpin->setRange(0.0, 1.0);
        sigmaSpin->setDecimals(4);
        sigmaSpin->setSingleStep(0.01);
        sigmaSpin->setValue(fluctuations[i].sigma);
        m_table->addWidget(sigmaSpin, row, 2);
        m_sigmaSpins.push_back(sigmaSpin);

        auto* tauSpin = new QDoubleSpinBox(m_tableWidget);
        tauSpin->setRange(0.0, 1.0e6);
        tauSpin->setDecimals(3);
        tauSpin->setSingleStep(0.5);
        tauSpin->setValue(fluctuations[i].tau);
        m_table->addWidget(tauSpin, row, 3);
        m_tauSpins.push_back(tauSpin);

        connect(meanSpin, &QDoubleSpinBox::valueChanged, this, &SplitterEditorWidget::OnValuesChanged);
        connect(sigmaSpin, &QDoubleSpinBox::valueChanged, this, &SplitterEditorWidget::OnValuesChanged);
        connect(tauSpin, &QDoubleSpinBox::valueChanged, this, &SplitterEditorWidget::OnValuesChanged);
    }

    const int restRow = static_cast<int>(numOutputs);
    m_table->addWidget(new QLabel(QString("Ausgang %1").arg(numOutputs), m_tableWidget), restRow, 0);
    m_restLabel = new QLabel(m_tableWidget);
    m_table->addWidget(m_restLabel, restRow, 1, 1, 3);

    OnValuesChanged();
}

double SplitterEditorWidget::ComputeRest() const
{
    double sum = 0.0;
    for (const QDoubleSpinBox* spin : m_meanSpins)
        sum += spin->value();
    return 1.0 - sum;
}

bool SplitterEditorWidget::IsStochasticSelected() const
{
    return m_modeCombo->currentIndex() == 1;
}

std::vector<double> SplitterEditorWidget::CurrentMeans() const
{
    std::vector<double> means;
    for (const QDoubleSpinBox* spin : m_meanSpins)
        means.push_back(spin->value());
    means.push_back(std::max(ComputeRest(), 0.0));
    return means;
}

std::vector<stosis::SSplitterFluctuation> SplitterEditorWidget::CurrentFluctuations() const
{
    std::vector<stosis::SSplitterFluctuation> fluctuations;
    for (size_t i = 0; i < m_sigmaSpins.size(); ++i)
        fluctuations.push_back({ m_sigmaSpins[i]->value(), m_tauSpins[i]->value() });
    return fluctuations;
}

void SplitterEditorWidget::OnValuesChanged()
{
    if (!m_restLabel)
        return;

    const bool stochastic = IsStochasticSelected();
    for (size_t i = 0; i < m_sigmaSpins.size(); ++i)
    {
        m_sigmaSpins[i]->setEnabled(stochastic);
        m_tauSpins[i]->setEnabled(stochastic);
    }

    const double rest = ComputeRest();
    // Kleine Rundungsreste (z.B. 1e-16) nicht als Fehler werten.
    const bool valid = rest >= -1e-9;
    m_restLabel->setText(QString::number(std::max(rest, 0.0), 'f', 4)
        + (valid ? "   (Rest, schwankt mit)" : "   - Summe > 1, bitte Anteile verringern"));
    m_restLabel->setStyleSheet(valid ? "" : "color: rgb(200, 40, 40);");
    m_applyButton->setEnabled(valid);

    const bool clippingLikely = valid && stochastic
        && stosis::CSplitterUnit::IsClippingLikely(CurrentMeans(), CurrentFluctuations());
    m_warningLabel->setText(clippingLikely
        ? "Hinweis: sigma ist so gross, dass Anteile oefter unter 0 bzw. ueber 1 laufen "
          "und abgeschnitten werden - der Mittelwert verschiebt sich dann leicht."
        : QString());
    m_warningLabel->setVisible(clippingLikely);

    UpdatePreview();
}

void SplitterEditorWidget::UpdatePreview()
{
    if (!m_graph || !m_restLabel || ComputeRest() < -1e-9)
    {
        m_preview->Clear();
        return;
    }

    // Einen Splitter mit den Editor-Werten (noch nicht uebernommen) genau
    // so laufen lassen wie in der Simulation: gleicher Seed, gleiches dt.
    // Die Anteile haengen nicht vom Eingangsstrom ab - die Vorschau zeigt
    // also exakt den Verlauf der naechsten Simulation.
    const size_t numOutputs = m_meanSpins.size() + 1;
    stosis::CSplitterUnit preview(numOutputs);
    if (!preview.SetFractions(CurrentMeans()))
    {
        m_preview->Clear();
        return;
    }
    preview.SetStochastic(IsStochasticSelected());
    preview.SetFluctuations(CurrentFluctuations());

    stosis::SSimulationGrid grid = m_graph->GetGrid();
    grid.numSizeClasses = 1;
    preview.Initialize(grid, m_graph->GetUnitSeed(m_nodeId));

    stosis::CStreamSlice input;
    input.Resize(1);
    input.SetValue(0, 0, 1.0);
    const std::vector<const stosis::CStreamSlice*> inputs = { &input };
    std::vector<stosis::CStreamSlice> outputs(numOutputs);
    std::vector<std::vector<double>> series(numOutputs);
    for (size_t k = 0; k < grid.numTimeSteps; ++k)
    {
        for (stosis::CStreamSlice& out : outputs)
            out.Resize(1);
        stosis::SStepContext context;
        context.stepIndex = k;
        context.dt = grid.GetDt();
        context.time = static_cast<double>(k) * context.dt;
        preview.Step(context, inputs, outputs);
        for (size_t i = 0; i < numOutputs; ++i)
            series[i].push_back(outputs[i].GetValue(0));
    }

    std::vector<QString> names;
    for (size_t i = 0; i < numOutputs; ++i)
        names.push_back(i + 1 < numOutputs ? QString("Ausgang %1").arg(i + 1) : QString("Rest"));
    m_preview->SetSeries(std::move(series), CurrentMeans(), std::move(names), grid.totalTime);

    m_previewCaption->setText(IsStochasticSelected()
        ? QString("Genau dieser Verlauf entsteht bei der naechsten Simulation (Seed %1, dt = %2 s). "
                  "Gestrichelt: Mittelwerte.").arg(grid.randomSeed).arg(grid.GetDt(), 0, 'g', 3)
        : QString("Deterministisch: die Anteile bleiben ueber die ganze Zeit konstant."));
}

void SplitterEditorWidget::OnApply()
{
    stosis::CSplitterUnit* splitter = m_graph ? m_graph->GetSplitter(m_nodeId) : nullptr;
    if (!splitter)
        return;

    if (!splitter->SetFractions(CurrentMeans()))
        return;
    splitter->SetStochastic(IsStochasticSelected());
    splitter->SetFluctuations(CurrentFluctuations());
    emit Applied();
}
