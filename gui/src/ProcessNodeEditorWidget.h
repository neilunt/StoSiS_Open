#pragma once
#include <QWidget>
#include <vector>
#include <utility>
#include <string>
#include "FlowsheetGraph.h"

class QComboBox;
class QFormLayout;
class QGroupBox;
class QDoubleSpinBox;
class QPushButton;

// Editor-Seite fuer EINEN Prozess-Knoten (im Einstellungsfenster, Reiter
// "Knoten"): ein Dropdown waehlt den Modelltyp (aus der
// CStreamProcessorRegistry), darunter das dynamische Parameter-Panel,
// genau nach dem Muster von ModelPanelWidget aus der Vergleichsansicht.
// Welcher Knoten bearbeitet wird, bestimmt SetNode().
class ProcessNodeEditorWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ProcessNodeEditorWidget(QWidget* parent = nullptr);

    void SetGraph(stosis::CFlowsheetGraph* graph);
    void SetNode(const std::string& nodeId);

signals:
    // Parameter wurden in den Prozessor geschrieben (simuliert NICHT -
    // Ergebnisse sind damit veraltet bis zum naechsten "Simulieren").
    void Applied();
    // Gefeuert, nachdem der Modelltyp eines Knotens gewechselt wurde.
    // Die alte Prozessor-Instanz ist dann zerstoert - Fenster, die einen
    // Zeiger auf ihre Ergebnisse halten, muessen ihn neu holen.
    void ModelTypeChanged();

private slots:
    void OnModelTypeChanged(int index);
    void OnApply();

private:
    void SetupUi();
    void LoadNode();
    void RebuildParameterPanel();

    stosis::CFlowsheetGraph* m_graph = nullptr;
    std::string m_nodeId;

    QComboBox* m_modelCombo = nullptr;

    QGroupBox* m_paramGroup = nullptr;
    QFormLayout* m_paramLayout = nullptr;
    // Je Parameter: ID, Eingabefeld, Anzeigefaktor (Anzeige = Wert * Faktor).
    struct SParamSpin
    {
        std::string id;
        QDoubleSpinBox* spin = nullptr;
        double displayFactor = 1.0;
    };
    std::vector<SParamSpin> m_paramSpins;

    QPushButton* m_applyButton = nullptr;
};
