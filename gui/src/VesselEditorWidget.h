#pragma once
#include <QWidget>
#include <string>
#include "FlowsheetGraph.h"

class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QCheckBox;
class QLabel;
class QPushButton;
class FractionPreviewWidget;

// Editor-Seite fuer EINEN Behaelter (Einstellungsfenster, Reiter "Knoten"):
//  - Mischung: ideal durchmischt, Pfropfenstroemung, Kaskade (Zahl der
//    Kessel), Kernfluss (Kernanteil, Kernzeit); mittlere Verweilzeit tau
//    oder Charge ohne Ablauf
//  - Anfangsfuellung: leer, wie im Dauerbetrieb oder vorgegeben (Masse,
//    Stoff, Temperatur, Groessenverteilung), mit Empfehlungen
//  - Vorrat im Partikelmodus: hoechstens ... x N Karten
//  - Vorschau: Ablauf relativ zum Zulauf nach leerem Start (Summenkurve der
//    Verweilzeit) und der Anteil eines Zulaufs, der noch im Behaelter ist
// "Uebernehmen" schreibt die Werte in den Behaelter (simuliert nicht).
class VesselEditorWidget : public QWidget
{
    Q_OBJECT

public:
    explicit VesselEditorWidget(QWidget* parent = nullptr);

    void SetGraph(stosis::CFlowsheetGraph* graph);
    // Stoffliste fuer die vorgegebene Anfangsfuellung; nullptr = keine Materialdatei.
    void SetMaterialDatabase(const stosis::CMaterialDatabase* materials);
    void RefreshMaterialList();
    void SetNode(const std::string& nodeId);
    // dt, N oder Dauer geaendert -> Hinweise und Vorschau neu.
    void UpdateHints();

signals:
    void Applied();

private slots:
    void OnValuesChanged();
    void OnDistributionTypeChanged(int index);
    void OnApply();

private:
    void SetupUi();
    void LoadNode();
    stosis::SVesselSettings CurrentSettings() const;
    void SelectMaterial(const std::string& name);
    // Beschriftung und Bereiche der Verteilungsparameter an den Typ anpassen.
    void ApplyDistributionTypeToControls(stosis::EDistributionType type);
    void UpdatePreview(const stosis::SVesselSettings& settings);

    stosis::CFlowsheetGraph* m_graph = nullptr;
    const stosis::CMaterialDatabase* m_materials = nullptr;
    std::string m_nodeId;

    QComboBox* m_mixingCombo = nullptr;
    QDoubleSpinBox* m_tauSpin = nullptr;
    QCheckBox* m_batchCheck = nullptr;
    QLabel* m_cellsLabel = nullptr;
    QSpinBox* m_cellsSpin = nullptr;
    QLabel* m_coreShareLabel = nullptr;
    QDoubleSpinBox* m_coreShareSpin = nullptr; // %
    QLabel* m_coreTimeLabel = nullptr;
    QDoubleSpinBox* m_coreTimeSpin = nullptr;  // % von tau
    QLabel* m_mixingInfo = nullptr;

    QComboBox* m_fillCombo = nullptr;
    QWidget* m_fillBox = nullptr;
    QDoubleSpinBox* m_fillMassSpin = nullptr;  // kg
    QComboBox* m_materialCombo = nullptr;
    QDoubleSpinBox* m_fillTemperatureSpin = nullptr; // K
    QComboBox* m_distTypeCombo = nullptr;
    QLabel* m_positionLabel = nullptr;
    QDoubleSpinBox* m_positionSpin = nullptr;
    QLabel* m_widthLabel = nullptr;
    QDoubleSpinBox* m_widthSpin = nullptr;
    QLabel* m_fillInfo = nullptr;

    QDoubleSpinBox* m_stockSpin = nullptr; // x N Karten
    QLabel* m_stockInfo = nullptr;

    FractionPreviewWidget* m_preview = nullptr;
    QLabel* m_previewCaption = nullptr;
    QLabel* m_errorLabel = nullptr;
    QPushButton* m_applyButton = nullptr;
};
