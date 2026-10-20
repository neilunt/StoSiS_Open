#include "ComparisonWindow.h"
#include "ModelPanelWidget.h"
#include "BuiltinTransportUnits.h"
#include "SimulationResultsDiff.h"
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QPushButton>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>

ComparisonWindow::ComparisonWindow(QWidget* parent)
    : QWidget(parent)
{
    setWindowTitle("Vergleichsansicht");
    resize(1400, 700);

    stosis::RegisterBuiltinTransportUnits(m_registry);

    SetupUi();
    OnRunAll();
}

void ComparisonWindow::SetupUi()
{
    auto* mainLayout = new QVBoxLayout(this);

    auto* gridGroup = new QGroupBox("Gemeinsames Zeit-/Groessengitter & Startverteilung", this);
    auto* gridForm = new QFormLayout(gridGroup);

    m_totalTimeSpin = new QDoubleSpinBox(gridGroup);
    m_totalTimeSpin->setRange(0.1, 1000.0);
    m_totalTimeSpin->setValue(10.0);
    gridForm->addRow("Gesamtzeit:", m_totalTimeSpin);

    m_numTimeStepsSpin = new QSpinBox(gridGroup);
    m_numTimeStepsSpin->setRange(2, 5000);
    m_numTimeStepsSpin->setValue(200);
    gridForm->addRow("Anzahl Zeitschritte:", m_numTimeStepsSpin);

    m_numSizeClassesSpin = new QSpinBox(gridGroup);
    m_numSizeClassesSpin->setRange(5, 2000);
    m_numSizeClassesSpin->setValue(150);
    gridForm->addRow("Anzahl Groessenklassen:", m_numSizeClassesSpin);

    m_d0Spin = new QDoubleSpinBox(gridGroup);
    m_d0Spin->setRange(0.0, 200.0);
    m_d0Spin->setValue(60.0);
    gridForm->addRow("Startgroesse d0:", m_d0Spin);

    m_sigma0Spin = new QDoubleSpinBox(gridGroup);
    m_sigma0Spin->setRange(0.01, 100.0);
    m_sigma0Spin->setValue(5.0);
    gridForm->addRow("Anfangsbreite sigma0:", m_sigma0Spin);

    mainLayout->addWidget(gridGroup);

    auto* runButton = new QPushButton("Simulation ausfuehren", this);
    connect(runButton, &QPushButton::clicked, this, &ComparisonWindow::OnRunAll);
    mainLayout->addWidget(runButton);

    auto* panelsLayout = new QHBoxLayout();

    m_panel1 = new ModelPanelWidget(&m_registry, /*offerDiffOption=*/false, this);
    m_panel2 = new ModelPanelWidget(&m_registry, /*offerDiffOption=*/false, this);
    m_panel3 = new ModelPanelWidget(&m_registry, /*offerDiffOption=*/true, this);

    panelsLayout->addWidget(m_panel1);
    panelsLayout->addWidget(m_panel2);
    panelsLayout->addWidget(m_panel3);

    mainLayout->addLayout(panelsLayout, 1);

    // Wenn in Spalte drei zwischen echtem Modell und Differenz-Modus
    // gewechselt wird, direkt neu berechnen, ohne dass extra auf den
    // Knopf geklickt werden muss.
    connect(m_panel3, &ModelPanelWidget::ModelChanged, this, &ComparisonWindow::OnRunAll);
}

void ComparisonWindow::UpdateGridConfig()
{
    const size_t numClasses = static_cast<size_t>(m_numSizeClassesSpin->value());
    const size_t numTimeSteps = static_cast<size_t>(m_numTimeStepsSpin->value());
    const double totalTime = m_totalTimeSpin->value();
    const double d0 = m_d0Spin->value();
    const double sigma0 = m_sigma0Spin->value();

    m_panel1->SetGridConfig(numClasses, 0.0, 200.0, numTimeSteps, totalTime, d0, sigma0);
    m_panel2->SetGridConfig(numClasses, 0.0, 200.0, numTimeSteps, totalTime, d0, sigma0);
    m_panel3->SetGridConfig(numClasses, 0.0, 200.0, numTimeSteps, totalTime, d0, sigma0);
}

void ComparisonWindow::OnRunAll()
{
    UpdateGridConfig();

    m_panel1->RunSimulation();
    m_panel2->RunSimulation();

    if (m_panel3->IsDiffMode())
    {
        const auto* results1 = m_panel1->GetResults();
        const auto* results2 = m_panel2->GetResults();
        if (results1 && results2)
        {
            m_diffResults = stosis::ComputeDifference(*results1, *results2);
            m_panel3->SetExternalResults(&m_diffResults, /*divergingColorScale=*/true);
        }
    }
    else
    {
        m_panel3->RunSimulation();
    }
}