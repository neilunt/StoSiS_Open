#include "InletEditorWidget.h"
#include "DistributionPreviewWidget.h"
#include <QTableWidget>
#include <QPushButton>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QGroupBox>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QGridLayout>
#include <QLocale>
#include <QApplication>
#include <QClipboard>
#include <QMessageBox>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <algorithm>
#include <cmath>
#include "SizeGrid.h"

// Groessen werden in um angezeigt, intern in m gespeichert: Faktor fuer die
// Lage (immer eine Groesse) und die Breite (nur bei Normal eine Groesse).
static double PositionDisplayFactor()
{
	return stosis::kSizeDisplayFactor;
}

static double WidthDisplayFactor(stosis::EDistributionType type)
{
	return stosis::GetDistributionParameterInfo(type).widthIsSize ? stosis::kSizeDisplayFactor : 1.0;
}

InletEditorWidget::InletEditorWidget(QWidget* parent)
	: QWidget(parent)
{
	SetupUi();
}

void InletEditorWidget::SetupUi()
{
	auto* mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(0, 0, 0, 0);

	mainLayout->addWidget(CreateMaterialGroup());
	mainLayout->addWidget(CreateTimeProfileGroup(), 1);
	mainLayout->addWidget(CreateDistributionGroup());

	// ---- Uebernehmen (simuliert NICHT) -------------
	m_applyButton = new QPushButton("Uebernehmen", this);
	connect(m_applyButton, &QPushButton::clicked, this, &InletEditorWidget::OnApply);
	mainLayout->addWidget(m_applyButton);
}

QWidget* InletEditorWidget::CreateMaterialGroup()
{
	auto* group = new QGroupBox("Stoff und Zustand", this);
	auto* layout = new QGridLayout(group);

	layout->addWidget(new QLabel("Stoff:", group), 0, 0);
	m_materialCombo = new QComboBox(group);
	m_materialCombo->setToolTip("Ein Inlet liefert genau einen Stoff. Mischungen ueber mehrere Inlets und einen Merger.\n"
		"Die Stoffdaten stehen im Einstellungsfenster unter \"Materialien\".");
	layout->addWidget(m_materialCombo, 0, 1, 1, 2);

	layout->addWidget(new QLabel("Temperatur T [K]:", group), 1, 0);
	m_temperatureSpin = new QDoubleSpinBox(group);
	m_temperatureSpin->setRange(1.0, 10000.0);
	m_temperatureSpin->setDecimals(2);
	m_temperatureSpin->setValue(stosis::kRoomTemperature);
	layout->addWidget(m_temperatureSpin, 1, 1);
	m_temperatureHint = new QLabel(group);
	layout->addWidget(m_temperatureHint, 1, 2);

	layout->addWidget(new QLabel("Druck p [Pa]:", group), 2, 0);
	m_pressureSpin = new QDoubleSpinBox(group);
	m_pressureSpin->setRange(1.0, 1.0e9);
	m_pressureSpin->setDecimals(0);
	m_pressureSpin->setSingleStep(1000.0);
	m_pressureSpin->setValue(stosis::kRoomPressure);
	layout->addWidget(m_pressureSpin, 2, 1);
	m_pressureHint = new QLabel(group);
	layout->addWidget(m_pressureHint, 2, 2);
	layout->setColumnStretch(2, 1);

	connect(m_temperatureSpin, &QDoubleSpinBox::valueChanged, this, &InletEditorWidget::UpdateConditionHints);
	connect(m_pressureSpin, &QDoubleSpinBox::valueChanged, this, &InletEditorWidget::UpdateConditionHints);
	UpdateConditionHints();
	RefreshMaterialList();
	return group;
}

void InletEditorWidget::UpdateConditionHints()
{
	const QLocale locale;
	m_temperatureHint->setText(QString("= %1 %2C").arg(locale.toString(m_temperatureSpin->value() - 273.15, 'f', 2)).arg(QChar(0x00B0)));
	m_pressureHint->setText(QString("= %1 bar").arg(locale.toString(m_pressureSpin->value() / 1.0e5, 'f', 3)));
}

void InletEditorWidget::SetMaterialDatabase(const stosis::CMaterialDatabase* materials)
{
	m_materials = materials;
	RefreshMaterialList();
}

void InletEditorWidget::RefreshMaterialList()
{
	const QString previous = m_materialCombo->currentData().toString();
	m_materialCombo->clear();
	if (m_materials)
	{
		for (const stosis::SMaterial& material : m_materials->GetMaterials())
		{
			QString text = QString::fromStdString(material.name);
			if (material.state == "gas")
				text += " (Gas)";
			else if (material.state == "liquid")
				text += " (fluessig)";
			m_materialCombo->addItem(text, QString::fromStdString(material.name));
		}
	}
	// Auswahl behalten; beim ersten Aufbau zeigt LoadInlet den Stoff des Inlets.
	if (!previous.isEmpty())
		SelectMaterial(previous.toStdString());
}

void InletEditorWidget::SelectMaterial(const std::string& name)
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

QWidget* InletEditorWidget::CreateTimeProfileGroup()
{
	auto* group = new QGroupBox("Zeitprofil", this);
	auto* layout = new QVBoxLayout(group);

	m_table = new QTableWidget(0, 2, group);
	m_table->setHorizontalHeaderLabels({ "Zeit [s]", "Flowrate [kg/s]" });
	m_table->horizontalHeader()->setStretchLastSection(true);

	// EditTrigger CurrentChanged sorgt dafuer, dass ein einzelner Klick
	// den Bearbeitungsmodus startet.
	m_table->setEditTriggers(QAbstractItemView::CurrentChanged | QAbstractItemView::AnyKeyPressed);
	layout->addWidget(m_table, 1);

	auto* rowButtonsLayout = new QHBoxLayout();
	m_addRowButton = new QPushButton("Zeile Hinzufuegen", group);
	connect(m_addRowButton, &QPushButton::clicked, this, &InletEditorWidget::OnAddRow);
	rowButtonsLayout->addWidget(m_addRowButton);

	m_removeRowButton = new QPushButton("Zeile entfernen", group);
	connect(m_removeRowButton, &QPushButton::clicked, this, &InletEditorWidget::OnRemoveRow);
	rowButtonsLayout->addWidget(m_removeRowButton);
	layout->addLayout(rowButtonsLayout);

	m_restoreProfileButton = new QPushButton("Anfangsprofil wiederherstellen", group);
	m_restoreProfileButton->setToolTip("Setzt die Tabelle auf das Startprofil zurueck.\n"
		"Wirksam wie jede Aenderung erst nach \"Uebernehmen\".");
	connect(m_restoreProfileButton, &QPushButton::clicked, this, &InletEditorWidget::OnRestoreInitialProfile);
	layout->addWidget(m_restoreProfileButton);

	return group;
}

QWidget* InletEditorWidget::CreateDistributionGroup()
{
	auto* group = new QGroupBox("Groessenverteilung (Masse)", this);
	auto* layout = new QVBoxLayout(group);
	auto* form = new QFormLayout();
	m_distForm = form;

	// Log-Normal zuerst (Standard), die Normalverteilung zuletzt und mit
	// Warnung (Methoden-Dokument Kap. 3).
	m_distTypeCombo = new QComboBox(group);
	for (stosis::EDistributionType type : { stosis::EDistributionType::LogNormal,
	                                              stosis::EDistributionType::RRSB,
	                                              stosis::EDistributionType::GGS,
	                                              stosis::EDistributionType::Table,
	                                              stosis::EDistributionType::Normal })
	{
		m_distTypeCombo->addItem(stosis::GetDistributionParameterInfo(type).typeName,
			static_cast<int>(type));
	}
	connect(m_distTypeCombo, &QComboBox::currentIndexChanged, this, &InletEditorWidget::OnDistributionTypeChanged);
	form->addRow("Typ:", m_distTypeCombo);

	m_distWarning = new QLabel(group);
	m_distWarning->setWordWrap(true);
	m_distWarning->setStyleSheet("color: #A35200;");
	form->addRow(m_distWarning);

	m_positionLabel = new QLabel(group);
	m_positionSpin = new QDoubleSpinBox(group);
	m_positionSpin->setDecimals(4);
	form->addRow(m_positionLabel, m_positionSpin);

	m_widthLabel = new QLabel(group);
	m_widthSpin = new QDoubleSpinBox(group);
	m_widthSpin->setDecimals(4);
	form->addRow(m_widthLabel, m_widthSpin);
	layout->addLayout(form);

	// Benutzerdefinierte Verteilung: Punkte (d, Q3), z.B. aus Siebanalyse
	// oder Laserbeugung; dazwischen Q3 linear in ln d.
	m_distTableBox = new QWidget(group);
	auto* tableLayout = new QVBoxLayout(m_distTableBox);
	tableLayout->setContentsMargins(0, 0, 0, 0);
	m_distTable = new QTableWidget(0, 2, m_distTableBox);
	m_distTable->setHorizontalHeaderLabels({ QString("d [") + QString::fromUtf8(stosis::kSizeDisplayUnit) + "]",
		"Q3 [%] (Masse kleiner d)" });
	m_distTable->horizontalHeader()->setStretchLastSection(true);
	m_distTable->setMinimumHeight(140);
	m_distTable->setToolTip("Summenkurve Q3: Massenanteil der Partikel kleiner als d.\n"
		"Kleinster Punkt Q3 = 0 %, groesster Q3 = 100 %. Zwischen den Punkten wird\n"
		"Q3 linear in ln d verbunden (wie bei einer Siebanalyse).");
	tableLayout->addWidget(m_distTable);
	auto* pointButtons = new QHBoxLayout();
	auto* addPoint = new QPushButton("Punkt hinzufuegen", m_distTableBox);
	auto* removePoint = new QPushButton("Punkt entfernen", m_distTableBox);
	auto* pastePoints = new QPushButton("Aus Zwischenablage einfuegen", m_distTableBox);
	pastePoints->setToolTip("Zwei Spalten: d in " + QString::fromUtf8(stosis::kSizeDisplayUnit) +
		" und Q3 in %, getrennt durch Tabulator, Semikolon oder Leerzeichen (z.B. aus Excel).\n"
		"Komma oder Punkt als Dezimalzeichen; Zeilen ohne zwei Zahlen (Ueberschriften) werden uebersprungen.");
	connect(addPoint, &QPushButton::clicked, this, &InletEditorWidget::OnAddDistributionPoint);
	connect(removePoint, &QPushButton::clicked, this, &InletEditorWidget::OnRemoveDistributionPoint);
	connect(pastePoints, &QPushButton::clicked, this, &InletEditorWidget::OnPasteDistributionPoints);
	pointButtons->addWidget(addPoint);
	pointButtons->addWidget(removePoint);
	pointButtons->addWidget(pastePoints);
	tableLayout->addLayout(pointButtons);
	m_distTableStatus = new QLabel(m_distTableBox);
	m_distTableStatus->setWordWrap(true);
	tableLayout->addWidget(m_distTableStatus);
	layout->addWidget(m_distTableBox);
	connect(m_distTable, &QTableWidget::itemChanged, this, &InletEditorWidget::UpdatePreview);

	m_preview = new DistributionPreviewWidget(group);
	layout->addWidget(m_preview);

	// Vorschau schon beim Tippen aktualisieren - noch vor "Uebernehmen".
	connect(m_positionSpin, &QDoubleSpinBox::valueChanged, this, &InletEditorWidget::UpdatePreview);
	connect(m_widthSpin, &QDoubleSpinBox::valueChanged, this, &InletEditorWidget::UpdatePreview);

	ApplyDistributionTypeToControls(stosis::EDistributionType::LogNormal);
	return group;
}

void InletEditorWidget::SetGraph(stosis::CFlowsheetGraph* graph)
{
	m_graph = graph;
	LoadInlet();
}

void InletEditorWidget::SetNode(const std::string& nodeId)
{
	m_nodeId = nodeId;
	LoadInlet();
}

void InletEditorWidget::RefreshGridPreview()
{
	UpdatePreview();
}

void InletEditorWidget::LoadInlet()
{
	m_table->setRowCount(0);
	stosis::CInletUnit* inlet = m_graph ? m_graph->GetInlet(m_nodeId) : nullptr;
	if (!inlet)
	{
		UpdatePreview();
		return;
	}

	SelectMaterial(inlet->GetMaterial());
	m_temperatureSpin->setValue(inlet->GetTemperature());
	m_pressureSpin->setValue(inlet->GetPressure());

	FillProfileTable(inlet->GetTimeProfile());

	// --- Groessenverteilung ---
	// Signale blockieren, damit der Typwechsel hier nicht die
	// Standardwerte des Typs ueber die gespeicherten Werte schreibt.
	const stosis::SSizeDistribution& dist = inlet->GetSizeDistribution();
	m_distTypeCombo->blockSignals(true);
	m_distTypeCombo->setCurrentIndex(m_distTypeCombo->findData(static_cast<int>(dist.type)));
	m_distTypeCombo->blockSignals(false);
	ApplyDistributionTypeToControls(dist.type);
	m_positionSpin->setValue(dist.position * PositionDisplayFactor());
	m_widthSpin->setValue(dist.width * WidthDisplayFactor(dist.type));
	FillDistributionTable(dist.type == stosis::EDistributionType::Table ? dist.points : DefaultTablePoints());
	UpdatePreview();
}

void InletEditorWidget::ApplyDistributionTypeToControls(stosis::EDistributionType type)
{
	// Anzeige in um; Untergrenze hoechstens 0,1 nm, damit die vier
	// Nachkommastellen sie noch darstellen.
	const stosis::SDistributionParameterInfo info = stosis::GetDistributionParameterInfo(type);
	const QString unit = QString::fromUtf8(stosis::kSizeDisplayUnit);
	const double fp = PositionDisplayFactor();
	const double fw = WidthDisplayFactor(type);
	// Tabelle statt zweier Parameter; Warnung nur, wo es eine gibt (Normal).
	m_distForm->setRowVisible(m_positionSpin, !info.isTable);
	m_distForm->setRowVisible(m_widthSpin, !info.isTable);
	m_distTableBox->setVisible(info.isTable);
	const QString warning = QString::fromUtf8(stosis::GetDistributionWarning(type));
	m_distWarning->setText(warning);
	m_distForm->setRowVisible(m_distWarning, !warning.isEmpty());
	if (info.isTable)
		return;
	m_positionLabel->setText(QString(info.positionLabel) + " [" + unit + "]:");
	m_positionSpin->setRange(info.positionMin > 0.0 ? std::max(info.positionMin * fp, 1e-4) : 0.0, info.positionMax * fp);
	m_widthLabel->setText(QString(info.widthLabel) + (info.widthIsSize ? " [" + unit + "]:" : QString(" [-]:")));
	m_widthSpin->setRange(std::max(info.widthMin * fw, 1e-4), info.widthMax * fw);
}

void InletEditorWidget::OnDistributionTypeChanged(int index)
{
	if (index < 0)
		return;

	const auto type = static_cast<stosis::EDistributionType>(m_distTypeCombo->itemData(index).toInt());
	ApplyDistributionTypeToControls(type);

	// Hat das Inlet schon diesen Typ, seine Werte zeigen - sonst die
	// Standardwerte des neuen Typs (die Parameter der Typen sind nicht
	// vergleichbar, z.B. sigma [um] vs. sigma_ln [-]).
	stosis::CInletUnit* inlet = m_graph ? m_graph->GetInlet(m_nodeId) : nullptr;
	if (inlet && inlet->GetSizeDistribution().type == type)
	{
		m_positionSpin->setValue(inlet->GetSizeDistribution().position * PositionDisplayFactor());
		m_widthSpin->setValue(inlet->GetSizeDistribution().width * WidthDisplayFactor(type));
	}
	else if (type != stosis::EDistributionType::Table)
	{
		const stosis::SDistributionParameterInfo info = stosis::GetDistributionParameterInfo(type);
		m_positionSpin->setValue(info.positionDefault * PositionDisplayFactor());
		m_widthSpin->setValue(info.widthDefault * WidthDisplayFactor(type));
	}
	// Die Tabelle behaelt ihren Inhalt (Startpunkte oder zuletzt bearbeitet).
	UpdatePreview();
}

stosis::SSizeDistribution InletEditorWidget::CurrentDistribution() const
{
	stosis::SSizeDistribution dist;
	dist.type = static_cast<stosis::EDistributionType>(m_distTypeCombo->currentData().toInt());
	dist.position = m_positionSpin->value() / PositionDisplayFactor();
	dist.width = m_widthSpin->value() / WidthDisplayFactor(dist.type);
	if (dist.type == stosis::EDistributionType::Table)
		dist.points = ReadDistributionTable();
	return dist;
}

void InletEditorWidget::UpdatePreview()
{
	if (m_graph)
	{
		const stosis::SSimulationGrid& grid = m_graph->GetGrid();
		m_preview->SetGrid(grid.GetSizeGrid());
		m_preview->SetCards(grid.representation == stosis::ERepresentation::Particles, grid.cards);
	}
	const stosis::SSizeDistribution dist = CurrentDistribution();
	m_preview->SetDistribution(dist);
	if (dist.type == stosis::EDistributionType::Table)
	{
		const std::string problem = dist.CheckTable();
		m_distTableStatus->setStyleSheet(problem.empty() ? "color: #1E823C;" : "color: #C82828;");
		m_distTableStatus->setText(problem.empty()
			? QString("%1 Punkte, Tabelle in Ordnung.").arg(dist.points.size())
			: QString::fromStdString(problem));
	}
}

// Startpunkte der Tabelle: Siebsatz 40 ... 250 um mit Q3 der Standard-
// Verteilung (Log-Normal, d50 = 100 um, sigma_ln = 0,25), auf 0,1 % gerundet;
// aussen genau 0 und 100 %.
std::vector<stosis::SDistributionPoint> InletEditorWidget::DefaultTablePoints()
{
	const stosis::SSizeDistribution standard;
	std::vector<stosis::SDistributionPoint> points;
	for (double size : { 40.0, 50.0, 63.0, 80.0, 100.0, 125.0, 160.0, 200.0, 250.0 })
		points.push_back({ size * 1e-6, std::round(standard.Cumulative(size * 1e-6) * 1000.0) / 1000.0 });
	points.front().fraction = 0.0;
	points.back().fraction = 1.0;
	return points;
}

void InletEditorWidget::FillDistributionTable(const std::vector<stosis::SDistributionPoint>& points)
{
	const QSignalBlocker blocker(m_distTable);
	m_distTable->setRowCount(static_cast<int>(points.size()));
	for (int row = 0; row < static_cast<int>(points.size()); ++row)
	{
		m_distTable->setItem(row, 0, new QTableWidgetItem(QString::number(points[row].size * stosis::kSizeDisplayFactor, 'g', 6)));
		m_distTable->setItem(row, 1, new QTableWidgetItem(QString::number(points[row].fraction * 100.0, 'g', 6)));
	}
}

// Liest die Punkte (Komma oder Punkt als Dezimalzeichen), sortiert nach d.
// Leere Zeilen werden uebersprungen; Unlesbares wird zu einem ungueltigen
// Punkt, den CheckTable meldet.
std::vector<stosis::SDistributionPoint> InletEditorWidget::ReadDistributionTable() const
{
	std::vector<stosis::SDistributionPoint> points;
	for (int row = 0; row < m_distTable->rowCount(); ++row)
	{
		const QTableWidgetItem* sizeItem = m_distTable->item(row, 0);
		const QTableWidgetItem* fractionItem = m_distTable->item(row, 1);
		const QString sizeText = sizeItem ? sizeItem->text().trimmed().replace(',', '.') : QString();
		const QString fractionText = fractionItem ? fractionItem->text().trimmed().replace(',', '.') : QString();
		if (sizeText.isEmpty() && fractionText.isEmpty())
			continue;
		bool okSize = false, okFraction = false;
		const double size = sizeText.toDouble(&okSize) / stosis::kSizeDisplayFactor;
		const double fraction = fractionText.toDouble(&okFraction) / 100.0;
		points.push_back({ okSize ? size : 0.0, okFraction ? fraction : -1.0 });
	}
	std::sort(points.begin(), points.end(), [](const stosis::SDistributionPoint& a, const stosis::SDistributionPoint& b)
		{
			return a.size < b.size;
		});
	return points;
}

void InletEditorWidget::OnAddDistributionPoint()
{
	const int row = m_distTable->rowCount();
	m_distTable->insertRow(row);
	m_distTable->setItem(row, 0, new QTableWidgetItem());
	m_distTable->setItem(row, 1, new QTableWidgetItem());
	m_distTable->setCurrentCell(row, 0);
}

void InletEditorWidget::OnRemoveDistributionPoint()
{
	const int row = m_distTable->currentRow();
	m_distTable->removeRow(row >= 0 ? row : m_distTable->rowCount() - 1);
	UpdatePreview();
}

void InletEditorWidget::OnPasteDistributionPoints()
{
	std::vector<stosis::SDistributionPoint> points;
	const QStringList lines = QApplication::clipboard()->text().split(QRegularExpression("[\r\n]+"), Qt::SkipEmptyParts);
	for (const QString& line : lines)
	{
		QStringList fields = line.split(QRegularExpression("[\t;]"), Qt::SkipEmptyParts);
		if (fields.size() < 2)
			fields = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
		if (fields.size() < 2)
			continue;
		bool okSize = false, okFraction = false;
		const double size = fields[0].trimmed().replace(',', '.').toDouble(&okSize);
		const double fraction = fields[1].trimmed().replace(',', '.').toDouble(&okFraction);
		if (okSize && okFraction)
			points.push_back({ size / stosis::kSizeDisplayFactor, fraction / 100.0 });
	}
	if (points.empty())
	{
		QMessageBox::information(this, "Aus Zwischenablage einfuegen",
			"In der Zwischenablage stehen keine Zeilen mit zwei Zahlen (d in um, Q3 in %).");
		return;
	}
	FillDistributionTable(points);
	UpdatePreview();
}

void InletEditorWidget::FillProfileTable(const std::vector<stosis::SInletTimePoint>& points)
{
	m_table->setRowCount(static_cast<int>(points.size()));
	for (int row = 0; row < static_cast<int>(points.size()); ++row)
	{
		m_table->setItem(row, 0, new QTableWidgetItem(QString::number(points[row].time)));
		m_table->setItem(row, 1, new QTableWidgetItem(QString::number(points[row].massFlow)));
	}
}

void InletEditorWidget::OnRestoreInitialProfile()
{
	FillProfileTable(stosis::GetDefaultInletTimeProfile());
}

void InletEditorWidget::OnAddRow()
{
	const int newRow = m_table->rowCount();
	m_table->insertRow(newRow);
	m_table->setItem(newRow, 0, new QTableWidgetItem(QString::number(0.0)));
	m_table->setItem(newRow, 1, new QTableWidgetItem(QString::number(0.0)));
}

void InletEditorWidget::OnRemoveRow()
{
	const int row = m_table->currentRow();
	if (row >= 0)
		m_table->removeRow(row);
	else if (m_table->rowCount() > 0)
		m_table->removeRow(m_table->rowCount() - 1);
}

void InletEditorWidget::OnApply()
{
	stosis::CInletUnit* inlet = m_graph ? m_graph->GetInlet(m_nodeId) : nullptr;
	if (!inlet)
		return;

	// Eine ungueltige Verteilungstabelle wird nicht uebernommen (sie lieferte
	// keinen Massenstrom).
	const stosis::SSizeDistribution distribution = CurrentDistribution();
	if (distribution.type == stosis::EDistributionType::Table && !distribution.CheckTable().empty())
	{
		QMessageBox::warning(this, "Groessenverteilung",
			"Die Tabelle ist noch nicht vollstaendig: " + QString::fromStdString(distribution.CheckTable()) +
			"\nNichts uebernommen.");
		return;
	}

	// Tabelle auslesen und als Zeitprofil ins CInletUnit schreiben
	std::vector<stosis::SInletTimePoint> points;
	for (int row = 0; row < m_table->rowCount(); ++row)
	{
		QTableWidgetItem* timeItem = m_table->item(row, 0);
		QTableWidgetItem* flowItem = m_table->item(row, 1);
		if (!timeItem || !flowItem)
			continue;

		stosis::SInletTimePoint point;
		point.time = timeItem->text().toDouble();
		point.massFlow = flowItem->text().toDouble();
		points.push_back(point);
	}
	std::sort(points.begin(), points.end(), [](const stosis::SInletTimePoint& a, const stosis::SInletTimePoint& b)
		{
			return a.time < b.time;
		});
	inlet->SetTimeProfile(points);
	inlet->SetSizeDistribution(distribution);
	if (m_materialCombo->currentIndex() >= 0)
		inlet->SetMaterial(m_materialCombo->currentData().toString().toStdString());
	inlet->SetTemperature(m_temperatureSpin->value());
	inlet->SetPressure(m_pressureSpin->value());

	// Sortierte Fassung zurueck in die Tabelle, damit man sieht, was
	// tatsaechlich uebernommen wurde.
	LoadInlet();
	emit Applied();
}
