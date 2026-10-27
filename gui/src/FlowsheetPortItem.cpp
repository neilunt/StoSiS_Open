#include "FlowsheetPortItem.h"
#include <QGraphicsSceneMouseEvent>
#include <QCursor>
#include <QBrush>
#include <QPen>

namespace
{
	constexpr qreal kPortRadius = 5.0;
}

FlowsheetPortItem::FlowsheetPortItem(const std::string& nodeId, stosis::ENodeType nodeType, QGraphicsItem* parent)
	: QGraphicsEllipseItem(-kPortRadius, -kPortRadius, 2 * kPortRadius, 2 * kPortRadius, parent)
	, m_nodeId(nodeId)
	, m_nodeType(nodeType)
{
	setBrush(QBrush(Qt::darkGray));
	setPen(QPen(Qt::black));
	setZValue(10.0); // ueber dem Node Rechteck und dem label liegen, damit der Klick sicher hier ankommt.
	setAcceptedMouseButtons(Qt::LeftButton);
	setCursor(Qt::CrossCursor);
}

void FlowsheetPortItem::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
	// Event konsumieren, damit der Klick NICHT zusaetzlich an die darunterliegene Node weitergereicht wird
	// Die wuerde sonst dank ItemIsMovable anfangen sich verschieben zu wollen.
	event->accept();

	if (m_onClicked)
		m_onClicked(this);
}
