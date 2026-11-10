#pragma once
#include <QWidget>
#include <functional>
#include <map>
#include "FlowsheetGraph.h"
#include "FlowsheetFile.h"

class QLabel;
class QPlainTextEdit;
class QTabWidget;
class QTreeWidget;
class QTreeWidgetItem;
class StructureDiagramWidget;

// Fenster "Struktur" (Werkzeugleiste, Menue Flowsheet; Methodenhandbuch
// 3.3): wie der Graph das Flowsheet in jedem Zeitschritt abarbeitet -
// Rechenreihenfolge, Kreise und aufgeschnittene Rueckfuehrungen. Reiter
// "Bild": Aststruktur als Zeichnung (StructureDiagramWidget); Reiter "Text":
// derselbe Baum als Liste. Daneben das Flowsheet so, wie es in der Datei
// steht (JSON); ein Klick auf einen Eintrag markiert die passende Stelle.
class FlowsheetStructureWindow : public QWidget
{
    Q_OBJECT

public:
    explicit FlowsheetStructureWindow(QWidget* parent = nullptr);

    void SetGraph(stosis::CFlowsheetGraph* graph);
    // Liefert, was ausser dem Graphen in die Datei gehoert (Anzahl Laeufe).
    void SetFileSettingsProvider(std::function<stosis::SFlowsheetFileSettings()> provider);
    // Nach Aenderungen am Flowsheet: neu aufbauen, falls sichtbar und
    // etwas anders ist.
    void Refresh();

    // Dialog "Flowsheet speichern unter"; liefert den Pfad, leer bei
    // Abbruch oder Fehler (Fehler mit Meldung).
    static QString SaveFlowsheetAs(QWidget* parent, const stosis::CFlowsheetGraph& graph,
        const stosis::SFlowsheetFileSettings& settings);

protected:
    void showEvent(QShowEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void Rebuild();
    void AddBranch(QTreeWidgetItem* parent, const stosis::SStructureBranch& branch,
        const stosis::SFlowsheetStructure& structure, const std::map<std::string, std::string>& placedUnder);
    QString DescribeSummary(const stosis::SFlowsheetStructure& structure) const;
    // Eintrag in der Datei hinterlegen: Unit (kind = Unit) bzw. Verbindung;
    // kind < 0 hebt die Markierung auf.
    void HighlightInJson(const QString& nodeId, const QString& connectionId, int kind);
    QString NodeName(const std::string& nodeId) const;

    stosis::CFlowsheetGraph* m_graph = nullptr;
    std::function<stosis::SFlowsheetFileSettings()> m_settingsProvider;

    QLabel* m_summary = nullptr;
    QTabWidget* m_tabs = nullptr;
    StructureDiagramWidget* m_diagram = nullptr;
    QTreeWidget* m_tree = nullptr;
    QPlainTextEdit* m_json = nullptr;
    QLabel* m_saveInfo = nullptr;
    QString m_lastJson; // nur neu aufbauen, wenn sich die Datei aendern wuerde
};
