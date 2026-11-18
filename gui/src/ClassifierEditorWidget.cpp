#include "ClassifierEditorWidget.h"
#include "GradeEfficiencyPreview.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QGroupBox>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QStringList>
#include <algorithm>
#include <cmath>

namespace
{
    // Decimal comma, about four digits.
    QString Number(double value)
    {
        return QString::number(value, 'g', 4).replace('.', ',');
    }

    // Size [m] at which T = 0.5 (bisection on log d; T rises with d);
    // 0 if T stays on one side between 1 nm and 10 m.
    double HalfShareSize(const std::function<double(double)>& share)
    {
        double low = 1e-9, high = 10.0;
        if (!(share(low) < 0.5 && share(high) >= 0.5))
            return 0.0;
        for (int i = 0; i < 100; ++i)
        {
            const double mid = std::sqrt(low * high);
            (share(mid) < 0.5 ? low : high) = mid;
        }
        return std::sqrt(low * high);
    }
}

ClassifierEditorWidget::ClassifierEditorWidget(QWidget* parent)
    : QWidget(parent)
{
    SetupUi();
}

void ClassifierEditorWidget::SetupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    auto* group = new QGroupBox("Trennung nach Sinkgeschwindigkeit", this);
    auto* form = new QFormLayout(group);

    m_fluidCombo = new QComboBox(group);
    form->addRow("Fluid:", m_fluidCombo);
    m_fluidInfoLabel = new QLabel(group);
    m_fluidInfoLabel->setWordWrap(true);
    form->addRow(QString(), m_fluidInfoLabel);

    m_modelCombo = new QComboBox(group);
    m_modelCombo->addItem("Plitt", static_cast<int>(stosis::EScreenModel::Plitt));
    m_modelCombo->addItem("Molerus-Hoffmann", static_cast<int>(stosis::EScreenModel::MolerusHoffmann));
    m_modelCombo->addItem("Teipel-Hennig", static_cast<int>(stosis::EScreenModel::TeipelHennig));
    form->addRow("Trennkurve:", m_modelCombo);

    m_cutVelocitySpin = new QDoubleSpinBox(group);
    m_cutVelocitySpin->setRange(1e-6, 1000.0);
    m_cutVelocitySpin->setDecimals(6);
    m_cutVelocitySpin->setSingleStep(0.1);
    m_cutVelocitySpin->setSuffix(" m/s");
    m_cutVelocitySpin->setToolTip("Sinkgeschwindigkeit, bei der etwa die Haelfte ins Schwergut geht: beim Windsichter "
                                  "die Luftgeschwindigkeit nach oben, beim Absetzbecken die Oberflaechenbeschickung Q/A.");
    form->addRow("Trenngeschwindigkeit w_cut:", m_cutVelocitySpin);

    m_alphaSpin = new QDoubleSpinBox(group);
    m_alphaSpin->setRange(0.0, 100.0);
    m_alphaSpin->setDecimals(3);
    m_alphaSpin->setSingleStep(0.5);
    form->addRow("Trennschaerfe alpha:", m_alphaSpin);

    m_betaLabel = new QLabel("beta:", group);
    m_betaSpin = new QDoubleSpinBox(group);
    m_betaSpin->setRange(0.001, 100.0);
    m_betaSpin->setDecimals(3);
    m_betaSpin->setSingleStep(0.1);
    form->addRow(m_betaLabel, m_betaSpin);

    m_offsetLabel = new QLabel("Offset (Leichtgut im Schwergut):", group);
    m_offsetSpin = new QDoubleSpinBox(group);
    m_offsetSpin->setRange(0.0, 1.0);
    m_offsetSpin->setDecimals(3);
    m_offsetSpin->setSingleStep(0.01);
    form->addRow(m_offsetLabel, m_offsetSpin);

    m_formulaLabel = new QLabel(group);
    m_formulaLabel->setTextFormat(Qt::RichText);
    m_formulaLabel->setWordWrap(true);
    m_formulaLabel->setStyleSheet(kFormulaLabelStyle);
    form->addRow(m_formulaLabel);

    m_errorLabel = new QLabel(group);
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setStyleSheet("color: rgb(200, 40, 40);");
    m_errorLabel->setVisible(false);
    form->addRow(m_errorLabel);
    mainLayout->addWidget(group);

    auto* previewGroup = new QGroupBox("Vorschau (Stoffe des Flowsheets, 293,15 K und 1,013 bar)", this);
    auto* previewLayout = new QVBoxLayout(previewGroup);
    m_preview = new GradeEfficiencyPreview("T(d) ins Schwergut", previewGroup);
    previewLayout->addWidget(m_preview, 1);
    m_previewNote = new QLabel(previewGroup);
    m_previewNote->setWordWrap(true);
    m_previewNote->setVisible(false);
    previewLayout->addWidget(m_previewNote);
    mainLayout->addWidget(previewGroup, 1);

    m_applyButton = new QPushButton("Uebernehmen", this);
    mainLayout->addWidget(m_applyButton);

    connect(m_fluidCombo, &QComboBox::currentIndexChanged, this, &ClassifierEditorWidget::OnValuesChanged);
    connect(m_modelCombo, &QComboBox::currentIndexChanged, this, &ClassifierEditorWidget::OnValuesChanged);
    for (QDoubleSpinBox* spin : { m_cutVelocitySpin, m_alphaSpin, m_betaSpin, m_offsetSpin })
        connect(spin, &QDoubleSpinBox::valueChanged, this, &ClassifierEditorWidget::OnValuesChanged);
    connect(m_applyButton, &QPushButton::clicked, this, &ClassifierEditorWidget::OnApply);
}

void ClassifierEditorWidget::SetGraph(stosis::CFlowsheetGraph* graph)
{
    m_graph = graph;
    LoadNode();
}

void ClassifierEditorWidget::SetMaterialDatabase(const stosis::CMaterialDatabase* materials)
{
    m_materials = materials;
    RefreshFluidList();
    UpdatePreview();
}

void ClassifierEditorWidget::SetNode(const std::string& nodeId)
{
    m_nodeId = nodeId;
    LoadNode();
}

void ClassifierEditorWidget::RefreshFluidList()
{
    const QString previous = m_fluidCombo->currentData().toString();
    const QSignalBlocker blocker(m_fluidCombo);
    m_fluidCombo->clear();
    if (m_materials)
        for (const stosis::SMaterial& material : m_materials->GetMaterials())
        {
            if (material.state != "gas" && material.state != "liquid")
                continue;
            const QString name = QString::fromStdString(material.name);
            m_fluidCombo->addItem(name + (material.state == "gas" ? " (Gas)" : " (fluessig)"), name);
        }
    if (!previous.isEmpty())
        SelectFluid(previous.toStdString());
}

void ClassifierEditorWidget::SelectFluid(const std::string& name)
{
    const QString key = QString::fromStdString(name);
    int index = m_fluidCombo->findData(key);
    if (index < 0)
    {
        m_fluidCombo->addItem(key + " (nicht in der Materialdatei)", key);
        index = m_fluidCombo->count() - 1;
    }
    m_fluidCombo->setCurrentIndex(index);
}

void ClassifierEditorWidget::LoadNode()
{
    const stosis::CClassifierUnit* classifier = m_graph ? m_graph->GetClassifier(m_nodeId) : nullptr;
    const stosis::SClassifierSettings settings = classifier ? classifier->GetSettings() : stosis::SClassifierSettings{};
    {
        const QSignalBlocker b0(m_modelCombo), b1(m_cutVelocitySpin), b2(m_alphaSpin), b3(m_betaSpin), b4(m_offsetSpin),
            b5(m_fluidCombo);
        m_modelCombo->setCurrentIndex(std::max(0, m_modelCombo->findData(static_cast<int>(settings.model))));
        m_cutVelocitySpin->setValue(settings.cutVelocity);
        m_alphaSpin->setValue(settings.alpha);
        m_betaSpin->setValue(settings.beta);
        m_offsetSpin->setValue(settings.offset);
        SelectFluid(settings.fluid);
    }
    OnValuesChanged();
}

stosis::SClassifierSettings ClassifierEditorWidget::CurrentSettings() const
{
    stosis::SClassifierSettings settings;
    settings.model = static_cast<stosis::EScreenModel>(m_modelCombo->currentData().toInt());
    settings.cutVelocity = m_cutVelocitySpin->value();
    settings.alpha = m_alphaSpin->value();
    settings.beta = m_betaSpin->value();
    settings.offset = m_offsetSpin->value();
    settings.fluid = m_fluidCombo->currentData().toString().toStdString();
    return settings;
}

void ClassifierEditorWidget::OnValuesChanged()
{
    const stosis::SClassifierSettings settings = CurrentSettings();
    const bool teipelHennig = settings.model == stosis::EScreenModel::TeipelHennig;
    m_betaLabel->setVisible(teipelHennig);
    m_betaSpin->setVisible(teipelHennig);
    m_offsetLabel->setVisible(teipelHennig);
    m_offsetSpin->setVisible(teipelHennig);
    m_formulaLabel->setText(GradeEfficiencyFormulaHtml(settings.model,
        // thin space after the italic d keeps the comma from hiding under it
        "<i>x</i> = <i>w</i><sub>s</sub>(<i>d</i>&#8201;,&nbsp;&rho;<sub>p</sub>) / <i>w</i><sub>cut</sub>",
        "Sinkgeschwindigkeit durch Trenngeschwindigkeit"));

    const QString problems = QString::fromStdString(settings.Check());
    m_errorLabel->setText(problems);
    m_errorLabel->setVisible(!problems.isEmpty());
    m_applyButton->setEnabled(m_graph && m_graph->GetClassifier(m_nodeId) && problems.isEmpty());
    UpdatePreview();
}

void ClassifierEditorWidget::UpdatePreview()
{
    const stosis::SClassifierSettings settings = CurrentSettings();
    const double T = stosis::kRoomTemperature, p = stosis::kRoomPressure;
    const stosis::SClassifierFluid fluid = stosis::GetClassifierFluid(m_materials, settings.fluid, T, p);
    m_fluidInfoLabel->setText(fluid.ok
        ? QString("Dichte %1 kg/m3, Viskositaet %2 Pa s bei 293,15 K und 1,013 bar; "
                  "im Lauf bei Temperatur und Druck des Zulaufs.").arg(Number(fluid.density), Number(fluid.viscosity))
        : QString::fromStdString(fluid.problem));

    if (!m_graph)
    {
        m_preview->SetCurves({}, 0.0, 0.0);
        return;
    }
    const stosis::SSimulationGrid& grid = m_graph->GetGrid();
    if (!settings.Check().empty() || !fluid.ok)
    {
        m_preview->SetCurves({}, grid.sizeMin, grid.sizeMax, fluid.ok ? QString() : QString::fromStdString(fluid.problem));
        m_previewNote->setVisible(false);
        return;
    }

    std::vector<GradeEfficiencyPreview::SCurve> curves;
    QStringList withoutDensity;
    for (const std::string& name : m_graph->GetActiveCompounds())
    {
        const double density = stosis::GetCompoundDensity(m_materials, name, T, p);
        if (!(density > 0.0))
        {
            withoutDensity << QString::fromStdString(name);
            continue;
        }
        const std::function<double(double)> share = [settings, density, fluid](double size)
        {
            return settings.GradeEfficiency(
                stosis::ComputeSettlingVelocity(size, density, fluid.density, fluid.viscosity).velocity);
        };
        const double half = HalfShareSize(share);
        QString label = QString("%1 (%2 kg/m3)").arg(QString::fromStdString(name), Number(density));
        if (half > 0.0)
            label += QString(": T = 0,5 bei %1 %2").arg(Number(half * stosis::kSizeDisplayFactor),
                QString::fromUtf8(stosis::kSizeDisplayUnit));
        else
            label += density <= fluid.density ? QString(": sinkt nicht, ganz ins Leichtgut") : QString(": T = 0,5 nicht zwischen 1 nm und 10 m");
        curves.push_back({ label, share, half, QString() });
    }
    const QString message = curves.empty() && withoutDensity.isEmpty()
        ? QString("Noch keine Stoffe im Flowsheet - die Vorschau zeigt die Stoffe der Inlets.") : QString();
    m_preview->SetCurves(std::move(curves), grid.sizeMin, grid.sizeMax, message);
    m_previewNote->setText(withoutDensity.isEmpty() ? QString()
        : "Ohne Dichte in der Materialdatei (gehen ganz ins Leichtgut): " + withoutDensity.join(", "));
    m_previewNote->setVisible(!withoutDensity.isEmpty());
}

void ClassifierEditorWidget::OnApply()
{
    stosis::CClassifierUnit* classifier = m_graph ? m_graph->GetClassifier(m_nodeId) : nullptr;
    if (!classifier)
        return;
    const stosis::SClassifierSettings settings = CurrentSettings();
    if (!classifier->SetSettings(settings))
    {
        m_errorLabel->setText(QString::fromStdString(settings.Check()));
        m_errorLabel->setVisible(true);
        return;
    }
    emit Applied();
}
