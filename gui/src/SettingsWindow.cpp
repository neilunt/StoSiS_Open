#include "SettingsWindow.h"
#include "InletEditorWidget.h"
#include "ProcessNodeEditorWidget.h"
#include "SplitterEditorWidget.h"
#include "MaterialsPage.h"
#include "SieveGridWidget.h"
#include <QTabWidget>
#include <QComboBox>
#include <QStackedWidget>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QGroupBox>
#include <QGridLayout>
#include <QStringList>

SettingsWindow::SettingsWindow(QWidget* parent)
    // Qt::Window: eigenes Fenster, aber mit Eltern - schliesst sich
    // dadurch zusammen mit dem Hauptfenster.
    : QWidget(parent, Qt::Window)
{
    setWindowTitle("Einstellungen");
    resize(640, 860);
    SetupUi();
}

void SettingsWindow::SetupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    m_tabs = new QTabWidget(this);
    m_nodeTab = CreateNodeTab();
    m_tabs->addTab(m_nodeTab, "Knoten");
    m_tabs->addTab(CreateGridTab(), "Raster");
    m_materialsPage = new MaterialsPage(this);
    m_tabs->addTab(m_materialsPage, "Materialien");
    // Neu geladene Stoffliste auch in der Inlet-Auswahl zeigen.
    connect(m_materialsPage, &MaterialsPage::DatabaseReloaded, this, [this]() { m_inletEditor->RefreshMaterialList(); });
    // Pruefung im Reiter "Raster" aktuell halten (Inlet geaendert, Reiter gewechselt).
    connect(this, &SettingsWindow::SettingsApplied, this, &SettingsWindow::UpdateGridCheck);
    connect(m_tabs, &QTabWidget::currentChanged, this, &SettingsWindow::UpdateGridCheck);
    mainLayout->addWidget(m_tabs);
}

QWidget* SettingsWindow::CreateNodeTab()
{
    auto* tab = new QWidget(this);
    auto* layout = new QVBoxLayout(tab);

    auto* selectionLayout = new QHBoxLayout();
    selectionLayout->addWidget(new QLabel("Knoten:", tab));
    m_nodeCombo = new QComboBox(tab);
    connect(m_nodeCombo, &QComboBox::currentIndexChanged, this, &SettingsWindow::OnNodeSelectionChanged);
    selectionLayout->addWidget(m_nodeCombo, 1);
    layout->addLayout(selectionLayout);

    m_pages = new QStackedWidget(tab);

    auto* nonePage = new QLabel("Kein Knoten ausgewaehlt.\n\n"
                                "Tipp: Doppelklick auf eine Box im Flowsheet.", m_pages);
    nonePage->setAlignment(Qt::AlignCenter);
    m_pages->insertWidget(PageNone, nonePage);

    m_inletEditor = new InletEditorWidget(m_pages);
    m_pages->insertWidget(PageInlet, m_inletEditor);

    m_processEditor = new ProcessNodeEditorWidget(m_pages);
    m_pages->insertWidget(PageProcess, m_processEditor);

    auto* outletPage = new QLabel("Ein Outlet hat (noch) keine Einstellungen.\n"
                                  "Seine Ergebnisse zeigen Kurven und Heatmap.", m_pages);
    outletPage->setAlignment(Qt::AlignCenter);
    m_pages->insertWidget(PageOutlet, outletPage);

    m_splitterEditor = new SplitterEditorWidget(m_pages);
    m_pages->insertWidget(PageSplitter, m_splitterEditor);

    auto* mergerPage = new QLabel("Ein Merger summiert alle Eingaenge\n"
                                  "(deterministisch, keine Einstellungen).", m_pages);
    mergerPage->setAlignment(Qt::AlignCenter);
    m_pages->insertWidget(PageMerger, mergerPage);

    layout->addWidget(m_pages, 1);

    connect(m_inletEditor, &InletEditorWidget::Applied, this, &SettingsWindow::SettingsApplied);
    connect(m_processEditor, &ProcessNodeEditorWidget::Applied, this, &SettingsWindow::SettingsApplied);
    connect(m_splitterEditor, &SplitterEditorWidget::Applied, this, &SettingsWindow::SettingsApplied);
    connect(m_processEditor, &ProcessNodeEditorWidget::ModelTypeChanged, this, &SettingsWindow::ModelTypeChanged);

    return tab;
}

QWidget* SettingsWindow::CreateGridTab()
{
    auto* tab = new QWidget(this);
    auto* layout = new QVBoxLayout(tab);
    auto* form = new QFormLayout();
    const QString unit = QString::fromUtf8(stosis::kSizeDisplayUnit);

    // Groessen werden in um eingegeben, intern in m gespeichert.
    m_gridTypeCombo = new QComboBox(tab);
    m_gridTypeCombo->addItem("logarithmisch (gleich viele Klassen je Dekade)", static_cast<int>(stosis::ESizeGridType::Logarithmic));
    m_gridTypeCombo->addItem("linear (gleiche Abstaende)", static_cast<int>(stosis::ESizeGridType::Linear));
    form->addRow("Rasterart:", m_gridTypeCombo);

    m_sizeMinSpin = new QDoubleSpinBox(tab);
    m_sizeMinSpin->setRange(0.0, 1.0e6);
    m_sizeMinSpin->setDecimals(4);
    form->addRow("Kleinste Groesse d_min [" + unit + "]:", m_sizeMinSpin);

    m_sizeMaxSpin = new QDoubleSpinBox(tab);
    m_sizeMaxSpin->setRange(0.0, 1.0e6);
    m_sizeMaxSpin->setDecimals(4);
    form->addRow("Groesste Groesse d_max [" + unit + "]:", m_sizeMaxSpin);

    m_numClassesSpin = new QSpinBox(tab);
    m_numClassesSpin->setRange(2, 100000);
    form->addRow("Anzahl Groessenklassen:", m_numClassesSpin);

    m_gridInfoLabel = new QLabel(tab);
    m_gridInfoLabel->setWordWrap(true);
    form->addRow("", m_gridInfoLabel);

    // Zeitraster (Dauer und Zeitschritt dt) steht in der
    // Simulationsleiste des Hauptfensters und im Auswertungsfenster.
    auto* timeHint = new QLabel("Dauer und Zeitschritt dt: in der Simulationsleiste des Hauptfensters\n"
                                "(oder im Fenster \"Auswertung\").", tab);
    timeHint->setStyleSheet("color: gray;");
    form->addRow("Zeitraster:", timeHint);

    layout->addLayout(form);

    // Groessenraster und Siebsatz auf derselben Achse (Methoden-Dokument 13.23).
    auto* sieveGroup = new QGroupBox("Groessenraster und Siebsatz", tab);
    auto* sieveLayout = new QVBoxLayout(sieveGroup);
    m_sieveWidget = new SieveGridWidget(sieveGroup);
    sieveLayout->addWidget(m_sieveWidget);
    auto* sieveHint = new QLabel(QString("Raster: Stuetzstellen, auf denen das Kontinuum rechnet und alle Ergebnisse "
                                         "angezeigt werden - bestimmt die Aufloesung. Siebsatz: Groessenbereiche 1 %1 * 2^k, "
                                         "nach denen im Partikelmodus jeder belegte Bereich eine Mindestzahl an Karten bekommt. "
                                         "Beide sind unabhaengig: Ein anderes Raster aendert die Karten nicht.").arg(unit),
                                 sieveGroup);
    sieveHint->setWordWrap(true);
    sieveHint->setStyleSheet("color: gray;");
    sieveLayout->addWidget(sieveHint);
    layout->addWidget(sieveGroup);

    // Richtwerte (Methoden-Dokument 2.6). "Einsetzen" fuellt nur die Felder,
    // uebernommen wird wie immer erst mit "Uebernehmen".
    struct SSuggestion
    {
        const char* process;
        double sizeMin; // m
        double sizeMax; // m
        int numClasses;
    };
    static const SSuggestion suggestions[] = {
        { "Agglomeration von Feststoffen (z. B. Sand, Kohle)", 1e-6, 1e-3, 300 },
        { "Mahlen und Bruch (feine Bruchstuecke)", 0.1e-6, 1e-3, 400 },
        { "Keimbildung und Wachstum (Nanopartikel aus der Gasphase)", 0.3e-9, 10e-6, 450 },
        { "Keimbildung bis Agglomerat", 1e-9, 1e-3, 600 },
    };
    auto* suggestionGroup = new QGroupBox("Richtwerte (logarithmisch, etwa 100 Klassen je Dekade)", tab);
    auto* grid = new QGridLayout(suggestionGroup);
    const auto formatSize = [&unit](double size)
    {
        if (size < 1e-6)
            return QString::number(size * 1e9, 'g', 6) + " nm";
        if (size >= 1e-3)
            return QString::number(size * 1e3, 'g', 6) + " mm";
        return QString::number(size * 1e6, 'g', 6) + " " + unit;
    };
    int row = 0;
    for (const SSuggestion& suggestion : suggestions)
    {
        auto* processLabel = new QLabel(suggestion.process, suggestionGroup);
        processLabel->setWordWrap(true);
        grid->addWidget(processLabel, row, 0);
        grid->addWidget(new QLabel(formatSize(suggestion.sizeMin) + " - " + formatSize(suggestion.sizeMax), suggestionGroup), row, 1);
        grid->addWidget(new QLabel(QString::number(suggestion.numClasses) + " Klassen", suggestionGroup), row, 2);
        auto* useButton = new QPushButton("Einsetzen", suggestionGroup);
        connect(useButton, &QPushButton::clicked, this, [this, suggestion]()
            {
                m_gridTypeCombo->setCurrentIndex(m_gridTypeCombo->findData(static_cast<int>(stosis::ESizeGridType::Logarithmic)));
                m_sizeMinSpin->setValue(suggestion.sizeMin * stosis::kSizeDisplayFactor);
                m_sizeMaxSpin->setValue(suggestion.sizeMax * stosis::kSizeDisplayFactor);
                m_numClassesSpin->setValue(suggestion.numClasses);
            });
        grid->addWidget(useButton, row, 3);
        ++row;
    }
    grid->setColumnStretch(0, 1);
    auto* rules = new QLabel("Faustregeln: d_max etwa 10-mal die groesste Inlet-Groesse, wenn koaguliert wird "
                             "(1000 Partikel zusammen sind erst 10-mal so gross); d_min etwa ein Drittel der "
                             "kleinsten erwarteten Groesse (Inlet, Bruchstuecke, Keime).", suggestionGroup);
    rules->setWordWrap(true);
    grid->addWidget(rules, row, 0, 1, 4);
    layout->addWidget(suggestionGroup);

    // Pruefung: Passen die Units (geprueft werden die Inlet-Verteilungen) ins Raster?
    auto* checkGroup = new QGroupBox("Pruefung des eingegebenen Rasters", tab);
    auto* checkLayout = new QVBoxLayout(checkGroup);
    m_gridCheckLabel = new QLabel(checkGroup);
    m_gridCheckLabel->setWordWrap(true);
    checkLayout->addWidget(m_gridCheckLabel);
    layout->addWidget(checkGroup);

    auto* hint = new QLabel("Die Simulationsdauer wird im Hauptfenster unter dem "
                            "Flowsheet eingestellt.", tab);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto* applyButton = new QPushButton("Uebernehmen", tab);
    connect(applyButton, &QPushButton::clicked, this, &SettingsWindow::OnApplyGrid);
    layout->addWidget(applyButton);
    layout->addStretch();

    connect(m_gridTypeCombo, &QComboBox::currentIndexChanged, this, &SettingsWindow::UpdateGridCheck);
    connect(m_sizeMinSpin, &QDoubleSpinBox::valueChanged, this, &SettingsWindow::UpdateGridCheck);
    connect(m_sizeMaxSpin, &QDoubleSpinBox::valueChanged, this, &SettingsWindow::UpdateGridCheck);
    connect(m_numClassesSpin, &QSpinBox::valueChanged, this, &SettingsWindow::UpdateGridCheck);

    return tab;
}

stosis::SSizeGrid SettingsWindow::EnteredSizeGrid() const
{
    stosis::SSizeGrid sizes;
    sizes.type = static_cast<stosis::ESizeGridType>(m_gridTypeCombo->currentData().toInt());
    sizes.numClasses = static_cast<size_t>(m_numClassesSpin->value());
    sizes.sizeMin = m_sizeMinSpin->value() / stosis::kSizeDisplayFactor;
    sizes.sizeMax = m_sizeMaxSpin->value() / stosis::kSizeDisplayFactor;
    return sizes;
}

void SettingsWindow::UpdateGridCheck()
{
    if (!m_gridInfoLabel || !m_gridCheckLabel)
        return;
    const stosis::SSizeGrid sizes = EnteredSizeGrid();
    const QString unit = QString::fromUtf8(stosis::kSizeDisplayUnit);
    if (m_sieveWidget)
        m_sieveWidget->SetGrid(sizes, m_graph ? m_graph->GetCardSettings() : stosis::SCardSettings());
    if (!sizes.IsValid())
    {
        m_gridInfoLabel->setText(sizes.IsLogarithmic() && sizes.sizeMin <= 0.0
            ? "Das logarithmische Raster braucht d_min > 0." : "d_max muss groesser als d_min sein.");
        m_gridCheckLabel->clear();
        return;
    }
    if (sizes.IsLogarithmic())
        m_gridInfoLabel->setText(QString("%1 Klassen je Dekade; benachbarte Groessen unterscheiden sich um %2 %.")
            .arg(sizes.GetClassesPerDecade(), 0, 'f', 1).arg((sizes.GetRatio() - 1.0) * 100.0, 0, 'f', 2));
    else
        m_gridInfoLabel->setText(QString("Abstand benachbarter Groessen: %1 %2.")
            .arg(sizes.GetStep() * stosis::kSizeDisplayFactor, 0, 'g', 4).arg(unit));

    const std::vector<std::string> notes = m_graph ? m_graph->CheckSizeGrid(sizes) : std::vector<std::string>();
    if (notes.empty())
    {
        m_gridCheckLabel->setStyleSheet("QLabel { color: #1E823C; }");
        m_gridCheckLabel->setText("Alle Inlet-Verteilungen liegen im Raster (hoechstens 0,1 % ausserhalb).");
        return;
    }
    QStringList lines;
    for (const std::string& note : notes)
        lines << QString::fromStdString(note);
    m_gridCheckLabel->setStyleSheet("QLabel { color: #C82828; }");
    m_gridCheckLabel->setText(lines.join("\n"));
}

void SettingsWindow::SetGraph(stosis::CFlowsheetGraph* graph)
{
    m_graph = graph;
    m_inletEditor->SetGraph(graph);
    m_processEditor->SetGraph(graph);
    m_splitterEditor->SetGraph(graph);
    LoadGridValues();
    RefreshNodeList();
}

void SettingsWindow::SetMaterialDatabase(stosis::CMaterialDatabase* database, const QString& loadError)
{
    m_materialsPage->SetDatabase(database, loadError);
    m_inletEditor->SetMaterialDatabase(database);
}

void SettingsWindow::ShowMaterials()
{
    m_tabs->setCurrentWidget(m_materialsPage);
    show();
    raise();
    activateWindow();
}

void SettingsWindow::RefreshNodeList()
{
    const QString previousId = m_nodeCombo->currentData().toString();

    m_nodeCombo->blockSignals(true);
    m_nodeCombo->clear();
    if (m_graph)
    {
        for (const stosis::SFlowsheetNode& node : m_graph->GetNodes())
        {
            m_nodeCombo->addItem(QString::fromStdString(node.name) + " (" +
                stosis::GetNodeTypeLabel(node.type) + ")",
                QString::fromStdString(node.id));
        }
    }
    const int index = m_nodeCombo->findData(previousId);
    m_nodeCombo->setCurrentIndex(index >= 0 ? index : (m_nodeCombo->count() > 0 ? 0 : -1));
    m_nodeCombo->blockSignals(false);

    // Signale waren blockiert -> passende Seite hier explizit laden.
    OnNodeSelectionChanged(m_nodeCombo->currentIndex());
    UpdateGridCheck();
}

void SettingsWindow::ShowNode(const std::string& nodeId)
{
    RefreshNodeList();
    const int index = m_nodeCombo->findData(QString::fromStdString(nodeId));
    if (index >= 0)
        m_nodeCombo->setCurrentIndex(index); // loest OnNodeSelectionChanged aus

    m_tabs->setCurrentWidget(m_nodeTab);
    show();
    raise();
    activateWindow();
}

void SettingsWindow::OnNodeSelectionChanged(int index)
{
    if (!m_graph || index < 0)
    {
        m_pages->setCurrentIndex(PageNone);
        return;
    }

    const std::string nodeId = m_nodeCombo->itemData(index).toString().toStdString();
    for (const stosis::SFlowsheetNode& node : m_graph->GetNodes())
    {
        if (node.id != nodeId)
            continue;

        switch (node.type)
        {
        case stosis::ENodeType::Inlet:
            m_inletEditor->SetNode(nodeId);
            m_pages->setCurrentIndex(PageInlet);
            break;
        case stosis::ENodeType::Process:
            m_processEditor->SetNode(nodeId);
            m_pages->setCurrentIndex(PageProcess);
            break;
        case stosis::ENodeType::Outlet:
            m_pages->setCurrentIndex(PageOutlet);
            break;
        case stosis::ENodeType::Splitter:
            m_splitterEditor->SetNode(nodeId);
            m_pages->setCurrentIndex(PageSplitter);
            break;
        case stosis::ENodeType::Merger:
            m_pages->setCurrentIndex(PageMerger);
            break;
        }
        return;
    }
    m_pages->setCurrentIndex(PageNone);
}

void SettingsWindow::LoadGridValues()
{
    if (!m_graph)
        return;
    const stosis::SSimulationGrid& grid = m_graph->GetGrid();
    m_gridTypeCombo->setCurrentIndex(m_gridTypeCombo->findData(static_cast<int>(grid.sizeGridType)));
    m_numClassesSpin->setValue(static_cast<int>(grid.numSizeClasses));
    m_sizeMinSpin->setValue(grid.sizeMin * stosis::kSizeDisplayFactor);
    m_sizeMaxSpin->setValue(grid.sizeMax * stosis::kSizeDisplayFactor);
    UpdateGridCheck();
}

void SettingsWindow::OnApplyGrid()
{
    if (!m_graph)
        return;

    const stosis::SSizeGrid sizes = EnteredSizeGrid();
    if (!sizes.IsValid())
    {
        QMessageBox::warning(this, "Raster", sizes.IsLogarithmic() && sizes.sizeMin <= 0.0
            ? "Das logarithmische Raster braucht d_min > 0." : "d_max muss groesser als d_min sein.");
        return;
    }

    // Zeitraster (Dauer, dt) bleibt, wie es ist - das gehoert zur Simulationsleiste.
    const stosis::SSimulationGrid& grid = m_graph->GetGrid();
    m_graph->ConfigureGrid(sizes.numClasses, sizes.sizeMin, sizes.sizeMax,
        grid.numTimeSteps, grid.totalTime, sizes.type);
    // Die Vorschauen zeigen immer das aktuelle Raster.
    RefreshPreviews();
    emit SettingsApplied();
}

void SettingsWindow::RefreshPreviews()
{
    UpdateGridCheck();
    m_inletEditor->RefreshGridPreview();
    m_splitterEditor->UpdatePreview();
}
