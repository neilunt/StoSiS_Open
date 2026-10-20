#include "FlowsheetConnectionItem.h"
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QPen>
#include <QLineF>
#include <vector>
#include <algorithm>
#include <cmath>

namespace
{
    constexpr qreal kStub = 20.0;         // gerades Stueck direkt am Port
    constexpr qreal kDetourMargin = 25.0; // Abstand der Umleitung zu den Bloecken
    constexpr qreal kCornerRadius = 10.0;
    constexpr qreal kClickWidth = 10.0;   // Breite der anklickbaren Flaeche
    constexpr qreal kLaneOffset = 8.0;    // Versatz je Port-Nummer
}

FlowsheetConnectionItem::FlowsheetConnectionItem()
{
    setPen(QPen(Qt::darkGray, 4));
    // Hinter den Bloecken zeichnen: laeuft ein Kabel durch einen Block,
    // verschwindet es dort, statt die Beschriftung zu ueberdecken.
    setZValue(-1.0);
}

void FlowsheetConnectionItem::UpdateRoute(QPointF start, QPointF end,
    const QRectF& sourceBox, const QRectF& targetBox, size_t sourcePort, size_t targetPort)
{
    std::vector<QPointF> points;
    // "Fahrspuren": je Port ein paar Pixel Versatz, damit z.B. zwei
    // Rueckfuehrungen durch dieselbe Luecke nicht uebereinanderliegen.
    const qreal sourceLane = kLaneOffset * static_cast<qreal>(sourcePort);
    const qreal targetLane = kLaneOffset * static_cast<qreal>(targetPort);

    if (end.x() - start.x() >= 2.0 * kStub + sourceLane)
    {
        // Vorwaerts: Knick auf halber Strecke.
        const qreal midX = 0.5 * (start.x() + end.x()) + sourceLane;
        points = { start, QPointF(midX, start.y()), QPointF(midX, end.y()), end };
    }
    else
    {
        // Rueckwaerts: Umweg auf der Hoehe detourY.
        qreal detourY = 0.0;
        if (sourceBox.bottom() + 2.0 * kDetourMargin <= targetBox.top())
            detourY = 0.5 * (sourceBox.bottom() + targetBox.top()); // Luecke: Ziel liegt darunter
        else if (targetBox.bottom() + 2.0 * kDetourMargin <= sourceBox.top())
            detourY = 0.5 * (targetBox.bottom() + sourceBox.top()); // Luecke: Ziel liegt darueber
        else
        {
            // Bloecke ueberlappen in der Hoehe: oben oder unten herum,
            // je nachdem, was kuerzer ist.
            const qreal above = std::min(sourceBox.top(), targetBox.top()) - kDetourMargin;
            const qreal below = std::max(sourceBox.bottom(), targetBox.bottom()) + kDetourMargin;
            const qreal costAbove = std::abs(start.y() - above) + std::abs(end.y() - above);
            const qreal costBelow = std::abs(start.y() - below) + std::abs(end.y() - below);
            detourY = (costAbove <= costBelow) ? above : below;
        }
        detourY += targetLane - 0.5 * kLaneOffset;
        const qreal outX = start.x() + kStub + sourceLane;
        const qreal inX = end.x() - kStub - targetLane;
        points = { start, QPointF(outX, start.y()), QPointF(outX, detourY),
                   QPointF(inX, detourY), QPointF(inX, end.y()), end };
    }

    // Doppelte Punkte (Segmente der Laenge 0) entfernen.
    std::vector<QPointF> cleaned;
    for (const QPointF& p : points)
        if (cleaned.empty() || QLineF(cleaned.back(), p).length() > 0.5)
            cleaned.push_back(p);

    // Polylinie mit abgerundeten Ecken: vor jeder Ecke um r kuerzen und
    // mit einer Bezier-Kurve (Kontrollpunkt = Ecke) in die neue Richtung.
    QPainterPath path(cleaned.front());
    for (size_t i = 1; i + 1 < cleaned.size(); ++i)
    {
        const QPointF a = cleaned[i - 1];
        const QPointF p = cleaned[i];
        const QPointF b = cleaned[i + 1];
        const qreal lenIn = QLineF(a, p).length();
        const qreal lenOut = QLineF(p, b).length();
        const qreal r = std::min({ kCornerRadius, 0.5 * lenIn, 0.5 * lenOut });
        const QPointF dirIn = (p - a) / lenIn;
        const QPointF dirOut = (b - p) / lenOut;
        path.lineTo(p - dirIn * r);
        path.quadTo(p, p + dirOut * r);
    }
    if (cleaned.size() > 1)
        path.lineTo(cleaned.back());

    setPath(path);
}

QPainterPath FlowsheetConnectionItem::shape() const
{
    QPainterPathStroker stroker;
    stroker.setWidth(std::max(kClickWidth, pen().widthF()));
    return stroker.createStroke(path());
}
