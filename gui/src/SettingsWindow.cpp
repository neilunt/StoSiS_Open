#include "SettingsWindow.h"
#include "InletEditorWidget.h"
#include "ProcessNodeEditorWidget.h"
#include "SplitterEditorWidget.h"
#include "MaterialsPage.h"
#include "SieveGridWidget.h"
#include "CardDensityPreviewWidget.h"
#include <QTabWidget>
#include <QScrollArea>
#include <QSlider>
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
#include <QLocale>

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
    m_tabs->addTab(CreateCardTab(), "Karten");
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

    // Groessenraster und Siebsatz auf derselben Achse (Methodenhandbuch 4, 9.6).
    auto* sieveGroup = new QGroupBox("Groessenraster und Siebsatz", tab);
    auto* sieveLayout = new QVBoxLayout(sieveGroup);
    m_sieveWidget = new SieveGridWidget(sieveGroup);
    sieveLayout->addWidget(m_sieveWidget);
    auto* sieveHint = new QLabel(QString("Raster: Stuetzstellen, auf denen das Kontinuum rechnet und alle Ergebnisse "
                                         "angezeigt werden - bestimmt die Aufloesung. Siebsatz: Groessenbereiche 1 %1 * 2^k, "
                                         "nach denen die Kartenansicht die Karten in Klassen zaehlt (nur Anzeige). Wo die "
                                         "Karten sitzen, legt die Kartendichte fest (Reiter \"Karten\"); im Partikelmodus "
                                         "zaehlen die Karten Partikel nur im Raster, sonst aendert das Raster die Karten nicht.")
                                     .arg(unit),
                                 sieveGroup);
    sieveHint->setWordWrap(true);
    sieveHint->setStyleSheet("color: gray;");
    sieveLayout->addWidget(sieveHint);
    layout->addWidget(sieveGroup);

    // Richtwerte fuer das Raster. "Einsetzen" fuellt nur die Felder,
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

QWidget* SettingsWindow::CreateCardTab()
{
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* tab = new QWidget(scroll);
    auto* layout = new QVBoxLayout(tab);

    // Regel, f und gamma (Methodenhandbuch 9.3).
    auto* ruleGroup = new QGroupBox("Kartendichte im Partikelmodus", tab);
    auto* form = new QFormLayout(ruleGroup);
    m_cardRuleCombo = new QComboBox(ruleGroup);
    m_cardRuleCombo->addItem("Summe - gewichtetes Mittel (empfohlen)", static_cast<int>(stosis::ECardDensityRule::Sum));
    m_cardRuleCombo->addItem("Quadratisch - quadratisches Mittel", static_cast<int>(stosis::ECardDensityRule::Quadratic));
    m_cardRuleCombo->addItem("Produkt - geometrisches Mittel", static_cast<int>(stosis::ECardDensityRule::Product));
    form->addRow("Regel:", m_cardRuleCombo);
    // Die gewaehlte Regel als gesetzte Formel, darunter mit den Werten.
    m_cardFormula = new QLabel(ruleGroup);
    m_cardFormula->setTextFormat(Qt::RichText);
    m_cardFormula->setWordWrap(true);
    m_cardFormula->setStyleSheet("QLabel { background: #f7f6fd; border: 1px solid #d9d5f2; border-radius: 4px; padding: 6px; }");
    form->addRow(m_cardFormula);

    auto* shareRow = new QHBoxLayout();
    m_cardShareSlider = new QSlider(Qt::Horizontal, ruleGroup);
    m_cardShareSlider->setRange(0, 100);
    m_cardShareSlider->setPageStep(10);
    m_cardShareSpin = new QDoubleSpinBox(ruleGroup);
    m_cardShareSpin->setRange(0.0, 1.0);
    m_cardShareSpin->setDecimals(2);
    m_cardShareSpin->setSingleStep(0.05);
    shareRow->addWidget(new QLabel("Masse", ruleGroup));
    shareRow->addWidget(m_cardShareSlider, 1);
    shareRow->addWidget(new QLabel("Anzahl", ruleGroup));
    shareRow->addWidget(m_cardShareSpin);
    form->addRow("Anteil Anzahl f:", shareRow);

    auto* exponentRow = new QHBoxLayout();
    m_cardExponentSlider = new QSlider(Qt::Horizontal, ruleGroup);
    m_cardExponentSlider->setRange(static_cast<int>(std::lround(stosis::kCardExponentMinUi * 100.0)), 100);
    m_cardExponentSlider->setPageStep(10);
    m_cardExponentSpin = new QDoubleSpinBox(ruleGroup);
    m_cardExponentSpin->setRange(stosis::kCardExponentMinUi, 1.0);
    m_cardExponentSpin->setDecimals(2);
    m_cardExponentSpin->setSingleStep(0.05);
    exponentRow->addWidget(new QLabel("flach", ruleGroup));
    exponentRow->addWidget(m_cardExponentSlider, 1);
    exponentRow->addWidget(new QLabel("keine", ruleGroup));
    exponentRow->addWidget(m_cardExponentSpin);
    form->addRow(QString("Exponent %1 (Abflachung):").arg(QChar(0x03B3)), exponentRow);

    auto* buttonRow = new QHBoxLayout();
    const stosis::SCardSettings standardCards;
    auto* defaultButton = new QPushButton(QString("Standard (%1, f = %2, %3 = %4)")
        .arg(QString(stosis::GetCardDensityRuleName(standardCards.rule)), QLocale().toString(standardCards.numberShare, 'g', 2),
             QString(QChar(0x03B3)), QLocale().toString(standardCards.exponent, 'g', 2)), ruleGroup);
    auto* applyButton = new QPushButton("Uebernehmen", ruleGroup);
    buttonRow->addWidget(defaultButton);
    buttonRow->addStretch();
    buttonRow->addWidget(applyButton);
    form->addRow(buttonRow);
    auto* scopeHint = new QLabel("Gilt im Partikelmodus fuer alle Inlets und fuer die Grenzen je Stromscheibe an allen "
                                 "Ausgaengen. Die Zahl N der Karten je Zeitschritt steht im Hauptfenster.", ruleGroup);
    scopeHint->setWordWrap(true);
    scopeHint->setStyleSheet("color: gray;");
    form->addRow(scopeHint);
    layout->addWidget(ruleGroup);

    // Vorschau fuer eine Beispielverteilung.
    auto* previewGroup = new QGroupBox("So verteilen sich die Karten", tab);
    auto* previewLayout = new QVBoxLayout(previewGroup);
    auto* exampleRow = new QHBoxLayout();
    exampleRow->addWidget(new QLabel("Beispielverteilung:", previewGroup));
    m_cardExampleCombo = new QComboBox(previewGroup);
    exampleRow->addWidget(m_cardExampleCombo, 1);
    previewLayout->addLayout(exampleRow);
    m_cardPreview = new CardDensityPreviewWidget(previewGroup);
    previewLayout->addWidget(m_cardPreview);
    m_cardSummary = new QLabel(previewGroup);
    m_cardSummary->setTextFormat(Qt::RichText);
    m_cardSummary->setWordWrap(true);
    previewLayout->addWidget(m_cardSummary);
    layout->addWidget(previewGroup);

    // Legende: was die Einstellungen bewirken, Empfehlungen je Prozess.
    auto* legendGroup = new QGroupBox("Was die Einstellungen bewirken", tab);
    auto* legendLayout = new QVBoxLayout(legendGroup);
    auto* legend = new QLabel(legendGroup);
    legend->setTextFormat(Qt::RichText);
    legend->setWordWrap(true);
    legend->setText(
        "Die <b>Kartendichte</b> c (lila) legt fest, wo die Karten sitzen: Die Flaeche unter ihr wird in N gleiche "
        "Streifen geteilt, in jeden Streifen kommt eine Karte. Wo die Kurve hoch ist, liegen die Karten dicht. "
        "Es gibt keine Groessenbereiche, keine Mindestzahl und keine Spruenge. Die Formel oben zeigt die gewaehlte "
        "Regel: ein Mittel aus Masse q<sub>3</sub>* und Anzahl q<sub>0</sub>*, hoch &gamma; genommen."
        "<ul style=\"margin-top: 2px; margin-bottom: 2px;\">"
        "<li><b>Regel Summe</b> (empfohlen): gewichtetes Mittel von Masse und Anzahl. Ohne Abflachung traegt keine "
        "Karte mehr als 1/(1 - f)-mal die mittlere Masse und mehr als 1/f-mal die mittlere Partikelzahl.</li>"
        "<li><b>Regel Quadratisch</b>: quadratisches Mittel; folgt an jeder Stelle staerker dem Groesseren von Masse "
        "und Anzahl, die Raender bekommen etwas mehr Karten als bei der Summe. Ohne Abflachung hoechstens "
        "1 + &radic;(f/(1 - f))-mal die mittlere Masse und 1 + &radic;((1 - f)/f)-mal die mittlere Partikelzahl "
        "(f = 0,5: beides das Doppelte).</li>"
        "<li><b>Regel Produkt</b>: geometrisches Mittel, ein einziger Buckel zwischen Masse und Anzahl (f = 0,5: "
        "&radic;(Masse &middot; Anzahl)); duenne Raender und Gewichte ohne Schranke - vor allem zum Vergleich.</li>"
        "<li><b>Anteil Anzahl f</b>: 0 = Karten nur nach der Masse, 1 = nur nach der Partikelzahl, 0,5 = ausgewogen.</li>"
        "<li><b>Exponent &gamma; (Abflachung)</b>: Die Kartendichte wird hoch &gamma; genommen. 1 = keine Abflachung. "
        "Kleiner = flachere Kurve: mehr Karten an beiden Raendern, etwas weniger in der Mitte. 0,8 leicht, 0,6 "
        "kraeftig; unter 0,5 landen merklich Karten in den aeussersten Auslaeufern, die kaum etwas tragen.</li>"
        "</ul>"
        "<b>Empfehlungen</b>"
        "<ul style=\"margin-top: 2px; margin-bottom: 2px;\">"
        "<li><b>Allgemein</b> (Mischen, Transport, Verweilzeit, Splitter): Summe, f = 0,5, &gamma; = 0,8 (Standard).</li>"
        "<li><b>Masse entscheidet, Trennung im Groben</b> (Massenstroeme, d50 und d90, Siebueberlauf, grobe "
        "Sichterfraktion): Summe, f = 0,2 bis 0,3.</li>"
        "<li><b>Partikelzahl entscheidet, Trennung im Feinen</b> (Keimbildung, Koagulation und Agglomeration, "
        "Anzahlkonzentration, Feingut eines Zyklons, Feinstaub): Summe, f = 0,7 bis 0,8.</li>"
        "<li><b>Ein Rand laeuft allein weiter</b> (Rueckfuehrung des Groben, Mahlkreislauf, Abtrennung von 1 bis 2 % "
        "der Masse): zusaetzlich &gamma; = 0,6 bis 0,7; mehr Karten je Zeitschritt helfen ebenfalls.</li>"
        "<li><b>Vergleich</b>: Quadratisch oder Produkt waehlen und die Kennzahlen unter der Vorschau vergleichen.</li>"
        "</ul>"
        "<b>Kennzahlen unter der Vorschau</b>"
        "<ul style=\"margin-top: 2px; margin-bottom: 2px;\">"
        "<li><b>Raender</b>: die feinsten 5 % der Partikel und die groebsten 5 % der Masse, jeweils mit dem Anteil der "
        "Karten, die dort liegen. Fein zaehlt die Anzahl (dort liegen viele, aber leichte Partikel: Feingut, Keime), "
        "grob die Masse (wenige, aber schwere: Ueberkorn, d95). Beide Raender sind gleich gross gewaehlt: Bei f = 0,5 "
        "bekommen sie fuer eine Log-Normal-Verteilung genau gleich viele Karten. Unter 10 Karten je Zeitschritt wird "
        "ein Rand unsicher (Streuung etwa 1/&radic;n).</li>"
        "<li><b>Je Karte</b>: die schwerste und die partikelreichste Karte im Vergleich zum Mittel.</li>"
        "<li><b>Unter d_min</b> (rot hinterlegt): Das Raster loest dort nichts mehr auf und legt diese Partikel in "
        "seine erste Klasse. Fuer die Kartendichte zaehlen sie deshalb so, als haetten sie die Groesse d_min - "
        "Anzahl und Kartendichte folgen dort der Form der Massenkurve (gestrichelt: die Anzahl mit echter Groesse). "
        "Ohne diese Regel waere die Partikelzahl bei manchen Verteilungen unbegrenzt (RRSB mit n &lt; 3). Sind die "
        "feinen Partikel wichtig, d_min kleiner waehlen.</li>"
        "</ul>"
        "Die Vorschau zeigt die eingegebenen Werte; im Flowsheet gelten sie erst nach \"Uebernehmen\".");
    legendLayout->addWidget(legend);
    layout->addWidget(legendGroup);
    layout->addStretch();
    scroll->setWidget(tab);

    // Regler und Zahlenfelder gegenseitig abgleichen, Vorschau sofort neu.
    connect(m_cardShareSlider, &QSlider::valueChanged, this, [this](int value)
        {
            m_cardShareSpin->blockSignals(true);
            m_cardShareSpin->setValue(value / 100.0);
            m_cardShareSpin->blockSignals(false);
            UpdateCardPreview();
        });
    connect(m_cardShareSpin, &QDoubleSpinBox::valueChanged, this, [this](double value)
        {
            m_cardShareSlider->blockSignals(true);
            m_cardShareSlider->setValue(static_cast<int>(std::lround(value * 100.0)));
            m_cardShareSlider->blockSignals(false);
            UpdateCardPreview();
        });
    connect(m_cardExponentSlider, &QSlider::valueChanged, this, [this](int value)
        {
            m_cardExponentSpin->blockSignals(true);
            m_cardExponentSpin->setValue(value / 100.0);
            m_cardExponentSpin->blockSignals(false);
            UpdateCardPreview();
        });
    connect(m_cardExponentSpin, &QDoubleSpinBox::valueChanged, this, [this](double value)
        {
            m_cardExponentSlider->blockSignals(true);
            m_cardExponentSlider->setValue(static_cast<int>(std::lround(value * 100.0)));
            m_cardExponentSlider->blockSignals(false);
            UpdateCardPreview();
        });
    connect(m_cardRuleCombo, &QComboBox::currentIndexChanged, this, &SettingsWindow::UpdateCardPreview);
    connect(m_cardExampleCombo, &QComboBox::currentIndexChanged, this, &SettingsWindow::UpdateCardPreview);
    connect(defaultButton, &QPushButton::clicked, this, [this]()
        {
            stosis::SCardSettings defaults = EnteredCardSettings();
            const stosis::SCardSettings standard;
            defaults.rule = standard.rule;
            defaults.numberShare = standard.numberShare;
            defaults.exponent = standard.exponent;
            LoadCardValues(defaults);
        });
    connect(applyButton, &QPushButton::clicked, this, &SettingsWindow::OnApplyCards);

    LoadCardValues(stosis::SCardSettings());
    RefreshCardExamples();
    return scroll;
}

void SettingsWindow::LoadCardValues(const stosis::SCardSettings& settings)
{
    if (!m_cardRuleCombo)
        return;
    for (QWidget* widget : std::initializer_list<QWidget*>{ m_cardRuleCombo, m_cardShareSlider, m_cardShareSpin,
                                                            m_cardExponentSlider, m_cardExponentSpin })
        widget->blockSignals(true);
    m_cardRuleCombo->setCurrentIndex(std::max(0, m_cardRuleCombo->findData(static_cast<int>(settings.rule))));
    m_cardShareSpin->setValue(settings.numberShare);
    m_cardShareSlider->setValue(static_cast<int>(std::lround(settings.numberShare * 100.0)));
    m_cardExponentSpin->setValue(settings.exponent);
    m_cardExponentSlider->setValue(static_cast<int>(std::lround(settings.exponent * 100.0)));
    for (QWidget* widget : std::initializer_list<QWidget*>{ m_cardRuleCombo, m_cardShareSlider, m_cardShareSpin,
                                                            m_cardExponentSlider, m_cardExponentSpin })
        widget->blockSignals(false);
    UpdateCardPreview();
}

void SettingsWindow::RefreshCardExamples()
{
    if (!m_cardExampleCombo)
        return;
    const QString previous = m_cardExampleCombo->currentData().toString();
    m_cardExampleCombo->blockSignals(true);
    m_cardExampleCombo->clear();
    if (m_graph)
        for (const stosis::SFlowsheetNode& node : m_graph->GetNodes())
        {
            const stosis::CInletUnit* inlet = node.type == stosis::ENodeType::Inlet ? m_graph->GetInlet(node.id) : nullptr;
            if (inlet)
                m_cardExampleCombo->addItem(QString::fromStdString(node.name) + " ("
                    + stosis::GetDistributionParameterInfo(inlet->GetSizeDistribution().type).typeName + ")",
                    "inlet:" + QString::fromStdString(node.id));
        }
    const QString unit = QString::fromUtf8(stosis::kSizeDisplayUnit);
    for (const char* sigma : { "0.25", "0.5", "1" })
        m_cardExampleCombo->addItem(QString("Beispiel: Log-Normal d50 = 100 %1, sigma_ln = %2")
            .arg(unit, QString(sigma).replace('.', ',')), QString("example:") + sigma);
    int index = m_cardExampleCombo->findData(previous);
    if (index < 0)
        index = m_cardExampleCombo->findData("example:0.5");
    if (index < 0 || (previous.isEmpty() && m_cardExampleCombo->itemData(0).toString().startsWith("inlet:")))
        index = 0;
    m_cardExampleCombo->setCurrentIndex(index);
    m_cardExampleCombo->blockSignals(false);
    UpdateCardPreview();
}

stosis::SCardSettings SettingsWindow::EnteredCardSettings() const
{
    stosis::SCardSettings settings = m_graph ? m_graph->GetCardSettings() : stosis::SCardSettings();
    if (m_cardRuleCombo)
    {
        settings.rule = static_cast<stosis::ECardDensityRule>(m_cardRuleCombo->currentData().toInt());
        settings.numberShare = m_cardShareSpin->value();
        settings.exponent = m_cardExponentSpin->value();
    }
    return settings;
}

namespace
{
    // Regel der Kartendichte als gesetzte Formel (Rich Text): allgemein,
    // darunter mit f und gamma eingesetzt, darunter die Zeichen.
    QString CardFormulaHtml(stosis::ECardDensityRule rule, double f, double gamma)
    {
        const QLocale locale;
        const auto number = [&locale](double value) { return locale.toString(value, 'f', 2); };
        const QString q3 = "<i>q</i><sub>3</sub>*(<i>d</i>)", q0 = "<i>q</i><sub>0</sub>*(<i>d</i>)";
        QString general, inserted;
        switch (rule)
        {
        case stosis::ECardDensityRule::Quadratic:
            // (Wurzel aus dem Mittel der Quadrate) hoch gamma = Mittel der Quadrate hoch gamma/2
            general = QString("[ (1 &minus; <i>f</i>) &middot; %1<sup>2</sup> + <i>f</i> &middot; %2<sup>2</sup> ]"
                              "<sup><i>&gamma;</i>/2</sup>").arg(q3, q0);
            inserted = QString("[ %3 &middot; %1<sup>2</sup> + %4 &middot; %2<sup>2</sup> ]<sup>%5</sup>")
                           .arg(q3, q0, number(1.0 - f), number(f), number(0.5 * gamma));
            break;
        case stosis::ECardDensityRule::Product:
            general = QString("[ %1<sup>1 &minus; <i>f</i></sup> &middot; %2<sup><i>f</i></sup> ]<sup><i>&gamma;</i></sup>")
                          .arg(q3, q0);
            inserted = QString("[ %1<sup>%3</sup> &middot; %2<sup>%4</sup> ]<sup>%5</sup>")
                           .arg(q3, q0, number(1.0 - f), number(f), number(gamma));
            break;
        case stosis::ECardDensityRule::Sum:
        default:
            general = QString("[ (1 &minus; <i>f</i>) &middot; %1 + <i>f</i> &middot; %2 ]<sup><i>&gamma;</i></sup>").arg(q3, q0);
            inserted = QString("[ %3 &middot; %1 + %4 &middot; %2 ]<sup>%5</sup>")
                           .arg(q3, q0, number(1.0 - f), number(f), number(gamma));
            break;
        }
        return QString("<div align=\"center\" style=\"font-family: 'Times New Roman'; font-size: 15pt;\"><i>c</i>(<i>d</i>) = %1</div>"
                       "<div align=\"center\" style=\"font-family: 'Times New Roman'; font-size: 12pt; color: #534ab7;\">"
                       "mit <i>f</i> = %3 und <i>&gamma;</i> = %4: &nbsp;<i>c</i>(<i>d</i>) = %2</div>"
                       "<div align=\"center\" style=\"font-size: 8pt; color: gray;\">"
                       "<i>q</i><sub>3</sub>*, <i>q</i><sub>0</sub>*: Masse- und Anzahldichte je ln d (Flaeche 1) &middot; "
                       "<i>f</i>: Anteil Anzahl &middot; <i>&gamma;</i>: Exponent der Abflachung (1 = keine)</div>")
            .arg(general, inserted, number(f), number(gamma));
    }
}

void SettingsWindow::UpdateCardPreview()
{
    if (m_cardFormula && m_cardRuleCombo)
        m_cardFormula->setText(CardFormulaHtml(static_cast<stosis::ECardDensityRule>(m_cardRuleCombo->currentData().toInt()),
                                               m_cardShareSpin->value(), m_cardExponentSpin->value()));
    if (!m_cardPreview || !m_cardExampleCombo)
        return;
    const QString example = m_cardExampleCombo->currentData().toString();
    stosis::SSizeDistribution distribution{ stosis::EDistributionType::LogNormal, 100e-6, 0.5 };
    if (example.startsWith("example:"))
        distribution.width = example.mid(8).toDouble();
    else if (example.startsWith("inlet:") && m_graph)
        if (const stosis::CInletUnit* inlet = m_graph->GetInlet(example.mid(6).toStdString()))
            distribution = inlet->GetSizeDistribution();
    const stosis::SSimulationGrid grid = m_graph ? m_graph->GetGrid() : stosis::SSimulationGrid();
    m_cardPreview->SetData(distribution, EnteredCardSettings(), grid.GetCardCountMin());
    if (m_cardSummary)
        m_cardSummary->setText(m_cardPreview->SummaryHtml());
}

void SettingsWindow::OnApplyCards()
{
    if (!m_graph)
        return;
    m_graph->SetCardSettings(EnteredCardSettings());
    RefreshPreviews();
    emit SettingsApplied();
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
    if (graph)
        LoadCardValues(graph->GetCardSettings());
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
    RefreshCardExamples();
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
    UpdateCardPreview();
}
