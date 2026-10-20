#pragma once
#include <QGraphicsView>
#include <functional>

// Duenne Erweiterung von QGraphicsView. Wird nur fuer zwei Dinge
// gebraucht, die reines QGraphicsView nicht von sich aus nach aussen
// gibt:
//  1) Mausbewegung ohne gedrueckte Taste (fuers Gummiband beim Ziehen
//     einer Verbindung) - dafuer muss MouseTracking aktiviert sein.
//  2) Ein Klick, der KEIN Item trifft (bzw. keinen Port) - fuers
//     Verwerfen eines angefangenen Verbindungsversuchs, wenn der
//     zweite Klick daneben geht.
// Die eigentliche Verbindungs-Logik lebt weiterhin in
// FlowsheetCanvasWidget, diese Klasse reicht nur Events durch.

class FlowsheetGraphicsView : public QGraphicsView
{
public:
	explicit FlowsheetGraphicsView(QGraphicsScene* scene, QWidget* parent = nullptr);

	// Bei jeder Mausbewegung mit der aktuellen Szenen-Position aufgerufen.
	void SetOnMouseMoved(std::function<void(QPointF)> callback) { m_onMouseMoved = std::move(callback); }


	// Wenn Linksklick KEIN FlowsheetPortItem trifft.
	void SetOnEmptyClick(std::function<void(QPointF)> callback) { m_onEmptyClick = std::move(callback); }

protected:
	void mouseMoveEvent(QMouseEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;

private:
	std::function<void(QPointF)> m_onMouseMoved;
	std::function<void(QPointF)> m_onEmptyClick;
};