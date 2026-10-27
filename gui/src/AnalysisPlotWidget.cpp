#include "AnalysisPlotWidget.h"
#include <QPainter>
#include <algorithm>
#include <cmath>

namespace
{
    constexpr int kMarginLeft = 62;
    constexpr int kMarginRight = 14;
    constexpr int kMarginTop = 26;
    constexpr int kMarginBottom = 40;
    // Wie die erste Kurve im Kurvenfenster (CurveWidget).
    const QColor kRunColor(31, 119, 180);
    const QColor kMeanColor(15, 55, 110);
    const QColor kNormalColor(214, 39, 40);

    // Lineare Abbildung Wert -> Pixel im Diagrammrahmen.
    struct SMap
    {
        QRect plot;
        double x0, x1, y0, y1;
        double X(double x) const { return plot.left() + (x - x0) / (x1 - x0) * plot.width(); }
        double Y(double y) const { return plot.bottom() - (y - y0) / (y1 - y0) * plot.height(); }
        QPointF P(double x, double y) const { return QPointF(X(x), Y(y)); }
    };

    // Glatte Teilstriche (1, 2, 2,5, 5 mal 10^k), etwa "wanted" Stueck.
    std::vector<double> NiceTicks(double lo, double hi, int wanted)
    {
        std::vector<double> ticks;
        if (!(hi > lo))
            return ticks;
        const double raw = (hi - lo) / wanted;
        const double decade = std::pow(10.0, std::floor(std::log10(raw)));
        double step = 10.0 * decade;
        for (double f : { 1.0, 2.0, 2.5, 5.0 })
            if (raw <= f * decade)
            {
                step = f * decade;
                break;
            }
        for (double t = std::ceil(lo / step) * step; t <= hi + 1e-9 * step; t += step)
            ticks.push_back(std::abs(t) < 1e-9 * step ? 0.0 : t);
        return ticks;
    }

    QString Number(double v) { return QString::number(v, 'g', 6); }

    void DrawFrameAndXAxis(QPainter& p, const SMap& m, const QString& label)
    {
        p.setPen(Qt::black);
        p.setBrush(Qt::NoBrush);
        p.drawRect(m.plot);
        for (double t : NiceTicks(m.x0, m.x1, 6))
        {
            const int x = static_cast<int>(std::lround(m.X(t)));
            p.drawLine(x, m.plot.bottom(), x, m.plot.bottom() + 4);
            p.drawText(QRect(x - 40, m.plot.bottom() + 6, 80, 16), Qt::AlignHCenter, Number(t));
        }
        p.drawText(QRect(m.plot.left(), m.plot.bottom() + 20, m.plot.width(), 18), Qt::AlignHCenter, label);
    }

    void DrawYTicks(QPainter& p, const SMap& m, const std::vector<std::pair<double, QString>>& ticks)
    {
        p.setPen(Qt::black);
        for (const auto& tick : ticks)
        {
            const int y = static_cast<int>(std::lround(m.Y(tick.first)));
            p.drawLine(m.plot.left() - 4, y, m.plot.left(), y);
            p.drawText(QRect(0, y - 8, kMarginLeft - 8, 16), Qt::AlignRight | Qt::AlignVCenter, tick.second);
        }
    }

    std::vector<std::pair<double, QString>> AutoTicks(double lo, double hi)
    {
        std::vector<std::pair<double, QString>> ticks;
        for (double t : NiceTicks(lo, hi, 5))
            ticks.emplace_back(t, Number(t));
        return ticks;
    }

    // Legende oben (bottom: unten) rechts im Diagramm. style: 0 Linie, 1 Flaeche, 2 gestrichelt, 3 Punkte.
    struct SLegendItem
    {
        QColor color;
        int style;
        QString text;
    };

    void DrawLegend(QPainter& p, const QRect& plot, const std::vector<SLegendItem>& items, bool bottom = false)
    {
        const QFontMetrics fm(p.font());
        int textWidth = 0;
        for (const SLegendItem& item : items)
            textWidth = std::max(textWidth, fm.horizontalAdvance(item.text));
        const int boxHeight = 6 + 17 * static_cast<int>(items.size());
        const QRect box(plot.right() - textWidth - 42, bottom ? plot.bottom() - boxHeight - 6 : plot.top() + 6, textWidth + 36, boxHeight);
        p.setPen(QColor(200, 200, 200));
        p.setBrush(QColor(255, 255, 255, 220));
        p.drawRect(box);
        int y = box.top() + 12;
        for (const SLegendItem& item : items)
        {
            const int x = box.left() + 6;
            if (item.style == 1)
            {
                p.setPen(Qt::NoPen);
                QColor fill = item.color;
                fill.setAlpha(60);
                p.setBrush(fill);
                p.drawRect(x, y - 5, 18, 10);
            }
            else if (item.style == 3)
            {
                p.setPen(QPen(item.color, 1));
                p.setBrush(Qt::NoBrush);
                p.drawEllipse(QPointF(x + 9, y), 2.5, 2.5);
            }
            else
            {
                QPen pen(item.color, 2);
                if (item.style == 2)
                    pen.setStyle(Qt::DashLine);
                p.setPen(pen);
                p.drawLine(x, y, x + 18, y);
            }
            p.setPen(Qt::black);
            p.drawText(QRect(x + 24, y - 8, textWidth + 4, 16), Qt::AlignLeft | Qt::AlignVCenter, item.text);
            y += 17;
        }
        p.setBrush(Qt::NoBrush);
    }

    double NormalDensity(double x, double mean, double stdDev)
    {
        const double z = (x - mean) / stdDev;
        return std::exp(-0.5 * z * z) / (stdDev * std::sqrt(2.0 * 3.14159265358979323846));
    }
}

AnalysisPlotWidget::AnalysisPlotWidget(EKind kind, QWidget* parent)
    : QWidget(parent), m_kind(kind)
{
    setMinimumSize(280, 220);
}

void AnalysisPlotWidget::SetUnit(const QString& unit)
{
    m_unit = unit;
    update();
}

void AnalysisPlotWidget::ShowMessage(const QString& message)
{
    m_message = message;
    update();
}

void AnalysisPlotWidget::SetDensity(const stosis::SHistogram& firstRun, std::vector<double> bandLow,
    std::vector<double> bandHigh, double mean, double stdDev)
{
    m_histogram = firstRun;
    m_bandLow = std::move(bandLow);
    m_bandHigh = std::move(bandHigh);
    m_mean = mean;
    m_stdDev = stdDev;
    m_message.clear();
    update();
}

void AnalysisPlotWidget::SetProbability(std::vector<std::pair<double, double>> points, double mean, double stdDev)
{
    m_points = std::move(points);
    m_mean = mean;
    m_stdDev = stdDev;
    m_message.clear();
    update();
}

void AnalysisPlotWidget::SetTimeSeries(std::vector<double> times, std::vector<double> values,
    std::vector<double> windowStarts, std::vector<double> windowMeans, double windowLength)
{
    m_times = std::move(times);
    m_values = std::move(values);
    m_windowStarts = std::move(windowStarts);
    m_windowMeans = std::move(windowMeans);
    m_windowLength = windowLength;
    m_message.clear();
    update();
}

void AnalysisPlotWidget::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), Qt::white);

    const char* titles[] = { "Dichte ueber dem Wert (Histogramm)", "Wahrscheinlichkeitsnetz", "Zeitverlauf (Lauf 1)" };
    QFont bold = painter.font();
    bold.setBold(true);
    painter.setFont(bold);
    painter.setPen(Qt::black);
    painter.drawText(QRect(kMarginLeft, 4, width() - kMarginLeft, 18), Qt::AlignLeft | Qt::AlignVCenter,
        titles[static_cast<int>(m_kind)]);
    bold.setBold(false);
    painter.setFont(bold);

    const QRect plot(kMarginLeft, kMarginTop, width() - kMarginLeft - kMarginRight, height() - kMarginTop - kMarginBottom);
    if (!m_message.isEmpty() || plot.width() < 40 || plot.height() < 40)
    {
        painter.setPen(Qt::darkGray);
        painter.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, m_message);
        return;
    }
    switch (m_kind)
    {
    case EKind::Density: PaintDensity(painter, plot); break;
    case EKind::Probability: PaintProbability(painter, plot); break;
    case EKind::TimeSeries: PaintTimeSeries(painter, plot); break;
    }
}

void AnalysisPlotWidget::PaintDensity(QPainter& p, const QRect& plot)
{
    const size_t n = m_histogram.density.size();
    if (n == 0)
        return;
    const bool withBand = m_bandLow.size() == n && m_bandHigh.size() == n;
    double top = 0.0;
    for (size_t i = 0; i < n; ++i)
        top = std::max(top, withBand ? std::max(m_bandHigh[i], m_histogram.density[i]) : m_histogram.density[i]);
    if (m_stdDev > 0.0)
        top = std::max(top, NormalDensity(m_mean, m_mean, m_stdDev));
    if (top <= 0.0)
        top = 1.0;
    const SMap m{ plot, m_histogram.start, m_histogram.GetLower(n), 0.0, 1.12 * top };

    p.save();
    p.setClipRect(plot);
    if (withBand)
    {
        // Treppe oben (groesste Dichte je Klasse) hin, unten zurueck.
        QPolygonF band;
        for (size_t i = 0; i < n; ++i)
            band << m.P(m_histogram.GetLower(i), m_bandHigh[i]) << m.P(m_histogram.GetLower(i + 1), m_bandHigh[i]);
        for (size_t i = n; i-- > 0;)
            band << m.P(m_histogram.GetLower(i + 1), m_bandLow[i]) << m.P(m_histogram.GetLower(i), m_bandLow[i]);
        QColor fill = kRunColor;
        fill.setAlpha(55);
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawPolygon(band);
        p.setBrush(Qt::NoBrush);
    }
    QPolygonF steps;
    steps << m.P(m_histogram.start, 0.0);
    for (size_t i = 0; i < n; ++i)
        steps << m.P(m_histogram.GetLower(i), m_histogram.density[i]) << m.P(m_histogram.GetLower(i + 1), m_histogram.density[i]);
    steps << m.P(m_histogram.GetLower(n), 0.0);
    p.setPen(QPen(kRunColor, 1.6));
    p.drawPolyline(steps);
    if (m_stdDev > 0.0)
    {
        QPolygonF curve;
        for (int k = 0; k <= 300; ++k)
        {
            const double x = m.x0 + (m.x1 - m.x0) * k / 300.0;
            curve << m.P(x, NormalDensity(x, m_mean, m_stdDev));
        }
        QPen pen(kNormalColor, 1.4);
        pen.setStyle(Qt::DashLine);
        p.setPen(pen);
        p.drawPolyline(curve);
    }
    p.restore();

    DrawFrameAndXAxis(p, m, "Wert [" + m_unit + "]");
    DrawYTicks(p, m, AutoTicks(m.y0, m.y1));
    p.setPen(Qt::darkGray);
    p.drawText(plot.adjusted(6, 4, 0, 0), Qt::AlignLeft | Qt::AlignTop, "Dichte [1/(" + m_unit + ")]");
    std::vector<SLegendItem> legend = { { kRunColor, 0, "Lauf 1" } };
    if (withBand)
        legend.push_back({ kRunColor, 1, "Band: alle Laeufe (min - max)" });
    legend.push_back({ kNormalColor, 2, "Normalverteilung" });
    DrawLegend(p, plot, legend);
}

void AnalysisPlotWidget::PaintProbability(QPainter& p, const QRect& plot)
{
    if (m_points.empty())
        return;
    double xMin = m_points.front().first, xMax = m_points.back().first, zMax = 3.0;
    for (const auto& point : m_points)
        zMax = std::max(zMax, std::abs(point.second) + 0.2);
    double pad = 0.04 * (xMax - xMin);
    if (pad <= 0.0)
        pad = std::max(1e-9, 0.01 * std::abs(xMax));
    const SMap m{ plot, xMin - pad, xMax + pad, -zMax, zMax };

    p.save();
    p.setClipRect(plot);
    if (m_stdDev > 0.0)
    {
        QPen pen(kNormalColor, 1.4);
        pen.setStyle(Qt::DashLine);
        p.setPen(pen);
        p.drawLine(m.P(m_mean - zMax * m_stdDev, -zMax), m.P(m_mean + zMax * m_stdDev, zMax));
    }
    // Bei vielen Werten nur jeden k-ten zeichnen, die Raender (|z| > 2) immer.
    const size_t every = std::max<size_t>(1, m_points.size() / 1500);
    p.setPen(QPen(kRunColor, 0.9));
    p.setBrush(Qt::NoBrush);
    for (size_t i = 0; i < m_points.size(); ++i)
        if (i % every == 0 || std::abs(m_points[i].second) > 2.0)
            p.drawEllipse(m.P(m_points[i].first, m_points[i].second), 1.8, 1.8);
    p.restore();

    DrawFrameAndXAxis(p, m, "Wert [" + m_unit + "]");
    std::vector<std::pair<double, QString>> ticks;
    for (double prob : { 0.001, 0.01, 0.1, 0.5, 0.9, 0.99, 0.999 })
    {
        const double z = stosis::NormalQuantile(prob);
        if (std::abs(z) < zMax)
            ticks.emplace_back(z, QString::number(prob, 'g', 3));
    }
    DrawYTicks(p, m, ticks);
    p.setPen(Qt::darkGray);
    p.drawText(plot.adjusted(6, 4, 0, 0), Qt::AlignLeft | Qt::AlignTop, "Anteil der Werte darunter");
    DrawLegend(p, plot, { { kRunColor, 3, "Lauf 1" }, { kNormalColor, 2, "Normalverteilung" } }, true);
}

void AnalysisPlotWidget::PaintTimeSeries(QPainter& p, const QRect& plot)
{
    if (m_times.size() < 2 || m_values.size() != m_times.size())
        return;
    double yMin = m_values.front(), yMax = m_values.front();
    for (double v : m_values)
    {
        yMin = std::min(yMin, v);
        yMax = std::max(yMax, v);
    }
    double pad = 0.06 * (yMax - yMin);
    if (pad <= 0.0)
        pad = std::max(1e-9, 0.01 * std::abs(yMax));
    double tEnd = m_times.back();
    if (!m_windowStarts.empty())
        tEnd = std::max(tEnd, m_windowStarts.back() + m_windowLength);
    const SMap m{ plot, m_times.front(), tEnd, yMin - pad, yMax + pad };

    p.save();
    p.setClipRect(plot);
    QPolygonF raw;
    for (size_t k = 0; k < m_times.size(); ++k)
        raw << m.P(m_times[k], m_values[k]);
    QColor light = kRunColor;
    light.setAlpha(120);
    p.setPen(QPen(light, 0.8));
    p.drawPolyline(raw);
    QPolygonF steps;
    for (size_t i = 0; i < m_windowStarts.size() && i < m_windowMeans.size(); ++i)
        steps << m.P(m_windowStarts[i], m_windowMeans[i]) << m.P(m_windowStarts[i] + m_windowLength, m_windowMeans[i]);
    p.setPen(QPen(kMeanColor, 2));
    p.drawPolyline(steps);
    p.restore();

    DrawFrameAndXAxis(p, m, "Zeit [s]");
    DrawYTicks(p, m, AutoTicks(m.y0, m.y1));
    p.setPen(Qt::darkGray);
    p.drawText(plot.adjusted(6, 4, 0, 0), Qt::AlignLeft | Qt::AlignTop, "Massenstrom [" + m_unit + "]");
    DrawLegend(p, plot, { { light, 0, "je Zeitschritt" }, { kMeanColor, 0, "Mittel je Fenster" } });
}
