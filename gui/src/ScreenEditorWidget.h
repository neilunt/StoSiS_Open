#pragma once
#include <QWidget>
#include <string>
#include <vector>
#include "FlowsheetGraph.h"

class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QPushButton;
class QTabBar;
class GradeEfficiencyPreview;

// Editor page for ONE screen (settings window, tab "Knoten"):
//  - one tab per deck (only with several decks), deck 1 = top deck
//  - per deck: model of the grade efficiency T(d) (Plitt, Molerus-Hoffmann,
//    Teipel-Hennig), cut size x_cut, sharpness alpha; beta and offset only
//    for Teipel-Hennig
//  - preview over the size range of the grid (logarithmic size axis): with
//    one deck T(d) into the coarse product, with several decks the share of
//    every output
// The number of outputs is fixed when the screen is created (palette
// "Sieb 2/3/4"). "Uebernehmen" writes the values into the screen (does not
// simulate).
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
    void OnDeckChanged(int index);
    void OnApply();

private:
    void SetupUi();
    void LoadNode();
    void LoadDeckIntoForm();
    stosis::SScreenSettings FormSettings() const;

    stosis::CFlowsheetGraph* m_graph = nullptr;
    std::string m_nodeId;
    std::vector<stosis::SScreenSettings> m_decks; // edited values, deck 1 (top) ... n - 1
    int m_currentDeck = 0;

    QTabBar* m_deckTabs = nullptr;
    QGroupBox* m_deckGroup = nullptr;
    QComboBox* m_modelCombo = nullptr;
    QDoubleSpinBox* m_cutSizeSpin = nullptr; // display unit (um)
    QDoubleSpinBox* m_alphaSpin = nullptr;
    QLabel* m_betaLabel = nullptr;
    QDoubleSpinBox* m_betaSpin = nullptr;
    QLabel* m_offsetLabel = nullptr;
    QDoubleSpinBox* m_offsetSpin = nullptr;
    QLabel* m_formulaLabel = nullptr; // selected model as a typeset formula
    QLabel* m_errorLabel = nullptr;
    GradeEfficiencyPreview* m_preview = nullptr;
    QPushButton* m_applyButton = nullptr;
};
