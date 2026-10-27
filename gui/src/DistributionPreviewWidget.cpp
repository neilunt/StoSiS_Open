#include "DistributionPreviewWidget.h"
#include "CardPlacement.h"
#include "InletUnit.h"
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

void DistributionPreviewWidget::SetCards(bool particles, const stosis::SCardSettings& settings)
{
    m_particles = particles;
    m_cardSettings = settings;
    setMinimumHeight(particles ? 170 : 130);
    update();
}

void DistributionPreviewWidget::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.fillRect(rect(), Qt::white);

    // Im Partikelmodus eine Zeile mehr oben (Karten je Bereich).
    const int marginTop = m_particles ? kMarginTop + 16 : kMarginTop;
    const QRect plotRect(kMarginLeft, marginTop,
                         width() - kMarginLeft - kMarginRight,
                         height() - marginTop - kMarginBottom);
    painter.setPen(Qt::black);
    painter.drawRect(plotRect);

    if (!m_sizes.IsValid())
    {
        painter.drawText(plotRect, Qt::AlignCenter, "Ungueltiges Raster");
        return;
    }
    if (m_distribution.type == stosis::EDistributionType::Table && !m_distribution.CheckTable().empty())
    {
        painter.drawText(plotRect, Qt::AlignCenter, "Tabelle noch unvollstaendig - siehe Hinweis darueber");
        return;
    }

    // --- Dichte an den Gitterpunkten ---------------------------------
    // Genau das, was das Inlet einspeist: Masse je Stuetzstelle exakt aus Q3,
    // geteilt durch das Trapezgewicht (SSizeDistribution::MassesAtNodes).
    // Die Punkte liegen gleichmaessig ueber die Breite: beim logarithmischen
    // Raster ist die x-Achse damit logarithmisch.
    const size_t numClasses = m_sizes.numClasses;
    const std::vector<double> masses = m_distribution.MassesAtNodes(m_sizes);
    std::vector<double> values(numClasses);
    for (size_t s = 0; s < numClasses; ++s)
        values[s] = masses[s] / m_sizes.GetWeight(s);
    const double maxVal = *std::max_element(values.begin(), values.end());

    // Partikelmodus: Karten zuerst, die Verteilungskurve liegt darueber; oben
    // bleibt eine Zeile frei fuer die Kartenzahl je Bereich.
    if (m_particles)
        PaintCards(painter, plotRect);
    const int headroom = m_particles ? 18 : 4;

    if (maxVal > 0.0 && std::isfinite(maxVal))
    {
        QPolygonF curve;
        for (size_t s = 0; s < numClasses; ++s)
        {
            const double frac = static_cast<double>(s) / static_cast<double>(numClasses - 1);
            const double amplitude = values[s] / maxVal * (plotRect.height() - headroom);
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
    // fehlende Anteil spuerbar die Massenbilanz. Orange, wenn die Masse
    // stimmt, aber nur wenige Stuetzstellen in den mittleren 90 % der Masse
    // liegen (die Form wird dann eckig).
    const double captured = m_distribution.CapturedFraction(m_sizes);
    const bool ok = std::abs(captured - 1.0) <= 0.01;
    const double central0 = m_distribution.Quantile(0.05);
    const double central1 = m_distribution.Quantile(0.95);
    size_t central = 0;
    for (size_t s = 0; s < numClasses; ++s)
        if (m_sizes.GetSizeAt(s) >= central0 && m_sizes.GetSizeAt(s) <= central1)
            ++central;
    const bool coarse = ok && central < kMinCentralNodes;
    painter.setPen(!ok ? QColor(200, 40, 40) : coarse ? QColor(163, 82, 0) : QColor(30, 130, 60));
    QString text = QString("Im Raster erfasst: %1 %").arg(captured * 100.0, 0, 'f', 2);
    if (!ok)
        text += "  - Teil der Verteilung liegt ausserhalb des Rasters";
    else if (coarse)
        text += QString("  - Raster grob: nur %1 Stuetzstellen in den mittleren 90 % der Masse").arg(central);
    painter.drawText(QRect(plotRect.left(), 2, plotRect.width(), kMarginTop - 2),
                     Qt::AlignLeft | Qt::AlignVCenter, text);
}

void DistributionPreviewWidget::PaintCards(QPainter& painter, const QRect& plotRect)
{
    // Karten genau wie am Inlet: Partikelzahl ab der kleinsten positiven
    // Stuetzstelle gezaehlt (CInletUnit::Initialize).
    const double countMin = (m_sizes.sizeMin > 0.0 || m_sizes.numClasses < 2) ? m_sizes.sizeMin : m_sizes.GetSizeAt(1);
    const stosis::SCardPlacement placement =
        stosis::PlaceCardsAtQuantiles(m_distribution, m_cardSettings, countMin, m_sizes.sizeMax);
    const QRect textRect(plotRect.left(), kMarginTop, plotRect.width(), 16);
    if (placement.sizes.empty())
    {
        painter.setPen(QColor(200, 40, 40));
        painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, "Partikelmodus: keine Karten - Verteilung ungueltig");
        return;
    }

    const bool log = m_sizes.IsLogarithmic();
    const auto X = [&](double size)
    {
        const double f = log ? std::log(size / m_sizes.sizeMin) / std::log(m_sizes.sizeMax / m_sizes.sizeMin)
                             : (size - m_sizes.sizeMin) / (m_sizes.sizeMax - m_sizes.sizeMin);
        return plotRect.left() + f * plotRect.width();
    };
    const QColor cardColor(83, 74, 183);
    const QColor boundColor(133, 79, 11);

    painter.save();
    painter.setClipRect(plotRect);

    // Grenzen der Bereiche mit Karten (gestrichelt) und Kartenzahl je Bereich oben.
    painter.setPen(QPen(boundColor, 1.0, Qt::DashLine));
    for (const stosis::SCardRange& range : placement.ranges)
    {
        if (range.numCards == 0)
            continue;
        for (double bound : { range.lower, range.upper })
            painter.drawLine(QPointF(X(bound), plotRect.top()), QPointF(X(bound), plotRect.bottom()));
    }
    QFont bold = painter.font();
    bold.setBold(true);
    painter.setFont(bold);
    painter.setPen(cardColor);
    for (const stosis::SCardRange& range : placement.ranges)
    {
        if (range.numCards == 0)
            continue;
        const double left = std::max<double>(X(range.lower), plotRect.left());
        const double right = std::min<double>(X(range.upper), plotRect.right());
        if (right - left >= 22)
            painter.drawText(QRectF(left, plotRect.top() + 1, right - left, 14), Qt::AlignHCenter | Qt::AlignTop,
                             QString::number(range.numCards));
    }
    painter.setFont(font());

    // Jede Karte als Stab an ihrer Groesse; Hoehe = Masse je Karte. Je
    // Bildpunktspalte nur ein Stab (der hoechste): Bei vielen Karten addiert
    // sich so die Deckkraft nicht auf, und die Kurve darueber bleibt lesbar.
    const double maxFraction = *std::max_element(placement.massFractions.begin(), placement.massFractions.end());
    std::vector<double> columns(static_cast<size_t>(plotRect.width()) + 1, 0.0);
    for (size_t k = 0; k < placement.sizes.size(); ++k)
    {
        const long column = std::lround(X(placement.sizes[k])) - plotRect.left();
        if (column < 0 || column >= static_cast<long>(columns.size()))
            continue;
        const double h = (maxFraction > 0.0 ? placement.massFractions[k] / maxFraction : 0.0) * 0.45 * plotRect.height();
        columns[static_cast<size_t>(column)] = std::max(columns[static_cast<size_t>(column)], h);
    }
    QColor stem = cardColor;
    stem.setAlpha(110);
    painter.setPen(QPen(stem, 1.0));
    for (size_t column = 0; column < columns.size(); ++column)
        if (columns[column] > 0.0)
        {
            const double x = plotRect.left() + static_cast<double>(column);
            painter.drawLine(QPointF(x, plotRect.bottom()), QPointF(x, plotRect.bottom() - columns[column]));
        }
    painter.restore();

    // Zusammenfassung ueber dem Diagramm; orange, wenn die Mindestzahl unter
    // der Warnschwelle des Inlets liegt.
    const bool few = placement.minPerRange < stosis::CInletUnit::kMinCardsPerRange;
    painter.setPen(few ? QColor(163, 82, 0) : cardColor);
    QString text = QString("Partikelmodus: %1 Karten je Zeitschritt in %2 Bereichen (1 %3 * 2^k), Mindestzahl %4")
        .arg(placement.sizes.size()).arg(placement.numOccupied).arg(QString::fromUtf8(stosis::kSizeDisplayUnit))
        .arg(placement.minPerRange);
    if (few)
        text += " - zu wenig je Bereich";
    painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, text);
}
