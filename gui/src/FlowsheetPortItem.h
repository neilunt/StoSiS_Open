#pragma once
#include <QGraphicsEllipseItem>
#include <functional>
#include <string>
#include "FlowsheetGraph.h"

// Ein einzelner Anschlusspunkt an einem Flowsheet-Knoten. Wird als
// Kind-Item von FlowsheetNodeItem angelegt, damit es beim Verschieben
// des Knotens automatisch mitwandert (Qt uebernimmt das ueber die
// Parent-Child-Beziehung im Graphics-View-Framework, keine manuelle
// Neupositionierung noetig).
//
// Uebernimmt selbst die Klick-Erkennung: Qt liefert mousePressEvent()
// hier nur dann, wenn der Klick tatsaechlich auf diesem Port landet -
// das "steht die Maus gerade ueber dem passenden Gegenport"-Pruefen
// aus der Aufgabenstellung passiert also quasi automatisch durch das
// Graphics-View-Framework selbst.
class FlowsheetPortItem : public QGraphicsEllipseItem
{
public:
	FlowsheetPortItem(const std::string& nodeId, stosis::ENodeType nodeType, QGraphicsItem* parent = nullptr);

	const std::string& GetNodeId() const { return m_nodeId; }
	stosis::ENodeType GetNodeType() const { return m_nodeType; }

	//Wird bei jedem Linksklick auf diesen Port aufgerufen (this als Parameter).
	void SetOnClickedCallback(std::function<void(FlowsheetPortItem*)> callback) { m_onClicked = std::move(callback); }

protected:
	void mousePressEvent(QGraphicsSceneMouseEvent* event) override;

private:
	std::string m_nodeId;
	stosis::ENodeType m_nodeType;
	std::function<void(FlowsheetPortItem*)> m_onClicked;

 };