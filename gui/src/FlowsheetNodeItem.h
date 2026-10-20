#pragma once
#include <QGraphicsRectItem>
#include <QGraphicsEllipseItem>
#include <functional>
#include <string>
#include <vector>
#include "FlowsheetGraph.h"

// Ob ein Port ein Ausgang (Verbindung darf hier STARTEN) oder ein
// Eingang (Verbindung darf hier ENDEN) ist.
enum class EPortDirection
{
    Output,
    Input
};

// Kleiner Kreis an der Kante einer Knoten-Box, der als Start- oder
// Endpunkt einer Verbindung dient. Kind-Objekt der zugehoerigen
// FlowsheetNodeItem, dadurch wandert er automatisch mit, wenn die Box
// verschoben wird - keine manuelle Positionsberechnung noetig.
// portIndex = Nummer des Ein- bzw. Ausgangs (0-basiert, oben beginnend).
class FlowsheetPortItem : public QGraphicsEllipseItem
{
public:
    FlowsheetPortItem(const std::string& nodeId, EPortDirection direction, size_t portIndex,
        QPointF localCenter, QGraphicsItem* parent);

    const std::string& GetNodeId() const { return m_nodeId; }
    EPortDirection GetDirection() const { return m_direction; }
    size_t GetPortIndex() const { return m_portIndex; }

private:
    std::string m_nodeId;
    EPortDirection m_direction;
    size_t m_portIndex = 0;
};

// Die visuelle Box fuer einen einzelnen Flowsheet-Knoten. Verschiebbar
// per Maus, traegt so viele Ein-/Ausgangs-Ports, wie der Knoten im
// Graphen hat (links Eingaenge, rechts Ausgaenge, gleichmaessig verteilt).
class FlowsheetNodeItem : public QGraphicsRectItem
{
public:
    FlowsheetNodeItem(stosis::CFlowsheetGraph* graph, const std::string& nodeId,
        const std::string& name, stosis::ENodeType type);

    void SetOnMovedCallback(std::function<void()> callback) { m_onMoved = std::move(callback); }
    // Box ist kein QObject und kann daher keine Signale senden - deshalb
    // wie beim Verschieben ein Callback, den die Zeichenflaeche setzt.
    void SetOnDoubleClickedCallback(std::function<void(const std::string&)> callback)
    {
        m_onDoubleClicked = std::move(callback);
    }
    const std::string& GetNodeId() const { return m_nodeId; }
    // Beschriftung der Box aendern (Umbenennen).
    void SetName(const std::string& name);

    // nullptr, wenn es diesen Port nicht gibt.
    FlowsheetPortItem* GetInputPort(size_t index) const;
    FlowsheetPortItem* GetOutputPort(size_t index) const;

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;

private:
    // Legt count Ports entlang einer Kante an; bei mehr als einem Port
    // mit kleiner Nummer daneben, damit "Ausgang 2" in den Ergebnislisten
    // eindeutig einem Port zuzuordnen ist.
    void CreatePorts(size_t count, EPortDirection direction, qreal height,
        std::vector<FlowsheetPortItem*>& ports);

    stosis::CFlowsheetGraph* m_graph = nullptr;
    std::string m_nodeId;
    std::function<void()> m_onMoved;
    std::function<void(const std::string&)> m_onDoubleClicked;
    std::vector<FlowsheetPortItem*> m_inputPorts;
    std::vector<FlowsheetPortItem*> m_outputPorts;
    QGraphicsTextItem* m_label = nullptr;
};
