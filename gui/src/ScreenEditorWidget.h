#pragma once
#include <QWidget>
#include <string>
#include "FlowsheetGraph.h"

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class GradeEfficiencyPreview;

// Editor page for ONE screen (settings window, tab "Knoten"):
//  - model of the grade efficiency T(d): Plitt, Molerus-Hoffmann, Teipel-Hennig
//  - cut size x_cut, sharpness alpha; beta and offset only for Teipel-Hennig
//  - preview: T(d) over the size range of the grid (logarithmic size axis)
// "Uebernehmen" writes the values into the screen (does not simulate).
class ScreenEditorWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ScreenEditorWidget(QWidget* parent = nullptr);

    void SetGraph(stosis::CFlowsheetGraph* graph);
    void SetNode(const std::string& nodeId);
    // Size range of the grid changed -> preview anew.
    void UpdatePreview();

signals:
    void Applied();

private slots:
    void OnValuesChanged();
    void OnApply();

private:
    void SetupUi();
    void LoadNode();
    stosis::SScreenSettings CurrentSettings() const;

    stosis::CFlowsheetGraph* m_graph = nullptr;
    std::string m_nodeId;

    QComboBox* m_modelCombo = nullptr;
    QDoubleSpinBox* m_cutSizeSpin = nullptr; // display unit (um)
    QDoubleSpinBox* m_alphaSpin = nullptr;
    QLabel* m_betaLabel = nullptr;
    QDoubleSpinBox* m_betaSpin = nullptr;
    QLabel* m_offsetLabel = nullptr;
    QDoubleSpinBox* m_offsetSpin = nullptr;
    QLabel* m_errorLabel = nullptr;
    GradeEfficiencyPreview* m_preview = nullptr;
    QPushButton* m_applyButton = nullptr;
};
