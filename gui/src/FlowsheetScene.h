#pragma once
#include <QGraphicsScene>
#include <QPainter>
#include <QVector>
#include <cmath>

// Zeichenflaeche des Flowsheets: hellgrauer Hintergrund mit einem
// Punktraster zur Orientierung. Die Bloecke rasten bewusst NICHT ein -
// das Raster ist nur eine visuelle Hilfe.
class FlowsheetScene : public QGraphicsScene
{
public:
    using QGraphicsScene::QGraphicsScene;

protected:
    void drawBackground(QPainter* painter, const QRectF& rect) override
    {
        constexpr qreal kSpacing = 25.0;

        painter->fillRect(rect, QColor(236, 236, 236));

        QPen dotPen(QColor(180, 180, 180));
        dotPen.setWidthF(4.0);
        dotPen.setCapStyle(Qt::RoundCap); // runde statt eckige Punkte
        painter->setPen(dotPen);

        // Nur die Punkte im sichtbaren (neu zu zeichnenden) Bereich.
        const qreal left = std::floor(rect.left() / kSpacing) * kSpacing;
        const qreal top = std::floor(rect.top() / kSpacing) * kSpacing;
        QVector<QPointF> points;
        for (qreal x = left; x <= rect.right(); x += kSpacing)
            for (qreal y = top; y <= rect.bottom(); y += kSpacing)
                points.append(QPointF(x, y));
        painter->drawPoints(points.constData(), static_cast<int>(points.size()));
    }
};
