#pragma once
#include <QGraphicsView>
#include "FlowsheetGraph.h"

class QGraphicsItem;
class QGraphicsRectItem;
class QGraphicsScene;

// Reiter "Bild" im Fenster "Struktur" (Methodenhandbuch Kap. 3): die
// Struktur als Zeichnung von links nach rechts in Stromrichtung, Aeste nach
// oben und unten. Kasten = Unit mit Name und darunter grau "node k" (k =
// Platz in der Rechenreihenfolge ab 0, bei jedem Zeichnen neu vergeben).
// Ein aufgeschnittener Rueckfuehrstrom endet in einem orangen "-> k" (Unit
// k liest dort den Strom des vorigen Zeitschritts), ein weiterer Zulauf zu
// einer Unit an anderer Stelle des Bildes in einem grauen "-> k".
// Mausrad zoomt, Ziehen verschiebt, Doppelklick passt das Bild ein.
class StructureDiagramWidget : public QGraphicsView
{
    Q_OBJECT

public:
    explicit StructureDiagramWidget(QWidget* parent = nullptr);

    // Zeichnet neu; eine Auswahl bleibt erhalten, wenn es den Eintrag noch gibt.
    void SetStructure(const stosis::CFlowsheetGraph& graph, const stosis::SFlowsheetStructure& structure);

signals:
    // Klick auf einen Kasten oder ein "-> k": Knoten-ID, Verbindungs-ID
    // (leer bei einer Wurzel), Art (SStructureBranch::EKind als int).
    void ItemClicked(const QString& nodeId, const QString& connectionId, int kind);

protected:
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    void Select(QGraphicsItem* item);
    QGraphicsItem* FindEntry(QGraphicsItem* item) const;

    QGraphicsScene* m_scene = nullptr;
    QGraphicsRectItem* m_highlight = nullptr; // Hinterlegung der Auswahl
    QString m_selectedKey;                    // Knoten|Verbindung|Art der Auswahl
};
