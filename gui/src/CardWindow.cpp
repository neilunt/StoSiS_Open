#include "CardWindow.h"
#include "CardPlotWidget.h"
#include "ResultsEntryKey.h"
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextStream>
#include <QVBoxLayout>
#include <algorithm>
#include <numeric>

namespace
{
    // Mehr Zeilen zeigt die Tabelle nicht an (der CSV-Export enthaelt alle).
    constexpr size_t kMaxTableRows = 20000;

    // Zahl im Gebietsschema, ohne Tausendertrennzeichen (1000 statt 1.000).
    QString Num(double v, int digits = 4)
    {
        QLocale locale;
        locale.setNumberOptions(QLocale::OmitGroupSeparator);
        return locale.toString(v, 'g', digits);
    }
    QString Unit() { return QString::fromUtf8(stosis::kSizeDisplayUnit); }

    QString CompoundName(const stosis::SSimulationGrid& grid, size_t c)
    {
        return c < grid.compounds.size() ? QString::fromStdString(grid.compounds[c]) : QString("Stoff %1").arg(c + 1);
    }

    // Masse jeder Karte ueber alle Stoffe.
    std::vector<double> CardMasses(const stosis::CCardHistory::SSnapshot& snapshot, size_t numC)
    {
        std::vector<double> masses(snapshot.sizes.size(), 0.0);
        for (size_t k = 0; k < masses.size(); ++k)
            for (size_t c = 0; c < numC; ++c)
                masses[k] += snapshot.massFlows[k * numC + c];
        return masses;
    }
}

CardWindow::CardWindow(QWidget* parent)
    : QWidget(parent, Qt::Window)
{
    setWindowTitle("Kartenansicht");
    resize(1100, 860);

    auto* layout = new QVBoxLayout(this);
    auto* top = new QHBoxLayout();
    top->addWidget(new QLabel("Strom:", this));
    m_entryCombo = new QComboBox(this);
    m_entryCombo->setObjectName("entryCombo");
    m_entryCombo->setMinimumWidth(260);
    m_entryCombo->setToolTip("Ausgang einer Unit (beim Outlet: was ankommt). Karten gibt es nur nach einer\n"
                             "Simulation im Partikelmodus; gespeichert ist Lauf 1.");
    top->addWidget(m_entryCombo);
    top->addSpacing(12);
    top->addWidget(new QLabel("Zeitpunkt:", this));
    m_stepSlider = new QSlider(Qt::Horizontal, this);
    m_stepSlider->setMinimumWidth(200);
    top->addWidget(m_stepSlider, 1);
    m_stepLabel = new QLabel(this);
    m_stepLabel->setMinimumWidth(260);
    top->addWidget(m_stepLabel);
    layout->addLayout(top);

    auto* controls = new QHBoxLayout();
    controls->addWidget(new QLabel("Klassen je Groessenbereich:", this));
    m_classCombo = new QComboBox(this);
    m_classCombo->setObjectName("classCombo");
    for (int n : { 1, 2, 4, 8, 16 })
        m_classCombo->addItem(QString::number(n), n);
    m_classCombo->setCurrentIndex(2);
    m_classCombo->setToolTip("Jeder Groessenbereich des Siebsatzes wird in so viele gleich breite Klassen (in ln d)\n"
                             "geteilt; gilt fuer A, C und E. Feinere Klassen zeigen mehr Einzelheiten, aber auch\n"
                             "mehr Rauschen, weil weniger Karten in jeder Klasse liegen.");
    controls->addWidget(m_classCombo);
    controls->addSpacing(16);
    controls->addWidget(new QLabel("Verteilung:", this));
    auto* densityRadio = new QRadioButton("Dichte q3* (A)", this);
    m_cumulativeRadio = new QRadioButton("Summenkurve Q3 (B)", this);
    densityRadio->setChecked(true);
    controls->addWidget(densityRadio);
    controls->addWidget(m_cumulativeRadio);
    controls->addStretch(1);
    layout->addLayout(controls);

    m_info = new QLabel(this);
    m_info->setWordWrap(true);
    m_info->setStyleSheet("color: gray;");
    layout->addWidget(m_info);

    // Verteilung: A/B, D und C untereinander mit gemeinsamer d-Achse.
    auto* tabs = new QTabWidget(this);
    auto* distributionPage = new QWidget(tabs);
    auto* distributionLayout = new QVBoxLayout(distributionPage);
    distributionLayout->setSpacing(0);
    m_distribution = new CardPlotWidget(CardPlotWidget::EKind::Distribution, distributionPage);
    m_rug = new CardPlotWidget(CardPlotWidget::EKind::Rug, distributionPage);
    m_classes = new CardPlotWidget(CardPlotWidget::EKind::Classes, distributionPage);
    m_classes->setObjectName("classesPlot");
    distributionLayout->addWidget(m_distribution, 5);
    distributionLayout->addWidget(m_rug);
    distributionLayout->addWidget(m_classes, 3);
    tabs->addTab(distributionPage, "Verteilung (A-D)");

    // Zeitverlauf: E und Kartenzahl untereinander mit gemeinsamer Zeitachse.
    auto* timePage = new QWidget(tabs);
    auto* timeLayout = new QVBoxLayout(timePage);
    timeLayout->setSpacing(0);
    m_timeMap = new CardPlotWidget(CardPlotWidget::EKind::TimeMap, timePage);
    m_counts = new CardPlotWidget(CardPlotWidget::EKind::Counts, timePage);
    m_counts->setObjectName("countsPlot");
    timeLayout->addWidget(m_timeMap, 3);
    timeLayout->addWidget(m_counts, 2);
    tabs->addTab(timePage, "Zeitverlauf (E)");

    m_cloud = new CardPlotWidget(CardPlotWidget::EKind::Cloud, tabs);
    tabs->addTab(m_cloud, "Gewichte (F)");

    auto* tablePage = new QWidget(tabs);
    auto* tableLayout = new QVBoxLayout(tablePage);
    m_table = new QTableWidget(tablePage);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    tableLayout->addWidget(m_table, 1);
    auto* exportButton = new QPushButton("Karten dieses Zeitpunkts als CSV ...", tablePage);
    connect(exportButton, &QPushButton::clicked, this, &CardWindow::ExportCsv);
    tableLayout->addWidget(exportButton, 0, Qt::AlignRight);
    tabs->addTab(tablePage, "Tabelle");
    layout->addWidget(tabs, 1);

    connect(m_entryCombo, &QComboBox::currentIndexChanged, this, &CardWindow::UpdateViews);
    connect(m_stepSlider, &QSlider::valueChanged, this, &CardWindow::UpdateViews);
    connect(m_classCombo, &QComboBox::currentIndexChanged, this, &CardWindow::UpdateViews);
    connect(m_cumulativeRadio, &QRadioButton::toggled, m_distribution, &CardPlotWidget::SetCumulativeMode);
}

void CardWindow::SetGraph(stosis::CFlowsheetGraph* graph)
{
    m_graph = graph;
    RefreshNodeList();
}

void CardWindow::RefreshNodeList()
{
    const QString previous = m_entryCombo->currentData().toString();
    m_entryCombo->blockSignals(true);
    m_entryCombo->clear();
    if (m_graph)
        for (const stosis::SResultsEntry& entry : m_graph->GetResultsEntries())
            if (m_graph->GetCardHistory(entry.nodeId, entry.port))
                m_entryCombo->addItem(QString::fromStdString(entry.label), MakeResultsKey(entry.nodeId, entry.port));
    const int index = m_entryCombo->findData(previous);
    m_entryCombo->setCurrentIndex(index >= 0 ? index : 0);
    m_entryCombo->blockSignals(false);
    // Nach einer Simulation koennen die Karten andere sein, auch wenn der
    // Speicher derselbe ist.
    m_axisHistory = nullptr;
    UpdateViews();
}

const stosis::CCardHistory* CardWindow::CurrentHistory() const
{
    std::string nodeId;
    size_t port = 0;
    if (!m_graph || !ParseResultsKey(m_entryCombo->currentData().toString(), nodeId, port))
        return nullptr;
    return m_graph->GetCardHistory(nodeId, port);
}

int CardWindow::GetClassesPerRange() const
{
    return std::max(1, m_classCombo->currentData().toInt());
}

void CardWindow::UpdateViews()
{
    const stosis::CCardHistory* history = CurrentHistory();
    if (!history || history->GetNumSteps() == 0)
    {
        const bool particles = m_graph && m_graph->GetGrid().representation == stosis::ERepresentation::Particles;
        const QString message = particles
            ? "Noch keine Karten gespeichert - \"Simulieren\" druecken."
            : "Die Kartenansicht gibt es im Partikelmodus: Darstellung \"Partikel (Karten)\" waehlen und simulieren.";
        for (CardPlotWidget* plot : { m_distribution, m_rug, m_classes, m_timeMap, m_counts, m_cloud })
            plot->ShowMessage(message);
        m_axisHistory = nullptr;
        m_info->clear();
        m_stepLabel->clear();
        m_table->setRowCount(0);
        return;
    }

    const size_t numT = history->GetNumSteps();
    m_stepSlider->blockSignals(true);
    m_stepSlider->setRange(0, static_cast<int>(numT - 1));
    m_stepSlider->blockSignals(false);
    const size_t step = static_cast<size_t>(m_stepSlider->value());
    const double dt = history->GetDt();
    const stosis::CCardHistory::SSnapshot* snapshot = history->FindNearest(step);

    QString stepText = QString("t = %1 s (Schritt %2 von %3)").arg(Num(static_cast<double>(step) * dt, 6)).arg(step).arg(numT - 1);
    if (snapshot && snapshot->step != step)
        stepText += QString(", gezeigt: t = %1 s").arg(Num(static_cast<double>(snapshot->step) * dt, 6));
    m_stepLabel->setText(stepText);

    UpdateAxisAndTimeMap(*history);

    const stosis::SSimulationGrid& grid = m_graph->GetGrid();
    const size_t numC = history->GetNumCompounds();
    const size_t numCards = snapshot ? snapshot->sizes.size() : 0;
    const size_t stride = history->GetStride();
    m_info->setText(QString("Lauf 1 - gespeichert %1, belegt %2 MB - %3 Karten zu diesem Zeitpunkt%4")
        .arg(stride == 1 ? QString("jeder Zeitschritt") : QString("jeder %1. Zeitschritt (Speichergrenze erreicht)").arg(stride))
        .arg(QLocale().toString(static_cast<double>(history->GetStoredBytes()) / 1048576.0, 'f', 1))
        .arg(numCards)
        .arg(numCards > kMaxTableRows ? QString(" (Tabelle zeigt die ersten %1)").arg(kMaxTableRows) : QString()));

    // Kartenzahl ueber die Zeit (alle Zeitschritte, auch ungespeicherte).
    std::vector<double> times(numT), counts(numT);
    for (size_t k = 0; k < numT; ++k)
    {
        times[k] = static_cast<double>(k) * dt;
        counts[k] = static_cast<double>(history->GetCardCounts()[k]);
    }
    m_counts->SetCounts(std::move(times), std::move(counts));
    m_counts->SetMarkTime(static_cast<double>(step) * dt);
    m_timeMap->SetMarkTime(static_cast<double>(step) * dt);

    if (!snapshot || numCards == 0)
    {
        for (CardPlotWidget* plot : { m_distribution, m_rug, m_classes, m_cloud })
            plot->ShowMessage("Keine Karten zu diesem Zeitpunkt");
        m_table->setRowCount(0);
        return;
    }

    // Karten: Masse ueber alle Stoffe, Farbe nach dem Stoff mit der groessten Masse.
    const std::vector<double> masses = CardMasses(*snapshot, numC);
    std::vector<CardPlotWidget::SCard> cards(numCards);
    for (size_t k = 0; k < numCards; ++k)
    {
        CardPlotWidget::SCard& card = cards[k];
        card.size = snapshot->sizes[k];
        card.mass = masses[k];
        double best = -1.0;
        for (size_t c = 0; c < numC; ++c)
            if (snapshot->massFlows[k * numC + c] > best)
            {
                best = snapshot->massFlows[k * numC + c];
                card.compound = static_cast<int>(c);
            }
    }

    const std::vector<double> bounds =
        stosis::MakeFineClassBounds(m_firstRange, m_lastRange, GetClassesPerRange(), m_graph->GetCardSettings());
    std::vector<stosis::SCardClass> classes = stosis::BinCards(snapshot->sizes, masses, bounds);
    std::vector<double> refSizes, refDensity;
    QString refLabel;
    FillReference(snapshot->step, refSizes, refDensity, refLabel);
    m_distribution->SetDistribution(classes, stosis::MakeCardCumulative(snapshot->sizes, masses),
                                    std::move(refSizes), std::move(refDensity), refLabel);
    m_classes->SetClasses(std::move(classes), stosis::CInletUnit::kMinCardsPerRange);

    // Grenzen je Stromscheibe der gezeigten Scheibe, wenn sie in der letzten
    // Simulation an waren (die Karten sind dann schon geteilt bzw. zusammengelegt).
    std::vector<stosis::SSliceRange> limitRanges;
    if (m_graph->GetSimulationTiming().sliceLimits)
    {
        const stosis::SCardSettings& settings = m_graph->GetCardSettings();
        const stosis::SSliceLimits limits = stosis::ComputeSliceLimits(snapshot->sizes, masses, settings);
        limitRanges = limits.ranges;
        m_info->setText(m_info->text() + QString(" - Grenzen je Stromscheibe: N_min = %1 je belegtem Bereich "
            "(%2 belegt), Obergrenze %3 x Sollzahl").arg(limits.minPerRange).arg(limits.numOccupied)
            .arg(QLocale().toString(settings.upperFactor, 'f', 1)));
    }
    m_classes->SetRangeLimits(std::move(limitRanges), GetClassesPerRange());

    QStringList compounds;
    for (size_t c = 0; c < numC; ++c)
        compounds << CompoundName(grid, c);
    m_rug->SetCards(cards, compounds);
    m_cloud->SetCards(std::move(cards), compounds);
    FillTable(*snapshot, numC);
}

void CardWindow::UpdateAxisAndTimeMap(const stosis::CCardHistory& history)
{
    const int perRange = GetClassesPerRange();
    const std::vector<stosis::CCardHistory::SSnapshot>& snapshots = history.GetSnapshots();
    if (&history == m_axisHistory && perRange == m_axisPerRange && snapshots.size() == m_axisSnapshots)
        return;
    m_axisHistory = &history;
    m_axisPerRange = perRange;
    m_axisSnapshots = snapshots.size();

    // Gemeinsame d-Achse: ganze Groessenbereiche von der kleinsten bis zur
    // groessten Karte aller gespeicherten Zeitpunkte - so bleibt die Achse
    // stehen, wenn der Zeitpunkt wechselt.
    const stosis::SCardSettings& settings = m_graph->GetCardSettings();
    double dMin = 0.0, dMax = 0.0;
    for (const stosis::CCardHistory::SSnapshot& snapshot : snapshots)
        for (double d : snapshot.sizes)
            if (d > 0.0)
            {
                dMin = (dMin > 0.0) ? std::min(dMin, d) : d;
                dMax = std::max(dMax, d);
            }
    if (!(dMin > 0.0))
        dMin = dMax = 100e-6;
    m_firstRange = stosis::GetCardRangeIndex(dMin, settings);
    m_lastRange = stosis::GetCardRangeIndex(dMax, settings);
    std::vector<double> rangeBounds;
    for (int k = m_firstRange; k <= m_lastRange + 1; ++k)
        rangeBounds.push_back(stosis::GetCardRangeLower(k, settings));
    for (CardPlotWidget* plot : { m_distribution, m_rug, m_classes, m_timeMap, m_cloud })
        plot->SetSizeAxis(rangeBounds.front(), rangeBounds.back(), rangeBounds);

    // Zeitkarte E: je gespeichertem Zeitpunkt die Dichte je feiner Klasse.
    const std::vector<double> bounds = stosis::MakeFineClassBounds(m_firstRange, m_lastRange, perRange, settings);
    const size_t numC = history.GetNumCompounds();
    const double dt = history.GetDt();
    std::vector<double> times, values;
    times.reserve(snapshots.size());
    values.reserve(snapshots.size() * (bounds.size() - 1));
    for (const stosis::CCardHistory::SSnapshot& snapshot : snapshots)
    {
        times.push_back(static_cast<double>(snapshot.step) * dt);
        for (const stosis::SCardClass& c : stosis::BinCards(snapshot.sizes, CardMasses(snapshot, numC), bounds))
            values.push_back(c.GetDensity());
    }
    const double endTime = static_cast<double>(history.GetNumSteps() - 1) * dt;
    m_timeMap->SetTimeMap(std::move(times), static_cast<double>(history.GetStride()) * dt, endTime, bounds,
                          std::move(values));
}

void CardWindow::FillReference(size_t step, std::vector<double>& sizes, std::vector<double>& density, QString& label) const
{
    std::string nodeId;
    size_t port = 0;
    if (!m_graph || !ParseResultsKey(m_entryCombo->currentData().toString(), nodeId, port))
        return;
    const stosis::CEnsembleStatistics* statistics = m_graph->GetStatistics(nodeId, port);
    if (!statistics || statistics->IsEmpty())
        return;
    const stosis::CSimulationResults& mean = statistics->GetMean();
    if (mean.IsEmpty() || mean.GetNumTimeSteps() == 0)
        return;
    const size_t t = std::min(step, mean.GetNumTimeSteps() - 1);
    const double total = mean.IntegrateOverSize(t);
    if (!(total > 0.0))
        return;
    // Gespeichert sind Dichten je m; mal d ergibt die Dichte je ln d.
    for (size_t s = 0; s < mean.GetNumSizeClasses(); ++s)
    {
        const double d = mean.GetSizeAt(s);
        if (d > 0.0)
        {
            sizes.push_back(d);
            density.push_back(d * mean.GetValue(t, s) / total);
        }
    }
    const size_t runs = statistics->GetNumRuns();
    label = runs > 1 ? QString("Mittel aus %1 Laeufen (Groessenraster)").arg(runs)
                     : QString("Lauf 1 auf dem Groessenraster");
}

void CardWindow::FillTable(const stosis::CCardHistory::SSnapshot& snapshot, size_t numC)
{
    const stosis::SSimulationGrid& grid = m_graph->GetGrid();
    const stosis::SCardSettings& settings = m_graph->GetCardSettings();
    QStringList headers = { "Nr", "Groesse [" + Unit() + "]", "Bereich [" + Unit() + "]" };
    for (size_t c = 0; c < numC; ++c)
        headers << CompoundName(grid, c) + " [kg/s]";
    headers << "Summe [kg/s]" << "Anteil [%]";

    // Nach Groesse sortiert; die Nummer ist die Stelle im Strom.
    const size_t numCards = snapshot.sizes.size();
    std::vector<size_t> order(numCards);
    std::iota(order.begin(), order.end(), size_t(0));
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return snapshot.sizes[a] < snapshot.sizes[b]; });
    double total = 0.0;
    for (double m : snapshot.massFlows)
        total += m;

    m_table->setUpdatesEnabled(false);
    m_table->clear();
    m_table->setColumnCount(headers.size());
    m_table->setHorizontalHeaderLabels(headers);
    m_table->horizontalHeaderItem(0)->setToolTip("Stelle der Karte im Strom (Reihenfolge, in der die Unit sie ausgibt);\n"
                                                 "die Tabelle ist nach Groesse sortiert.");
    const size_t rows = std::min(numCards, kMaxTableRows);
    m_table->setRowCount(static_cast<int>(rows));
    for (size_t r = 0; r < rows; ++r)
    {
        const size_t k = order[r];
        const int range = stosis::GetCardRangeIndex(snapshot.sizes[k], settings);
        double sum = 0.0;
        QStringList texts = { QString::number(k + 1), Num(snapshot.sizes[k] * stosis::kSizeDisplayFactor, 5),
            Num(stosis::GetCardRangeLower(range, settings) * stosis::kSizeDisplayFactor) + " - "
                + Num(stosis::GetCardRangeLower(range + 1, settings) * stosis::kSizeDisplayFactor) };
        for (size_t c = 0; c < numC; ++c)
        {
            const double m = snapshot.massFlows[k * numC + c];
            sum += m;
            texts << Num(m, 5);
        }
        texts << Num(sum, 5) << (total > 0.0 ? QLocale().toString(100.0 * sum / total, 'f', 4) : QString("-"));
        for (int c = 0; c < texts.size(); ++c)
        {
            auto* item = new QTableWidgetItem(texts[c]);
            item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_table->setItem(static_cast<int>(r), c, item);
        }
    }
    m_table->resizeColumnsToContents();
    m_table->setUpdatesEnabled(true);
}

void CardWindow::ExportCsv()
{
    const stosis::CCardHistory* history = CurrentHistory();
    const stosis::CCardHistory::SSnapshot* snapshot =
        history ? history->FindNearest(static_cast<size_t>(m_stepSlider->value())) : nullptr;
    if (!snapshot)
    {
        QMessageBox::information(this, "Karten als CSV", "Keine Karten zu diesem Zeitpunkt.");
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, "Karten als CSV", "StoSiS_Karten.csv", "CSV (*.csv)");
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::warning(this, "Karten als CSV", "Datei laesst sich nicht schreiben:\n" + path);
        return;
    }
    const stosis::SSimulationGrid& grid = m_graph->GetGrid();
    const stosis::SCardSettings& settings = m_graph->GetCardSettings();
    const size_t numC = history->GetNumCompounds();
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << "# Strom;" << m_entryCombo->currentText() << "\n# Lauf;1\n# Zeit [s];"
        << QString::number(static_cast<double>(snapshot->step) * history->GetDt(), 'g', 10) << "\n# Karten;"
        << snapshot->sizes.size() << "\nGroesse [m];Bereich unten [m];Bereich oben [m]";
    for (size_t c = 0; c < numC; ++c)
        out << ';' << CompoundName(grid, c) << " [kg/s]";
    out << ";Summe [kg/s]\n";
    for (size_t k = 0; k < snapshot->sizes.size(); ++k)
    {
        const int range = stosis::GetCardRangeIndex(snapshot->sizes[k], settings);
        out << QString::number(snapshot->sizes[k], 'g', 10) << ';'
            << QString::number(stosis::GetCardRangeLower(range, settings), 'g', 10) << ';'
            << QString::number(stosis::GetCardRangeLower(range + 1, settings), 'g', 10);
        double sum = 0.0;
        for (size_t c = 0; c < numC; ++c)
        {
            const double m = snapshot->massFlows[k * numC + c];
            sum += m;
            out << ';' << QString::number(m, 'g', 10);
        }
        out << ';' << QString::number(sum, 'g', 10) << '\n';
    }
}
