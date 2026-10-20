#include "ResultsHeatmapWindow.h"
#include "HeatmapWidget.h"
#include "ResultsEntryKey.h"
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include "SizeGrid.h"

// Beschriftung der Farbskala: Massenstromdichte je um (intern je m).
static QString DensityLabel(const QString& prefix, const QString& suffix = QString())
{
    return prefix + "Massenstromdichte" + suffix + " [kg/(s " + QString::fromUtf8(stosis::kSizeDisplayUnit) + ")]";
}

ResultsHeatmapWindow::ResultsHeatmapWindow(QWidget* parent)
    : QWidget(parent)
{
    setWindowTitle("Ergebnis-Heatmap");
    resize(700, 600);
    SetupUi();
}

void ResultsHeatmapWindow::SetupUi()
{
    auto* mainLayout = new QVBoxLayout(this);

    auto* controls = new QHBoxLayout();
    controls->addWidget(new QLabel("Einheit:", this));
    m_nodeCombo = new QComboBox(this);
    m_nodeCombo->setMinimumWidth(180);
    connect(m_nodeCombo, &QComboBox::currentIndexChanged, this,
        &ResultsHeatmapWindow::OnNodeSelectionChanged);
    controls->addWidget(m_nodeCombo);
    controls->addSpacing(15);

    controls->addWidget(new QLabel("Anzeige:", this));
    m_viewCombo = new QComboBox(this);
    m_viewCombo->insertItem(ViewFirstRun, "Einzellauf (Lauf 1)");
    m_viewCombo->insertItem(ViewMean, "Mittelwert");
    m_viewCombo->insertItem(ViewStdDev, "Standardabweichung");
    m_viewCombo->setCurrentIndex(ViewMean);
    m_viewCombo->setToolTip("Bei mehreren Laeufen: einzelner Lauf, Mittelwert oder Streuung je Zelle");
    connect(m_viewCombo, &QComboBox::currentIndexChanged, this, &ResultsHeatmapWindow::UpdateHeatmap);
    controls->addWidget(m_viewCombo);
    controls->addSpacing(15);

    m_compoundLabel = new QLabel("Stoff:", this);
    controls->addWidget(m_compoundLabel);
    m_compoundCombo = new QComboBox(this);
    m_compoundCombo->addItem("alle Stoffe", -1);
    connect(m_compoundCombo, &QComboBox::currentIndexChanged, this, &ResultsHeatmapWindow::UpdateHeatmap);
    controls->addWidget(m_compoundCombo);
    controls->addSpacing(15);
    // Nur sichtbar, wenn es mehr als einen Stoff gibt.
    m_compoundLabel->hide();
    m_compoundCombo->hide();

    m_timeOnXCheck = new QCheckBox("Zeit auf x-Achse", this);
    m_timeOnXCheck->setChecked(true);
    controls->addWidget(m_timeOnXCheck);

    m_logCheck = new QCheckBox("Log-Skala", this);
    controls->addWidget(m_logCheck);

    m_decadesSpin = new QSpinBox(this);
    m_decadesSpin->setRange(1, 12);
    m_decadesSpin->setValue(4);
    m_decadesSpin->setSuffix(" Dekaden");
    m_decadesSpin->setEnabled(false);
    controls->addWidget(m_decadesSpin);

    m_marginalsCheck = new QCheckBox("Randverteilungen", this);
    m_marginalsCheck->setChecked(true);
    m_marginalsCheck->setToolTip("Oben/rechts: ueber die jeweils andere Achse integriert\n"
                                 "(Massenstrom ueber die Zeit bzw. Groessenverteilung des Durchsatzes)");
    controls->addWidget(m_marginalsCheck);
    controls->addStretch(1);
    mainLayout->addLayout(controls);

    m_heatmap = new HeatmapWidget(this);
    m_heatmap->SetValueLabel(DensityLabel(""));
    m_heatmap->SetSizeUnit(stosis::kSizeDisplayFactor, QString::fromUtf8(stosis::kSizeDisplayUnit));
    m_heatmap->SetTimeOnXAxis(true);
    m_heatmap->SetShowMarginals(true);
    mainLayout->addWidget(m_heatmap, 1);

    m_totalLabel = new QLabel(this);
    mainLayout->addWidget(m_totalLabel);

    connect(m_timeOnXCheck, &QCheckBox::toggled, m_heatmap, &HeatmapWidget::SetTimeOnXAxis);
    connect(m_logCheck, &QCheckBox::toggled, this, [this](bool enabled)
        {
            m_heatmap->SetLogScale(enabled);
            m_decadesSpin->setEnabled(enabled);
        });
    connect(m_decadesSpin, &QSpinBox::valueChanged, m_heatmap, &HeatmapWidget::SetLogDecades);
    connect(m_marginalsCheck, &QCheckBox::toggled, m_heatmap, &HeatmapWidget::SetShowMarginals);
}

void ResultsHeatmapWindow::SetGraph(stosis::CFlowsheetGraph* graph)
{
    m_graph = graph;
    RefreshNodeList();
}

void ResultsHeatmapWindow::RefreshNodeList()
{
    const QString previousId = m_nodeCombo->currentData().toString();

    m_nodeCombo->blockSignals(true);
    m_nodeCombo->clear();

    int firstOutletIndex = -1;
    if (m_graph)
    {
        // Ein Eintrag pro Knoten, beim Splitter einer pro Ausgang.
        for (const stosis::SResultsEntry& entry : m_graph->GetResultsEntries())
        {
            const stosis::SFlowsheetNode* node = m_graph->FindNode(entry.nodeId);
            if (firstOutletIndex < 0 && node && node->type == stosis::ENodeType::Outlet)
                firstOutletIndex = m_nodeCombo->count();

            m_nodeCombo->addItem(QString::fromStdString(entry.label), MakeResultsKey(entry.nodeId, entry.port));
        }
    }

    // Bisherige Auswahl behalten; sonst standardmaessig das erste
    // Outlet zeigen, weil das meist das interessante Ergebnis ist.
    int index = m_nodeCombo->findData(previousId);
    if (index < 0)
        index = (firstOutletIndex >= 0) ? firstOutletIndex : (m_nodeCombo->count() > 0 ? 0 : -1);
    m_nodeCombo->setCurrentIndex(index);
    m_nodeCombo->blockSignals(false);

    UpdateHeatmap();
}

void ResultsHeatmapWindow::OnNodeSelectionChanged(int /*index*/)
{
    UpdateHeatmap();
}

void ResultsHeatmapWindow::UpdateHeatmap()
{
    const stosis::CStreamStatistics* streamStats = nullptr;
    if (m_graph && m_nodeCombo->currentIndex() >= 0)
    {
        std::string nodeId;
        size_t port = 0;
        if (ParseResultsKey(m_nodeCombo->currentData().toString(), nodeId, port))
            streamStats = m_graph->GetStreamStatistics(nodeId, port);
    }
    if (streamStats)
        UpdateCompoundList(streamStats->GetCompoundNames());
    const int compound = m_compoundCombo->currentData().toInt(); // -1 = alle Stoffe
    const bool singleCompound = streamStats && compound >= 0 && streamStats->GetNumCompounds() > 1
        && static_cast<size_t>(compound) < streamStats->GetNumCompounds();
    const stosis::CEnsembleStatistics* stats = !streamStats ? nullptr
        : (singleCompound ? &streamStats->GetCompound(static_cast<size_t>(compound)) : &streamStats->GetTotal());
    const QString compoundSuffix = singleCompound ? " (" + m_compoundCombo->currentText() + ")" : QString();
    if (!stats || stats->IsEmpty())
    {
        m_heatmap->ClearMarginalOverride();
        m_heatmap->SetResults(nullptr);
        m_totalLabel->setText("Keine Ergebnisse - Simulation noch nicht ausgefuehrt?");
        return;
    }

    const int view = m_viewCombo->currentIndex();
    const stosis::CSimulationResults* results = &stats->GetMean();
    if (view == ViewFirstRun)
        results = &stats->GetFirstRun();
    else if (view == ViewStdDev)
        results = &stats->GetStdDev();

    if (view == ViewStdDev)
    {
        // Randverteilungen = echte Streuung ueber die Laeufe (nicht das
        // Integral der sigma-Matrix).
        const auto& runSeries = stats->GetRunTimeSeries();
        const auto& runSizes = stats->GetRunSizeDistributions();
        std::vector<double> overSize(results->GetNumTimeSteps());
        std::vector<double> overTime(results->GetNumSizeClasses());
        double mean = 0.0;
        for (size_t t = 0; t < overSize.size(); ++t)
            stosis::CEnsembleStatistics::MeanAndStdDev(stosis::CEnsembleStatistics::Column(runSeries, t), mean, overSize[t]);
        for (size_t s = 0; s < overTime.size(); ++s)
            stosis::CEnsembleStatistics::MeanAndStdDev(stosis::CEnsembleStatistics::Column(runSizes, s), mean, overTime[s]);
        m_heatmap->SetMarginalOverride(std::move(overSize), std::move(overTime), "Std. ");
        m_heatmap->SetValueLabel(DensityLabel("Std. ", compoundSuffix));
    }
    else
    {
        // Mittelwert und Einzellauf: Integral der Matrix ist korrekt
        // (der Mittelwert ist linear).
        m_heatmap->ClearMarginalOverride();
        m_heatmap->SetValueLabel(DensityLabel("", compoundSuffix));
    }
    m_heatmap->SetResults(results);

    double totalMean = 0.0, totalStd = 0.0;
    stosis::CEnsembleStatistics::MeanAndStdDev(stats->GetRunTotals(), totalMean, totalStd);
    const QString period = QString::number(results->GetTotalTime(), 'g', 4);
    if (stats->GetNumRuns() > 1)
    {
        m_totalLabel->setText(QString("Durchgesetzte Masse%5 im Zeitraum 0 - %1: %2 +- %3   "
                                      "(Mittelwert +- Standardabweichung ueber N = %4 Laeufe)")
            .arg(period).arg(totalMean, 0, 'g', 6).arg(totalStd, 0, 'g', 3).arg(stats->GetNumRuns()).arg(compoundSuffix));
    }
    else
    {
        m_totalLabel->setText(QString("Durchgesetzte Masse%3 im Zeitraum 0 - %1: %2   "
                                      "(Integral ueber Zeit und Groesse, 1 Lauf)")
            .arg(period).arg(totalMean, 0, 'g', 6).arg(compoundSuffix));
    }
}

void ResultsHeatmapWindow::UpdateCompoundList(const std::vector<std::string>& names)
{
    // Nur neu aufbauen, wenn sich die Stoffe geaendert haben.
    QStringList wanted;
    if (names.size() > 1)
        for (const std::string& name : names)
            wanted << QString::fromStdString(name);
    QStringList current;
    for (int i = 1; i < m_compoundCombo->count(); ++i)
        current << m_compoundCombo->itemText(i);
    if (wanted != current)
    {
        const QString previous = m_compoundCombo->currentText();
        m_compoundCombo->blockSignals(true);
        m_compoundCombo->clear();
        m_compoundCombo->addItem("alle Stoffe", -1);
        for (int i = 0; i < wanted.size(); ++i)
            m_compoundCombo->addItem(wanted[i], i);
        const int index = m_compoundCombo->findText(previous);
        m_compoundCombo->setCurrentIndex(index >= 0 ? index : 0);
        m_compoundCombo->blockSignals(false);
    }
    m_compoundLabel->setVisible(!wanted.isEmpty());
    m_compoundCombo->setVisible(!wanted.isEmpty());
}
