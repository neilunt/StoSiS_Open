#include "FlowsheetNodeItem.h"
#include "NodeTemplates.h"
#include <QGraphicsTextItem>
#include <QGraphicsSimpleTextItem>
#include <QBrush>
#include <QPen>
#include <QFont>
#include <algorithm>

namespace
{
    constexpr qreal kNodeWidth = 120.0;
    constexpr qreal kMinNodeHeight = 50.0;
    constexpr qreal kPortSpacing = 20.0; // Mindestabstand zwischen zwei Ports
    constexpr qreal kPortRadius = 6.0;
}

FlowsheetPortItem::FlowsheetPortItem(const std::string& nodeId, EPortDirection direction, size_t portIndex,
    QPointF localCenter, QGraphicsItem* parent)
    : QGraphicsEllipseItem(-kPortRadius, -kPortRadius, 2.0 * kPortRadius, 2.0 * kPortRadius, parent)
    , m_nodeId(nodeId)
    , m_direction(direction)
    , m_portIndex(portIndex)
{
    setPos(localCenter);
    const QColor fillColor = (direction == EPortDirection::Output) ? QColor(80, 160, 80) : QColor(80, 110, 180);
    setBrush(QBrush(fillColor));
    setPen(QPen(Qt::black));
    setZValue(1.0); // ueber der Box zeichnen, damit der Kreis immer klickbar bleibt
}

FlowsheetNodeItem::FlowsheetNodeItem(stosis::CFlowsheetGraph* graph, const std::string& nodeId,
    const std::string& name, stosis::ENodeType type)
    : m_graph(graph)
    , m_nodeId(nodeId)
{
    const stosis::SFlowsheetNode* node = graph ? graph->FindNode(nodeId) : nullptr;
    const size_t numInputs = node ? node->numInputs : 0;
    const size_t numOutputs = node ? node->numOutputs : 0;

    // Box waechst mit, wenn mehr Ports drauf muessen (Splitter/Merger).
    const size_t maxPorts = std::max(numInputs, numOutputs);
    const qreal height = std::max(kMinNodeHeight, kPortSpacing * static_cast<qreal>(maxPorts + 1));
    setRect(0, 0, kNodeWidth, height);

    setFlag(QGraphicsItem::ItemIsMovable);
    setFlag(QGraphicsItem::ItemIsSelectable);
    setFlag(QGraphicsItem::ItemSendsGeometryChanges);

    setBrush(QBrush(GetNodeColor(type)));
    setPen(QPen(Qt::black));

    m_label = new QGraphicsTextItem(QString::fromStdString(name), this);
    m_label->setPos(8, height / 2.0 - 10.0);
    m_label->setDefaultTextColor(Qt::white);

    // Eingaenge links, Ausgaenge rechts.
    CreatePorts(numInputs, EPortDirection::Input, height, m_inputPorts);
    CreatePorts(numOutputs, EPortDirection::Output, height, m_outputPorts);
}

void FlowsheetNodeItem::CreatePorts(size_t count, EPortDirection direction, qreal height,
    std::vector<FlowsheetPortItem*>& ports)
{
    const qreal x = (direction == EPortDirection::Output) ? kNodeWidth : 0.0;
    for (size_t i = 0; i < count; ++i)
    {
        const qreal y = height * static_cast<qreal>(i + 1) / static_cast<qreal>(count + 1);
        ports.push_back(new FlowsheetPortItem(m_nodeId, direction, i, QPointF(x, y), this));

        if (count > 1)
        {
            auto* number = new QGraphicsSimpleTextItem(QString::number(i + 1), this);
            QFont font = number->font();
            font.setPointSize(7);
            number->setFont(font);
            // Ausserhalb der Box, schraeg ueber dem Port - innen wuerde die
            // Nummer mit dem Namen der Box kollidieren.
            const qreal offset = (direction == EPortDirection::Output) ? kPortRadius - 1.0 : -kPortRadius - 5.0;
            number->setPos(x + offset, y - kPortRadius - 12.0);
        }
    }
}

void FlowsheetNodeItem::SetName(const std::string& name)
{
    m_label->setPlainText(QString::fromStdString(name));
}

FlowsheetPortItem* FlowsheetNodeItem::GetInputPort(size_t index) const
{
    return index < m_inputPorts.size() ? m_inputPorts[index] : nullptr;
}

FlowsheetPortItem* FlowsheetNodeItem::GetOutputPort(size_t index) const
{
    return index < m_outputPorts.size() ? m_outputPorts[index] : nullptr;
}

QVariant FlowsheetNodeItem::itemChange(GraphicsItemChange change, const QVariant& value)
{
    if (change == QGraphicsItem::ItemPositionHasChanged)
    {
        const QPointF newPos = value.toPointF();
        if (m_graph)
            m_graph->SetNodePosition(m_nodeId, newPos.x(), newPos.y());
        if (m_onMoved)
            m_onMoved();
    }
    return QGraphicsRectItem::itemChange(change, value);
}

void FlowsheetNodeItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event)
{
    if (m_onDoubleClicked)
        m_onDoubleClicked(m_nodeId);
    QGraphicsRectItem::mouseDoubleClickEvent(event);
}
