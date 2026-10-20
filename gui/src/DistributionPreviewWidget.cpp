#include "DistributionPreviewWidget.h"
#include <QPainter>
#include <QPolygonF>
#include <algorithm>
#include <cmath>
#include <vector>

DistributionPreviewWidget::DistributionPreviewWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(130);
}

void DistributionPreviewWidget::SetDistribution(const stosis::SSizeDistribution& distribution)
{
    m_distribution = distribution;
    update();
}

void DistributionPreviewWidget::SetGrid(const stosis::SSizeGrid& sizes)
{
    m_sizes = sizes;
    update();
}

void DistributionPreviewWidget::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.fillRect(rect(), Qt::white);

    const QRect plotRect(kMarginLeft, kMarginTop,
                         width() - kMarginLeft - kMarginRight,
                         height() - kMarginTop - kMarginBottom);
    painter.setPen(Qt::black);
    painter.drawRect(plotRect);

    if (!m_sizes.IsValid())
    {
        painter.drawText(plotRect, Qt::AlignCenter, "Ungueltiges Raster");
        return;
    }

    // --- Dichte an den Gitterpunkten ---------------------------------
    // Die Punkte liegen gleichmaessig ueber die Breite: beim logarithmischen
    // Raster ist die x-Achse damit logarithmisch.
    const size_t numClasses = m_sizes.numClasses;
    std::vector<double> values(numClasses);
    for (size_t s = 0; s < numClasses; ++s)
        values[s] = m_distribution.Density(m_sizes.GetSizeAt(s));
    const double maxVal = *std::max_element(values.begin(), values.end());

    if (maxVal > 0.0 && std::isfinite(maxVal))
    {
        QPolygonF curve;
        for (size_t s = 0; s < numClasses; ++s)
        {
            const double frac = static_cast<double>(s) / static_cast<double>(numClasses - 1);
            const double amplitude = values[s] / maxVal * (plotRect.height() - 4);
            curve << QPointF(plotRect.left() + frac * plotRect.width(), plotRect.bottom() - amplitude);
        }
        QPolygonF area = curve;
        area.prepend(QPointF(curve.first().x(), plotRect.bottom()));
        area.append(QPointF(curve.last().x(), plotRect.bottom()));

        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(33, 145, 140, 60));
        painter.drawPolygon(area);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(33, 145, 140), 1.5));
        painter.drawPolyline(curve);
        painter.restore();
    }
    else
    {
        painter.drawText(plotRect, Qt::AlignCenter, "Verteilung liegt ausserhalb des Rasters");
    }

    // --- x-Achse: d_min, Mitte, d_max in um --------------------------
    // Mitte: beim logarithmischen Raster das geometrische Mittel.
    painter.setPen(Qt::black);
    const double factor = stosis::kSizeDisplayFactor;
    const QString unit = QString::fromUtf8(stosis::kSizeDisplayUnit);
    const double mid = m_sizes.IsLogarithmic() ? std::sqrt(m_sizes.sizeMin * m_sizes.sizeMax)
                                               : 0.5 * (m_sizes.sizeMin + m_sizes.sizeMax);
    painter.drawText(QRect(plotRect.left(), plotRect.bottom() + 3, 100, 16),
                     Qt::AlignLeft, QString::number(m_sizes.sizeMin * factor, 'g', 4));
    painter.drawText(QRect(plotRect.center().x() - 80, plotRect.bottom() + 3, 160, 16),
                     Qt::AlignHCenter, QString::number(mid * factor, 'g', 4) + "  (d [" + unit + "]"
                     + (m_sizes.IsLogarithmic() ? ", log.)" : ")"));
    painter.drawText(QRect(plotRect.right() - 100, plotRect.bottom() + 3, 100, 16),
                     Qt::AlignRight, QString::number(m_sizes.sizeMax * factor, 'g', 4));

    // --- Erfasster Massenanteil --------------------------------------
    // Gruen bei mindestens 99 %, sonst rot - ab dann verfaelscht der
    // fehlende (bzw. zu viel gezaehlte) Anteil spuerbar die Massenbilanz.
    const double captured = m_distribution.CapturedFraction(m_sizes);
    const bool ok = std::abs(captured - 1.0) <= 0.01;
    painter.setPen(ok ? QColor(30, 130, 60) : QColor(200, 40, 40));
    painter.drawText(QRect(plotRect.left(), 2, plotRect.width(), kMarginTop - 2),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     QString("Im Raster erfasst: %1 %").arg(captured * 100.0, 0, 'f', 2)
                     + (ok ? "" : "  - Verteilung abgeschnitten oder Raster zu grob"));
}
