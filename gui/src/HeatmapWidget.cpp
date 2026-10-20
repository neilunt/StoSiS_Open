#include "HeatmapWidget.h"
#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QToolTip>
#include <algorithm>
#include <cmath>
#include <iterator>

HeatmapWidget::HeatmapWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(400, 300);
    // Ohne MouseTracking kaemen mouseMoveEvents nur bei gedrueckter
    // Maustaste - fuer die Hover-Anzeige brauchen wir sie immer.
    setMouseTracking(true);
}

void HeatmapWidget::SetResults(const stosis::CSimulationResults* results)
{
    m_results = results;
    update(); // Qt-Aufruf: paintEvent() wird neu getriggert
}

void HeatmapWidget::SetLogScale(bool enabled)
{
    m_logScale = enabled;
    update();
}

void HeatmapWidget::SetLogDecades(int decades)
{
    m_logDecades = std::max(1, decades);
    update();
}

void HeatmapWidget::SetTimeOnXAxis(bool enabled)
{
    m_timeOnXAxis = enabled;
    update();
}

void HeatmapWidget::SetShowMarginals(bool enabled)
{
    m_showMarginals = enabled;
    update();
}

void HeatmapWidget::SetSizeUnit(double factor, const QString& unit)
{
    m_sizeFactor = factor > 0.0 ? factor : 1.0;
    m_sizeUnit = unit;
    update();
}

// Zeitachse: sechs gleichmaessig verteilte Marken von 0 bis Ende.
std::vector<HeatmapWidget::SAxisTick> HeatmapWidget::TimeAxisTicks() const
{
    std::vector<SAxisTick> ticks;
    const double totalTime = m_results->GetTotalTime();
    const int numTicks = 5;
    for (int i = 0; i <= numTicks; ++i)
    {
        const double frac = static_cast<double>(i) / numTicks;
        ticks.push_back({ frac, QString::number(frac * totalTime, 'g', 3) });
    }
    return ticks;
}

// Groessenachse: linear sechs gleichmaessige Marken; logarithmisch Marken
// bei 1, 2 ... 9 je Dekade, beschriftet die Dekaden (bei weniger als zwei
// Dekaden auch 2 und 5). Werte in der Anzeigeeinheit (m_sizeFactor).
std::vector<HeatmapWidget::SAxisTick> HeatmapWidget::SizeAxisTicks() const
{
    std::vector<SAxisTick> ticks;
    const stosis::SSizeGrid& grid = m_results->GetSizeGrid();
    if (!grid.IsLogarithmic() || grid.sizeMin <= 0.0)
    {
        const int numTicks = 5;
        for (int i = 0; i <= numTicks; ++i)
        {
            const double frac = static_cast<double>(i) / numTicks;
            const double size = grid.sizeMin + frac * (grid.sizeMax - grid.sizeMin);
            ticks.push_back({ frac, QString::number(size * m_sizeFactor, 'g', 3) });
        }
        return ticks;
    }

    const double low = grid.sizeMin * m_sizeFactor;
    const double high = grid.sizeMax * m_sizeFactor;
    const bool fewDecades = std::log10(high / low) < 2.0;
    int numLabeled = 0;
    for (int e = static_cast<int>(std::floor(std::log10(low))); e <= static_cast<int>(std::ceil(std::log10(high))); ++e)
    {
        for (int m = 1; m <= 9; ++m)
        {
            const double value = m * std::pow(10.0, e);
            if (value < low * (1.0 - 1e-9) || value > high * (1.0 + 1e-9))
                continue;
            const bool labeled = (m == 1) || (fewDecades && (m == 2 || m == 5));
            ticks.push_back({ grid.GetAxisFraction(value / m_sizeFactor), labeled ? QString::number(value, 'g', 6) : QString() });
            numLabeled += labeled ? 1 : 0;
        }
    }
    if (numLabeled < 2)
    {
        // Sehr schmales Raster: wenigstens Anfang und Ende beschriften.
        ticks.push_back({ 0.0, QString::number(low, 'g', 3) });
        ticks.push_back({ 1.0, QString::number(high, 'g', 3) });
    }
    return ticks;
}

// Aufteilung des Widgets (mit Randplots):
//
//   +-----------------------+   +------+
//   | TopMarginalRect       |   |Ecke  |          (Beschriftung rechter Plot)
//   +-----------------------+   +------+
//   +-----------------------+   +------+   +--+
//   | PlotRect (Heatmap)    |   |Right |   |  |  Farbskala
//   |                       |   |Marg. |   |  |
//   +-----------------------+   +------+   +--+
QRect HeatmapWidget::PlotRect() const
{
    const int extra = m_showMarginals ? kMarginalSize + kMarginalGap : 0;
    const int top = kMarginTop + extra;
    return QRect(kMarginLeft, top,
                 width() - kMarginLeft - kMarginRight - extra,
                 height() - top - kMarginBottom);
}

QRect HeatmapWidget::TopMarginalRect() const
{
    const QRect plotRect = PlotRect();
    return QRect(plotRect.left(), kMarginTop, plotRect.width(), kMarginalSize);
}

QRect HeatmapWidget::RightMarginalRect() const
{
    const QRect plotRect = PlotRect();
    return QRect(plotRect.right() + kMarginalGap, plotRect.top(), kMarginalSize, plotRect.height());
}

double HeatmapWidget::NormalizeValue(double value, double scaleMax) const
{
    if (scaleMax <= 0.0)
        return 0.0;

    if (m_colorMode == EColorMode::Diverging)
        return value / scaleMax;

    if (!m_logScale)
        return value / scaleMax;

    // Log-Skala: log10 zwischen scaleMax*10^-Dekaden und scaleMax
    // linear auf 0..1 abbilden.
    const double minVal = scaleMax * std::pow(10.0, -m_logDecades);
    if (value <= minVal)
        return 0.0;
    return std::log10(value / minVal) / static_cast<double>(m_logDecades);
}

void HeatmapWidget::ComputeMarginals()
{
    m_columnMarginal.clear();
    m_rowMarginal.clear();
    if (!m_results)
        return;

    const size_t numT = m_results->GetNumTimeSteps();
    const size_t numS = m_results->GetNumSizeClasses();

    std::vector<double> overSize(numT);   // Massenstrom ueber die Zeit
    std::vector<double> overTime(numS);   // Groessenverteilung des Durchsatzes
    if (m_useMarginalOverride && m_overrideOverSize.size() == numT && m_overrideOverTime.size() == numS)
    {
        overSize = m_overrideOverSize;
        overTime = m_overrideOverTime;
    }
    else
    {
        for (size_t t = 0; t < numT; ++t)
            overSize[t] = m_results->IntegrateOverSize(t);
        for (size_t s = 0; s < numS; ++s)
            overTime[s] = m_results->IntegrateOverTime(s);
    }
    // Masse je Groesse in der Anzeigeeinheit (z.B. je um statt je m).
    for (double& value : overTime)
        value /= m_sizeFactor;

    if (m_timeOnXAxis)
    {
        // Spalten = Zeit, Zeilen = Groesse (oben = gross, daher umdrehen).
        m_columnMarginal = std::move(overSize);
        m_rowMarginal.assign(overTime.rbegin(), overTime.rend());
    }
    else
    {
        // Spalten = Groesse, Zeilen = Zeit (oben = t=0).
        m_columnMarginal = std::move(overTime);
        m_rowMarginal = std::move(overSize);
    }
}

double HeatmapWidget::ColumnAxisValue(size_t column) const
{
    return m_timeOnXAxis ? static_cast<double>(column) * m_results->GetDt()
                         : m_results->GetSizeAt(column) * m_sizeFactor;
}

double HeatmapWidget::RowAxisValue(size_t row) const
{
    return m_timeOnXAxis ? m_results->GetSizeAt(m_results->GetNumSizeClasses() - 1 - row) * m_sizeFactor
                         : static_cast<double>(row) * m_results->GetDt();
}

void HeatmapWidget::SetMarginalOverride(std::vector<double> overSize, std::vector<double> overTime,
    const QString& labelPrefix)
{
    m_useMarginalOverride = true;
    m_overrideOverSize = std::move(overSize);
    m_overrideOverTime = std::move(overTime);
    m_marginalLabelPrefix = labelPrefix;
    update();
}

void HeatmapWidget::ClearMarginalOverride()
{
    m_useMarginalOverride = false;
    m_overrideOverSize.clear();
    m_overrideOverTime.clear();
    m_marginalLabelPrefix.clear();
    update();
}

QString HeatmapWidget::SizeMarginalLabel() const
{
    return m_sizeUnit.isEmpty() ? QString("Masse pro Groesse") : "Masse je " + m_sizeUnit;
}

QString HeatmapWidget::ColumnMarginalLabel() const
{
    return m_marginalLabelPrefix + (m_timeOnXAxis ? QString("Massenstrom") : SizeMarginalLabel());
}

QString HeatmapWidget::RowMarginalLabel() const
{
    return m_marginalLabelPrefix + (m_timeOnXAxis ? SizeMarginalLabel() : QString("Massenstrom"));
}

// Zeichnet eine Randverteilung als Linie mit leicht gefuellter Flaeche.
// Jeder Wert sitzt in der Mitte "seiner" Bildspalte/-zeile, damit die
// Kurve exakt ueber bzw. neben den Heatmap-Zellen liegt.
//   vertical = false: Werte entlang x, Ausschlag nach oben
//   vertical = true:  Werte entlang y, Ausschlag nach rechts
void HeatmapWidget::DrawMarginal(QPainter& painter, const QRect& rect,
                                 const std::vector<double>& values, bool vertical) const
{
    painter.setPen(Qt::black);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(rect);
    if (values.empty())
        return;

    const double maxVal = *std::max_element(values.begin(), values.end());
    if (maxVal <= 0.0)
        return;

    const size_t n = values.size();
    const double usable = (vertical ? rect.width() : rect.height()) - 4.0;
    QPolygonF curve;
    for (size_t i = 0; i < n; ++i)
    {
        const double pos = (static_cast<double>(i) + 0.5) / static_cast<double>(n);
        const double amplitude = std::max(0.0, values[i] / maxVal) * usable;
        if (vertical)
            curve << QPointF(rect.left() + amplitude, rect.top() + pos * rect.height());
        else
            curve << QPointF(rect.left() + pos * rect.width(), rect.bottom() - amplitude);
    }

    QPolygonF area = curve;
    if (vertical)
    {
        area.prepend(QPointF(rect.left(), curve.first().y()));
        area.append(QPointF(rect.left(), curve.last().y()));
    }
    else
    {
        area.prepend(QPointF(curve.first().x(), rect.bottom()));
        area.append(QPointF(curve.last().x(), rect.bottom()));
    }

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

// Vereinfachte "viridis"-artige Colormap: ein paar Stuetzstellen von
// dunkelblau/lila (niedrig) ueber gruen bis gelb (hoch), linear
// interpoliert. Kein 1:1-Nachbau von matplotlibs viridis, aber optisch
// sehr aehnlich und ohne Lookup-Tabelle einzubinden.
QColor HeatmapWidget::ValueToColor(double normalizedValue) const
{
    if (m_colorMode == EColorMode::Diverging)
    {
        // Divergierende Skala: -1 -> Blau, 0 -> Weiss, +1 -> Rot.
        // normalizedValue kommt hier im Bereich -1 bis +1 an (wird
        // unten in paintEvent so skaliert).
        normalizedValue = std::clamp(normalizedValue, -1.0, 1.0);

        if (normalizedValue < 0.0)
        {
            // Blau nach Weiss
            const double t = normalizedValue + 1.0; // 0 bei -1, 1 bei 0
            const int r = static_cast<int>(t * 255);
            const int g = static_cast<int>(t * 255);
            return QColor(r, g, 255);
        }
        else
        {
            // Weiss nach Rot
            const double t = 1.0 - normalizedValue; // 1 bei 0, 0 bei +1
            const int g = static_cast<int>(t * 255);
            const int b = static_cast<int>(t * 255);
            return QColor(255, g, b);
        }
    }

    // Sequential (bisherige Viridis-artige Skala, unveraendert)
    normalizedValue = std::clamp(normalizedValue, 0.0, 1.0);

    struct Stop { double pos; int r, g, b; };
    static const Stop stops[] = {
        {0.00,  68,   1,  84},
        {0.25,  59,  82, 139},
        {0.50,  33, 145, 140},
        {0.75,  94, 201,  98},
        {1.00, 253, 231,  37},
    };

    for (size_t i = 0; i + 1 < std::size(stops); ++i)
    {
        const Stop& a = stops[i];
        const Stop& b = stops[i + 1];
        if (normalizedValue >= a.pos && normalizedValue <= b.pos)
        {
            const double t = (normalizedValue - a.pos) / (b.pos - a.pos);
            const int r = static_cast<int>(a.r + t * (b.r - a.r));
            const int g = static_cast<int>(a.g + t * (b.g - a.g));
            const int bl = static_cast<int>(a.b + t * (b.b - a.b));
            return QColor(r, g, bl);
        }
    }
    return QColor(253, 231, 37);
}

void HeatmapWidget::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.fillRect(rect(), Qt::white);

    if (!m_results || m_results->GetNumTimeSteps() == 0 || m_results->GetNumSizeClasses() == 0)
    {
        m_columnMarginal.clear();
        m_rowMarginal.clear();
        painter.drawText(rect(), Qt::AlignCenter, "Keine Daten geladen.");
        return;
    }

    const size_t numT = m_results->GetNumTimeSteps();
    const size_t numS = m_results->GetNumSizeClasses();
    const bool diverging = (m_colorMode == EColorMode::Diverging);
    const bool logActive = m_logScale && !diverging;
    const double scaleMax = diverging ? m_results->GetAbsMaxValue() : m_results->GetMaxValue();

    // --- 1) Werte -> Farbmatrix -> QImage --------------------------
    // Ein Pixel pro Datenpunkt. QImage-Zeilen laufen von oben nach
    // unten:
    //   Standard:      Spalte = Groesse, Zeile = Zeit (t=0 oben)
    //   Zeit auf x:    Spalte = Zeit, Zeile = Groesse (gespiegelt, damit
    //                  kleine Partikel unten liegen)
    const int imageWidth = static_cast<int>(m_timeOnXAxis ? numT : numS);
    const int imageHeight = static_cast<int>(m_timeOnXAxis ? numS : numT);
    QImage image(imageWidth, imageHeight, QImage::Format_RGB32);
    for (size_t t = 0; t < numT; ++t)
    {
        for (size_t s = 0; s < numS; ++s)
        {
            const QColor color = ValueToColor(NormalizeValue(m_results->GetValue(t, s), scaleMax));
            if (m_timeOnXAxis)
                image.setPixelColor(static_cast<int>(t), static_cast<int>(numS - 1 - s), color);
            else
                image.setPixelColor(static_cast<int>(s), static_cast<int>(t), color);
        }
    }

    // --- 2) Plot-Bereich (unter Abzug der Achsen-Raender) -----------
    const QRect plotRect = PlotRect();

    // FastTransformation = kein Weichzeichnen -> jede Zelle bleibt
    // ein sichtbares Rechteck. Bei sehr feinen Gittern (dein
    // "beliebig dicht einstellen") kannst du hier spaeter auf
    // SmoothTransformation umstellen, wenn Zellen kleiner als 1 Pixel
    // werden.
    painter.drawImage(plotRect, image);
    painter.setPen(Qt::black);
    painter.drawRect(plotRect);

    // --- 3) Achsenbeschriftung ---------------------------------------
    // Die Zeit laeuft links -> rechts bzw. oben -> unten, die Groesse links
    // -> rechts bzw. unten -> oben. Beim logarithmischen Raster liegen die
    // Bildspalten in ln d gleichmaessig - die Achse ist dann logarithmisch.
    const QString sizeLabel = m_sizeUnit.isEmpty() ? QString("Partikelgroesse") : "Partikelgroesse [" + m_sizeUnit + "]";
    const QString xLabel = m_timeOnXAxis ? "Zeit" : sizeLabel;
    const QString yLabel = m_timeOnXAxis ? sizeLabel : "Zeit";

    for (const SAxisTick& tick : (m_timeOnXAxis ? TimeAxisTicks() : SizeAxisTicks()))
    {
        const int x = plotRect.left() + static_cast<int>(tick.fraction * plotRect.width());
        painter.drawLine(x, plotRect.bottom(), x, plotRect.bottom() + (tick.label.isEmpty() ? 2 : 4));
        if (!tick.label.isEmpty())
            painter.drawText(QRect(x - 30, plotRect.bottom() + 6, 60, 20), Qt::AlignHCenter, tick.label);
    }
    for (const SAxisTick& tick : (m_timeOnXAxis ? SizeAxisTicks() : TimeAxisTicks()))
    {
        const double frac = m_timeOnXAxis ? 1.0 - tick.fraction : tick.fraction;
        const int y = plotRect.top() + static_cast<int>(frac * plotRect.height());
        painter.drawLine(plotRect.left() - (tick.label.isEmpty() ? 2 : 4), y, plotRect.left(), y);
        if (!tick.label.isEmpty())
            painter.drawText(QRect(0, y - 8, kMarginLeft - 8, 16), Qt::AlignRight | Qt::AlignVCenter, tick.label);
    }
    painter.drawText(QRect(plotRect.left(), height() - 20, plotRect.width(), 20),
                      Qt::AlignHCenter, xLabel);
    painter.save();
    painter.translate(15, plotRect.center().y());
    painter.rotate(-90);
    painter.drawText(QRect(-plotRect.height() / 2, -10, plotRect.height(), 20), Qt::AlignCenter, yLabel);
    painter.restore();

    QFont smallFont = painter.font();
    smallFont.setPointSize(8);

    // --- 4) Randverteilungen -----------------------------------------
    if (m_showMarginals)
    {
        ComputeMarginals();
        const QRect topRect = TopMarginalRect();
        const QRect rightRect = RightMarginalRect();
        DrawMarginal(painter, topRect, m_columnMarginal, false);
        DrawMarginal(painter, rightRect, m_rowMarginal, true);

        const auto maxOf = [](const std::vector<double>& v)
        {
            return v.empty() ? 0.0 : *std::max_element(v.begin(), v.end());
        };

        painter.save();
        painter.setFont(smallFont);
        painter.setPen(Qt::black);
        painter.drawText(topRect.adjusted(5, 3, -5, -3), Qt::AlignLeft | Qt::AlignTop,
            ColumnMarginalLabel() + "  (max " + QString::number(maxOf(m_columnMarginal), 'g', 3) + ")");
        // Beschriftung des rechten Plots in die freie Ecke oberhalb davon.
        const QRect cornerRect(rightRect.left(), topRect.top(), rightRect.width(), topRect.height());
        painter.drawText(cornerRect.adjusted(2, 0, 0, -3), Qt::AlignLeft | Qt::AlignBottom | Qt::TextWordWrap,
            RowMarginalLabel() + "\n(max " + QString::number(maxOf(m_rowMarginal), 'g', 3) + ")");
        painter.restore();
    }

    // --- 5) Farbskala rechts -----------------------------------------
    const int legendLeft = (m_showMarginals ? RightMarginalRect().right() : plotRect.right()) + 20;
    const QRect legendRect(legendLeft, plotRect.top(), 20, plotRect.height());
    for (int y = 0; y < legendRect.height(); ++y)
    {
        double normalized;
        if (diverging)
            normalized = 1.0 - 2.0 * static_cast<double>(y) / legendRect.height();
        else
            normalized = 1.0 - static_cast<double>(y) / legendRect.height();
        painter.setPen(ValueToColor(normalized));
        painter.drawLine(legendRect.left(), legendRect.top() + y, legendRect.right(), legendRect.top() + y);
    }
    painter.setPen(Qt::black);
    painter.drawRect(legendRect);

    QString topLabel;
    QString bottomLabel;
    if (diverging)
    {
        topLabel = "+" + QString::number(scaleMax / m_sizeFactor, 'g', 3);
        bottomLabel = "-" + QString::number(scaleMax / m_sizeFactor, 'g', 3);
    }
    else if (logActive)
    {
        topLabel = QString::number(scaleMax / m_sizeFactor, 'g', 3);
        bottomLabel = QString::number(scaleMax / m_sizeFactor * std::pow(10.0, -m_logDecades), 'g', 2);
    }
    else
    {
        topLabel = QString::number(scaleMax / m_sizeFactor, 'g', 3);
        bottomLabel = "0";
    }
    painter.drawText(QRect(legendRect.left() - 10, legendRect.top() - 15, legendRect.width() + 50, 15),
                      Qt::AlignLeft, topLabel);
    painter.drawText(QRect(legendRect.left() - 10, legendRect.bottom(), legendRect.width() + 50, 15),
                      Qt::AlignLeft, bottomLabel);

    painter.save();
    painter.translate(width() - 15, plotRect.center().y());
    painter.rotate(90);
    painter.setFont(smallFont);
    const QString valueLabel = logActive ? m_valueLabel + " (log)" : m_valueLabel;
    painter.drawText(QRect(-plotRect.height() / 2, -10, plotRect.height(), 20), Qt::AlignCenter, valueLabel);
    painter.restore();
}

void HeatmapWidget::mouseMoveEvent(QMouseEvent* event)
{
    const QPoint pos = event->position().toPoint();
    if (!m_results || m_results->GetNumTimeSteps() == 0 || m_results->GetNumSizeClasses() == 0)
    {
        QToolTip::hideText();
        return;
    }

    const QRect plotRect = PlotRect();
    const size_t numT = m_results->GetNumTimeSteps();
    const size_t numS = m_results->GetNumSizeClasses();
    const size_t numColumns = m_timeOnXAxis ? numT : numS;
    const size_t numRows = m_timeOnXAxis ? numS : numT;
    const double fracX = static_cast<double>(pos.x() - plotRect.left()) / plotRect.width();
    const double fracY = static_cast<double>(pos.y() - plotRect.top()) / plotRect.height();

    // Bildpixel i deckt den Bereich [i/n, (i+1)/n) ab -> abrunden.
    auto toIndex = [](double frac, size_t n)
    {
        const size_t idx = static_cast<size_t>(std::max(0.0, frac) * static_cast<double>(n));
        return std::min(idx, n - 1);
    };
    const QString columnAxisName = m_timeOnXAxis ? "t" : "d";
    const QString rowAxisName = m_timeOnXAxis ? "d" : "t";

    QString text;
    if (plotRect.contains(pos))
    {
        const size_t column = toIndex(fracX, numColumns);
        const size_t row = toIndex(fracY, numRows);
        const size_t t = m_timeOnXAxis ? column : row;
        const size_t s = m_timeOnXAxis ? numS - 1 - row : column;
        text = QString("t = %1\nd = %2%3\n%4 = %5")
            .arg(static_cast<double>(t) * m_results->GetDt(), 0, 'g', 4)
            .arg(m_results->GetSizeAt(s) * m_sizeFactor, 0, 'g', 4)
            .arg(m_sizeUnit.isEmpty() ? QString() : " " + m_sizeUnit)
            .arg(m_valueLabel)
            .arg(m_results->GetValue(t, s) / m_sizeFactor, 0, 'g', 4);
    }
    else if (m_showMarginals && TopMarginalRect().contains(pos) && m_columnMarginal.size() == numColumns)
    {
        const size_t column = toIndex(fracX, numColumns);
        text = QString("%1 = %2\n%3 = %4")
            .arg(columnAxisName).arg(ColumnAxisValue(column), 0, 'g', 4)
            .arg(ColumnMarginalLabel()).arg(m_columnMarginal[column], 0, 'g', 4);
    }
    else if (m_showMarginals && RightMarginalRect().contains(pos) && m_rowMarginal.size() == numRows)
    {
        const size_t row = toIndex(fracY, numRows);
        text = QString("%1 = %2\n%3 = %4")
            .arg(rowAxisName).arg(RowAxisValue(row), 0, 'g', 4)
            .arg(RowMarginalLabel()).arg(m_rowMarginal[row], 0, 'g', 4);
    }

    if (text.isEmpty())
        QToolTip::hideText();
    else
        QToolTip::showText(event->globalPosition().toPoint(), text, this);
}

void HeatmapWidget::leaveEvent(QEvent* /*event*/)
{
    QToolTip::hideText();
}
