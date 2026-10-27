#include "FlowsheetGraphicsView.h"
#include "FlowsheetPortItem.h"
#include <QGraphicsScene>
#include <QMouseEvent>

FlowsheetGraphicsView::FlowsheetGraphicsView(QGraphicsScene* scene, QWidget* parent)
	:QGraphicsView(scene, parent)
{
	// Ohne die Folgende Zeile kommen mouseMoveEvent()s nur bei gedrueckter Maustaste an, 
	// das Gummiband braucht sie aber staendig
	setMouseTracking(true);
}

void FlowsheetGraphicsView::mouseMoveEvent(QMouseEvent* event)
{
	if (m_onMouseMoved)
		m_onMouseMoved(mapToScene(event->pos()));

	QGraphicsView::mouseMoveEvent(event);
}

void FlowsheetGraphicsView::mousePressEvent(QMouseEvent* event)
{
	const QPointF scenePos = mapToScene(event->pos());
	QGraphicsItem* item = scene() ? scene()->itemAt(scenePos, transform()) : nullptr;
	const bool hitPort = (dynamic_cast<FlowsheetPortItem*>(item) != nullptr);

	if (!hitPort && m_onEmptyClick)
		m_onEmptyClick(scenePos);

	// Normale Verarbeitung laeuft in jedem Fall weier, dadurch funktionieren Node
	// Verschiebungen, Selektion und der eigentliche Portklick (der ueber FlowsheetPortItem::mousePressEvent
	// reinkommt) unveraendert weiter.
	QGraphicsView::mousePressEvent(event);

}