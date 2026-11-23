#include "ScreenEditorWidget.h"
#include "GradeEfficiencyPreview.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QGroupBox>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QTabBar>
#include <algorithm>

ScreenEditorWidget::ScreenEditorWidget(QWidget* parent)
    : QWidget(parent)
{
    SetupUi();
}

void ScreenEditorWidget::SetupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    m_deckTabs = new QTabBar(this);
    m_deckTabs->setExpanding(false);
    m_deckTabs->setVisible(false);
    mainLayout->addWidget(m_deckTabs);

    m_deckGroup = new QGroupBox("Trennkurve", this);
    auto* form = new QFormLayout(m_deckGroup);

    m_modelCombo = new QComboBox(m_deckGroup);
    m_modelCombo->addItem("Plitt", static_cast<int>(stosis::EScreenModel::Plitt));
    m_modelCombo->addItem("Molerus-Hoffmann", static_cast<int>(stosis::EScreenModel::MolerusHoffmann));
    m_modelCombo->addItem("Teipel-Hennig", static_cast<int>(stosis::EScreenModel::TeipelHennig));
    form->addRow("Modell:", m_modelCombo);

    m_cutSizeSpin = new QDoubleSpinBox(m_deckGroup);
    m_cutSizeSpin->setRange(1e-3, 1e7);
    m_cutSizeSpin->setDecimals(3);
    m_cutSizeSpin->setSuffix(QString(" ") + QString::fromUtf8(stosis::kSizeDisplayUnit));
    form->addRow("Trenngroesse x_cut:", m_cutSizeSpin);

    m_alphaSpin = new QDoubleSpinBox(m_deckGroup);
    m_alphaSpin->setRange(0.0, 100.0);
    m_alphaSpin->setDecimals(3);
    m_alphaSpin->setSingleStep(0.5);
    form->addRow("Trennschaerfe alpha:", m_alphaSpin);

    m_betaLabel = new QLabel("beta:", m_deckGroup);
    m_betaSpin = new QDoubleSpinBox(m_deckGroup);
    m_betaSpin->setRange(0.001, 100.0);
    m_betaSpin->setDecimals(3);
    m_betaSpin->setSingleStep(0.1);
    form->addRow(m_betaLabel, m_betaSpin);

    m_offsetLabel = new QLabel("Offset (Feingut im Grobgut):", m_deckGroup);
    m_offsetSpin = new QDoubleSpinBox(m_deckGroup);
    m_offsetSpin->setRange(0.0, 1.0);
    m_offsetSpin->setDecimals(3);
    m_offsetSpin->setSingleStep(0.01);
    form->addRow(m_offsetLabel, m_offsetSpin);

    m_formulaLabel = new QLabel(m_deckGroup);
    m_formulaLabel->setTextFormat(Qt::RichText);
    m_formulaLabel->setWordWrap(true);
    m_formulaLabel->setStyleSheet(kFormulaLabelStyle);
    form->addRow(m_formulaLabel);
    mainLayout->addWidget(m_deckGroup);

    m_errorLabel = new QLabel(this);
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setStyleSheet("color: rgb(200, 40, 40);");
    m_errorLabel->setVisible(false);
    mainLayout->addWidget(m_errorLabel);

    auto* previewGroup = new QGroupBox("Vorschau", this);
    auto* previewLayout = new QVBoxLayout(previewGroup);
    m_preview = new GradeEfficiencyPreview("T(d) ins Grobgut", previewGroup);
    previewLayout->addWidget(m_preview, 1);
    mainLayout->addWidget(previewGroup, 1);

    m_applyButton = new QPushButton("Uebernehmen", this);
    mainLayout->addWidget(m_applyButton);

    connect(m_deckTabs, &QTabBar::currentChanged, this, &ScreenEditorWidget::OnDeckChanged);
    connect(m_modelCombo, &QComboBox::currentIndexChanged, this, &ScreenEditorWidget::OnValuesChanged);
    for (QDoubleSpinBox* spin : { m_cutSizeSpin, m_alphaSpin, m_betaSpin, m_offsetSpin })
        connect(spin, &QDoubleSpinBox::valueChanged, this, &ScreenEditorWidget::OnValuesChanged);
    connect(m_applyButton, &QPushButton::clicked, this, &ScreenEditorWidget::OnApply);
}

void ScreenEditorWidget::SetGraph(stosis::CFlowsheetGraph* graph)
{
    m_graph = graph;
    LoadNode();
}

void ScreenEditorWidget::SetNode(const std::string& nodeId)
{
    m_nodeId = nodeId;
    LoadNode();
}

void ScreenEditorWidget::LoadNode()
{
    const stosis::CScreenUnit* screen = m_graph ? m_graph->GetScreen(m_nodeId) : nullptr;
    m_decks = screen ? screen->GetDecks() : std::vector<stosis::SScreenSettings>(1);
    m_currentDeck = 0;
    {
        const QSignalBlocker blocker(m_deckTabs);
        while (m_deckTabs->count() > 0)
            m_deckTabs->removeTab(0);
        for (size_t k = 0; k < m_decks.size(); ++k)
            m_deckTabs->addTab(k == 0 ? QString("Deck 1 (oben)") : QString("Deck %1").arg(k + 1));
        m_deckTabs->setCurrentIndex(0);
    }
    m_deckTabs->setVisible(m_decks.size() > 1);
    m_preview->SetAxisTitle(m_decks.size() > 1 ? "Anteil je Ausgang" : "T(d) ins Grobgut");
    LoadDeckIntoForm();
}

void ScreenEditorWidget::LoadDeckIntoForm()
{
    const stosis::SScreenSettings& settings = m_decks[static_cast<size_t>(m_currentDeck)];
    m_deckGroup->setTitle(m_decks.size() > 1 ? QString("Trennkurve Deck %1 (Grobgut %1)").arg(m_currentDeck + 1)
                                             : QString("Trennkurve"));
    {
        const QSignalBlocker b0(m_modelCombo), b1(m_cutSizeSpin), b2(m_alphaSpin), b3(m_betaSpin), b4(m_offsetSpin);
        m_modelCombo->setCurrentIndex(std::max(0, m_modelCombo->findData(static_cast<int>(settings.model))));
        m_cutSizeSpin->setValue(settings.cutSize * stosis::kSizeDisplayFactor);
        m_alphaSpin->setValue(settings.alpha);
        m_betaSpin->setValue(settings.beta);
        m_offsetSpin->setValue(settings.offset);
    }
    OnValuesChanged();
}

stosis::SScreenSettings ScreenEditorWidget::FormSettings() const
{
    stosis::SScreenSettings settings;
    settings.model = static_cast<stosis::EScreenModel>(m_modelCombo->currentData().toInt());
    settings.cutSize = m_cutSizeSpin->value() / stosis::kSizeDisplayFactor;
    settings.alpha = m_alphaSpin->value();
    settings.beta = m_betaSpin->value();
    settings.offset = m_offsetSpin->value();
    return settings;
}

void ScreenEditorWidget::OnDeckChanged(int index)
{
    if (index < 0 || static_cast<size_t>(index) >= m_decks.size())
        return;
    m_currentDeck = index;
    LoadDeckIntoForm();
}

void ScreenEditorWidget::OnValuesChanged()
{
    if (m_decks.empty())
        return;
    m_decks[static_cast<size_t>(m_currentDeck)] = FormSettings();
    const stosis::SScreenSettings& settings = m_decks[static_cast<size_t>(m_currentDeck)];
    const bool teipelHennig = settings.model == stosis::EScreenModel::TeipelHennig;
    m_betaLabel->setVisible(teipelHennig);
    m_betaSpin->setVisible(teipelHennig);
    m_offsetLabel->setVisible(teipelHennig);
    m_offsetSpin->setVisible(teipelHennig);
    m_formulaLabel->setText(GradeEfficiencyFormulaHtml(settings.model,
        "<i>x</i> = <i>d</i> / <i>x</i><sub>cut</sub>", "Partikelgroesse durch Trenngroesse"));

    const QString problems = QString::fromStdString(stosis::CheckScreenDecks(m_decks, m_decks.size()));
    m_errorLabel->setText(problems);
    m_errorLabel->setVisible(!problems.isEmpty());
    m_applyButton->setEnabled(m_graph && m_graph->GetScreen(m_nodeId) && problems.isEmpty());
    UpdatePreview();
}

void ScreenEditorWidget::UpdatePreview()
{
    if (!m_graph)
    {
        m_preview->SetCurves({}, 0.0, 0.0);
        return;
    }
    const stosis::SSimulationGrid& grid = m_graph->GetGrid();
    std::vector<GradeEfficiencyPreview::SCurve> curves;
    if (stosis::CheckScreenDecks(m_decks, m_decks.size()).empty())
    {
        if (m_decks.size() == 1)
        {
            const stosis::SScreenSettings settings = m_decks.front();
            curves.push_back({ QString(), [settings](double size) { return settings.GradeEfficiency(size); },
                settings.cutSize, "x_cut" });
        }
        else
        {
            // One curve per output; the coarse product of deck k is marked at its x_cut.
            const std::vector<stosis::SScreenSettings> decks = m_decks;
            for (size_t j = 0; j <= decks.size(); ++j)
            {
                const auto share = [decks, j](double size)
                {
                    std::vector<double> shares;
                    stosis::ComputeScreenShares(decks, size, shares);
                    return shares[j];
                };
                const bool fines = j == decks.size();
                curves.push_back({ fines ? QString("Ausgang %1: Feingut").arg(j + 1)
                                         : QString("Ausgang %1: Grobgut %1").arg(j + 1),
                    share, fines ? 0.0 : decks[j].cutSize,
                    fines ? QString() : QString("x_cut %1").arg(j + 1) });
            }
        }
    }
    m_preview->SetCurves(std::move(curves), grid.sizeMin, grid.sizeMax);
}

void ScreenEditorWidget::OnApply()
{
    stosis::CScreenUnit* screen = m_graph ? m_graph->GetScreen(m_nodeId) : nullptr;
    if (!screen)
        return;
    if (!screen->SetDecks(m_decks))
    {
        m_errorLabel->setText(QString::fromStdString(stosis::CheckScreenDecks(m_decks, screen->GetNumDecks())));
        m_errorLabel->setVisible(true);
        return;
    }
    emit Applied();
}
