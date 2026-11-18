#pragma once
#include <QWidget>
#include <string>
#include <vector>
#include "FlowsheetGraph.h"

class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QTableWidget;

// Editor page for ONE compound separator (settings window, tab "Knoten"):
// a table with one row per compound - first those of the flowsheet, then
// the other compounds of the material file - and the share of each that
// goes to output 1 (abgetrennt). Compounds with share 0 go completely to
// output 2 (Rest). "Uebernehmen" writes the shares into the unit (does
// not simulate).
class CompoundSeparatorEditorWidget : public QWidget
{
    Q_OBJECT

public:
    explicit CompoundSeparatorEditorWidget(QWidget* parent = nullptr);

    void SetGraph(stosis::CFlowsheetGraph* graph);
    void SetMaterialDatabase(const stosis::CMaterialDatabase* materials);
    void SetNode(const std::string& nodeId);

signals:
    void Applied();

private slots:
    void OnValuesChanged();
    void OnApply();

private:
    void SetupUi();
    void LoadNode();
    stosis::SCompoundSeparatorSettings CurrentSettings() const;

    stosis::CFlowsheetGraph* m_graph = nullptr;
    const stosis::CMaterialDatabase* m_materials = nullptr;
    std::string m_nodeId;

    QTableWidget* m_table = nullptr;
    std::vector<std::string> m_rowNames;
    std::vector<QDoubleSpinBox*> m_rowSpins;
    QLabel* m_summaryLabel = nullptr;
    QLabel* m_errorLabel = nullptr;
    QPushButton* m_applyButton = nullptr;
};
