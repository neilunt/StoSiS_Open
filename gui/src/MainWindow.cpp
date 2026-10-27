#include "MainWindow.h"
#include "ComparisonWindow.h"
#include "HeatmapDemoWindow.h"
#include "CudaTest.h"
#include "FlowsheetCanvasWidget.h"
#include "ResultsCurveWindow.h"
#include "AnalysisWindow.h"
#include "CardWindow.h"
#include "SimulationLogWindow.h"
#include "ResultsHeatmapWindow.h"
#include "SettingsWindow.h"
#include "MaterialsPage.h"
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QSplitter>
#include <QLabel>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QComboBox>
#include <QGridLayout>
#include <QProgressDialog>
#include <QCoreApplication>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLocale>
#include <algorithm>
#include <cmath>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    // Debug-Fassung im Titel: sie rechnet etwa sechsmal langsamer (Methoden Kap. 15).
    setWindowTitle(stosis::IsDebugBuild() ? "StoSiS [Debug-Fassung]" : "StoSiS");
    resize(1600, 900); // Startgroesse, falls nicht maximiert gestartet wird

    LoadMaterials();
    BuildTestFlowsheet();
    SetupMenuAndToolBar();
    SetupCentralArea();
    if (m_materialLoadError.isEmpty())
        statusBar()->showMessage("Bereit. Doppelklick auf einen Knoten oeffnet seine Einstellungen. Flowsheet: Mausrad zoomt, Ziehen auf freier Flaeche verschiebt.");
    else
        statusBar()->showMessage("Materialdatei konnte nicht gelesen werden - Details unter Flowsheet > Materialien.");
}

void MainWindow::LoadMaterials()
{
    std::string error;
    if (!m_materials.LoadFromFile(FindMaterialFile().toStdString(), error))
        m_materialLoadError = QString::fromStdString(error);
    // Auch ohne lesbare Datei: der Graph rechnet dann ohne Stoffwerte.
    m_graph.SetMaterialDatabase(&m_materials);
}

void MainWindow::BuildTestFlowsheet()
{
    // Start-Flowsheet: einfache Rueckfuehrung, damit Einstellungen und
    // Ergebnisansichten sofort etwas zum Anzeigen haben:
    //   Inlet -> Merger -> Splitter -> Outlet (70 %),
    //   Splitter -> Prozess -> Merger (30 %, Rueckfuehrung ueber die
    //   Verzoegerung; der Graph schneidet den Kreis automatisch an einer
    //   Verbindung auf, Kapitel 6).
    // Lage: Der Splitter verzweigt nach oben zum Outlet und nach unten zum
    // Prozess; die Rueckfuehrung laeuft unten herum in den Merger - so
    // kreuzen sich keine Kabel (Merger und Splitter sind 60 hoch, Ports bei
    // 20 und 40; das Inlet sitzt auf Hoehe des ersten Merger-Eingangs).
    const std::string inletId = m_graph.AddInletNode(
        m_graph.MakeDefaultNodeName(stosis::ENodeType::Inlet), 0.0, 55.0);
    const std::string mergerId = m_graph.AddMergerNode(
        m_graph.MakeDefaultNodeName(stosis::ENodeType::Merger), 2, 160.0, 60.0);
    const std::string splitterId = m_graph.AddSplitterNode(
        m_graph.MakeDefaultNodeName(stosis::ENodeType::Splitter), 2, 320.0, 60.0);
    const std::string outletId = m_graph.AddOutletNode(
        m_graph.MakeDefaultNodeName(stosis::ENodeType::Outlet), 500.0, 0.0);
    const std::string processId = m_graph.AddProcessNode(
        m_graph.MakeDefaultNodeName(stosis::ENodeType::Process), 500.0, 120.0);
    m_graph.GetSplitter(splitterId)->SetFractions({ 0.7, 0.3 });
    m_graph.AddConnection(inletId, 0, mergerId, 0);
    m_graph.AddConnection(mergerId, 0, splitterId, 0);
    m_graph.AddConnection(splitterId, 0, outletId, 0);
    m_graph.AddConnection(splitterId, 1, processId, 0);
    m_graph.AddConnection(processId, 0, mergerId, 1);

    // Zeitprofil und Groessenverteilung: Standardwerte aus CInletUnit
    // (GetDefaultInletTimeProfile in InletUnit.h, Log-Normal d50 = 100 um,
    // sigma_ln = 0,25) - dieselben bekommt jedes neu angelegte Inlet.
    // Raster: logarithmisch 1 um bis 1 mm mit 300 Klassen (Methoden 2.5).

    // Zeitraster: 40 s mit dt = 0,2 s (201 Zeitpunkte).
    m_graph.ConfigureGrid(300, 1e-6, 1e-3, 201, 40.0);
    m_graph.RunSimulations(kDefaultNumRuns);
}

void MainWindow::SetupMenuAndToolBar()
{
    QAction* settingsAction = new QAction("&Einstellungen oeffnen", this);
    connect(settingsAction, &QAction::triggered, this, &MainWindow::OnShowSettings);

    QMenu* flowsheetMenu = menuBar()->addMenu("&Flowsheet");
    flowsheetMenu->addAction(settingsAction);
    QAction* materialsAction = flowsheetMenu->addAction("&Materialien anzeigen");
    connect(materialsAction, &QAction::triggered, this, &MainWindow::OnShowMaterials);
    QAction* analysisAction = flowsheetMenu->addAction("&Auswertung oeffnen");
    connect(analysisAction, &QAction::triggered, this, &MainWindow::OnShowAnalysis);
    QAction* cardsAction = flowsheetMenu->addAction("&Kartenansicht oeffnen");
    connect(cardsAction, &QAction::triggered, this, &MainWindow::OnShowCards);
    QAction* logAction = flowsheetMenu->addAction("&Laufzeitprotokoll oeffnen");
    connect(logAction, &QAction::triggered, this, &MainWindow::OnShowLog);

    // Alte Test-/Demo-Fenster: weiterhin erreichbar, aber aus dem
    // Hauptablauf heraus.
    QMenu* devMenu = menuBar()->addMenu("&Entwicklung");
    QAction* heatmapAction = devMenu->addAction("&Heatmap-Demo");
    connect(heatmapAction, &QAction::triggered, this, &MainWindow::OnShowHeatmapDemo);
    QAction* comparisonAction = devMenu->addAction("&Vergleichsansicht (Transportmodelle)");
    connect(comparisonAction, &QAction::triggered, this, &MainWindow::OnShowComparisonWindow);
    QAction* cudaTestAction = devMenu->addAction("&CUDA-Test ausfuehren");
    connect(cudaTestAction, &QAction::triggered, this, &MainWindow::OnRunCudaTest);

    QToolBar* toolBar = addToolBar("Werkzeuge");
    toolBar->setMovable(false);
    toolBar->addAction(settingsAction);
    toolBar->addAction(materialsAction);
    toolBar->addAction(analysisAction);
    toolBar->addAction(cardsAction);
    toolBar->addAction(logAction);
}

void MainWindow::SetupCentralArea()
{
    // --- Links: Flowsheet + Simulationsleiste ------------------------
    auto* flowsheetPanel = new QWidget(this);
    auto* flowsheetLayout = new QVBoxLayout(flowsheetPanel);
    flowsheetLayout->setContentsMargins(0, 0, 0, 0);

    m_canvas = new FlowsheetCanvasWidget(flowsheetPanel);
    m_canvas->SetGraph(&m_graph);
    flowsheetLayout->addWidget(m_canvas, 1);

    // Simulationsleiste: Dauer, Zeitschritt, Seed und Laeufe untereinander,
    // "Simulieren" daneben. Dauer und dt stehen bewusst zusammen: die Zahl
    // der Zeitpunkte folgt aus beiden, eine neue Dauer aendert dt nicht.
    auto* simulationBar = new QGridLayout();
    simulationBar->addWidget(new QLabel("Simulationsdauer [s]:", flowsheetPanel), 0, 0);
    m_durationSpin = new QDoubleSpinBox(flowsheetPanel);
    m_durationSpin->setRange(0.1, 1.0e6);
    m_durationSpin->setValue(m_graph.GetGrid().totalTime);
    simulationBar->addWidget(m_durationSpin, 0, 1);

    simulationBar->addWidget(new QLabel("Zeitschritt dt [s]:", flowsheetPanel), 1, 0);
    m_dtSpin = new QDoubleSpinBox(flowsheetPanel);
    m_dtSpin->setRange(0.0001, 1.0e6);
    m_dtSpin->setDecimals(4);
    m_dtSpin->setStepType(QAbstractSpinBox::AdaptiveDecimalStepType);
    m_dtSpin->setValue(m_graph.GetGrid().GetDt());
    m_dtSpin->setToolTip("Laenge eines Rechenschritts. Kleiner = feiner aufgeloest, aber mehr Rechenzeit\n"
                         "und Speicher. Die Zahl der Zeitpunkte folgt aus Dauer / dt; geht die Dauer\n"
                         "nicht genau auf, wird dt minimal angepasst (siehe Hinweis darunter).");
    simulationBar->addWidget(m_dtSpin, 1, 1);
    m_timeInfoLabel = new QLabel(flowsheetPanel);
    m_timeInfoLabel->setStyleSheet("color: gray;");
    simulationBar->addWidget(m_timeInfoLabel, 2, 0, 1, 2);

    simulationBar->addWidget(new QLabel("Seed:", flowsheetPanel), 3, 0);
    m_seedSpin = new QSpinBox(flowsheetPanel);
    m_seedSpin->setRange(0, 2147483647);
    m_seedSpin->setValue(static_cast<int>(m_graph.GetGrid().randomSeed));
    m_seedSpin->setToolTip("Startwert fuer alle Zufallszahlen: gleicher Seed = gleiches Ergebnis.\n"
                           "Jeder stochastische Block leitet daraus seinen eigenen Zufallsstrom ab.");
    simulationBar->addWidget(m_seedSpin, 3, 1);

    simulationBar->addWidget(new QLabel("Anzahl Laeufe:", flowsheetPanel), 4, 0);
    m_runsSpin = new QSpinBox(flowsheetPanel);
    m_runsSpin->setRange(1, 100000);
    m_runsSpin->setValue(static_cast<int>(kDefaultNumRuns));
    m_runsSpin->setToolTip("Wie oft das Flowsheet mit verschiedenen Seeds gerechnet wird.\n"
                           "Lauf 1 nutzt genau den eingestellten Seed. Ein deterministisches\n"
                           "Flowsheet (ohne stochastische Bloecke) wird nur einmal gerechnet.");
    simulationBar->addWidget(m_runsSpin, 4, 1);

    // Kontinuum oder Partikel (Karten) - gilt fuer alle Stroeme der
    // Simulation (Methoden-Dokument 13.16, 13.20).
    simulationBar->addWidget(new QLabel("Darstellung:", flowsheetPanel), 5, 0);
    m_representationCombo = new QComboBox(flowsheetPanel);
    m_representationCombo->addItem("Kontinuum", static_cast<int>(stosis::ERepresentation::Continuum));
    m_representationCombo->addItem("Partikel (Karten)", static_cast<int>(stosis::ERepresentation::Particles));
    m_representationCombo->setToolTip("Kontinuum: Massenstromdichte auf dem Groessenraster.\n"
                                      "Partikel: gewichtete Karten mit freier Groesse; fuer die Anzeige werden sie\n"
                                      "aufs Groessenraster umgerechnet. Im Aufbau - die Units lernen Karten\n"
                                      "schrittweise; bis dahin meldet \"Simulieren\", welche Unit noch fehlt.");
    simulationBar->addWidget(m_representationCombo, 5, 1);

    // Karten je Stromscheibe N (Methoden-Dokument 13.19, 13.21); nur im
    // Partikelmodus wirksam.
    simulationBar->addWidget(new QLabel("Karten je Zeitschritt:", flowsheetPanel), 6, 0);
    m_cardsSpin = new QSpinBox(flowsheetPanel);
    m_cardsSpin->setRange(10, 1000000);
    m_cardsSpin->setValue(static_cast<int>(m_graph.GetCardSettings().cardsPerSlice));
    m_cardsSpin->setToolTip("Karten, die jedes Inlet in jedem Zeitschritt erzeugt (N). Mehr Karten = weniger\n"
                            "Rauschen (etwa 1/Wurzel(N)), aber mehr Rechenzeit. 20 % davon sind fuer die\n"
                            "Mindestzahl je belegtem Groessenbereich reserviert, der Rest geht nach Masse.");
    m_cardsSpin->setEnabled(false);
    simulationBar->addWidget(m_cardsSpin, 6, 1);
    connect(m_cardsSpin, &QSpinBox::valueChanged, this, [this](int cards)
        {
            stosis::SCardSettings settings = m_graph.GetCardSettings();
            settings.cardsPerSlice = static_cast<size_t>(cards);
            m_graph.SetCardSettings(settings);
            if (m_settingsWindow)
                m_settingsWindow->RefreshPreviews();
        });
    connect(m_representationCombo, &QComboBox::currentIndexChanged, this, [this]()
        {
            const auto representation = static_cast<stosis::ERepresentation>(m_representationCombo->currentData().toInt());
            m_graph.SetRepresentation(representation);
            m_cardsSpin->setEnabled(representation == stosis::ERepresentation::Particles);
            SyncAnalysisSettings();
            if (m_settingsWindow)
                m_settingsWindow->RefreshPreviews();
        });

    auto* simulateButton = new QPushButton("Simulieren", flowsheetPanel);
    simulateButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    connect(simulateButton, &QPushButton::clicked, this, &MainWindow::OnSimulate);
    simulationBar->addWidget(simulateButton, 0, 2, 7, 1);
    simulationBar->setColumnStretch(2, 1);
    flowsheetLayout->addLayout(simulationBar);

    // Dauer, dt und Seed gelten sofort (nicht erst beim Simulieren), damit die
    // Vorschauen im Einstellungsfenster schon den richtigen Verlauf zeigen.
    connect(m_durationSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::ApplyTimeGrid);
    connect(m_dtSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::ApplyTimeGrid);
    connect(m_runsSpin, &QSpinBox::valueChanged, this, &MainWindow::SyncAnalysisSettings);
    ApplyTimeGrid();
    connect(m_seedSpin, &QSpinBox::valueChanged, this, [this](int seed)
        {
            m_graph.SetRandomSeed(static_cast<std::uint64_t>(seed));
            if (m_settingsWindow)
                m_settingsWindow->RefreshPreviews();
        });

    // --- Rechts: Kurven ueber Heatmap --------------------------------
    // Beide gleich breit, damit die Zeitachsen exakt untereinander liegen.
    m_resultsCurveView = new ResultsCurveWindow(this);
    m_resultsCurveView->SetGraph(&m_graph);
    m_resultsHeatmapView = new ResultsHeatmapWindow(this);
    m_resultsHeatmapView->SetGraph(&m_graph);

    auto* resultsSplitter = new QSplitter(Qt::Vertical, this);
    resultsSplitter->addWidget(m_resultsCurveView);
    resultsSplitter->addWidget(m_resultsHeatmapView);
    resultsSplitter->setStretchFactor(0, 1);
    resultsSplitter->setStretchFactor(1, 1);
    resultsSplitter->setSizes({ 500, 500 });

    // --- Gesamtaufteilung: ~35 % Flowsheet, ~65 % Ergebnisse ---------
    auto* mainSplitter = new QSplitter(Qt::Horizontal, this);
    mainSplitter->addWidget(flowsheetPanel);
    mainSplitter->addWidget(resultsSplitter);
    mainSplitter->setStretchFactor(0, 35);
    mainSplitter->setStretchFactor(1, 65);
    mainSplitter->setSizes({ 350, 650 });
    setCentralWidget(mainSplitter);

    connect(m_canvas, &FlowsheetCanvasWidget::NodesChanged, this, [this]()
        {
            m_resultsCurveView->RefreshNodeList();
            m_resultsHeatmapView->RefreshNodeList();
            if (m_settingsWindow)
                m_settingsWindow->RefreshNodeList();
            if (m_analysisWindow)
                m_analysisWindow->RefreshNodeList();
            if (m_cardWindow)
                m_cardWindow->RefreshNodeList();
        });
    connect(m_canvas, &FlowsheetCanvasWidget::NodeDoubleClicked, this, &MainWindow::OnNodeDoubleClicked);
}

void MainWindow::OnSimulate()
{
    // Dauer und dt aus der Leiste ins gemeinsame Raster uebernehmen, das
    // Groessenraster kommt aus dem Einstellungsfenster (Reiter "Raster").
    ApplyTimeGrid();

    const size_t requestedRuns = static_cast<size_t>(m_runsSpin->value());
    const bool stochastic = m_graph.IsStochastic();

    // Fortschritt mit Abbrechen-Knopf - erscheint erst, wenn die Rechnung
    // laenger als eine halbe Sekunde dauert. Der Rechenkern meldet sich je
    // Zeitschritt (hoechstens alle 0,05 s); Abbrechen wirkt auch mitten im Lauf.
    QProgressDialog progressDialog("Simuliere ...", "Abbrechen", 0, 1000, this);
    progressDialog.setWindowTitle("Simulation");
    progressDialog.setWindowModality(Qt::WindowModal);
    progressDialog.setMinimumDuration(500);
    progressDialog.setMinimumWidth(420);
    const QChar dot(0x00B7);
    const auto duration = [](double seconds) { return QString::fromStdString(stosis::FormatDuration(seconds)); };
    const size_t runsDone = m_graph.RunSimulations(requestedRuns,
        stosis::CFlowsheetGraph::StepProgressCallback([&](const stosis::SSimulationProgress& progress)
        {
            const size_t run = std::min(progress.runsDone + (progress.runFinished ? 0 : 1), progress.runs);
            QString text = QString("Lauf %1 von %2").arg(run).arg(progress.runs);
            if (!progress.runFinished)
                text += QString(" %1 Zeitschritt %2 von %3").arg(dot).arg(progress.stepsDone).arg(progress.steps);
            text += QString("\nvergangen %1").arg(duration(progress.elapsedSeconds));
            const double remaining = progress.GetRemainingSeconds();
            if (remaining >= 0.0)
                text += QString(" %1 noch etwa %2").arg(dot).arg(duration(remaining));
            progressDialog.setLabelText(text);
            progressDialog.setValue(static_cast<int>(std::lround(1000.0 * progress.GetFraction())));
            QCoreApplication::processEvents();
            return !progressDialog.wasCanceled();
        }));
    progressDialog.setValue(progressDialog.maximum());

    // Ergebnisfenster neu zeichnen; die Zeit dafuer kommt ins Laufzeitprotokoll.
    stosis::CStopwatch guiClock;
    RefreshResultViews();
    if (m_cardWindow)
        m_cardWindow->RefreshNodeList();
    m_lastGuiSeconds = guiClock.GetSeconds();
    const stosis::SSimulationTiming& timing = m_graph.GetSimulationTiming();
    if (runsDone > 0)
        m_graph.AddLogLine("Ergebnisfenster neu gezeichnet in " + stosis::FormatDuration(m_lastGuiSeconds));
    if (m_logWindow)
        m_logWindow->Refresh(m_lastGuiSeconds);

    const QString took = duration(timing.totalSeconds);
    if (runsDone == 0 && timing.cancelled)
        statusBar()->showMessage(QString("Abgebrochen nach %1 in Lauf 1 - keine vollstaendigen Laeufe.").arg(took), 8000);
    else if (runsDone == 0)
        statusBar()->showMessage("Nicht simuliert - siehe Hinweise. Angezeigt werden die bisherigen Ergebnisse.", 8000);
    else if (!stochastic)
        statusBar()->showMessage(QString("Simulation abgeschlossen in %1 - Flowsheet ist deterministisch, 1 Lauf genuegt.").arg(took), 8000);
    else if (runsDone < requestedRuns)
        statusBar()->showMessage(QString("Abgebrochen nach %1 - Statistik ueber die %2 fertigen Laeufe.").arg(took).arg(runsDone), 8000);
    else
        statusBar()->showMessage(QString("Simulation abgeschlossen: %1 Laeufe in %2 (Details: Laufzeitprotokoll).")
            .arg(runsDone).arg(took), 8000);

    // Hinweise der Units (z.B. Stoff ohne Waermekapazitaet) nicht nur in
    // der Statusleiste verstecken - sie betreffen die Ergebnisse.
    const std::vector<std::string> warnings = m_graph.GetSimulationWarnings();
    if (!warnings.empty())
    {
        QStringList lines;
        for (const std::string& warning : warnings)
            lines << QString::fromStdString(warning);
        QMessageBox::warning(this, "Hinweise zur Simulation", lines.join("\n\n"));
    }
}

void MainWindow::RefreshResultViews()
{
    m_resultsCurveView->UpdateCurvesPublic(); // Markierung bleibt erhalten
    m_resultsHeatmapView->UpdateHeatmap();
    // Liste neu aufbauen: Ein Splitter kann seit dem letzten Mal stochastisch
    // geworden sein (die Auswertung zeigt nur Stroeme mit Zufall).
    if (m_analysisWindow)
        m_analysisWindow->RefreshNodeList();
}

SettingsWindow* MainWindow::EnsureSettingsWindow()
{
    if (!m_settingsWindow)
    {
        m_settingsWindow = new SettingsWindow(this);
        m_settingsWindow->SetGraph(&m_graph);
        m_settingsWindow->SetMaterialDatabase(&m_materials, m_materialLoadError);

        connect(m_settingsWindow, &SettingsWindow::SettingsApplied, this, [this]()
            {
                statusBar()->showMessage(
                    "Einstellungen uebernommen - \"Simulieren\" aktualisiert die Ergebnisse.");
            });
        // Modellwechsel zerstoert die alte Prozessor-Instanz - die
        // Ergebnisansichten halten Zeiger auf deren Ergebnisse und muessen
        // sie sofort neu holen, sonst zeigen sie auf freigegebenen Speicher.
        connect(m_settingsWindow, &SettingsWindow::ModelTypeChanged, this, &MainWindow::RefreshResultViews);
    }
    return m_settingsWindow;
}

void MainWindow::OnShowSettings()
{
    SettingsWindow* settings = EnsureSettingsWindow();
    settings->RefreshNodeList();
    settings->show();
    settings->raise();
    settings->activateWindow();
}

void MainWindow::OnShowMaterials()
{
    EnsureSettingsWindow()->ShowMaterials();
}

void MainWindow::ApplyTimeGrid()
{
    m_graph.ConfigureTime(m_durationSpin->value(), m_dtSpin->value());
    m_timeInfoLabel->setText(TimeGridInfo());
    if (m_settingsWindow)
        m_settingsWindow->RefreshPreviews();
    SyncAnalysisSettings();
}

QString MainWindow::TimeGridInfo() const
{
    const stosis::SSimulationGrid& grid = m_graph.GetGrid();
    const QLocale locale;
    const double dt = grid.GetDt();
    QString info = QString("= %1 pro s, %2 Zeitpunkte")
        .arg(locale.toString(1.0 / dt, 'g', 4))
        .arg(locale.toString(static_cast<qulonglong>(grid.numTimeSteps)));
    if (grid.numTimeSteps >= stosis::SSimulationGrid::kMaxTimePoints)
        info += " (Hoechstwert - dt vergroessern)";
    else if (std::abs(dt - m_dtSpin->value()) > 1e-9 * dt)
        info += QString(", dt angepasst auf %1 s").arg(locale.toString(dt, 'g', 6));
    return info;
}

void MainWindow::SyncAnalysisSettings()
{
    if (m_analysisWindow)
        m_analysisWindow->SetSimulationSettings(m_durationSpin->value(), m_dtSpin->value(),
            m_runsSpin->value(), m_representationCombo->currentData().toInt(), TimeGridInfo());
}

void MainWindow::OnShowAnalysis()
{
    if (!m_analysisWindow)
    {
        m_analysisWindow = new AnalysisWindow(this);
        m_analysisWindow->SetGraph(&m_graph);
        // Die Simulationsleiste des Auswertungsfensters schreibt in die
        // Leiste hier - eine Quelle fuer Dauer, dt und Laeufe.
        connect(m_analysisWindow, &AnalysisWindow::SimulationSettingsEdited, this,
            [this](double duration, double dt, int runs, int representation)
            {
                m_durationSpin->setValue(duration);
                m_dtSpin->setValue(dt);
                m_runsSpin->setValue(runs);
                m_representationCombo->setCurrentIndex(std::max(0, m_representationCombo->findData(representation)));
            });
        connect(m_analysisWindow, &AnalysisWindow::SimulateRequested, this, &MainWindow::OnSimulate);
        SyncAnalysisSettings();
    }
    m_analysisWindow->show();
    m_analysisWindow->raise();
    m_analysisWindow->activateWindow();
}

void MainWindow::OnShowLog()
{
    if (!m_logWindow)
    {
        m_logWindow = new SimulationLogWindow(this);
        m_logWindow->SetGraph(&m_graph);
        m_logWindow->Refresh(m_lastGuiSeconds);
    }
    m_logWindow->show();
    m_logWindow->raise();
    m_logWindow->activateWindow();
}

void MainWindow::OnShowCards()
{
    if (!m_cardWindow)
    {
        m_cardWindow = new CardWindow(this);
        m_cardWindow->SetGraph(&m_graph);
    }
    m_cardWindow->show();
    m_cardWindow->raise();
    m_cardWindow->activateWindow();
}

void MainWindow::OnNodeDoubleClicked(const QString& nodeId)
{
    EnsureSettingsWindow()->ShowNode(nodeId.toStdString());
}

void MainWindow::OnShowHeatmapDemo()
{
    if (!m_heatmapDemoWindow)
    {
        // Eigenes Fenster, aber mit dem Hauptfenster als Eltern - schliesst
        // sich dann zusammen mit ihm, statt das Programm offen zu halten.
        m_heatmapDemoWindow = new HeatmapDemoWindow(nullptr);
        m_heatmapDemoWindow->setParent(this, Qt::Window);
    }

    m_heatmapDemoWindow->show();
    m_heatmapDemoWindow->raise();
    m_heatmapDemoWindow->activateWindow();
}

void MainWindow::OnShowComparisonWindow()
{
    if (!m_comparisonWindow)
    {
        m_comparisonWindow = new ComparisonWindow(nullptr);
        m_comparisonWindow->setParent(this, Qt::Window);
    }

    m_comparisonWindow->show();
    m_comparisonWindow->raise();
    m_comparisonWindow->activateWindow();
}

void MainWindow::OnRunCudaTest()
{
    std::string info;
    const bool success = stosis::GetCudaDeviceInfo(info);

    if (success)
        QMessageBox::information(this, "CUDA-Test", QString::fromStdString(info));
    else
        QMessageBox::warning(this, "CUDA-Test", QString::fromStdString(info));
}
