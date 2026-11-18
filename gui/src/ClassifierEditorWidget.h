#pragma once
#include <QWidget>
#include <string>
#include "FlowsheetGraph.h"

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class GradeEfficiencyPreview;

// Editor page for ONE classifier (settings window, tab "Knoten"):
//  - fluid from the material file (gases and liquids): air for an air
//    classifier, water for a settling tank
//  - grade efficiency over w_s / w_cut: Plitt, Molerus-Hoffmann,
//    Teipel-Hennig; cut velocity w_cut, sharpness alpha, beta and offset
//    only for Teipel-Hennig
//  - preview: T(d) for every compound of the flowsheet at room conditions,
//    with the size of T = 0.5 per compound
// "Uebernehmen" writes the values into the classifier (does not simulate).
class ClassifierEditorWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ClassifierEditorWidget(QWidget* parent = nullptr);

    void SetGraph(stosis::CFlowsheetGraph* graph);
    void SetMaterialDatabase(const stosis::CMaterialDatabase* materials);
    void SetNode(const std::string& nodeId);
    // Size range of the grid or the compounds changed -> preview anew.
    void UpdatePreview();

signals:
    void Applied();

private slots:
    void OnValuesChanged();
    void OnApply();

private:
    void SetupUi();
    void LoadNode();
    void RefreshFluidList();
    void SelectFluid(const std::string& name);
    stosis::SClassifierSettings CurrentSettings() const;

    stosis::CFlowsheetGraph* m_graph = nullptr;
    const stosis::CMaterialDatabase* m_materials = nullptr;
    std::string m_nodeId;

    QComboBox* m_fluidCombo = nullptr;
    QLabel* m_fluidInfoLabel = nullptr;
    QComboBox* m_modelCombo = nullptr;
    QDoubleSpinBox* m_cutVelocitySpin = nullptr; // m/s
    QDoubleSpinBox* m_alphaSpin = nullptr;
    QLabel* m_betaLabel = nullptr;
    QDoubleSpinBox* m_betaSpin = nullptr;
    QLabel* m_offsetLabel = nullptr;
    QDoubleSpinBox* m_offsetSpin = nullptr;
    QLabel* m_formulaLabel = nullptr; // selected model as a typeset formula
    QLabel* m_errorLabel = nullptr;
    GradeEfficiencyPreview* m_preview = nullptr;
    QLabel* m_previewNote = nullptr;
    QPushButton* m_applyButton = nullptr;
};
