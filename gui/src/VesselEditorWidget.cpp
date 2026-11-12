#include "VesselEditorWidget.h"
#include "FractionPreviewWidget.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace
{
    QString Num(double value, int decimals)
    {
        return QLocale().toString(value, 'f', decimals);
    }

    // Zahl mit hoechstens digits gueltigen Stellen.
    QString Short(double value, int digits = 3)
    {
        return QLocale().toString(value, 'g', digits);
    }

    QLabel* MakeInfoLabel(QWidget* parent)
    {
        auto* label = new QLabel(parent);
        label->setWordWrap(true);
        label->setStyleSheet("color: gray;");
        return label;
    }
}

VesselEditorWidget::VesselEditorWidget(QWidget* parent)
    : QWidget(parent)
{
    SetupUi();
}

void VesselEditorWidget::SetupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    // ---- Mischung --------------------------------------------------------
    auto* mixingGroup = new QGroupBox("Mischung und Verweilzeit", this);
    auto* mixingLayout = new QVBoxLayout(mixingGroup);
    auto* mixingForm = new QFormLayout();
    m_mixingCombo = new QComboBox(mixingGroup);
    m_mixingCombo->addItem("ideal durchmischt (Ruehrkessel, Wirbelschicht, Aerosolkammer)", static_cast<int>(stosis::EVesselMixing::Mixed));
    m_mixingCombo->addItem("Pfropfenstroemung (Rohr, Foerderband, Massenfluss-Silo)", static_cast<int>(stosis::EVesselMixing::Plug));
    m_mixingCombo->addItem("Kaskade aus n ideal durchmischten Kesseln", static_cast<int>(stosis::EVesselMixing::Cascade));
    m_mixingCombo->addItem("Kernfluss (Silo mit Totzonen)", static_cast<int>(stosis::EVesselMixing::CoreFlow));
    mixingForm->addRow("Mischung:", m_mixingCombo);

    m_tauSpin = new QDoubleSpinBox(mixingGroup);
    m_tauSpin->setRange(0.001, 1.0e7);
    m_tauSpin->setDecimals(3);
    m_tauSpin->setSingleStep(1.0);
    mixingForm->addRow("Mittlere Verweilzeit tau [s]:", m_tauSpin);

    m_batchCheck = new QCheckBox("Charge (kein Ablauf, tau unendlich)", mixingGroup);
    mixingForm->addRow("", m_batchCheck);

    m_cellsLabel = new QLabel("Zahl der Kessel n:", mixingGroup);
    m_cellsSpin = new QSpinBox(mixingGroup);
    m_cellsSpin->setRange(1, static_cast<int>(stosis::SVesselSettings::kMaxCells));
    mixingForm->addRow(m_cellsLabel, m_cellsSpin);

    m_coreShareLabel = new QLabel("Kernanteil [%]:", mixingGroup);
    m_coreShareSpin = new QDoubleSpinBox(mixingGroup);
    m_coreShareSpin->setRange(1.0, 99.0);
    m_coreShareSpin->setDecimals(1);
    mixingForm->addRow(m_coreShareLabel, m_coreShareSpin);

    m_coreTimeLabel = new QLabel("Kernzeit [% von tau]:", mixingGroup);
    m_coreTimeSpin = new QDoubleSpinBox(mixingGroup);
    m_coreTimeSpin->setRange(1.0, 99.0);
    m_coreTimeSpin->setDecimals(1);
    mixingForm->addRow(m_coreTimeLabel, m_coreTimeSpin);
    mixingLayout->addLayout(mixingForm);

    m_mixingInfo = MakeInfoLabel(mixingGroup);
    mixingLayout->addWidget(m_mixingInfo);
    mainLayout->addWidget(mixingGroup);

    // ---- Anfangsfuellung -------------------------------------------------
    auto* fillGroup = new QGroupBox("Anfangsfuellung", this);
    auto* fillLayout = new QVBoxLayout(fillGroup);
    auto* fillForm = new QFormLayout();
    m_fillCombo = new QComboBox(fillGroup);
    m_fillCombo->addItem("leer", static_cast<int>(stosis::EVesselFill::Empty));
    m_fillCombo->addItem("wie im Dauerbetrieb (Zulauf bei t = 0 mal tau)", static_cast<int>(stosis::EVesselFill::SteadyState));
    m_fillCombo->addItem("vorgegeben (Masse, Stoff, Groessenverteilung)", static_cast<int>(stosis::EVesselFill::Prescribed));
    fillForm->addRow("Inhalt bei t = 0:", m_fillCombo);
    fillLayout->addLayout(fillForm);

    m_fillBox = new QWidget(fillGroup);
    auto* boxForm = new QFormLayout(m_fillBox);
    boxForm->setContentsMargins(0, 0, 0, 0);
    m_fillMassSpin = new QDoubleSpinBox(m_fillBox);
    m_fillMassSpin->setRange(0.0, 1.0e9);
    m_fillMassSpin->setDecimals(3);
    boxForm->addRow("Masse [kg]:", m_fillMassSpin);
    m_materialCombo = new QComboBox(m_fillBox);
    boxForm->addRow("Stoff:", m_materialCombo);
    m_fillTemperatureSpin = new QDoubleSpinBox(m_fillBox);
    m_fillTemperatureSpin->setRange(1.0, 5000.0);
    m_fillTemperatureSpin->setDecimals(2);
    boxForm->addRow("Temperatur T [K]:", m_fillTemperatureSpin);
    m_distTypeCombo = new QComboBox(m_fillBox);
    for (const stosis::EDistributionType type : { stosis::EDistributionType::LogNormal, stosis::EDistributionType::RRSB,
             stosis::EDistributionType::GGS, stosis::EDistributionType::Normal })
        m_distTypeCombo->addItem(QString::fromUtf8(stosis::GetDistributionParameterInfo(type).typeName), static_cast<int>(type));
    boxForm->addRow("Groessenverteilung:", m_distTypeCombo);
    m_positionLabel = new QLabel(m_fillBox);
    m_positionSpin = new QDoubleSpinBox(m_fillBox);
    m_positionSpin->setDecimals(4);
    boxForm->addRow(m_positionLabel, m_positionSpin);
    m_widthLabel = new QLabel(m_fillBox);
    m_widthSpin = new QDoubleSpinBox(m_fillBox);
    m_widthSpin->setDecimals(4);
    boxForm->addRow(m_widthLabel, m_widthSpin);
    fillLayout->addWidget(m_fillBox);

    m_fillInfo = MakeInfoLabel(fillGroup);
    fillLayout->addWidget(m_fillInfo);
    mainLayout->addWidget(fillGroup);

    // ---- Vorrat ----------------------------------------------------------
    auto* stockGroup = new QGroupBox("Vorrat im Partikelmodus", this);
    auto* stockLayout = new QVBoxLayout(stockGroup);
    auto* stockForm = new QFormLayout();
    m_stockSpin = new QDoubleSpinBox(stockGroup);
    m_stockSpin->setRange(1.0, 100000.0);
    m_stockSpin->setDecimals(0);
    m_stockSpin->setSingleStep(10.0);
    stockForm->addRow("Mindestens [x N Karten]:", m_stockSpin);
    stockLayout->addLayout(stockForm);
    m_stockInfo = MakeInfoLabel(stockGroup);
    stockLayout->addWidget(m_stockInfo);
    mainLayout->addWidget(stockGroup);

    // ---- Vorschau --------------------------------------------------------
    auto* previewGroup = new QGroupBox("Vorschau der Verweilzeit", this);
    auto* previewLayout = new QVBoxLayout(previewGroup);
    m_preview = new FractionPreviewWidget(previewGroup);
    previewLayout->addWidget(m_preview, 1);
    m_previewCaption = MakeInfoLabel(previewGroup);
    previewLayout->addWidget(m_previewCaption);
    mainLayout->addWidget(previewGroup, 1);

    m_errorLabel = new QLabel(this);
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setStyleSheet("color: rgb(200, 40, 40);");
    mainLayout->addWidget(m_errorLabel);

    m_applyButton = new QPushButton("Uebernehmen", this);
    connect(m_applyButton, &QPushButton::clicked, this, &VesselEditorWidget::OnApply);
    mainLayout->addWidget(m_applyButton);

    connect(m_mixingCombo, &QComboBox::currentIndexChanged, this, &VesselEditorWidget::OnValuesChanged);
    connect(m_tauSpin, &QDoubleSpinBox::valueChanged, this, &VesselEditorWidget::OnValuesChanged);
    connect(m_batchCheck, &QCheckBox::toggled, this, &VesselEditorWidget::OnValuesChanged);
    connect(m_cellsSpin, &QSpinBox::valueChanged, this, &VesselEditorWidget::OnValuesChanged);
    connect(m_coreShareSpin, &QDoubleSpinBox::valueChanged, this, &VesselEditorWidget::OnValuesChanged);
    connect(m_coreTimeSpin, &QDoubleSpinBox::valueChanged, this, &VesselEditorWidget::OnValuesChanged);
    connect(m_fillCombo, &QComboBox::currentIndexChanged, this, &VesselEditorWidget::OnValuesChanged);
    connect(m_fillMassSpin, &QDoubleSpinBox::valueChanged, this, &VesselEditorWidget::OnValuesChanged);
    connect(m_positionSpin, &QDoubleSpinBox::valueChanged, this, &VesselEditorWidget::OnValuesChanged);
    connect(m_widthSpin, &QDoubleSpinBox::valueChanged, this, &VesselEditorWidget::OnValuesChanged);
    connect(m_stockSpin, &QDoubleSpinBox::valueChanged, this, &VesselEditorWidget::OnValuesChanged);
    connect(m_distTypeCombo, &QComboBox::currentIndexChanged, this, &VesselEditorWidget::OnDistributionTypeChanged);

    ApplyDistributionTypeToControls(stosis::EDistributionType::LogNormal);
}

void VesselEditorWidget::SetGraph(stosis::CFlowsheetGraph* graph)
{
    m_graph = graph;
    LoadNode();
}

void VesselEditorWidget::SetMaterialDatabase(const stosis::CMaterialDatabase* materials)
{
    m_materials = materials;
    RefreshMaterialList();
}

void VesselEditorWidget::RefreshMaterialList()
{
    const QString previous = m_materialCombo->currentData().toString();
    m_materialCombo->blockSignals(true);
    m_materialCombo->clear();
    if (m_materials)
        for (const stosis::SMaterial& material : m_materials->GetMaterials())
            m_materialCombo->addItem(QString::fromStdString(material.name), QString::fromStdString(material.name));
    m_materialCombo->blockSignals(false);
    if (!previous.isEmpty())
        SelectMaterial(previous.toStdString());
}

void VesselEditorWidget::SelectMaterial(const std::string& name)
{
    const QString key = QString::fromStdString(name);
    int index = m_materialCombo->findData(key);
    if (index < 0)
    {
        m_materialCombo->addItem(key + " (nicht in der Materialdatei)", key);
        index = m_materialCombo->count() - 1;
    }
    m_materialCombo->setCurrentIndex(index);
}

void VesselEditorWidget::SetNode(const std::string& nodeId)
{
    m_nodeId = nodeId;
    LoadNode();
}

void VesselEditorWidget::LoadNode()
{
    stosis::CVesselUnit* vessel = m_graph ? m_graph->GetVessel(m_nodeId) : nullptr;
    const stosis::SVesselSettings settings = vessel ? vessel->GetSettings() : stosis::SVesselSettings();
    const QList<QWidget*> fields = { m_mixingCombo, m_tauSpin, m_batchCheck, m_cellsSpin, m_coreShareSpin, m_coreTimeSpin,
        m_fillCombo, m_fillMassSpin, m_materialCombo, m_fillTemperatureSpin, m_distTypeCombo, m_positionSpin, m_widthSpin, m_stockSpin };
    for (QWidget* field : fields)
        field->blockSignals(true);

    m_mixingCombo->setCurrentIndex(std::max(0, m_mixingCombo->findData(static_cast<int>(settings.mixing))));
    m_tauSpin->setValue(settings.residenceTime);
    m_batchCheck->setChecked(settings.batch);
    m_cellsSpin->setValue(static_cast<int>(settings.cells));
    m_coreShareSpin->setValue(100.0 * settings.coreShare);
    m_coreTimeSpin->setValue(100.0 * settings.coreTime);
    m_fillCombo->setCurrentIndex(std::max(0, m_fillCombo->findData(static_cast<int>(settings.fill))));
    m_fillMassSpin->setValue(settings.fillMass);
    SelectMaterial(settings.fillMaterial);
    m_fillTemperatureSpin->setValue(settings.fillTemperature);
    const stosis::EDistributionType type = settings.fillDistribution.type == stosis::EDistributionType::Table
        ? stosis::EDistributionType::LogNormal : settings.fillDistribution.type;
    m_distTypeCombo->setCurrentIndex(std::max(0, m_distTypeCombo->findData(static_cast<int>(type))));
    ApplyDistributionTypeToControls(type);
    const stosis::SDistributionParameterInfo info = stosis::GetDistributionParameterInfo(type);
    m_positionSpin->setValue(settings.fillDistribution.position * stosis::kSizeDisplayFactor);
    m_widthSpin->setValue(settings.fillDistribution.width * (info.widthIsSize ? stosis::kSizeDisplayFactor : 1.0));
    m_stockSpin->setValue(settings.stockFactor);

    for (QWidget* field : fields)
        field->blockSignals(false);
    OnValuesChanged();
}

void VesselEditorWidget::ApplyDistributionTypeToControls(stosis::EDistributionType type)
{
    const stosis::SDistributionParameterInfo info = stosis::GetDistributionParameterInfo(type);
    const QString unit = QString::fromUtf8(stosis::kSizeDisplayUnit);
    m_positionLabel->setText(QString::fromUtf8(info.positionLabel) + " [" + unit + "]:");
    m_positionSpin->setRange(info.positionMin * stosis::kSizeDisplayFactor, info.positionMax * stosis::kSizeDisplayFactor);
    const double widthFactor = info.widthIsSize ? stosis::kSizeDisplayFactor : 1.0;
    m_widthLabel->setText(QString::fromUtf8(info.widthLabel) + (info.widthIsSize ? " [" + unit + "]:" : QString(":")));
    m_widthSpin->setRange(info.widthMin * widthFactor, info.widthMax * widthFactor);
}

void VesselEditorWidget::OnDistributionTypeChanged(int /*index*/)
{
    const auto type = static_cast<stosis::EDistributionType>(m_distTypeCombo->currentData().toInt());
    ApplyDistributionTypeToControls(type);
    // Neuer Typ: dessen Standardwerte, die alten Zahlen passen meist nicht.
    const stosis::SDistributionParameterInfo info = stosis::GetDistributionParameterInfo(type);
    m_positionSpin->blockSignals(true);
    m_widthSpin->blockSignals(true);
    m_positionSpin->setValue(info.positionDefault * stosis::kSizeDisplayFactor);
    m_widthSpin->setValue(info.widthDefault * (info.widthIsSize ? stosis::kSizeDisplayFactor : 1.0));
    m_positionSpin->blockSignals(false);
    m_widthSpin->blockSignals(false);
    OnValuesChanged();
}

stosis::SVesselSettings VesselEditorWidget::CurrentSettings() const
{
    stosis::CVesselUnit* vessel = m_graph ? m_graph->GetVessel(m_nodeId) : nullptr;
    stosis::SVesselSettings settings = vessel ? vessel->GetSettings() : stosis::SVesselSettings();
    settings.mixing = static_cast<stosis::EVesselMixing>(m_mixingCombo->currentData().toInt());
    settings.residenceTime = m_tauSpin->value();
    settings.batch = m_batchCheck->isChecked();
    settings.cells = static_cast<size_t>(m_cellsSpin->value());
    settings.coreShare = m_coreShareSpin->value() / 100.0;
    settings.coreTime = m_coreTimeSpin->value() / 100.0;
    settings.fill = static_cast<stosis::EVesselFill>(m_fillCombo->currentData().toInt());
    settings.fillMass = m_fillMassSpin->value();
    if (!m_materialCombo->currentData().toString().isEmpty())
        settings.fillMaterial = m_materialCombo->currentData().toString().toStdString();
    settings.fillTemperature = m_fillTemperatureSpin->value();
    const auto type = static_cast<stosis::EDistributionType>(m_distTypeCombo->currentData().toInt());
    const stosis::SDistributionParameterInfo info = stosis::GetDistributionParameterInfo(type);
    settings.fillDistribution.type = type;
    settings.fillDistribution.position = m_positionSpin->value() / stosis::kSizeDisplayFactor;
    settings.fillDistribution.width = m_widthSpin->value() / (info.widthIsSize ? stosis::kSizeDisplayFactor : 1.0);
    settings.stockFactor = m_stockSpin->value();
    return settings;
}

void VesselEditorWidget::UpdateHints()
{
    OnValuesChanged();
}

void VesselEditorWidget::OnValuesChanged()
{
    const stosis::SVesselSettings settings = CurrentSettings();
    const bool batch = settings.batch;
    const stosis::EVesselMixing mixing = settings.mixing;
    m_mixingCombo->setEnabled(!batch);
    m_tauSpin->setEnabled(!batch);
    const bool cascade = !batch && mixing == stosis::EVesselMixing::Cascade;
    const bool core = !batch && mixing == stosis::EVesselMixing::CoreFlow;
    m_cellsLabel->setVisible(cascade);
    m_cellsSpin->setVisible(cascade);
    m_coreShareLabel->setVisible(core);
    m_coreShareSpin->setVisible(core);
    m_coreTimeLabel->setVisible(core);
    m_coreTimeSpin->setVisible(core);
    m_fillBox->setVisible(settings.fill == stosis::EVesselFill::Prescribed);

    const double dt = m_graph ? m_graph->GetGrid().GetDt() : 0.0;
    const size_t N = m_graph ? m_graph->GetCardSettings().cardsPerSlice : 0;
    const double tau = settings.residenceTime;
    const double steps = dt > 0.0 ? tau / dt : 0.0;

    // ---- Mischung: was passiert, mit den Zahlen dieses Flowsheets.
    QString mixingText;
    if (batch)
        mixingText = "Charge: kein Ablauf. Der Behaelter sammelt alles, was zulaeuft, die Mischung spielt dann keine Rolle. "
                     "Den Inhalt zeigen die Ergebnisse noch nicht an.";
    else if (mixing == stosis::EVesselMixing::Plug)
        mixingText = QString("Alles bleibt genau tau = %1 Zeitschritte, die Reihenfolge bleibt erhalten. Ist tau kein Vielfaches "
                             "von dt, verteilt sich ein Zulaufschritt auf zwei Austrittsschritte.").arg(Short(steps, 4));
    else if (mixing == stosis::EVesselMixing::Cascade)
        mixingText = QString("%1 ideal durchmischte Kessel mit je tau/n = %2 s hintereinander. Die Verweilzeit folgt einer "
                             "Gamma-Verteilung (Erlang): n = 1 ist ein Ruehrkessel, mit wachsendem n naehert sie sich der "
                             "Pfropfenstroemung (Tanks-in-Series-Modell).").arg(settings.cells).arg(Short(tau / static_cast<double>(std::max<size_t>(settings.cells, 1))));
    else if (mixing == stosis::EVesselMixing::CoreFlow)
        mixingText = QString("%1 % des Zulaufs laufen als Pfropfen in %2 s durch den Kern, der Rest durch eine durchmischte "
                             "Randzone mit im Mittel %3 s, damit das Mittel tau bleibt: frueher Durchbruch, langer Auslaeufer.")
                         .arg(Short(100.0 * settings.coreShare)).arg(Short(settings.coreTime * tau)).arg(Short(settings.GetWallTime()));
    else
        mixingText = QString("Jede Karte geht in jedem Zeitschritt mit derselben Wahrscheinlichkeit, egal wie lange sie schon "
                             "im Behaelter ist: bei dt = %1 s gehen je Schritt %2 % des Inhalts (1 - e^(-dt/tau)). 63 % des "
                             "Zulaufs gehen innerhalb von tau, 5 % bleiben laenger als 3 tau.")
                         .arg(Short(dt)).arg(Num(100.0 * stosis::VesselExitFraction(dt, tau), 2));
    m_mixingInfo->setText(mixingText);

    // ---- Anfangsfuellung: Empfehlungen.
    QString fillText;
    if (settings.fill == stosis::EVesselFill::Empty)
    {
        if (batch)
            fillText = "Leer: Die Charge fuellt sich mit dem Zulauf.";
        else
            fillText = QString("Leer, wie beim Anfahren einer Anlage. Der Ablauf erreicht 95 % des Zulaufs nach %1 s und 99 % "
                               "nach %2 s - Kennzahlen erst danach auswerten.")
                           .arg(Short(stosis::VesselTimeToFraction(settings, 0.95))).arg(Short(stosis::VesselTimeToFraction(settings, 0.99)));
    }
    else if (settings.fill == stosis::EVesselFill::SteadyState)
        fillText = batch
            ? "Eine Charge hat keinen Dauerbetrieb - sie startet damit leer. Fuer einen gefuellten Start \"vorgegeben\" waehlen."
            : "Wie im Dauerbetrieb: Bei t = 0 liegt im Behaelter so viel, wie bei dem Zulauf von t = 0 im Dauerbetrieb darin "
              "laege (Zulauf mal tau), mit dessen Groessenverteilung. Keine Anlaufphase - empfohlen fuer Schwankungen und "
              "Statistik. Ist der Zulauf bei t = 0 null, startet der Behaelter leer.";
    else
        fillText = "Vorgegeben: Masse, Stoff und Verteilung von Hand, verteilt wie im Dauerbetrieb. Fuer eine Charge (z. B. eine "
                   "Aerosolkammer) oder einen Auswaschversuch: mit einem anderen Stoff als dem des Zulaufs fuellen - dessen "
                   "Anteil im Ablauf zeigt dann die Verweilzeitverteilung.";
    fillText += "\nFaustregel: Statistik und Schwankungen - wie im Dauerbetrieb; Anfahren - leer; Charge und Auswaschversuch - vorgegeben.";
    m_fillInfo->setText(fillText);

    // ---- Vorrat: room for (tau/dt + 2) inflow steps, as in CVesselUnit::StockCap.
    QString stockText;
    {
        const stosis::SCardSettings cards = m_graph ? m_graph->GetCardSettings() : stosis::SCardSettings();
        const double perSlice = (cards.sliceLimits ? std::max(cards.upperFactor, 1.0) : 2.0) * static_cast<double>(N);
        const double slices = (batch ? static_cast<double>(m_graph ? m_graph->GetGrid().numTimeSteps : 0) : steps) + 2.0;
        const double limit = static_cast<double>(stosis::CVesselUnit::kDefaultStockLimit);
        const double wanted = std::max(settings.stockFactor * static_cast<double>(N), slices * perSlice);
        stockText = QString("Der Vorrat waechst mit tau/dt: Platz fuer %1 Zulaufschritte mit je hoechstens %2 Karten, "
                            "also %3 Karten (hoechstens 10 Mio.).").arg(Short(slices, 4)).arg(Num(perSlice, 0)).arg(Num(std::min(wanted, limit), 0));
        if (wanted > limit)
            stockText += QString(" Hier wird begrenzt: Neue Karten werden etwa %1 : 1 zusammengelegt.").arg(Num(wanted / limit, 1));
    }
    m_stockInfo->setText(stockText);

    const std::string problem = settings.Check();
    m_errorLabel->setText(QString::fromStdString(problem));
    m_errorLabel->setVisible(!problem.empty());
    m_applyButton->setEnabled(problem.empty() && m_graph && m_graph->GetVessel(m_nodeId));
    UpdatePreview(settings);
}

void VesselEditorWidget::UpdatePreview(const stosis::SVesselSettings& settings)
{
    if (settings.batch || !settings.Check().empty())
    {
        m_preview->Clear();
        m_previewCaption->setText(settings.batch ? "Charge: Es geht nichts hinaus." : QString());
        return;
    }
    // Zeitachse bis etwas ueber 99 % Ablauf, mindestens 2 tau.
    const double span = std::max(2.0 * settings.residenceTime, 1.25 * stosis::VesselTimeToFraction(settings, 0.99));
    const size_t points = 400;
    std::vector<std::vector<double>> series(2);
    for (size_t i = 0; i <= points; ++i)
    {
        const double t = span * static_cast<double>(i) / static_cast<double>(points);
        const double F = stosis::VesselResidenceCdf(settings, t);
        series[0].push_back(F);
        series[1].push_back(1.0 - F);
    }
    m_preview->SetSeries(std::move(series), {}, { "Ablauf / Zulauf, leer gestartet", "noch im Behaelter" }, span);
    m_previewCaption->setText(QString("Summenkurve der Verweilzeit F(t): Anteil eines Zulaufs, der nach der Zeit t wieder gegangen "
                                      "ist. Mittlere Verweilzeit %1 s; 50 % sind nach %2 s gegangen.")
                                  .arg(Short(settings.residenceTime)).arg(Short(stosis::VesselTimeToFraction(settings, 0.5))));
}

void VesselEditorWidget::OnApply()
{
    stosis::CVesselUnit* vessel = m_graph ? m_graph->GetVessel(m_nodeId) : nullptr;
    if (!vessel)
        return;
    if (!vessel->SetSettings(CurrentSettings()))
    {
        m_errorLabel->setText(QString::fromStdString(CurrentSettings().Check()));
        m_errorLabel->setVisible(true);
        return;
    }
    emit Applied();
}
