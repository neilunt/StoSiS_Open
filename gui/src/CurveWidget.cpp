#include "CurveWidget.h"
#include <QPainter>
#include <QPaintEvent>
#include <algorithm>
#include <cmath>

CurveWidget::CurveWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(400, 300);
}

void CurveWidget::SetTimeAxis(double totalTime, size_t numTimeSteps)
{
    m_totalTime = totalTime;
    m_numTimeSteps = numTimeSteps;
}

void CurveWidget::SetSeries(std::vector<SCurveSeries> series)
{
    m_series = std::move(series);
    update();
}

void CurveWidget::SetCaption(const QString& caption)
{
    m_caption = caption;
    update();
}

void CurveWidget::SetValueAxis(const QString& label, bool zeroBased)
{
    m_valueLabel = label;
    m_zeroBased = zeroBased;
    update();
}

// Feste, gut unterscheidbare Farbfolge; wiederholt sich zyklisch bei mehr
// Kurven als Farben.
QColor CurveWidget::ColorForIndex(int index) const
{
    static const QColor palette[] = {
        QColor(31, 119, 180), QColor(255, 127, 14), QColor(44, 160, 44),
        QColor(214, 39, 40), QColor(148, 103, 189), QColor(140, 86, 75),
        QColor(227, 119, 194), QColor(127, 127, 127),
    };
    return palette[index % static_cast<int>(std::size(palette))];
}

void CurveWidget::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), Qt::white);

    if (m_series.empty() || m_numTimeSteps < 2)
    {
        painter.drawText(rect(), Qt::AlignCenter, "Keine Einheit ausgewaehlt");
        return;
    }

    const QRect plotRect(kMarginLeft, kMarginTop,
        width() - kMarginLeft - kMarginRight,
        height() - kMarginTop - kMarginBottom);

    // --- Wertebereich ueber ALLE Kurven, Baender und Einzellaeufe --------
    // Mengen beginnen bei 0; Temperatur und Druck zeigen nur den Bereich, in
    // dem sie sich bewegen (sonst waere eine Aenderung um 20 K bei 293 K
    // kaum zu sehen).
    double minValue = 0.0;
    double maxValue = 0.0;
    bool first = true;
    auto include = [&](double v)
    {
        minValue = first ? v : std::min(minValue, v);
        maxValue = first ? v : std::max(maxValue, v);
        first = false;
    };
    for (const SCurveSeries& s : m_series)
    {
        for (double v : s.values)
            include(v);
        for (double v : s.upper)
            include(v);
        if (!m_zeroBased)
            for (double v : s.lower)
                include(v);
        for (const std::vector<double>& run : s.runs)
            for (double v : run)
                include(v);
    }
    if (m_zeroBased)
    {
        minValue = 0.0;
        // 5 % Luft nach oben, sonst liegt ein konstanter Verlauf genau auf
        // dem Rahmen und ist nicht zu sehen.
        maxValue = 1.05 * std::max(maxValue, 0.0);
        if (maxValue <= 0.0)
            maxValue = 1.0;
    }
    else
    {
        double range = maxValue - minValue;
        if (range <= 1e-9 * std::abs(maxValue))
            range = std::max(0.01 * std::abs(maxValue), 1.0);
        const double middle = 0.5 * (minValue + maxValue);
        minValue = middle - 0.55 * range;
        maxValue = middle + 0.55 * range;
    }
    const double valueRange = maxValue - minValue;

    auto toPoint = [&](size_t t, double value)
    {
        const double frac = static_cast<double>(t) / static_cast<double>(m_numTimeSteps - 1);
        return QPointF(plotRect.left() + frac * plotRect.width(),
            plotRect.bottom() - ((value - minValue) / valueRange) * plotRect.height());
    };
    auto toPolyline = [&](const std::vector<double>& values)
    {
        QPolygonF polyline;
        for (size_t t = 0; t < values.size() && t < m_numTimeSteps; ++t)
            polyline << toPoint(t, values[t]);
        return polyline;
    };

    // --- Achsen -----------------------------------------------------------
    painter.setPen(Qt::black);
    painter.drawRect(plotRect);

    const int numTicks = 5;
    for (int i = 0; i <= numTicks; ++i)
    {
        const double frac = static_cast<double>(i) / numTicks;

        const double timeVal = frac * m_totalTime;
        const int x = plotRect.left() + static_cast<int>(frac * plotRect.width());
        painter.drawLine(x, plotRect.bottom(), x, plotRect.bottom() + 4);
        painter.drawText(QRect(x - 30, plotRect.bottom() + 6, 60, 20),
            Qt::AlignHCenter, QString::number(timeVal, 'g', 3));

        const double valueVal = minValue + (1.0 - frac) * valueRange;
        const int y = plotRect.top() + static_cast<int>(frac * plotRect.height());
        painter.drawLine(plotRect.left() - 4, y, plotRect.left(), y);
        painter.drawText(QRect(0, y - 8, kMarginLeft - 8, 16),
            Qt::AlignRight | Qt::AlignVCenter, QString::number(valueVal, 'g', 3));
    }
    painter.drawText(QRect(plotRect.left(), height() - 20, plotRect.width(), 20),
        Qt::AlignHCenter, "Zeit");

    painter.save();
    painter.setClipRect(plotRect);

    // --- 1) Streubaender (unterste Ebene) ---------------------------------
    for (int seriesIdx = 0; seriesIdx < static_cast<int>(m_series.size()); ++seriesIdx)
    {
        const SCurveSeries& s = m_series[seriesIdx];
        if (s.lower.empty() || s.lower.size() != s.upper.size())
            continue;
        QPolygonF band = toPolyline(s.upper);
        const QPolygonF lower = toPolyline(s.lower);
        for (auto it = lower.rbegin(); it != lower.rend(); ++it)
            band << *it;
        QColor fill = ColorForIndex(seriesIdx);
        fill.setAlpha(55);
        painter.setPen(Qt::NoPen);
        painter.setBrush(fill);
        painter.drawPolygon(band);
    }
    painter.setBrush(Qt::NoBrush);

    // --- 2) Einzellaeufe (duenn, halbtransparent) -------------------------
    for (int seriesIdx = 0; seriesIdx < static_cast<int>(m_series.size()); ++seriesIdx)
    {
        QColor runColor = ColorForIndex(seriesIdx);
        runColor.setAlpha(70);
        painter.setPen(QPen(runColor, 0.8));
        for (const std::vector<double>& run : m_series[seriesIdx].runs)
            painter.drawPolyline(toPolyline(run));
    }

    // --- 3) Hauptlinien obenauf -------------------------------------------
    for (int seriesIdx = 0; seriesIdx < static_cast<int>(m_series.size()); ++seriesIdx)
    {
        painter.setPen(QPen(ColorForIndex(seriesIdx), 2));
        painter.drawPolyline(toPolyline(m_series[seriesIdx].values));
    }
    painter.restore();

    // Beschriftung der Werteachse oben links im Diagramm.
    if (!m_valueLabel.isEmpty())
    {
        painter.setPen(Qt::darkGray);
        painter.drawText(plotRect.adjusted(6, 4, 0, 0), Qt::AlignLeft | Qt::AlignTop, m_valueLabel);
    }

    // --- Legende rechts -----------------------------------------------------
    int legendY = plotRect.top();
    for (int seriesIdx = 0; seriesIdx < static_cast<int>(m_series.size()); ++seriesIdx)
    {
        painter.setPen(QPen(ColorForIndex(seriesIdx), 3));
        painter.drawLine(plotRect.right() + 15, legendY + 8, plotRect.right() + 35, legendY + 8);
        painter.setPen(Qt::black);
        painter.drawText(QRect(plotRect.right() + 40, legendY, kMarginRight - 45, 18),
            Qt::AlignLeft | Qt::AlignVCenter, m_series[seriesIdx].name);
        legendY += 20;
    }
    if (!m_caption.isEmpty())
    {
        painter.setPen(Qt::darkGray);
        painter.drawText(QRect(plotRect.right() + 15, legendY + 6, kMarginRight - 20, 60),
            Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, m_caption);
    }
}
