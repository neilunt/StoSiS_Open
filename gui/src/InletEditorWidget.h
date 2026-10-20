#pragma once
#include <QWidget>
#include <string>
#include "FlowsheetGraph.h"

class QTableWidget;
class QPushButton;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class DistributionPreviewWidget;

// Editor-Seite fuer EIN Inlet (im Einstellungsfenster, Reiter "Knoten"):
//   - Stoff (aus der Materialdatei), Temperatur [K] und Druck [Pa]
//   - Zeitprofil: editierbare Zeit-Flowrate-Tabelle
//   - Groessenverteilung: Typ (Normal, Log-Normal, RRSB), zwei Parameter
//     und eine Live-Vorschau ueber dem aktuellen Groessenraster
// "Uebernehmen" schreibt alles ins zugehoerige CInletUnit. Welches
// Inlet bearbeitet wird, bestimmt SetNode() - die Auswahl selbst trifft
// das Einstellungsfenster (Dropdown oder Doppelklick im Flowsheet).
// Simuliert wird nur ueber den "Simulieren"-Knopf im Hauptfenster.

class InletEditorWidget : public QWidget
{
	Q_OBJECT

public:
	explicit InletEditorWidget(QWidget* parent = nullptr);

	// graph muss mindestens so lange leben wie dieses Widget:
	void SetGraph(stosis::CFlowsheetGraph* graph);
	// Stoffliste fuer die Auswahl; nullptr = keine Materialdatei.
	void SetMaterialDatabase(const stosis::CMaterialDatabase* materials);
	// Materialdatei neu geladen -> Stoffliste neu aufbauen.
	void RefreshMaterialList();
	// Laedt Zeitprofil und Groessenverteilung des Inlets nodeId.
	void SetNode(const std::string& nodeId);
	// Raster hat sich geaendert -> Vorschau neu ausrichten.
	void RefreshGridPreview();

signals:
	// Werte wurden ins Inlet geschrieben (Ergebnisse sind damit veraltet).
	void Applied();

private slots:
	void OnAddRow();
	void OnRemoveRow();
	void OnRestoreInitialProfile();
	void OnDistributionTypeChanged(int index);
	void OnApply();

private:
	void SetupUi();
	QWidget* CreateMaterialGroup();
	QWidget* CreateTimeProfileGroup();
	// "= 20.00 C" / "= 1.013 bar" unter den Eingabefeldern.
	void UpdateConditionHints();
	// Stoff im Dropdown waehlen (fehlt er in der Liste, wird er ergaenzt).
	void SelectMaterial(const std::string& name);
	QWidget* CreateDistributionGroup();
	void LoadInlet();
	void FillProfileTable(const std::vector<stosis::SInletTimePoint>& points);
	// Beschriftung + Bereiche der Parameterfelder an den Typ anpassen.
	void ApplyDistributionTypeToControls(stosis::EDistributionType type);
	stosis::SSizeDistribution CurrentDistribution() const;
	void UpdatePreview();

	stosis::CFlowsheetGraph* m_graph = nullptr;
	std::string m_nodeId;
	const stosis::CMaterialDatabase* m_materials = nullptr;

	QComboBox* m_materialCombo = nullptr;
	QDoubleSpinBox* m_temperatureSpin = nullptr;
	QDoubleSpinBox* m_pressureSpin = nullptr;
	QLabel* m_temperatureHint = nullptr;
	QLabel* m_pressureHint = nullptr;

	QTableWidget* m_table = nullptr;
	QPushButton* m_addRowButton = nullptr;
	QPushButton* m_removeRowButton = nullptr;
	QPushButton* m_restoreProfileButton = nullptr;

	QComboBox* m_distTypeCombo = nullptr;
	QLabel* m_positionLabel = nullptr;
	QDoubleSpinBox* m_positionSpin = nullptr;
	QLabel* m_widthLabel = nullptr;
	QDoubleSpinBox* m_widthSpin = nullptr;
	DistributionPreviewWidget* m_preview = nullptr;

	QPushButton* m_applyButton = nullptr;
};
