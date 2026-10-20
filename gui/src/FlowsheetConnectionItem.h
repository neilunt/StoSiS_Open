#pragma once
#include <QGraphicsPathItem>
#include <QRectF>
#include <QPointF>

// Ein Verbindungskabel im Flowsheet. Laeuft nur waagerecht und senkrecht
// (orthogonales Routing) mit abgerundeten Ecken:
//  - Ziel rechts vom Start: raus nach rechts, auf halber Strecke hoch
//    bzw. runter, rein von links (3 Segmente).
//  - Ziel links vom Start (typisch Rueckfuehrung): raus nach rechts,
//    zwischen/ueber/unter den Bloecken zurueck, rein von links (5 Segmente).
// Kabel duerfen (noch) durch andere Bloecke hindurchlaufen.
class FlowsheetConnectionItem : public QGraphicsPathItem
{
public:
    FlowsheetConnectionItem();

    // start = Ausgangs-Port (rechte Kante von sourceBox), end = Eingangs-
    // Port (linke Kante von targetBox), alles in Szenen-Koordinaten.
    // Die Port-Nummern versetzen die Knicke um ein paar Pixel, damit
    // Kabel von/zu verschiedenen Ports nicht deckungsgleich uebereinander
    // laufen.
    void UpdateRoute(QPointF start, QPointF end, const QRectF& sourceBox, const QRectF& targetBox,
        size_t sourcePort, size_t targetPort);

    // Nur der Strich selbst ist anklickbar - die Standard-Form eines
    // QGraphicsPathItem wuerde auch die "Innenflaeche" eines L-foermigen
    // Kabels mitzaehlen, ein Rechtsklick daneben traefe dann das Kabel.
    QPainterPath shape() const override;
};
