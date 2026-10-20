#pragma once
#include <QWidget>
#include <map>
#include <vector>
#include <string>
#include "FlowsheetGraph.h"

class QGraphicsScene;
class QGraphicsView;
class QGraphicsLineItem;
class QGraphicsItem;
class FlowsheetNodeItem;
class FlowsheetConnectionItem;
struct SNodeTemplate;

// Die Zeichenflaeche des Flowsheets mit der Block-Palette darueber.
//  - Bloecke anlegen: aus der Palette ziehen oder Rechtsklick auf die
//    freie Flaeche.
//  - Bloecke verschieben per Maus, die Kabel folgen (orthogonal geroutet).
//  - Verbinden per Klick-Klick: erster Klick auf einen freien Ausgangs-
//    Port startet ein Gummiband, zweiter Klick auf einen freien Eingangs-
//    Port verbindet (Esc bricht ab). Jeder Port traegt hoechstens eine
//    Verbindung.
//  - Rechtsklick auf einen Block: Einstellungen, Umbenennen, Loeschen.
//    Loeschen auch mit Entf fuer alle markierten Bloecke (Rueckfrage nur
//    bei Inlets, weil dort Zeitprofil und Verteilung verloren gehen).
//  - Rechtsklick auf ein Kabel: als Rueckfuehrung markieren, loeschen.
//  - Ansicht: Mausrad zoomt (um die Mausposition, 20 % bis 400 %), Ziehen
//    auf freier Flaeche oder mit der mittleren Maustaste verschiebt die
//    Ansicht. Solange weder gezoomt noch verschoben wurde, passt sich die
//    Ansicht an die Fenstergroesse an (verkleinert nur, wenn das Flowsheet
//    sonst nicht ganz zu sehen waere). Rechtsklick auf die freie Flaeche:
//    "Ganzes Flowsheet zeigen" und "Zoom 100 %".
class FlowsheetCanvasWidget : public QWidget
{
    Q_OBJECT

public:
    explicit FlowsheetCanvasWidget(QWidget* parent = nullptr);

    void SetGraph(stosis::CFlowsheetGraph* graph);

signals:
    // Bloecke hinzugefuegt, geloescht oder umbenannt - Listen neu laden.
    void NodesChanged();
    // Doppelklick auf eine Knoten-Box - oeffnet dessen Einstellungen.
    void NodeDoubleClicked(const QString& nodeId);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void RebuildScene();
    void UpdateConnectionLines();
    // Rueckfuehrungen (aufgeschnittene Verbindungen) gestrichelt zeichnen.
    void UpdateConnectionStyles();
    bool IsMarkedRecycle(const std::string& connectionId) const;
    void HandleClick(QPointF scenePos);
    void CancelConnecting();

    void ShowContextMenu(const QPoint& viewPos);
    void ShowConnectionMenu(const QPoint& viewPos, size_t lineIndex);
    void ShowNodeMenu(const QPoint& viewPos, FlowsheetNodeItem* nodeItem);
    void ShowAddNodeMenu(const QPoint& viewPos, QPointF scenePos);

    // Ansicht: Zoom um factor (Mausposition bleibt stehen), begrenzt auf
    // kMinZoom..kMaxZoom; ganzes Flowsheet zeigen (hoechstens 100 %).
    void ZoomBy(double factor);
    void FitAll();

    // Legt einen Block nach Vorlage an (Name automatisch: Inlet1, ...).
    // topLeft = linke obere Ecke der Box in Szenen-Koordinaten.
    void AddNodeFromTemplate(const SNodeTemplate& tmpl, QPointF topLeft, bool centerOnPoint);
    void AddConnectionItem(const std::string& connectionId, const std::string& sourceNodeId,
        size_t sourcePort, const std::string& targetNodeId, size_t targetPort);
    void RenameNode(const std::string& nodeId);
    // Loescht die Bloecke samt Kabeln (bei Inlets nach Rueckfrage).
    void DeleteNodes(const std::vector<std::string>& nodeIds);

    // Erzeugt eine Box fuer einen Knoten und haengt alle Callbacks an
    // (Verschieben, Doppelklick).
    FlowsheetNodeItem* CreateNodeItem(const std::string& nodeId, const std::string& name,
        stosis::ENodeType type, QPointF pos);
    // Box zu einem getroffenen Item (auch Port oder Beschriftung darin).
    static FlowsheetNodeItem* FindNodeItem(QGraphicsItem* item);

    stosis::CFlowsheetGraph* m_graph = nullptr;

    QGraphicsScene* m_scene = nullptr;
    QGraphicsView* m_view = nullptr;

    std::map<std::string, FlowsheetNodeItem*> m_nodeItems;
    struct SConnectionLineEntry
    {
        FlowsheetConnectionItem* line = nullptr;
        std::string connectionId;
        std::string sourceNodeId;
        size_t sourcePort = 0;
        std::string targetNodeId;
        size_t targetPort = 0;
    };
    std::vector<SConnectionLineEntry> m_connectionLines;

    // Zustand waehrend des Verbindungs-Ziehens.
    bool m_isConnecting = false;
    std::string m_connectionSourceNodeId;
    size_t m_connectionSourcePort = 0;
    QGraphicsLineItem* m_rubberBandLine = nullptr;

    // Ansicht verschieben (Ziehen auf freier Flaeche / mittlere Maustaste).
    bool m_isPanning = false;
    QPoint m_panLastPos;
    // Hat der Benutzer selbst gezoomt oder verschoben? Bis dahin passt sich
    // die Ansicht an die Fenstergroesse an.
    bool m_userNavigated = false;
    static constexpr double kMinZoom = 0.2;
    static constexpr double kMaxZoom = 4.0;
};
