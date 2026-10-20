#include "ResultsCurveWindow.h"
#include "CurveWidget.h"
#include "ResultsEntryKey.h"
#include <QListWidget>
#include <QComboBox>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSet>
#include <algorithm>

namespace
{
    // So viele Einzellaeufe werden hoechstens gezeichnet - mehr sind
    // optisch ohnehin nur noch eine Flaeche und kosten Zeichenzeit.
    constexpr size_t kMaxDrawnRuns = 100;
}

ResultsCurveWindow::ResultsCurveWindow(QWidget* parent)
    : QWidget(parent)
{
    setWindowTitle("Ergebnis-Vergleich: Kurven");
    resize(700, 600);
    SetupUi();
}

void ResultsCurveWindow::SetupUi()
{
    auto* mainLayout = new QVBoxLayout(this);

    mainLayout->addWidget(new QLabel("Einheiten auswaehlen:", this));

    m_nodeList = new QListWidget(this);
    m_nodeList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    connect(m_nodeList, &QListWidget::itemSelectionChanged, this,
        &ResultsCurveWindow::OnSelectionChanged);
    m_nodeList->setMaximumHeight(90);
    mainLayout->addWidget(m_nodeList);

    auto* quantityRow = new QHBoxLayout();
    quantityRow->addWidget(new QLabel("Groesse:", this));
    m_quantityCombo = new QComboBox(this);
    m_quantityCombo->insertItem(QuantityMassFlow, "Massenstrom");
    m_quantityCombo->insertItem(QuantityTemperature, "Temperatur");
    m_quantityCombo->insertItem(QuantityPressure, "Druck");
    connect(m_quantityCombo, &QComboBox::currentIndexChanged, this, &ResultsCurveWindow::UpdateCurves);
    quantityRow->addWidget(m_quantityCombo);
    quantityRow->addSpacing(15);
    quantityRow->addWidget(new QLabel("Stoff:", this));
    m_compoundCombo = new QComboBox(this);
    m_compoundCombo->setMinimumWidth(120);
    m_compoundCombo->addItem("alle Stoffe", -1);
    m_compoundCombo->setToolTip("Massenstrom aller Stoffe zusammen oder eines einzelnen Stoffs\n"
                                "(mehrere Stoffe gibt es, wenn Inlets verschiedene Stoffe liefern).");
    connect(m_compoundCombo, &QComboBox::currentIndexChanged, this, &ResultsCurveWindow::UpdateCurves);
    quantityRow->addWidget(m_compoundCombo);
    quantityRow->addStretch(1);
    mainLayout->addLayout(quantityRow);

    auto* controls = new QHBoxLayout();
    controls->addWidget(new QLabel("Streuband:", this));
    m_bandCombo = new QComboBox(this);
    m_bandCombo->insertItem(BandNone, "keins");
    m_bandCombo->insertItem(BandOneSigma, "Mittelwert +- 1 sigma");
    m_bandCombo->insertItem(BandTwoSigma, "Mittelwert +- 2 sigma");
    m_bandCombo->insertItem(BandPercentile, "5 - 95 %-Perzentil");
    m_bandCombo->setCurrentIndex(BandOneSigma);
    m_bandCombo->setToolTip("Streuung ueber alle Laeufe. Perzentile sind erst ab etwa 20 Laeufen aussagekraeftig.");
    connect(m_bandCombo, &QComboBox::currentIndexChanged, this, &ResultsCurveWindow::UpdateCurves);
    controls->addWidget(m_bandCombo);
    controls->addSpacing(15);

    m_runsCheck = new QCheckBox("Einzellaeufe zeigen", this);
    connect(m_runsCheck, &QCheckBox::toggled, this, &ResultsCurveWindow::UpdateCurves);
    controls->addWidget(m_runsCheck);
    controls->addStretch(1);
    mainLayout->addLayout(controls);

    m_curveWidget = new CurveWidget(this);
    // CurveWidget verlangt sonst 300 px Mindesthoehe - zusammen mit der
    // Knotenliste waere das Fenster dann hoeher als vorgesehen.
    m_curveWidget->setMinimumHeight(150);
    mainLayout->addWidget(m_curveWidget, 1);
}

void ResultsCurveWindow::SetGraph(stosis::CFlowsheetGraph* graph)
{
    m_graph = graph;
    RefreshNodeList();
}

void ResultsCurveWindow::RefreshNodeList()
{
    // Markierung ueber den Neuaufbau hinweg behalten (z.B. wenn im
    // Flowsheet ein Knoten dazukommt).
    QSet<QString> selectedKeys;
    for (QListWidgetItem* item : m_nodeList->selectedItems())
        selectedKeys.insert(item->data(Qt::UserRole).toString());

    m_nodeList->blockSignals(true);
    m_nodeList->clear();
    if (m_graph)
    {
        // Ein Eintrag pro Knoten, beim Splitter einer pro Ausgang.
        for (const stosis::SResultsEntry& entry : m_graph->GetResultsEntries())
        {
            auto* item = new QListWidgetItem(QString::fromStdString(entry.label));
            const QString key = MakeResultsKey(entry.nodeId, entry.port);
            item->setData(Qt::UserRole, key);
            m_nodeList->addItem(item);
            item->setSelected(selectedKeys.contains(key));
        }
    }
    m_nodeList->blockSignals(false);
    UpdateCurves();
}

void ResultsCurveWindow::OnSelectionChanged()
{
    UpdateCurves();
}

void ResultsCurveWindow::UpdateCurves()
{
    if (!m_graph)
        return;

    const int band = m_bandCombo->currentIndex();
    const int quantity = m_quantityCombo->currentIndex();
    int compound = m_compoundCombo->currentData().toInt(); // -1 = alle Stoffe
    bool compoundListUpdated = false;
    std::vector<SCurveSeries> series;
    double totalTime = 1.0;
    size_t numTimeSteps = 0;
    size_t numRuns = 0;
    for (QListWidgetItem* item : m_nodeList->selectedItems())
    {
        std::string nodeId;
        size_t port = 0;
        if (!ParseResultsKey(item->data(Qt::UserRole).toString(), nodeId, port))
            continue;

        const stosis::CStreamStatistics* streamStats = m_graph->GetStreamStatistics(nodeId, port);
        if (!streamStats || streamStats->GetTotal().IsEmpty())
            continue;
        if (!compoundListUpdated)
        {
            UpdateCompoundList(streamStats->GetCompoundNames());
            compound = m_compoundCombo->currentData().toInt(); // Liste kann sich geaendert haben
            compoundListUpdated = true;
        }

        // Je Lauf eine Zeitreihe: Massenstrom (Integral ueber die Groesse,
        // mit Delta d), Temperatur oder Druck.
        const bool singleCompound = quantity == QuantityMassFlow && compound >= 0
            && static_cast<size_t>(compound) < streamStats->GetNumCompounds() && streamStats->GetNumCompounds() > 1;
        const std::vector<std::vector<double>>& runs =
            quantity == QuantityTemperature ? streamStats->GetRunTemperatures()
            : quantity == QuantityPressure ? streamStats->GetRunPressures()
            : (singleCompound ? streamStats->GetCompound(static_cast<size_t>(compound)) : streamStats->GetTotal()).GetRunTimeSeries();
        if (runs.empty())
            continue;
        totalTime = streamStats->GetTotal().GetMean().GetTotalTime();
        numTimeSteps = runs.front().size();
        numRuns = runs.size();

        SCurveSeries curve;
        curve.name = item->text();
        if (singleCompound)
            curve.name += " - " + m_compoundCombo->currentText();
        curve.values.resize(numTimeSteps);
        const bool withBand = band != BandNone && runs.size() > 1;
        if (withBand)
        {
            curve.lower.resize(numTimeSteps);
            curve.upper.resize(numTimeSteps);
        }
        for (size_t t = 0; t < numTimeSteps; ++t)
        {
            const std::vector<double> column = stosis::CEnsembleStatistics::Column(runs, t);
            double mean = 0.0, stdDev = 0.0;
            stosis::CEnsembleStatistics::MeanAndStdDev(column, mean, stdDev);
            curve.values[t] = mean;
            if (!withBand)
                continue;
            if (band == BandPercentile)
            {
                curve.lower[t] = stosis::CEnsembleStatistics::Quantile(column, 0.05);
                curve.upper[t] = stosis::CEnsembleStatistics::Quantile(column, 0.95);
            }
            else
            {
                const double k = (band == BandTwoSigma) ? 2.0 : 1.0;
                // Ein Massenstrom ist nie negativ - das Band auch nicht.
                curve.lower[t] = std::max(0.0, mean - k * stdDev);
                curve.upper[t] = mean + k * stdDev;
            }
        }
        if (m_runsCheck->isChecked() && runs.size() > 1)
            curve.runs.assign(runs.begin(), runs.begin() + static_cast<std::ptrdiff_t>(std::min(runs.size(), kMaxDrawnRuns)));

        series.push_back(std::move(curve));
    }

    QString caption;
    if (numRuns > 1)
    {
        caption = QString("Linie: Mittelwert\nN = %1 Laeufe").arg(numRuns);
        if (m_runsCheck->isChecked() && numRuns > kMaxDrawnRuns)
            caption += QString("\n(%1 Laeufe gezeichnet)").arg(kMaxDrawnRuns);
    }
    else if (numRuns == 1)
    {
        caption = "1 Lauf";
    }

    if (quantity == QuantityTemperature)
        m_curveWidget->SetValueAxis("Temperatur [K]", false);
    else if (quantity == QuantityPressure)
        m_curveWidget->SetValueAxis("Druck [Pa]", false);
    else
        m_curveWidget->SetValueAxis("Massenstrom", true);
    m_compoundCombo->setEnabled(quantity == QuantityMassFlow && m_compoundCombo->count() > 1);
    m_curveWidget->SetTimeAxis(totalTime, numTimeSteps);
    m_curveWidget->SetCaption(caption);
    m_curveWidget->SetSeries(std::move(series));
}

void ResultsCurveWindow::UpdateCompoundList(const std::vector<std::string>& names)
{
    // Nur neu aufbauen, wenn sich die Stoffe geaendert haben.
    QStringList wanted;
    if (names.size() > 1)
        for (const std::string& name : names)
            wanted << QString::fromStdString(name);
    QStringList current;
    for (int i = 1; i < m_compoundCombo->count(); ++i)
        current << m_compoundCombo->itemText(i);
    if (wanted == current)
        return;

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
