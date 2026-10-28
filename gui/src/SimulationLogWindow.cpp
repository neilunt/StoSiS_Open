#include "SimulationLogWindow.h"
#include <QCheckBox>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextStream>
#include <QVBoxLayout>
#include <algorithm>

namespace
{
    QString Duration(double seconds) { return QString::fromStdString(stosis::FormatDuration(seconds)); }

    QString Share(double part, double total)
    {
        return total > 0.0 ? QString::number(100.0 * part / total, 'f', 1).replace('.', ',') + " %" : QString("-");
    }

    // Spalten der Tabelle; die Zeitspalten stehen in Sekunden auch im CSV.
    enum EColumn { kName, kType, kInit, kStep, kLimits, kRecord, kTotal, kShare, kCards, kNumColumns };
}

SimulationLogWindow::SimulationLogWindow(QWidget* parent)
    : QWidget(parent, Qt::Window)
{
    setWindowTitle("Laufzeitprotokoll");
    resize(1000, 660);

    auto* layout = new QVBoxLayout(this);
    m_summary = new QLabel("Noch keine Simulation.", this);
    m_summary->setWordWrap(true);
    m_summary->setTextFormat(Qt::RichText);
    layout->addWidget(m_summary);

    auto* tabs = new QTabWidget(this);
    m_text = new QPlainTextEdit(tabs);
    m_text->setReadOnly(true);
    m_text->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    tabs->addTab(m_text, "Protokoll");

    m_table = new QTableWidget(0, kNumColumns, tabs);
    m_table->setHorizontalHeaderLabels({ "Unit", "Typ", "Initialisieren", "Rechnen", "Grenzen", "Aufzeichnen",
        "Gesamt", "Anteil", "Karten je Schritt" });
    m_table->horizontalHeaderItem(kStep)->setToolTip("Step der Unit ueber alle Laeufe. Beim Outlet steckt hier\n"
                                                    "das Aufzeichnen des ankommenden Stroms (samt Umrechnung).");
    m_table->horizontalHeaderItem(kLimits)->setToolTip("Grenzen je Stromscheibe an den Ausgaengen (Partikelmodus): Karten\n"
                                                      "teilen bzw. zusammenlegen. Wie viele, zeigt der Tooltip der Zelle.");
    m_table->horizontalHeaderItem(kRecord)->setToolTip("Ausgaenge fuer die Ergebnisfenster aufzeichnen; im Partikelmodus\n"
                                                      "werden dabei die Karten aufs Groessenraster umgerechnet.");
    m_table->horizontalHeaderItem(kShare)->setToolTip("Anteil an der Gesamtzeit des Rechenkerns.");
    m_table->horizontalHeaderItem(kCards)->setToolTip("Karten an den Ausgaengen im Mittel je Zeitschritt (Outlet: am Eingang).");
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(kName, QHeaderView::Stretch);
    tabs->addTab(m_table, "Zeiten je Unit");
    layout->addWidget(tabs, 1);

    auto* bottom = new QHBoxLayout();
    m_detailedCheck = new QCheckBox("Ausfuehrlich: in Lauf 1 jede Unit in jedem Zeitschritt protokollieren", this);
    m_detailedCheck->setToolTip("Gilt ab der naechsten Simulation. Je Zeitschritt und Unit zwei Zeilen (Start und Ende);\n"
                                "nach 20 000 Zeilen wird das Protokoll gekappt.");
    connect(m_detailedCheck, &QCheckBox::toggled, this, [this](bool on)
        {
            if (m_graph)
                m_graph->SetDetailedLog(on);
        });
    bottom->addWidget(m_detailedCheck);
    m_microsecondsCheck = new QCheckBox(QString::fromUtf8("Zeitstempel auf 1 \xC2\xB5s genau"), this);
    m_microsecondsCheck->setToolTip("Zeit seit dem Start mit sechs statt drei Nachkommastellen (in s).\n"
                                    "Gilt sofort, auch fuer \"Protokoll speichern\".");
    connect(m_microsecondsCheck, &QCheckBox::toggled, this, [this]()
        {
            // Bildlauf behalten; nur die Zeitstempel wechseln die Genauigkeit.
            const int scroll = m_text->verticalScrollBar()->value();
            ShowLogText();
            m_text->verticalScrollBar()->setValue(scroll);
        });
    bottom->addWidget(m_microsecondsCheck);
    bottom->addStretch(1);
    auto* saveLogButton = new QPushButton("Protokoll speichern ...", this);
    connect(saveLogButton, &QPushButton::clicked, this, &SimulationLogWindow::SaveLog);
    bottom->addWidget(saveLogButton);
    auto* saveTableButton = new QPushButton("Tabelle als CSV ...", this);
    connect(saveTableButton, &QPushButton::clicked, this, &SimulationLogWindow::SaveTableCsv);
    bottom->addWidget(saveTableButton);
    layout->addLayout(bottom);
}

void SimulationLogWindow::SetGraph(stosis::CFlowsheetGraph* graph)
{
    m_graph = graph;
    if (m_graph)
        m_detailedCheck->setChecked(m_graph->IsDetailedLog());
    Refresh(m_guiSeconds);
}

void SimulationLogWindow::Refresh(double guiSeconds)
{
    m_guiSeconds = guiSeconds;
    if (!m_graph || m_graph->GetSimulationLog().GetLines().empty())
        return;
    const stosis::SSimulationTiming& timing = m_graph->GetSimulationTiming();

    QString summary = QString("<b>Letzte Simulation:</b> %1 %2, %3 Zeitpunkte je Lauf, %4 &ndash; Rechenkern %5")
        .arg(timing.runs).arg(timing.runs == 1 ? "Lauf" : "Laeufe").arg(timing.steps)
        .arg(timing.particles ? "Partikelmodus" : "Kontinuum").arg(Duration(timing.totalSeconds));
    if (timing.runs > 0)
        summary += QString(" (je Lauf %1)").arg(Duration(timing.totalSeconds / static_cast<double>(timing.runs)));
    summary += QString(", Neuzeichnen der Ergebnisfenster %1.").arg(Duration(m_guiSeconds));
    if (timing.cancelled)
        summary += " <b>Abgebrochen.</b>";
    if (stosis::IsDebugBuild())
        summary += "<br><span style='color:#C82828'>Debug-Fassung: Rechenzeiten etwa sechsmal laenger als in der "
                   "Release-Fassung. Fuer Rechnungen die Release-Fassung starten.</span>";
    m_summary->setText(summary);

    ShowLogText();
    FillTable();
}

void SimulationLogWindow::ShowLogText()
{
    if (m_graph)
        m_text->setPlainText(QString::fromStdString(m_graph->GetSimulationLog().ToText(GetLogDecimals())));
}

int SimulationLogWindow::GetLogDecimals() const
{
    return m_microsecondsCheck->isChecked() ? 6 : 3;
}

void SimulationLogWindow::FillTable()
{
    const stosis::SSimulationTiming& timing = m_graph->GetSimulationTiming();
    const double total = timing.totalSeconds;
    m_table->setRowCount(0);
    const auto addTexts = [this](const QStringList& texts, bool bold)
    {
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        for (int c = 0; c < kNumColumns; ++c)
        {
            auto* item = new QTableWidgetItem(texts.value(c));
            if (c >= kInit)
                item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            if (bold)
            {
                QFont font = item->font();
                font.setBold(true);
                item->setFont(font);
            }
            m_table->setItem(row, c, item);
        }
    };
    const auto addRow = [total, &addTexts](const QString& name, const QString& type, double init, double step,
                                           double limit, double record, double sum, const QString& share,
                                           const QString& cards, bool bold)
    {
        // Eine Zeiteinheit je Zeile (us oder ms), nach dem groessten Wert der Zeile.
        const stosis::ETimeUnit unit = stosis::ChooseTimeUnit(std::max({ init, step, limit, record, sum }));
        const auto time = [unit](double seconds) { return QString::fromStdString(stosis::FormatDurationIn(seconds, unit)); };
        addTexts({ name, type, init > 0.0 ? time(init) : "", step > 0.0 ? time(step) : "",
                   limit > 0.0 ? time(limit) : "", record > 0.0 ? time(record) : "", time(sum), share, cards }, bold);
    };

    double initSum = 0.0, stepSum = 0.0, limitSum = 0.0, recordSum = 0.0;
    for (const stosis::SUnitTiming& unit : timing.units)
    {
        addRow(QString::fromStdString(unit.name), QString::fromStdString(unit.type), unit.initSeconds,
            unit.stepSeconds, unit.limitSeconds, unit.recordSeconds, unit.GetTotalSeconds(),
            Share(unit.GetTotalSeconds(), total),
            timing.particles ? QString::number(unit.GetCardsPerStep(), 'f', 0) : QString("-"), false);
        if (unit.limitSeconds > 0.0)
            m_table->item(m_table->rowCount() - 1, kLimits)->setToolTip(
                QString("Je Schritt im Mittel %1 Karten durch Teilen dazu, %2 durch Zusammenlegen weg.")
                    .arg(QLocale().toString(unit.GetAddedPerStep(), 'f', 1))
                    .arg(QLocale().toString(unit.GetRemovedPerStep(), 'f', 1)));
        initSum += unit.initSeconds;
        stepSum += unit.stepSeconds;
        limitSum += unit.limitSeconds;
        recordSum += unit.recordSeconds;
    }
    addRow("Statistik ueber die Laeufe", "", 0.0, 0.0, 0.0, 0.0, timing.statisticsSeconds,
        Share(timing.statisticsSeconds, total), "", false);
    if (timing.historySeconds > 0.0)
        addRow("Karten fuer die Kartenansicht speichern", "", 0.0, 0.0, 0.0, 0.0, timing.historySeconds,
            Share(timing.historySeconds, total), "", false);
    const double rest = std::max(0.0, total - timing.GetUnitsSeconds() - timing.statisticsSeconds - timing.historySeconds);
    addRow("Ablauf, Speicher, Zeitmessung (Rest)", "", 0.0, 0.0, 0.0, 0.0, rest, Share(rest, total), "", false);
    // Spaltensummen: Initialisieren, Rechnen, Grenzen und Aufzeichnen der
    // Units; die Zeilen darueber ohne Spaltenwert stecken nur in "Gesamt".
    addRow("Gesamt Rechenkern", "", initSum, stepSum, limitSum, recordSum, total, Share(total, total), "", true);
    addTexts({ "Anteil am Rechenkern", "", Share(initSum, total), Share(stepSum, total),
               limitSum > 0.0 ? Share(limitSum, total) : QString(), Share(recordSum, total), Share(total, total), "", "" },
             true);
    m_table->item(m_table->rowCount() - 1, kName)->setToolTip(
        "Spaltensummen geteilt durch die Gesamtzeit des Rechenkerns. Was bis 100 % fehlt, steckt in\n"
        "Statistik, Kartenspeicher und Ablauf (deren Anteile stehen in der Spalte \"Anteil\").");
    addRow("Ergebnisfenster neu zeichnen (zusaetzlich)", "", 0.0, 0.0, 0.0, 0.0, m_guiSeconds, "", "", false);
}

void SimulationLogWindow::SaveLog()
{
    if (!m_graph)
        return;
    const QString path = QFileDialog::getSaveFileName(this, "Protokoll speichern", "StoSiS_Laufzeitprotokoll.txt",
                                                      "Text (*.txt)");
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::warning(this, "Protokoll speichern", "Datei laesst sich nicht schreiben:\n" + path);
        return;
    }
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << QString::fromStdString(m_graph->GetSimulationLog().ToText(GetLogDecimals()));
}

void SimulationLogWindow::SaveTableCsv()
{
    if (!m_graph)
        return;
    const QString path = QFileDialog::getSaveFileName(this, "Tabelle als CSV", "StoSiS_Laufzeiten.csv", "CSV (*.csv)");
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::warning(this, "Tabelle als CSV", "Datei laesst sich nicht schreiben:\n" + path);
        return;
    }
    const stosis::SSimulationTiming& timing = m_graph->GetSimulationTiming();
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << "# Laeufe;" << timing.runs << "\n# Zeitpunkte je Lauf;" << timing.steps << "\n# Darstellung;"
        << (timing.particles ? "Partikel" : "Kontinuum") << "\n# Fassung;" << (stosis::IsDebugBuild() ? "Debug" : "Release")
        << "\nUnit;Typ;Initialisieren [s];Rechnen [s];Grenzen [s];Aufzeichnen [s];Gesamt [s];Karten je Schritt;"
           "durch Teilen je Schritt;durch Zusammenlegen je Schritt\n";
    double initSum = 0.0, stepSum = 0.0, limitSum = 0.0, recordSum = 0.0;
    for (const stosis::SUnitTiming& unit : timing.units)
    {
        out << QString::fromStdString(unit.name) << ';' << QString::fromStdString(unit.type) << ';'
            << QString::number(unit.initSeconds, 'g', 6) << ';' << QString::number(unit.stepSeconds, 'g', 6) << ';'
            << QString::number(unit.limitSeconds, 'g', 6) << ';' << QString::number(unit.recordSeconds, 'g', 6) << ';'
            << QString::number(unit.GetTotalSeconds(), 'g', 6) << ';' << QString::number(unit.GetCardsPerStep(), 'g', 6) << ';'
            << QString::number(unit.GetAddedPerStep(), 'g', 6) << ';' << QString::number(unit.GetRemovedPerStep(), 'g', 6)
            << '\n';
        initSum += unit.initSeconds;
        stepSum += unit.stepSeconds;
        limitSum += unit.limitSeconds;
        recordSum += unit.recordSeconds;
    }
    const double rest = std::max(0.0, timing.totalSeconds - timing.GetUnitsSeconds() - timing.statisticsSeconds
                                          - timing.historySeconds);
    out << "Statistik;;;;;;" << QString::number(timing.statisticsSeconds, 'g', 6) << ";;;\n"
        << "Kartenspeicher;;;;;;" << QString::number(timing.historySeconds, 'g', 6) << ";;;\n"
        << "Rest;;;;;;" << QString::number(rest, 'g', 6) << ";;;\n"
        << "Gesamt Rechenkern;;" << QString::number(initSum, 'g', 6) << ';' << QString::number(stepSum, 'g', 6) << ';'
        << QString::number(limitSum, 'g', 6) << ';' << QString::number(recordSum, 'g', 6) << ';'
        << QString::number(timing.totalSeconds, 'g', 6) << ";;;\n"
        << "Ergebnisfenster neu zeichnen;;;;;;" << QString::number(m_guiSeconds, 'g', 6) << ";;;\n";
}
