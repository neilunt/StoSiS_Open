#pragma once
#include <QWidget>
#include <string>
#include "FlowsheetGraph.h"
#include "Materials.h"

class QTabWidget;
class QComboBox;
class QStackedWidget;
class QSpinBox;
class QDoubleSpinBox;
class QLabel;
class InletEditorWidget;
class ProcessNodeEditorWidget;
class SplitterEditorWidget;
class MaterialsPage;
class SieveGridWidget;

// Einstellungsfenster (Einstellungsebene, getrennt von der Darstellung
// im Hauptfenster). Reiter:
//   "Knoten": ein Dropdown mit ALLEN Flowsheet-Knoten, darunter die zum
//             Knotentyp passende Editor-Seite (Inlet, Prozess, ...). Neue
//             Unit-Arten brauchen nur eine weitere Seite im Stapel.
//   "Raster": gemeinsames Groessen-/Zeitraster des Flowsheets (Groessen in
//             um, logarithmisch oder linear), darunter Richtwerte fuer
//             typische Prozesse und die Pruefung, ob die Inlets passen.
//   "Materialien": Stoffdaten aus data/Materialien.json (nur ansehen).
// "Uebernehmen" schreibt ueberall nur Werte - simuliert wird
// ausschliesslich ueber "Simulieren" im Hauptfenster.
class SettingsWindow : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsWindow(QWidget* parent = nullptr);

    void SetGraph(stosis::CFlowsheetGraph* graph);
    // Stoffdaten fuer den Reiter "Materialien"; loadError: Meldung, falls das
    // Einlesen beim Programmstart scheiterte.
    void SetMaterialDatabase(stosis::CMaterialDatabase* database, const QString& loadError);
    // Oeffnet das Fenster im Reiter "Materialien".
    void ShowMaterials();
    // Knotenliste neu aufbauen (Knoten hinzugefuegt); Auswahl bleibt erhalten.
    void RefreshNodeList();
    // Oeffnet das Fenster im Reiter "Knoten" mit diesem Knoten ausgewaehlt
    // (Doppelklick im Flowsheet).
    void ShowNode(const std::string& nodeId);
    // Seed, Dauer oder Raster geaendert -> Vorschauen (Verteilung,
    // Splitter-Anteile) neu berechnen.
    void RefreshPreviews();

signals:
    // Irgendeine Einstellung wurde uebernommen - Ergebnisse sind veraltet.
    void SettingsApplied();
    // Durchgereicht vom Prozess-Editor: alte Prozessor-Instanz zerstoert.
    void ModelTypeChanged();

private slots:
    void OnNodeSelectionChanged(int index);
    void OnApplyGrid();
    // Info und Pruefung des eingegebenen (noch nicht uebernommenen) Rasters.
    void UpdateGridCheck();

private:
    void SetupUi();
    QWidget* CreateNodeTab();
    QWidget* CreateGridTab();
    void LoadGridValues();
    // Das Raster aus den Eingabefeldern (Groessen in m).
    stosis::SSizeGrid EnteredSizeGrid() const;

    // Reihenfolge der Seiten im QStackedWidget.
    enum EPage
    {
        PageNone = 0,
        PageInlet,
        PageProcess,
        PageOutlet,
        PageSplitter,
        PageMerger
    };

    stosis::CFlowsheetGraph* m_graph = nullptr;

    QTabWidget* m_tabs = nullptr;
    QWidget* m_nodeTab = nullptr;
    MaterialsPage* m_materialsPage = nullptr;

    QComboBox* m_nodeCombo = nullptr;
    QStackedWidget* m_pages = nullptr;
    InletEditorWidget* m_inletEditor = nullptr;
    ProcessNodeEditorWidget* m_processEditor = nullptr;
    SplitterEditorWidget* m_splitterEditor = nullptr;

    QComboBox* m_gridTypeCombo = nullptr;
    QSpinBox* m_numClassesSpin = nullptr;
    QDoubleSpinBox* m_sizeMinSpin = nullptr; // um
    QDoubleSpinBox* m_sizeMaxSpin = nullptr; // um
    QLabel* m_gridInfoLabel = nullptr;   // Klassen je Dekade bzw. Delta d
    QLabel* m_gridCheckLabel = nullptr;  // Meldungen der Units zum Raster
    SieveGridWidget* m_sieveWidget = nullptr; // Raster und Siebsatz nebeneinander
};
