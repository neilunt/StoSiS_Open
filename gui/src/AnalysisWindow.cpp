#include "AnalysisWindow.h"
#include "AnalysisPlotWidget.h"
#include "ResultsEntryKey.h"
#include "SignalAnalysis.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTextStream>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace
{
    // Einheit des Massenstroms in den Ergebnissen (SI, wie im Kurvenfenster).
    const QString kUnit = "kg/s";
    // Mehr Klassen zeichnet das Histogramm nicht (zu kleine Klassenbreite).
    constexpr size_t kMaxBins = 2000;

    QString Num(double v, int digits = 5) { return QString::number(v, 'g', digits); }
}

AnalysisWindow::AnalysisWindow(QWidget* parent)
    : QWidget(parent, Qt::Window)
{
    setWindowTitle("Auswertung");
    resize(1000, 820);
    SetupUi();
}

void AnalysisWindow::SetupUi()
{
    auto* mainLayout = new QVBoxLayout(this);

    // Simulationsleiste: dieselben Werte wie im Hauptfenster (MainWindow
    // gleicht beide ab), damit man ohne Fensterwechsel neu rechnen kann.
    auto* simBox = new QGroupBox("Simulation (dieselben Werte wie im Hauptfenster)", this);
    auto* simRow = new QHBoxLayout(simBox);
    simRow->addWidget(new QLabel("Dauer [s]:", simBox));
    m_durationSpin = new QDoubleSpinBox(simBox);
    m_durationSpin->setRange(0.1, 1.0e6);
    m_durationSpin->setDecimals(2);
    simRow->addWidget(m_durationSpin);
    simRow->addSpacing(10);
    simRow->addWidget(new QLabel("Zeitschritt dt [s]:", simBox));
    m_dtSpin = new QDoubleSpinBox(simBox);
    m_dtSpin->setRange(0.0001, 1.0e6);
    m_dtSpin->setDecimals(4);
    m_dtSpin->setStepType(QAbstractSpinBox::AdaptiveDecimalStepType);
    m_dtSpin->setToolTip("Laenge eines Rechenschritts. Die Zahl der Zeitpunkte folgt aus Dauer / dt;\n"
                         "eine neue Dauer aendert dt nicht.");
    simRow->addWidget(m_dtSpin);
    m_timeInfo = new QLabel(simBox);
    m_timeInfo->setStyleSheet("color: gray;");
    simRow->addWidget(m_timeInfo);
    simRow->addStretch(1);
    simRow->addWidget(new QLabel("Laeufe:", simBox));
    m_runsSpin = new QSpinBox(simBox);
    m_runsSpin->setRange(1, 100000);
    simRow->addWidget(m_runsSpin);
    simRow->addWidget(new QLabel("Darstellung:", simBox));
    m_representationCombo = new QComboBox(simBox);
    m_representationCombo->addItem("Kontinuum", static_cast<int>(stosis::ERepresentation::Continuum));
    m_representationCombo->addItem("Partikel (Karten)", static_cast<int>(stosis::ERepresentation::Particles));
    m_representationCombo->setToolTip("Wie im Hauptfenster: Kontinuum oder Karten. So lassen sich beide\n"
                                      "Darstellungen direkt nacheinander rechnen und vergleichen.");
    simRow->addWidget(m_representationCombo);
    auto* simulateButton = new QPushButton("Simulieren", simBox);
    simulateButton->setToolTip("Rechnet das Flowsheet neu (wie \"Simulieren\" im Hauptfenster);\n"
                               "die Auswertung aktualisiert sich danach von selbst.");
    simRow->addWidget(simulateButton);
    mainLayout->addWidget(simBox);
    auto emitSettings = [this]()
    {
        emit SimulationSettingsEdited(m_durationSpin->value(), m_dtSpin->value(), m_runsSpin->value(),
            m_representationCombo->currentData().toInt());
    };
    connect(m_durationSpin, &QDoubleSpinBox::valueChanged, this, emitSettings);
    connect(m_dtSpin, &QDoubleSpinBox::valueChanged, this, emitSettings);
    connect(m_runsSpin, &QSpinBox::valueChanged, this, emitSettings);
    connect(m_representationCombo, &QComboBox::currentIndexChanged, this, emitSettings);
    connect(simulateButton, &QPushButton::clicked, this, &AnalysisWindow::SimulateRequested);

    auto* topRow = new QHBoxLayout();
    topRow->addWidget(new QLabel("Ausgang:", this));
    m_entryCombo = new QComboBox(this);
    m_entryCombo->setMinimumWidth(240);
    m_entryCombo->setToolTip("Welcher Strom ausgewertet wird (Massenstrom aller Stoffe).");
    topRow->addWidget(m_entryCombo);
    topRow->addStretch(1);
    m_exportButton = new QPushButton("Als CSV exportieren ...", this);
    m_exportButton->setToolTip("Speichert die Fenstermittel aller Laeufe (ungefiltert) mit den Kennzahlen,\n"
                               "z.B. zum Weiterrechnen in Excel. Trennzeichen ';', Dezimalpunkt '.'.");
    topRow->addWidget(m_exportButton);
    mainLayout->addLayout(topRow);

    auto* settingsBox = new QGroupBox("Aufbereitung wie bei einer Messung", this);
    auto* grid = new QGridLayout(settingsBox);

    grid->addWidget(new QLabel("Mittelung ueber [s]:", settingsBox), 0, 0);
    m_windowSpin = new QDoubleSpinBox(settingsBox);
    m_windowSpin->setRange(0.0, 1.0e6);
    m_windowSpin->setDecimals(3);
    m_windowSpin->setSingleStep(0.1);
    m_windowSpin->setValue(1.0);
    m_windowSpin->setSpecialValueText("jeder Zeitschritt");
    m_windowSpin->setToolTip("Wie eine Waage, die alle x Sekunden abliest: Mittel des Massenstroms ueber x s.\n"
                             "Die Streuung der Mittel ist kleiner als die Streuung je Zeitschritt.");
    grid->addWidget(m_windowSpin, 0, 1);
    m_windowInfo = new QLabel(settingsBox);
    grid->addWidget(m_windowInfo, 0, 2, 1, 2);
    grid->addWidget(new QLabel("Auswertung ab [s]:", settingsBox), 0, 4);
    m_startSpin = new QDoubleSpinBox(settingsBox);
    m_startSpin->setRange(0.0, 1.0e9);
    m_startSpin->setDecimals(3);
    m_startSpin->setToolTip("Werte davor nicht auswerten, z.B. um das Einschwingen auszuschliessen.");
    grid->addWidget(m_startSpin, 0, 5);

    m_filterCheck = new QCheckBox("Ausreisserfilter (Engisch && Muzzio 2012)", settingsBox);
    m_filterCheck->setToolTip("Erst alle Mittel ausserhalb Sollwert +- x % verwerfen (z.B. Eimerwechsel),\n"
                              "dann wiederholt alle ausserhalb Mittelwert +- k sigma, bis sich nichts mehr aendert.");
    grid->addWidget(m_filterCheck, 1, 0, 1, 2);
    grid->addWidget(new QLabel("Sollwert:", settingsBox), 1, 2);
    m_setpointSpin = new QDoubleSpinBox(settingsBox);
    m_setpointSpin->setRange(0.0, 1.0e12);
    m_setpointSpin->setDecimals(4);
    m_setpointSpin->setSpecialValueText("Mittelwert");
    m_setpointSpin->setToolTip("Bezugswert fuer die erste Grenze. 0 = Mittelwert der Fenstermittel.");
    grid->addWidget(m_setpointSpin, 1, 3);
    m_bandSpin = new QDoubleSpinBox(settingsBox);
    m_bandSpin->setRange(0.1, 100.0);
    m_bandSpin->setDecimals(1);
    m_bandSpin->setValue(10.0);
    m_bandSpin->setPrefix("+- ");
    m_bandSpin->setSuffix(" %");
    grid->addWidget(m_bandSpin, 1, 4);
    m_sigmaSpin = new QDoubleSpinBox(settingsBox);
    m_sigmaSpin->setRange(1.0, 10.0);
    m_sigmaSpin->setDecimals(1);
    m_sigmaSpin->setValue(3.0);
    m_sigmaSpin->setPrefix("dann +- ");
    m_sigmaSpin->setSuffix(" sigma");
    grid->addWidget(m_sigmaSpin, 1, 5);

    grid->addWidget(new QLabel("Klassenbreite:", settingsBox), 2, 0);
    m_classWidthSpin = new QDoubleSpinBox(settingsBox);
    m_classWidthSpin->setRange(0.0, 1.0e12);
    m_classWidthSpin->setDecimals(4);
    m_classWidthSpin->setSpecialValueText("automatisch");
    m_classWidthSpin->setToolTip("Breite der Histogramm-Klassen. 0 = automatisch (etwa 2 * n^(1/3) Klassen).");
    grid->addWidget(m_classWidthSpin, 2, 1);
    grid->addWidget(new QLabel("Zeitverlauf: erste [s]:", settingsBox), 2, 4);
    m_shownTimeSpin = new QDoubleSpinBox(settingsBox);
    m_shownTimeSpin->setRange(0.0, 1.0e9);
    m_shownTimeSpin->setDecimals(1);
    m_shownTimeSpin->setValue(60.0);
    m_shownTimeSpin->setSpecialValueText("alles");
    m_shownTimeSpin->setToolTip("Nur fuer die Anzeige unten: welcher Zeitausschnitt gezeichnet wird.");
    grid->addWidget(m_shownTimeSpin, 2, 5);
    grid->setColumnStretch(6, 1);
    mainLayout->addWidget(settingsBox);

    m_statsLabel = new QLabel(this);
    m_statsLabel->setWordWrap(true);
    m_statsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    mainLayout->addWidget(m_statsLabel);

    auto* plots = new QGridLayout();
    m_densityPlot = new AnalysisPlotWidget(AnalysisPlotWidget::EKind::Density, this);
    m_probabilityPlot = new AnalysisPlotWidget(AnalysisPlotWidget::EKind::Probability, this);
    m_timePlot = new AnalysisPlotWidget(AnalysisPlotWidget::EKind::TimeSeries, this);
    for (AnalysisPlotWidget* plot : { m_densityPlot, m_probabilityPlot, m_timePlot })
        plot->SetUnit(kUnit);
    plots->addWidget(m_densityPlot, 0, 0);
    plots->addWidget(m_probabilityPlot, 0, 1);
    plots->addWidget(m_timePlot, 1, 0, 1, 2);
    plots->setRowStretch(0, 3);
    plots->setRowStretch(1, 2);
    mainLayout->addLayout(plots, 1);

    connect(m_entryCombo, &QComboBox::currentIndexChanged, this, &AnalysisWindow::Recompute);
    for (QDoubleSpinBox* spin : { m_windowSpin, m_startSpin, m_setpointSpin, m_bandSpin, m_sigmaSpin, m_classWidthSpin, m_shownTimeSpin })
        connect(spin, &QDoubleSpinBox::valueChanged, this, &AnalysisWindow::Recompute);
    connect(m_filterCheck, &QCheckBox::toggled, this, [this](bool on)
        {
            m_setpointSpin->setEnabled(on);
            m_bandSpin->setEnabled(on);
            m_sigmaSpin->setEnabled(on);
            Recompute();
        });
    m_setpointSpin->setEnabled(false);
    m_bandSpin->setEnabled(false);
    m_sigmaSpin->setEnabled(false);
    connect(m_exportButton, &QPushButton::clicked, this, &AnalysisWindow::ExportCsv);
}

void AnalysisWindow::SetGraph(stosis::CFlowsheetGraph* graph)
{
    m_graph = graph;
    RefreshNodeList();
}

void AnalysisWindow::SetSimulationSettings(double duration, double dt, int runs, int representation, const QString& info)
{
    const QSignalBlocker blockDuration(m_durationSpin);
    const QSignalBlocker blockDt(m_dtSpin);
    const QSignalBlocker blockRuns(m_runsSpin);
    const QSignalBlocker blockRepresentation(m_representationCombo);
    m_durationSpin->setValue(duration);
    m_dtSpin->setValue(dt);
    m_runsSpin->setValue(runs);
    m_representationCombo->setCurrentIndex(std::max(0, m_representationCombo->findData(representation)));
    m_timeInfo->setText(info);
}

void AnalysisWindow::RefreshNodeList()
{
    const QString previous = m_entryCombo->currentData().toString();
    m_entryCombo->blockSignals(true);
    m_entryCombo->clear();
    int select = -1;
    int firstOutlet = -1;
    if (m_graph)
    {
        // Nur Stroeme, die vom Zufall abhaengen: Bei allen anderen gibt es
        // keine Schwankung, Dichte und Wahrscheinlichkeitsnetz zeigten nur
        // den zeitlichen Verlauf (z.B. das Anfahren) als "Streuung".
        for (const stosis::SResultsEntry& entry : m_graph->GetResultsEntries())
        {
            if (!m_graph->IsAffectedByRandomness(entry.nodeId))
                continue;
            const QString key = MakeResultsKey(entry.nodeId, entry.port);
            const QString label = QString::fromStdString(entry.label);
            m_entryCombo->addItem(label, key);
            if (key == previous)
                select = m_entryCombo->count() - 1;
            if (firstOutlet < 0 && label.contains("(Outlet"))
                firstOutlet = m_entryCombo->count() - 1;
        }
    }
    // Ohne vorherige Auswahl: das erste Outlet (dort wird meist gemessen).
    m_entryCombo->setCurrentIndex(select >= 0 ? select : std::max(firstOutlet, 0));
    m_entryCombo->blockSignals(false);
    Recompute();
}

void AnalysisWindow::Recompute()
{
    if (!m_graph)
        return;
    m_windowMeans.clear();
    m_windowStarts.clear();
    m_exportHeader.clear();
    m_exportButton->setEnabled(false);
    auto showMessage = [this](const QString& text)
    {
        m_statsLabel->setText(text);
        for (AnalysisPlotWidget* plot : { m_densityPlot, m_probabilityPlot, m_timePlot })
            plot->ShowMessage(text);
    };

    if (m_entryCombo->count() == 0)
    {
        m_windowInfo->clear();
        showMessage("Kein Strom im Flowsheet haengt vom Zufall ab. Diese Auswertung zeigt Schwankungen, die von einer "
                    "stochastischen Unit kommen (heute: Splitter im stochastischen Modus, Einstellungsfenster) - "
                    "an ihren Ausgaengen und allem, was dahinter liegt.");
        return;
    }

    std::string nodeId;
    size_t port = 0;
    const stosis::CStreamStatistics* stats = ParseResultsKey(m_entryCombo->currentData().toString(), nodeId, port)
        ? m_graph->GetStreamStatistics(nodeId, port) : nullptr;
    if (!stats || stats->GetTotal().IsEmpty() || stats->GetTotal().GetRunTimeSeries().empty())
    {
        m_windowInfo->clear();
        showMessage("Noch keine Ergebnisse - erst simulieren.");
        return;
    }
    const std::vector<std::vector<double>>& runs = stats->GetTotal().GetRunTimeSeries();
    const double dt = stats->GetTotal().GetMean().GetDt();

    // --- Fenstermittel je Lauf -----------------------------------------
    const size_t n = stosis::StepsPerWindow(dt, m_windowSpin->value());
    const double windowLength = static_cast<double>(n) * dt;
    const size_t firstStep = static_cast<size_t>(std::max(0.0, std::ceil(m_startSpin->value() / dt - 1e-9)));
    QString info = QString("= %1 Zeitschritt(e), dt = %2 s").arg(n).arg(Num(dt, 4));
    if (m_windowSpin->value() > dt && std::abs(windowLength - m_windowSpin->value()) > 1e-9 * windowLength)
        info += QString(" (gerundet auf %1 s)").arg(Num(windowLength, 4));
    m_windowInfo->setText(info);
    for (const std::vector<double>& run : runs)
        m_windowMeans.push_back(stosis::AverageOverWindows(run, firstStep, n));
    if (m_windowMeans.front().size() < 3)
    {
        m_windowMeans.clear();
        showMessage("Zu wenige Fenster: Mittelungszeit verkuerzen, frueher auswerten oder laenger simulieren.");
        return;
    }
    for (size_t j = 0; j < m_windowMeans.front().size(); ++j)
        m_windowStarts.push_back(static_cast<double>(firstStep + j * n) * dt);

    // --- Filter und Kennzahlen je Lauf ----------------------------------
    const bool filter = m_filterCheck->isChecked();
    std::vector<std::vector<double>> kept(runs.size());
    std::vector<stosis::SSignalStatistics> runStats(runs.size());
    std::vector<size_t> removed(runs.size(), 0);
    for (size_t r = 0; r < runs.size(); ++r)
    {
        if (filter)
        {
            const double setpoint = m_setpointSpin->value() > 0.0 ? m_setpointSpin->value()
                : stosis::ComputeSignalStatistics(m_windowMeans[r]).mean;
            kept[r] = stosis::FilterLikeEngischMuzzio(m_windowMeans[r], setpoint, m_bandSpin->value() / 100.0,
                m_sigmaSpin->value(), removed[r]);
        }
        else
        {
            kept[r] = m_windowMeans[r];
        }
        runStats[r] = stosis::ComputeSignalStatistics(kept[r]);
    }
    if (kept.front().size() < 3)
    {
        showMessage("Der Filter hat (fast) alle Werte entfernt - Sollwert und Grenzen pruefen.");
        return;
    }

    // --- Histogramm: gemeinsame Klassen fuer alle Laeufe -----------------
    double minValue = kept.front().front(), maxValue = minValue;
    for (const std::vector<double>& values : kept)
        for (double v : values)
        {
            minValue = std::min(minValue, v);
            maxValue = std::max(maxValue, v);
        }
    double start = 0.0, width = m_classWidthSpin->value();
    size_t numBins = 0;
    if (width > 0.0)
    {
        start = std::floor(minValue / width) * width;
        numBins = static_cast<size_t>(std::floor((maxValue - start) / width)) + 1;
    }
    if (width <= 0.0 || numBins > kMaxBins)
        stosis::ChooseHistogramBins(minValue, maxValue, kept.front().size(), start, width, numBins);
    const stosis::SHistogram first = stosis::MakeHistogram(kept.front(), start, width, numBins);
    std::vector<double> bandLow, bandHigh;
    if (runs.size() > 1)
    {
        bandLow.assign(numBins, 1e300);
        bandHigh.assign(numBins, 0.0);
        for (const std::vector<double>& values : kept)
        {
            const stosis::SHistogram h = stosis::MakeHistogram(values, start, width, numBins);
            for (size_t i = 0; i < numBins; ++i)
            {
                bandLow[i] = std::min(bandLow[i], h.density[i]);
                bandHigh[i] = std::max(bandHigh[i], h.density[i]);
            }
        }
    }
    const stosis::SSignalStatistics& s = runStats.front();
    m_densityPlot->SetDensity(first, bandLow, bandHigh, s.mean, s.stdDev);
    m_probabilityPlot->SetProbability(stosis::MakeProbabilityPlot(kept.front()), s.mean, s.stdDev);

    // --- Zeitverlauf von Lauf 1 im gewaehlten Ausschnitt ----------------
    const std::vector<double>& run0 = runs.front();
    size_t lastStep = run0.size() - 1;
    if (m_shownTimeSpin->value() > 0.0)
        lastStep = std::min(lastStep, firstStep + static_cast<size_t>(std::llround(m_shownTimeSpin->value() / dt)));
    std::vector<double> times, values, starts, means;
    for (size_t k = firstStep; k <= lastStep; ++k)
    {
        times.push_back(static_cast<double>(k) * dt);
        values.push_back(run0[k]);
    }
    for (size_t j = 0; j < m_windowStarts.size(); ++j)
        if (m_windowStarts[j] + windowLength <= static_cast<double>(lastStep) * dt + 1e-9 * dt)
        {
            starts.push_back(m_windowStarts[j]);
            means.push_back(m_windowMeans.front()[j]);
        }
    m_timePlot->SetTimeSeries(times, values, starts, means, windowLength);

    // --- Kennzahlen ------------------------------------------------------
    QString text = QString("<b>Lauf 1:</b> %1 Werte%2 &middot; Mittel %3 %4 &middot; &sigma; %5 %4 &middot; RSD %6 % "
                           "&middot; Schiefe %7 &middot; Korrelation benachbarter Werte %8")
        .arg(s.count)
        .arg(filter ? QString(" (%1 gefiltert)").arg(removed.front()) : QString())
        .arg(Num(s.mean)).arg(kUnit).arg(Num(s.stdDev, 4))
        .arg(QString::number(100.0 * s.rsd, 'f', 3)).arg(QString::number(s.skewness, 'f', 2))
        .arg(QString::number(s.lag1Correlation, 'f', 2));
    if (runs.size() > 1)
    {
        double meanLo = 1e300, meanHi = -1e300, sdLo = 1e300, sdHi = -1e300, rsdLo = 1e300, rsdHi = -1e300, sdSum = 0.0;
        for (const stosis::SSignalStatistics& rs : runStats)
        {
            meanLo = std::min(meanLo, rs.mean); meanHi = std::max(meanHi, rs.mean);
            sdLo = std::min(sdLo, rs.stdDev); sdHi = std::max(sdHi, rs.stdDev);
            rsdLo = std::min(rsdLo, rs.rsd); rsdHi = std::max(rsdHi, rs.rsd);
            sdSum += rs.stdDev;
        }
        text += QString("<br><b>Alle %1 Laeufe:</b> Mittel %2 bis %3 &middot; &sigma; %4 bis %5 (im Mittel %6) &middot; RSD %7 bis %8 %")
            .arg(runs.size()).arg(Num(meanLo)).arg(Num(meanHi)).arg(Num(sdLo, 4)).arg(Num(sdHi, 4))
            .arg(Num(sdSum / static_cast<double>(runs.size()), 4))
            .arg(QString::number(100.0 * rsdLo, 'f', 3)).arg(QString::number(100.0 * rsdHi, 'f', 3));
    }
    text += QString("<br><span style='color:gray'>&sigma; gilt fuer die Mittel ueber %1 s. Das Band im Kurvenfenster "
                    "zeigt dagegen die Streuung je Zeitschritt - bei kurzen Korrelationszeiten deutlich groesser.</span>")
        .arg(Num(windowLength, 4));
    m_statsLabel->setText(text);

    // --- Kopf fuer den CSV-Export ----------------------------------------
    m_exportHeader = QString("# StoSiS-Auswertung: %1\n# Mittelung %2 s (%3 Zeitschritte, dt %4 s), ab %5 s\n")
        .arg(m_entryCombo->currentText()).arg(Num(windowLength, 6)).arg(n).arg(Num(dt, 6)).arg(Num(m_startSpin->value(), 6));
    if (filter)
        m_exportHeader += QString("# Filter: Sollwert %1, +-%2 %, dann +-%3 sigma (Lauf 1: %4 Werte entfernt)\n")
            .arg(m_setpointSpin->value() > 0.0 ? Num(m_setpointSpin->value(), 8) : QString("Mittelwert"))
            .arg(Num(m_bandSpin->value(), 4)).arg(Num(m_sigmaSpin->value(), 3)).arg(removed.front());
    m_exportHeader += QString("# Lauf 1 (nach Filter): n %1, Mittel %2 %3, Std %4 %3, RSD %5 %\n")
        .arg(s.count).arg(Num(s.mean, 8)).arg(kUnit).arg(Num(s.stdDev, 6)).arg(QString::number(100.0 * s.rsd, 'f', 4));
    m_exportHeader += "# Werte: Fenstermittel je Lauf, ungefiltert\n";
    m_exportButton->setEnabled(true);
}

void AnalysisWindow::ExportCsv()
{
    if (m_windowMeans.empty())
        return;
    const QString path = QFileDialog::getSaveFileName(this, "Fenstermittel als CSV speichern", "auswertung.csv",
        "CSV-Dateien (*.csv)");
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::warning(this, "Export", "Die Datei konnte nicht geschrieben werden:\n" + path);
        return;
    }
    QTextStream out(&file);
    out << m_exportHeader << "fenster_start_s";
    for (size_t r = 0; r < m_windowMeans.size(); ++r)
        out << ";lauf_" << (r + 1);
    out << "\n";
    for (size_t j = 0; j < m_windowStarts.size(); ++j)
    {
        out << QString::number(m_windowStarts[j], 'g', 10);
        for (const std::vector<double>& run : m_windowMeans)
            out << ";" << (j < run.size() ? QString::number(run[j], 'g', 10) : QString());
        out << "\n";
    }
}
