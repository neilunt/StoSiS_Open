#include "CardWindow.h"
#include "CardPlotWidget.h"
#include "ResultsEntryKey.h"
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
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
}

CardWindow::CardWindow(QWidget* parent)
    : QWidget(parent, Qt::Window)
{
    setWindowTitle("Kartenansicht");
    resize(1100, 820);

    auto* layout = new QVBoxLayout(this);
    auto* top = new QHBoxLayout();
    top->addWidget(new QLabel("Strom:", this));
    m_entryCombo = new QComboBox(this);
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

    m_info = new QLabel(this);
    m_info->setWordWrap(true);
    m_info->setStyleSheet("color: gray;");
    layout->addWidget(m_info);

    auto* tabs = new QTabWidget(this);
    auto* plots = new QWidget(tabs);
    auto* grid = new QGridLayout(plots);
    m_cloud = new CardPlotWidget(CardPlotWidget::EKind::Cloud, plots);
    m_ranges = new CardPlotWidget(CardPlotWidget::EKind::Ranges, plots);
    m_counts = new CardPlotWidget(CardPlotWidget::EKind::Counts, plots);
    grid->addWidget(m_cloud, 0, 0);
    grid->addWidget(m_ranges, 0, 1);
    grid->addWidget(m_counts, 1, 0, 1, 2);
    grid->setColumnStretch(0, 3);
    grid->setColumnStretch(1, 2);
    grid->setRowStretch(0, 3);
    grid->setRowStretch(1, 2);
    tabs->addTab(plots, "Diagramme");

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

void CardWindow::UpdateViews()
{
    const stosis::CCardHistory* history = CurrentHistory();
    if (!history || history->GetNumSteps() == 0)
    {
        const bool particles = m_graph && m_graph->GetGrid().representation == stosis::ERepresentation::Particles;
        const QString message = particles
            ? "Noch keine Karten gespeichert - \"Simulieren\" druecken."
            : "Die Kartenansicht gibt es im Partikelmodus: Darstellung \"Partikel (Karten)\" waehlen und simulieren.";
        m_cloud->ShowMessage(message);
        m_ranges->ShowMessage(message);
        m_counts->ShowMessage(message);
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
    m_counts->SetCounts(std::move(times), std::move(counts), static_cast<double>(step) * dt);

    if (!snapshot || numCards == 0)
    {
        m_cloud->ShowMessage("Keine Karten zu diesem Zeitpunkt");
        m_ranges->ShowMessage("Keine Karten zu diesem Zeitpunkt");
        m_table->setRowCount(0);
        return;
    }

    // Karten: Masse ueber alle Stoffe, Farbe nach dem Stoff mit der groessten Masse.
    std::vector<CardPlotWidget::SCard> cards(numCards);
    double totalMass = 0.0, dMin = snapshot->sizes[0], dMax = dMin;
    for (size_t k = 0; k < numCards; ++k)
    {
        CardPlotWidget::SCard& card = cards[k];
        card.size = snapshot->sizes[k];
        double best = -1.0;
        for (size_t c = 0; c < numC; ++c)
        {
            const double m = snapshot->massFlows[k * numC + c];
            card.mass += m;
            if (m > best)
            {
                best = m;
                card.compound = static_cast<int>(c);
            }
        }
        totalMass += card.mass;
        dMin = std::min(dMin, card.size);
        dMax = std::max(dMax, card.size);
    }

    // Groessenbereiche des Siebsatzes (1 um * 2^k) von der kleinsten bis zur groessten Karte.
    const stosis::SCardSettings& settings = m_graph->GetCardSettings();
    const int kMin = stosis::GetCardRangeIndex(dMin, settings);
    const int kMax = stosis::GetCardRangeIndex(dMax, settings);
    std::vector<double> bounds;
    std::vector<CardPlotWidget::SRange> ranges;
    for (int k = kMin; k <= kMax + 1; ++k)
        bounds.push_back(stosis::GetCardRangeLower(k, settings));
    for (int k = kMin; k <= kMax; ++k)
    {
        CardPlotWidget::SRange range;
        range.lower = stosis::GetCardRangeLower(k, settings);
        range.upper = stosis::GetCardRangeLower(k + 1, settings);
        ranges.push_back(range);
    }
    for (const CardPlotWidget::SCard& card : cards)
    {
        const int k = std::clamp(stosis::GetCardRangeIndex(card.size, settings), kMin, kMax);
        CardPlotWidget::SRange& range = ranges[static_cast<size_t>(k - kMin)];
        ++range.cards;
        range.massShare += totalMass > 0.0 ? card.mass / totalMass : 0.0;
    }

    QStringList compounds;
    for (size_t c = 0; c < numC; ++c)
        compounds << CompoundName(grid, c);
    m_cloud->SetCloud(std::move(cards), std::move(bounds), compounds);
    m_ranges->SetRanges(std::move(ranges), stosis::CInletUnit::kMinCardsPerRange);
    FillTable(*snapshot, numC);
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
