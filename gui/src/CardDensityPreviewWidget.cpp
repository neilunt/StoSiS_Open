#include "CardDensityPreviewWidget.h"
#include "InletUnit.h"
#include <QLocale>
#include <QPainter>
#include <QPolygonF>
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace
{
    const QColor kMassColor(33, 145, 140);  // wie die Verteilung im Inlet-Editor
    const QColor kNumberColor(230, 120, 20);
    const QColor kCardColor(83, 74, 183);   // wie die Karten im Inlet-Editor
    const QColor kGridLimitColor(200, 40, 40);
    const QColor kBelowGridShade(200, 40, 40, 18);
    const QColor kGridColor(220, 220, 220);
    constexpr int kMarginLeft = 58;
    constexpr int kMarginRight = 16;

    // Groesse in um, ohne Tausendertrennzeichen (1000 statt 1.000).
    QString Size(double meters)
    {
        QLocale locale;
        locale.setNumberOptions(QLocale::OmitGroupSeparator);
        return locale.toString(meters * stosis::kSizeDisplayFactor, 'g', 3);
    }

    // Achsenbeschriftung: runde Werte ganz ausgeschrieben (1000 statt 1e+03).
    QString TickLabel(double meters)
    {
        QLocale locale;
        locale.setNumberOptions(QLocale::OmitGroupSeparator);
        return locale.toString(meters * stosis::kSizeDisplayFactor, 'g', 7);
    }

    QString Ratio(double value)
    {
        return QLocale().toString(value, 'f', value < 10.0 ? 2 : 1);
    }

    // Anteil (0 bis 1) in Prozent: unter 1 % zwei, unter 10 % eine Nachkommastelle.
    QString Percent(double share)
    {
        const double percent = 100.0 * share;
        return QLocale().toString(percent, 'f', percent < 1.0 ? 2 : (percent < 10.0 ? 1 : 0));
    }

    // Feste Randanteile ohne angehaengte Nullen (5 statt 5.0).
    QString EdgePercent(double share)
    {
        return QLocale().toString(100.0 * share, 'g', 3);
    }

    // Eintrag der Legende: Linie, gestrichelte Linie, Kartenstriche oder Flaeche.
    struct SLegendItem
    {
        enum class EMark
        {
            Line,
            Dashed,
            Strokes,
            Area
        };
        QColor color;
        QString text;
        EMark mark;
    };
}

CardDensityPreviewWidget::CardDensityPreviewWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(400);
}

void CardDensityPreviewWidget::SetData(const stosis::SSizeDistribution& distribution, const stosis::SCardSettings& settings,
                                       double countMin)
{
    m_placement = stosis::PlaceCardsAtQuantiles(distribution, settings, countMin);
    m_countMin = countMin;
    update();
}

QString CardDensityPreviewWidget::SummaryHtml() const
{
    const stosis::SCardPlacement& placement = m_placement;
    if (placement.sizes.empty())
        return QString();
    const double numCards = static_cast<double>(placement.sizes.size());
    const QString unit = QString::fromUtf8(stosis::kSizeDisplayUnit);
    QString text = QString("<b>Raender:</b> Die feinsten %1 % der Partikel (unter %2 %3) bekommen %4 % der Karten, "
                           "die groebsten %5 % der Masse (ueber %6 %3) %7 % der Karten.")
                       .arg(EdgePercent(stosis::kFineEdgeNumberShare), Size(placement.fineSize), unit,
                            Percent(static_cast<double>(placement.fineCards) / numCards),
                            EdgePercent(stosis::kCoarseEdgeMassShare), Size(placement.coarseSize),
                            Percent(static_cast<double>(placement.coarseCards) / numCards));
    const size_t warnBelow = stosis::CInletUnit::kMinCards;
    const bool fewFine = placement.fineCards < warnBelow, fewCoarse = placement.coarseCards < warnBelow;
    if (fewFine || fewCoarse)
        text += QString(" <span style=\"color:#a35200;\">%1 weniger als %2 Karten je Zeitschritt - dort unsicher.</span>")
                    .arg(QString(fewFine && fewCoarse ? "An beiden Raendern" : (fewFine ? "Am feinen Rand" : "Am groben Rand")))
                    .arg(warnBelow);
    text += QString("<br><b>Je Karte:</b> hoechstens das %1-fache der mittleren Masse und das %2-fache der mittleren "
                    "Partikelzahl.")
                .arg(Ratio(placement.maxMassRatio), Ratio(placement.maxNumberRatio));
    const auto below = std::count_if(placement.sizes.begin(), placement.sizes.end(), [this](double d) { return d < m_countMin; });
    if (below > 0)
        text += QString("<br><b>Unter d_min (%1 %2):</b> %3 % der Karten; ihre Partikel zaehlen wie bei d_min.")
                    .arg(Size(m_countMin), unit, Percent(static_cast<double>(below) / numCards));
    return text;
}

void CardDensityPreviewWidget::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.fillRect(rect(), Qt::white);
    const stosis::SCardPlacement& placement = m_placement;
    if (placement.sizes.empty())
    {
        p.setPen(QColor(200, 40, 40));
        p.drawText(rect(), Qt::AlignCenter, "Keine Karten - Verteilung ungueltig");
        return;
    }
    const stosis::SCardDensityCurve& curve = placement.curve;
    const size_t numCards = placement.sizes.size();

    // x-Bereich: alle Karten und alles, wo eine Kurve ueber 0,1 % ihres
    // Hoechstwerts liegt.
    double maxMass = 0.0, maxNumber = 0.0, maxCards = 0.0;
    for (size_t k = 0; k < curve.sizes.size(); ++k)
    {
        maxMass = std::max(maxMass, curve.mass[k]);
        maxNumber = std::max(maxNumber, curve.number[k]);
        maxCards = std::max(maxCards, curve.cards[k]);
    }
    double dLo = placement.sizes.front(), dHi = placement.sizes.back();
    for (size_t k = 0; k < curve.sizes.size(); ++k)
        if (curve.mass[k] > 1e-3 * maxMass || curve.number[k] > 1e-3 * maxNumber || curve.cards[k] > 1e-3 * maxCards)
        {
            dLo = std::min(dLo, curve.sizes[k]);
            dHi = std::max(dHi, curve.sizes[k]);
        }
    if (!(dLo > 0.0) || !(dHi > dLo))
    {
        dLo = placement.sizes.front() * 0.5;
        dHi = placement.sizes.back() * 2.0;
    }
    const double pad = std::pow(dHi / dLo, 0.03);
    dLo /= pad;
    dHi *= pad;
    const double lnLo = std::log(dLo), lnSpan = std::log(dHi / dLo);
    const bool belowGrid = m_countMin > dLo && m_countMin < dHi;

    // Legende mit Zeilenumbruch: erst messen (draw = false), dann zeichnen.
    std::vector<SLegendItem> legend = {
        { kMassColor, "Masse q3*", SLegendItem::EMark::Line },
        { kNumberColor, "Anzahl q0*", SLegendItem::EMark::Line },
        { kCardColor, "Kartendichte", SLegendItem::EMark::Line },
        { kCardColor, "je Karte ein Strich", SLegendItem::EMark::Strokes } };
    if (belowGrid)
    {
        legend.push_back({ kNumberColor, "Anzahl mit echter Groesse", SLegendItem::EMark::Dashed });
        legend.push_back({ kBelowGridShade, "unter d_min: zaehlt wie bei d_min", SLegendItem::EMark::Area });
    }
    const auto layoutLegend = [&](bool draw)
    {
        const int rowHeight = 18, right = width() - kMarginRight;
        int x = kMarginLeft, y = 10;
        for (const SLegendItem& item : legend)
        {
            const int textWidth = p.fontMetrics().horizontalAdvance(item.text);
            const int itemWidth = 22 + textWidth;
            if (x > kMarginLeft && x + itemWidth > right)
            {
                x = kMarginLeft;
                y += rowHeight;
            }
            if (draw)
            {
                switch (item.mark)
                {
                case SLegendItem::EMark::Strokes:
                    p.setPen(QPen(item.color, 1.0));
                    for (int s = 0; s < 3; ++s)
                        p.drawLine(QPointF(x + 4 + 5 * s, y - 7), QPointF(x + 4 + 5 * s, y + 7));
                    break;
                case SLegendItem::EMark::Area:
                    p.fillRect(QRectF(x, y - 7, 18, 14), item.color);
                    break;
                default:
                    p.setPen(QPen(item.color, 2.0, item.mark == SLegendItem::EMark::Dashed ? Qt::DashLine : Qt::SolidLine));
                    p.drawLine(QPointF(x, y), QPointF(x + 18, y));
                    break;
                }
                p.setPen(Qt::black);
                p.drawText(QRect(x + 22, y - 9, textWidth + 4, 18), Qt::AlignLeft | Qt::AlignVCenter, item.text);
            }
            x += itemWidth + 16;
        }
        return y + 12;
    };
    const int legendHeight = layoutLegend(false);

    // Aufteilung: Legende, oberes Diagramm, Kartenteppich, unteres Diagramm, Achse.
    const int plotWidth = std::max(60, width() - kMarginLeft - kMarginRight);
    const int rugHeight = 18, gap = 10, axisHeight = 42;
    const int available = std::max(140, height() - legendHeight - rugHeight - gap - axisHeight);
    const QRect plotA(kMarginLeft, legendHeight, plotWidth, static_cast<int>(available * 0.6));
    const QRect rug(kMarginLeft, plotA.bottom() + 3, plotWidth, rugHeight);
    const QRect plotB(kMarginLeft, rug.bottom() + gap, plotWidth, available - plotA.height());
    const auto X = [&](double d) { return plotA.left() + (std::log(d) - lnLo) / lnSpan * plotWidth; };
    layoutLegend(true);

    // --- Oberes Diagramm: Dichten je ln d -------------------------------------
    double yMax = 0.0;
    for (size_t k = 0; k < curve.sizes.size(); ++k)
        if (curve.sizes[k] >= dLo && curve.sizes[k] <= dHi)
            yMax = std::max({ yMax, curve.mass[k], curve.number[k], curve.cards[k] });
    yMax = yMax > 0.0 ? 1.08 * yMax : 1.0;
    // Werte weit ueber dem Rahmen (Anzahl mit echter Groesse) nur bis knapp darueber.
    const auto YA = [&](double v) { return plotA.bottom() - std::min(v, 3.0 * yMax) / yMax * plotA.height(); };
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::white);
    p.drawRect(plotA);
    p.save();
    p.setClipRect(plotA);
    // Unter d_min loest das Raster nichts auf: Partikel zaehlen dort wie bei d_min.
    if (belowGrid)
        p.fillRect(QRectF(plotA.left(), plotA.top(), X(m_countMin) - plotA.left(), plotA.height()), kBelowGridShade);
    p.setRenderHint(QPainter::Antialiasing, true);
    const auto drawCurve = [&](const std::vector<double>& values, const QColor& color, bool fill, double sizeLimit)
    {
        QPolygonF line;
        for (size_t k = 0; k < curve.sizes.size(); ++k)
            if (curve.sizes[k] >= dLo && curve.sizes[k] <= std::min(dHi, sizeLimit))
                line << QPointF(X(curve.sizes[k]), YA(values[k]));
        if (line.size() < 2)
            return;
        if (fill)
        {
            QPolygonF area = line;
            area.prepend(QPointF(line.first().x(), plotA.bottom()));
            area.append(QPointF(line.last().x(), plotA.bottom()));
            QColor shade = color;
            shade.setAlpha(45);
            p.setPen(Qt::NoPen);
            p.setBrush(shade);
            p.drawPolygon(area);
            p.setBrush(Qt::NoBrush);
        }
        p.setPen(QPen(color, fill ? 2.2 : 1.6));
        p.drawPolyline(line);
    };
    drawCurve(curve.cards, kCardColor, true, dHi);
    drawCurve(curve.mass, kMassColor, false, dHi);
    drawCurve(curve.number, kNumberColor, false, dHi);
    if (belowGrid)
    {
        // Anzahl mit echter Groesse: unter d_min, bis zur ersten Zelle darueber.
        QPolygonF line;
        for (size_t k = 0; k < curve.sizes.size(); ++k)
            if (curve.sizes[k] >= dLo)
            {
                line << QPointF(X(curve.sizes[k]), YA(curve.numberTrue[k]));
                if (curve.sizes[k] > m_countMin)
                    break;
            }
        p.setPen(QPen(kNumberColor, 1.6, Qt::DashLine));
        p.drawPolyline(line);
    }
    // Raender: feinste 5 % der Partikel, groebste 5 % der Masse.
    struct SEdge
    {
        double size;
        QString text;
        bool left; // Beschriftung links der Linie
    };
    p.setPen(QPen(QColor(120, 120, 120), 1.0, Qt::DashLine));
    for (const SEdge& edge : { SEdge{ placement.fineSize, EdgePercent(stosis::kFineEdgeNumberShare) + " % der Partikel", true },
                               SEdge{ placement.coarseSize, EdgePercent(stosis::kCoarseEdgeMassShare) + " % der Masse", false } })
    {
        if (!(edge.size > dLo && edge.size < dHi))
            continue;
        const double ex = X(edge.size);
        p.drawLine(QPointF(ex, plotA.top()), QPointF(ex, plotA.bottom()));
        p.drawText(QRectF(edge.left ? ex - 124 : ex + 4, plotA.top() + 2, 120, 16),
                   (edge.left ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter, edge.text);
    }
    if (belowGrid)
    {
        const double gx = X(m_countMin);
        p.setPen(QPen(kGridLimitColor, 1.0, Qt::DotLine));
        p.drawLine(QPointF(gx, plotA.top()), QPointF(gx, plotA.bottom()));
        p.drawText(QRectF(gx + 4, plotA.bottom() - 18, 140, 16), Qt::AlignLeft | Qt::AlignVCenter, "d_min des Rasters");
    }
    p.restore();
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(Qt::black);
    p.setBrush(Qt::NoBrush);
    p.drawRect(plotA);
    p.save();
    p.translate(14, plotA.center().y());
    p.rotate(-90);
    p.drawText(QRect(-plotA.height() / 2, -8, plotA.height(), 16), Qt::AlignCenter, "Dichte je ln d");
    p.restore();

    // --- Kartenteppich ----------------------------------------------------------
    QColor stroke = kCardColor;
    stroke.setAlpha(numCards > 3000 ? 60 : 130);
    p.setPen(QPen(stroke, 1.0));
    for (double d : placement.sizes)
    {
        const double cx = X(d);
        p.drawLine(QPointF(cx, rug.top()), QPointF(cx, rug.bottom()));
    }
    p.setPen(Qt::darkGray);
    p.drawText(QRect(0, rug.top(), kMarginLeft - 6, rug.height()), Qt::AlignRight | Qt::AlignVCenter, "Karten");

    // --- Unteres Diagramm: Masse und Partikel je Karte relativ zum Mittel -----
    const double logLo = -2.0, logHi = 2.0;
    const auto YB = [&](double v)
    {
        const double l = std::clamp(std::log10(std::max(v, 1e-30)), logLo, logHi);
        return plotB.bottom() - (l - logLo) / (logHi - logLo) * plotB.height();
    };
    for (int e = -2; e <= 2; ++e)
    {
        const double y = YB(std::pow(10.0, e));
        p.setPen(QPen(e == 0 ? QColor(150, 150, 150) : kGridColor, 1.0, e == 0 ? Qt::DashLine : Qt::SolidLine));
        p.drawLine(QPointF(plotB.left(), y), QPointF(plotB.right(), y));
        p.setPen(Qt::black);
        p.drawText(QRect(0, static_cast<int>(y) - 8, kMarginLeft - 6, 16), Qt::AlignRight | Qt::AlignVCenter,
                   e == 0 ? QString("1 = Mittel") : QLocale().toString(std::pow(10.0, e), 'g', 3));
    }
    std::vector<double> numbers(numCards);
    double numberSum = 0.0;
    for (size_t i = 0; i < numCards; ++i)
    {
        numbers[i] = stosis::CardNumberWeight(placement.massFractions[i], placement.sizes[i], m_countMin);
        numberSum += numbers[i];
    }
    const double numberMean = numberSum / static_cast<double>(numCards);
    p.save();
    p.setClipRect(plotB.adjusted(0, -2, 0, 2));
    for (size_t i = 0; i < numCards; ++i)
    {
        const double cx = X(placement.sizes[i]);
        p.fillRect(QRectF(cx - 1.0, YB(placement.massFractions[i] * static_cast<double>(numCards)) - 1.0, 2.0, 2.0), kMassColor);
        if (numberMean > 0.0)
            p.fillRect(QRectF(cx - 1.0, YB(numbers[i] / numberMean) - 1.0, 2.0, 2.0), kNumberColor);
    }
    p.restore();
    p.setPen(Qt::black);
    p.drawRect(plotB);
    // Titel in den Farben der Punkte.
    int tx = plotB.left() + 6;
    for (const auto& [text, color] : { std::pair<QString, QColor>{ "Masse je Karte", kMassColor },
                                       std::pair<QString, QColor>{ " und ", Qt::black },
                                       std::pair<QString, QColor>{ "Partikel je Karte", kNumberColor },
                                       std::pair<QString, QColor>{ ", relativ zum Mittel", Qt::black } })
    {
        p.setPen(color);
        const int textWidth = p.fontMetrics().horizontalAdvance(text);
        p.drawText(QRect(tx, plotB.top() + 2, textWidth + 2, 16), Qt::AlignLeft | Qt::AlignVCenter, text);
        tx += textWidth;
    }

    // --- x-Achse ------------------------------------------------------------------
    std::vector<double> ticks;
    const int e0 = static_cast<int>(std::floor(std::log10(dLo))), e1 = static_cast<int>(std::ceil(std::log10(dHi)));
    const bool fine = e1 - e0 <= 3;
    for (int e = e0; e <= e1; ++e)
        for (double m : fine ? std::vector<double>{ 1.0, 2.0, 5.0 } : std::vector<double>{ 1.0 })
        {
            const double t = m * std::pow(10.0, e);
            if (t >= dLo && t <= dHi)
                ticks.push_back(t);
        }
    p.setPen(Qt::black);
    for (double t : ticks)
    {
        const double tickX = X(t);
        p.drawLine(QPointF(tickX, plotB.bottom()), QPointF(tickX, plotB.bottom() + 4));
        p.drawText(QRectF(tickX - 30, plotB.bottom() + 5, 60, 16), Qt::AlignHCenter, TickLabel(t));
    }
    p.drawText(QRect(plotB.left(), plotB.bottom() + 21, plotB.width(), 18), Qt::AlignHCenter,
               "Groesse d [" + QString::fromUtf8(stosis::kSizeDisplayUnit) + "] (logarithmisch)");
}
