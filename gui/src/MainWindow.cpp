#include "MainWindow.h"
#include "ComparisonWindow.h"
#include "HeatmapDemoWindow.h"
#include "CudaTest.h"
#include "FlowsheetCanvasWidget.h"
#include "ResultsCurveWindow.h"
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
#include <QGridLayout>
#include <QProgressDialog>
#include <QCoreApplication>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QHBoxLayout>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("StoSiS");
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
    // (GetDefaultInletTimeProfile in InletUnit.h, Normal d0 = 100 um,
    // sigma = 25 um) - dieselben bekommt jedes neu angelegte Inlet.
    // Raster: logarithmisch 1 um bis 1 mm mit 300 Klassen (Methoden 2.5).

    m_graph.ConfigureGrid(300, 1e-6, 1e-3, 200, 40.0);
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

    // Simulationsleiste: Dauer und Seed untereinander, "Simulieren" daneben.
    auto* simulationBar = new QGridLayout();
    simulationBar->addWidget(new QLabel("Simulationsdauer [s]:", flowsheetPanel), 0, 0);
    m_durationSpin = new QDoubleSpinBox(flowsheetPanel);
    m_durationSpin->setRange(0.1, 1.0e6);
    m_durationSpin->setValue(m_graph.GetGrid().totalTime);
    simulationBar->addWidget(m_durationSpin, 0, 1);

    simulationBar->addWidget(new QLabel("Seed:", flowsheetPanel), 1, 0);
    m_seedSpin = new QSpinBox(flowsheetPanel);
    m_seedSpin->setRange(0, 2147483647);
    m_seedSpin->setValue(static_cast<int>(m_graph.GetGrid().randomSeed));
    m_seedSpin->setToolTip("Startwert fuer alle Zufallszahlen: gleicher Seed = gleiches Ergebnis.\n"
                           "Jeder stochastische Block leitet daraus seinen eigenen Zufallsstrom ab.");
    simulationBar->addWidget(m_seedSpin, 1, 1);

    simulationBar->addWidget(new QLabel("Anzahl Laeufe:", flowsheetPanel), 2, 0);
    m_runsSpin = new QSpinBox(flowsheetPanel);
    m_runsSpin->setRange(1, 100000);
    m_runsSpin->setValue(static_cast<int>(kDefaultNumRuns));
    m_runsSpin->setToolTip("Wie oft das Flowsheet mit verschiedenen Seeds gerechnet wird.\n"
                           "Lauf 1 nutzt genau den eingestellten Seed. Ein deterministisches\n"
                           "Flowsheet (ohne stochastische Bloecke) wird nur einmal gerechnet.");
    simulationBar->addWidget(m_runsSpin, 2, 1);

    auto* simulateButton = new QPushButton("Simulieren", flowsheetPanel);
    simulateButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    connect(simulateButton, &QPushButton::clicked, this, &MainWindow::OnSimulate);
    simulationBar->addWidget(simulateButton, 0, 2, 3, 1);
    simulationBar->setColumnStretch(2, 1);
    flowsheetLayout->addLayout(simulationBar);

    // Dauer und Seed gelten sofort (nicht erst beim Simulieren), damit die
    // Vorschauen im Einstellungsfenster schon den richtigen Verlauf zeigen.
    connect(m_durationSpin, &QDoubleSpinBox::valueChanged, this, [this](double duration)
        {
            const stosis::SSimulationGrid grid = m_graph.GetGrid();
            m_graph.ConfigureGrid(grid.numSizeClasses, grid.sizeMin, grid.sizeMax, grid.numTimeSteps, duration, grid.sizeGridType);
            if (m_settingsWindow)
                m_settingsWindow->RefreshPreviews();
        });
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
        });
    connect(m_canvas, &FlowsheetCanvasWidget::NodeDoubleClicked, this, &MainWindow::OnNodeDoubleClicked);
}

void MainWindow::OnSimulate()
{
    // Dauer aus der Leiste ins gemeinsame Raster uebernehmen, der Rest
    // des Rasters kommt aus dem Einstellungsfenster (Reiter "Raster").
    const stosis::SSimulationGrid grid = m_graph.GetGrid();
    m_graph.ConfigureGrid(grid.numSizeClasses, grid.sizeMin, grid.sizeMax,
        grid.numTimeSteps, m_durationSpin->value(), grid.sizeGridType);

    const size_t requestedRuns = static_cast<size_t>(m_runsSpin->value());
    const bool stochastic = m_graph.IsStochastic();

    // Fortschritt mit Abbrechen-Knopf - erscheint erst, wenn die Rechnung
    // laenger als eine halbe Sekunde dauert.
    QProgressDialog progressDialog("Simuliere ...", "Abbrechen", 0,
        static_cast<int>(stochastic ? requestedRuns : 1), this);
    progressDialog.setWindowTitle("Simulation");
    progressDialog.setWindowModality(Qt::WindowModal);
    progressDialog.setMinimumDuration(500);
    const size_t runsDone = m_graph.RunSimulations(requestedRuns, [&](size_t done, size_t total)
        {
            progressDialog.setLabelText(QString("Lauf %1 von %2 ...").arg(done).arg(total));
            progressDialog.setValue(static_cast<int>(done));
            QCoreApplication::processEvents();
            return !progressDialog.wasCanceled();
        });
    progressDialog.setValue(progressDialog.maximum());

    RefreshResultViews();
    if (!stochastic)
        statusBar()->showMessage("Simulation abgeschlossen - Flowsheet ist deterministisch, 1 Lauf genuegt.", 8000);
    else if (runsDone < requestedRuns)
        statusBar()->showMessage(QString("Abgebrochen - Statistik ueber die %1 fertigen Laeufe.").arg(runsDone), 8000);
    else
        statusBar()->showMessage(QString("Simulation abgeschlossen: %1 Laeufe.").arg(runsDone), 8000);

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
