#include "ResultsCurveWindow.h"
#include "ResultsEntryKey.h"
#include "SaveLocations.h"
#include <QListWidget>
#include <QComboBox>
#include <QCheckBox>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QTextStream>
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
    // Entry of the compound list for the sum of all compounds.
    const QString kAllCompounds = "alle Stoffe";
    // Folder of the last saved curve file; at first the folder "Kurven".
    QString g_curveFolder;

    QString CurveFolder()
    {
        if (g_curveFolder.isEmpty())
            g_curveFolder = save_locations::Subfolder("Kurven");
        return g_curveFolder;
    }
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

    // Units on the left, compounds on the right; both with Ctrl/Shift multi-selection.
    auto* listsRow = new QHBoxLayout();
    auto* nodeColumn = new QVBoxLayout();
    nodeColumn->addWidget(new QLabel("Einheiten:", this));
    m_nodeList = new QListWidget(this);
    m_nodeList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    connect(m_nodeList, &QListWidget::itemSelectionChanged, this, &ResultsCurveWindow::OnSelectionChanged);
    m_nodeList->setMaximumHeight(90);
    nodeColumn->addWidget(m_nodeList);
    listsRow->addLayout(nodeColumn, 3);

    auto* compoundColumn = new QVBoxLayout();
    compoundColumn->addWidget(new QLabel("Stoffe:", this));
    m_compoundList = new QListWidget(this);
    m_compoundList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_compoundList->setMaximumHeight(90);
    m_compoundList->setToolTip("Massenstrom aller Stoffe zusammen oder einzelner Stoffe; mehrere mit Strg+Klick.");
    auto* all = new QListWidgetItem(kAllCompounds);
    m_compoundList->addItem(all);
    all->setSelected(true);
    connect(m_compoundList, &QListWidget::itemSelectionChanged, this, &ResultsCurveWindow::UpdateCurves);
    compoundColumn->addWidget(m_compoundList);
    listsRow->addLayout(compoundColumn, 2);
    mainLayout->addLayout(listsRow);

    auto* quantityRow = new QHBoxLayout();
    quantityRow->addWidget(new QLabel("Groesse:", this));
    m_quantityCombo = new QComboBox(this);
    m_quantityCombo->insertItem(QuantityMassFlow, "Massenstrom");
    m_quantityCombo->insertItem(QuantityTemperature, "Temperatur");
    m_quantityCombo->insertItem(QuantityPressure, "Druck");
    connect(m_quantityCombo, &QComboBox::currentIndexChanged, this, &ResultsCurveWindow::UpdateCurves);
    quantityRow->addWidget(m_quantityCombo);
    quantityRow->addSpacing(15);

    quantityRow->addWidget(new QLabel("Streuband:", this));
    m_bandCombo = new QComboBox(this);
    m_bandCombo->insertItem(BandNone, "keins");
    m_bandCombo->insertItem(BandOneSigma, "Mittelwert +- 1 sigma");
    m_bandCombo->insertItem(BandTwoSigma, "Mittelwert +- 2 sigma");
    m_bandCombo->insertItem(BandPercentile, "5 - 95 %-Perzentil");
    m_bandCombo->setCurrentIndex(BandOneSigma);
    m_bandCombo->setToolTip("Streuung ueber alle Laeufe. Perzentile sind erst ab etwa 20 Laeufen aussagekraeftig.");
    connect(m_bandCombo, &QComboBox::currentIndexChanged, this, &ResultsCurveWindow::UpdateCurves);
    quantityRow->addWidget(m_bandCombo);
    quantityRow->addSpacing(15);

    m_runsCheck = new QCheckBox("Einzellaeufe zeigen", this);
    connect(m_runsCheck, &QCheckBox::toggled, this, &ResultsCurveWindow::UpdateCurves);
    quantityRow->addWidget(m_runsCheck);
    quantityRow->addStretch(1);

    auto* resetButton = new QPushButton("Gesamtansicht", this);
    resetButton->setToolTip("Ganze Zeit und alle Werte zeigen (auch: Doppelklick ins Diagramm).");
    quantityRow->addWidget(resetButton);
    m_exportButton = new QPushButton("Kurven als CSV ...", this);
    m_exportButton->setEnabled(false);
    connect(m_exportButton, &QPushButton::clicked, this, &ResultsCurveWindow::ExportCsv);
    quantityRow->addWidget(m_exportButton);
    mainLayout->addLayout(quantityRow);

    m_curveWidget = new CurveWidget(this);
    // CurveWidget verlangt sonst 300 px Mindesthoehe - zusammen mit den
    // Listen waere das Fenster dann hoeher als vorgesehen.
    m_curveWidget->setMinimumHeight(150);
    mainLayout->addWidget(m_curveWidget, 1);
    connect(resetButton, &QPushButton::clicked, m_curveWidget, &CurveWidget::ResetView);
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
    bool compoundListUpdated = false;
    std::vector<SCurveSeries> series;
    double totalTime = 1.0;
    size_t numTimeSteps = 0;
    size_t numRuns = 0;
    double dt = 0.0;
    // Units and compounds in list order: one colour per unit, one line style per compound.
    int unitIndex = 0;
    for (int row = 0; row < m_nodeList->count(); ++row)
    {
        QListWidgetItem* item = m_nodeList->item(row);
        if (!item->isSelected())
            continue;
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
            compoundListUpdated = true;
        }

        // Curves of this unit: for the mass flow one per selected compound entry
        // ("alle Stoffe" = sum), for temperature and pressure one.
        std::vector<std::pair<QString, int>> compounds; // name, line style
        if (quantity == QuantityMassFlow)
            for (int c = 0; c < m_compoundList->count(); ++c)
                if (m_compoundList->item(c)->isSelected())
                    compounds.push_back({ m_compoundList->item(c)->text(), c });
        if (compounds.empty())
            compounds.push_back({ kAllCompounds, 0 });
        const bool nameWithCompound = quantity == QuantityMassFlow && (compounds.size() > 1 || compounds.front().first != kAllCompounds);
        const size_t curvesBefore = series.size();
        const std::vector<std::string> names = streamStats->GetCompoundNames();

        for (const auto& [compound, style] : compounds)
        {
            // Je Lauf eine Zeitreihe: Massenstrom (Integral ueber die Groesse,
            // mit Delta d), Temperatur oder Druck.
            const stosis::CEnsembleStatistics* massFlow = &streamStats->GetTotal();
            if (quantity == QuantityMassFlow && compound != kAllCompounds)
            {
                const auto it = std::find(names.begin(), names.end(), compound.toStdString());
                if (it == names.end())
                    continue; // this stream does not carry the compound
                if (names.size() > 1)
                    massFlow = &streamStats->GetCompound(static_cast<size_t>(it - names.begin()));
            }
            const std::vector<std::vector<double>>& runs =
                quantity == QuantityTemperature ? streamStats->GetRunTemperatures()
                : quantity == QuantityPressure ? streamStats->GetRunPressures()
                : massFlow->GetRunTimeSeries();
            if (runs.empty())
                continue;
            totalTime = streamStats->GetTotal().GetMean().GetTotalTime();
            dt = streamStats->GetTotal().GetMean().GetDt();
            numTimeSteps = runs.front().size();
            numRuns = runs.size();

            SCurveSeries curve;
            curve.name = item->text();
            if (nameWithCompound)
                curve.name += " - " + compound;
            curve.unit = item->text();
            curve.compound = nameWithCompound ? compound : QString();
            curve.colorIndex = unitIndex;
            curve.styleIndex = style;
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
        if (series.size() > curvesBefore)
            ++unitIndex;
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
        m_shownQuantity = "Temperatur [K]";
    else if (quantity == QuantityPressure)
        m_shownQuantity = "Druck [Pa]";
    else
        m_shownQuantity = "Massenstrom [kg/s]";
    m_curveWidget->SetValueAxis(m_shownQuantity, quantity == QuantityMassFlow);
    m_compoundList->setEnabled(quantity == QuantityMassFlow && m_compoundList->count() > 1);
    m_curveWidget->SetTimeAxis(totalTime, numTimeSteps);
    m_curveWidget->SetCaption(caption);
    m_shownSeries = series;
    m_shownDt = dt;
    m_shownRuns = numRuns;
    m_exportButton->setEnabled(!m_shownSeries.empty());
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
    for (int i = 1; i < m_compoundList->count(); ++i)
        current << m_compoundList->item(i)->text();
    if (wanted == current)
        return;

    QSet<QString> selected;
    for (QListWidgetItem* item : m_compoundList->selectedItems())
        selected.insert(item->text());
    m_compoundList->blockSignals(true);
    m_compoundList->clear();
    m_compoundList->addItem(kAllCompounds);
    for (const QString& name : wanted)
        m_compoundList->addItem(name);
    bool any = false;
    for (int i = 0; i < m_compoundList->count(); ++i)
        if (selected.contains(m_compoundList->item(i)->text()))
        {
            m_compoundList->item(i)->setSelected(true);
            any = true;
        }
    if (!any)
        m_compoundList->item(0)->setSelected(true);
    m_compoundList->blockSignals(false);
}

void ResultsCurveWindow::ExportCsv()
{
    if (m_shownSeries.empty())
        return;
    const QString path = QFileDialog::getSaveFileName(this, "Kurven als CSV speichern",
        QDir(CurveFolder()).filePath(save_locations::SuggestedName("Kurven", "csv")), "CSV-Dateien (*.csv)");
    if (path.isEmpty())
        return;
    g_curveFolder = QFileInfo(path).absolutePath();
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::warning(this, "Export", "Die Datei konnte nicht geschrieben werden:\n" + path);
        return;
    }
    // Column names without the separator.
    const auto column = [](QString name) { return name.replace(';', ','); };
    QTextStream out(&file);
    out << "# StoSiS-Kurven: " << m_shownQuantity << ", " << m_shownRuns << (m_shownRuns == 1 ? " Lauf" : " Laeufe")
        << ", Zeitschritt " << QString::number(m_shownDt, 'g', 10) << " s\n";
    out << "# Wert je Zeitschritt ab t; je Kurve der Mittelwert ueber die Laeufe, bei Streuband auch unten/oben,"
           " bei Einzellaeufen jeder gezeichnete Lauf. Alle Zeitschritte, unabhaengig vom Ausschnitt im Diagramm.\n";
    out << "t_s";
    for (const SCurveSeries& s : m_shownSeries)
    {
        out << ";" << column(s.name);
        if (!s.lower.empty())
            out << ";" << column(s.name) << " unten;" << column(s.name) << " oben";
        for (size_t r = 0; r < s.runs.size(); ++r)
            out << ";" << column(s.name) << " Lauf " << (r + 1);
    }
    out << "\n";
    const size_t steps = m_shownSeries.front().values.size();
    for (size_t k = 0; k < steps; ++k)
    {
        out << QString::number(static_cast<double>(k) * m_shownDt, 'g', 10);
        for (const SCurveSeries& s : m_shownSeries)
        {
            out << ";" << (k < s.values.size() ? QString::number(s.values[k], 'g', 10) : QString());
            if (!s.lower.empty())
                out << ";" << QString::number(s.lower[k], 'g', 10) << ";" << QString::number(s.upper[k], 'g', 10);
            for (const std::vector<double>& run : s.runs)
                out << ";" << (k < run.size() ? QString::number(run[k], 'g', 10) : QString());
        }
        out << "\n";
    }
}
