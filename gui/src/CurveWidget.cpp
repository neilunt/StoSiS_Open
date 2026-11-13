#include "CurveWidget.h"
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace
{
    // Tick spacing 1, 2 or 5 times a power of ten, about 'count' ticks over 'range'.
    double NiceStep(double range, int count)
    {
        if (!(range > 0.0))
            return 1.0;
        const double raw = range / count;
        const double power = std::pow(10.0, std::floor(std::log10(raw)));
        const double mantissa = raw / power;
        return (mantissa < 1.5 ? 1.0 : mantissa < 3.5 ? 2.0 : mantissa < 7.5 ? 5.0 : 10.0) * power;
    }

    // Label with as many decimals as the tick spacing needs.
    QString TickLabel(double value, double step)
    {
        const int decimals = std::clamp(static_cast<int>(-std::floor(std::log10(step) + 1e-9)), 0, 12);
        if (std::abs(value) < 0.5 * step)
            value = 0.0;
        if (std::abs(value) >= 1e6 || (std::abs(value) > 0.0 && std::abs(value) < 1e-4))
            return QString::number(value, 'g', 4);
        return QString::number(value, 'f', decimals);
    }
}

CurveWidget::CurveWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(400, 300);
    setMouseTracking(false);
}

void CurveWidget::SetTimeAxis(double totalTime, size_t numTimeSteps)
{
    if (totalTime != m_totalTime || numTimeSteps != m_numTimeSteps)
        m_viewSet = false;
    m_totalTime = totalTime;
    m_numTimeSteps = numTimeSteps;
}

void CurveWidget::SetSeries(std::vector<SCurveSeries> series)
{
    // Keep the view when the same curves come again (other band, new run).
    bool sameCurves = series.size() == m_series.size();
    for (size_t i = 0; sameCurves && i < series.size(); ++i)
        sameCurves = series[i].name == m_series[i].name;
    if (!sameCurves)
        m_viewSet = false;
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
    if (label != m_valueLabel)
        m_viewSet = false;
    m_valueLabel = label;
    m_zeroBased = zeroBased;
    update();
}

void CurveWidget::ResetView()
{
    m_viewSet = false;
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

// Line style per compound: solid, dashed, dotted, dash-dot, dash-dot-dot.
Qt::PenStyle CurveWidget::StyleForIndex(int index)
{
    static const Qt::PenStyle styles[] = { Qt::SolidLine, Qt::DashLine, Qt::DotLine, Qt::DashDotLine, Qt::DashDotDotLine };
    return styles[index % static_cast<int>(std::size(styles))];
}

int CurveWidget::Legend(QPainter* painter, int top) const
{
    // Units and compounds in the order of the curves, each once.
    std::vector<std::pair<int, QString>> units, compounds;
    for (const SCurveSeries& s : m_series)
    {
        if (std::none_of(units.begin(), units.end(), [&](const auto& u) { return u.first == s.colorIndex; }))
            units.push_back({ s.colorIndex, s.unit.isEmpty() ? s.name : s.unit });
        if (!s.compound.isEmpty() && std::none_of(compounds.begin(), compounds.end(), [&](const auto& c) { return c.first == s.styleIndex; }))
            compounds.push_back({ s.styleIndex, s.compound });
    }
    const QFontMetrics metrics = fontMetrics();
    const int lineHeight = metrics.height() + 4;
    const int left = kMarginLeft, right = width() - kMarginRight;
    const int sample = 26, gap = 18;
    int x = left, y = top + 4;
    // One entry: a sample line (colour or style) and the text; wraps to the next line.
    const auto entry = [&](const QPen& pen, const QString& text, bool withLine)
    {
        const int w = (withLine ? sample + 6 : 0) + metrics.horizontalAdvance(text);
        if (x > left && x + w > right)
        {
            x = left;
            y += lineHeight;
        }
        if (painter)
        {
            if (withLine)
            {
                painter->setPen(pen);
                painter->drawLine(x, y + lineHeight / 2, x + sample, y + lineHeight / 2);
            }
            painter->setPen(withLine ? QPen(Qt::black) : pen);
            painter->drawText(QRect(x + (withLine ? sample + 6 : 0), y, w, lineHeight), Qt::AlignLeft | Qt::AlignVCenter, text);
        }
        x += w + gap;
    };
    const QPen gray(QColor(110, 110, 110));
    if (painter)
        painter->setRenderHint(QPainter::Antialiasing, false);
    if (!compounds.empty())
        entry(gray, "Einheiten:", false);
    for (const auto& u : units)
        entry(QPen(ColorForIndex(u.first), 3), u.second, true);
    if (!compounds.empty())
    {
        x = left;
        y += lineHeight;
        entry(gray, "Stoffe:", false);
        for (const auto& c : compounds)
            entry(QPen(QColor(60, 60, 60), 2, StyleForIndex(c.first)), c.second, true);
    }
    if (!m_caption.isEmpty())
    {
        QString caption = m_caption;
        entry(gray, caption.replace(QChar('\n'), ", "), false);
    }
    if (painter)
        painter->setRenderHint(QPainter::Antialiasing, true);
    return m_series.empty() ? 0 : y + lineHeight + 4 - top;
}

QRect CurveWidget::PlotRect() const
{
    return QRect(kMarginLeft, kMarginTop, width() - kMarginLeft - kMarginRight,
        height() - kMarginTop - kMarginBottom - Legend(nullptr, 0));
}

QRectF CurveWidget::AutoView() const
{
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
    const double totalTime = m_totalTime > 0.0 ? m_totalTime : 1.0;
    return QRectF(0.0, minValue, totalTime, maxValue - minValue);
}

QPointF CurveWidget::ToData(const QPointF& pixel, const QRectF& view) const
{
    const QRect plot = PlotRect();
    const double fx = (pixel.x() - plot.left()) / std::max(1, plot.width());
    const double fy = (plot.bottom() - pixel.y()) / std::max(1, plot.height());
    return QPointF(view.x() + fx * view.width(), view.y() + fy * view.height());
}

void CurveWidget::SetView(const QRectF& view)
{
    // Not narrower than a millionth of the full view, so the axes stay readable.
    const QRectF full = AutoView();
    if (view.width() < 1e-6 * full.width() || view.height() < 1e-9 * std::max(1e-300, std::abs(full.height())))
        return;
    m_view = view;
    m_viewSet = true;
    update();
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

    const QRect plotRect = PlotRect();
    const QRectF view = CurrentView();
    const double stepTime = m_totalTime / static_cast<double>(m_numTimeSteps - 1);

    auto toPoint = [&](size_t t, double value)
    {
        const double time = static_cast<double>(t) * stepTime;
        return QPointF(plotRect.left() + (time - view.x()) / view.width() * plotRect.width(),
            plotRect.bottom() - (value - view.y()) / view.height() * plotRect.height());
    };
    // Only the steps in the visible time range (plus one on each side).
    size_t firstStep = 0, lastStep = m_numTimeSteps - 1;
    if (stepTime > 0.0)
    {
        const double a = std::floor(view.x() / stepTime) - 1.0;
        const double b = std::ceil((view.x() + view.width()) / stepTime) + 1.0;
        firstStep = a <= 0.0 ? 0 : std::min(static_cast<size_t>(a), m_numTimeSteps - 1);
        lastStep = b <= 0.0 ? 0 : std::min(static_cast<size_t>(b), m_numTimeSteps - 1);
    }
    auto toPolyline = [&](const std::vector<double>& values)
    {
        QPolygonF polyline;
        for (size_t t = firstStep; t <= lastStep && t < values.size(); ++t)
            polyline << toPoint(t, values[t]);
        return polyline;
    };

    // --- Axes and grid ------------------------------------------------------
    // Labelled ticks every 1, 2 or 5 times a power of ten with dashed grid
    // lines; three unlabelled ticks between two of them with dotted lines.
    const double timeStep = NiceStep(view.width(), 5);
    const double valueStep = NiceStep(view.height(), 5);
    const auto forTicks = [](double from, double length, double major, auto&& draw)
    {
        const double minor = 0.25 * major;
        const long long first = static_cast<long long>(std::ceil(from / minor - 1e-9));
        for (long long i = first; static_cast<double>(i) * minor <= from + length + 1e-9 * minor; ++i)
            draw(static_cast<double>(i) * minor, ((i % 4) + 4) % 4 == 0);
    };
    const auto xOf = [&](double time) { return plotRect.left() + static_cast<int>(std::lround((time - view.x()) / view.width() * plotRect.width())); };
    const auto yOf = [&](double value) { return plotRect.bottom() - static_cast<int>(std::lround((value - view.y()) / view.height() * plotRect.height())); };
    const QPen majorGrid(QColor(150, 150, 150), 1, Qt::DashLine);
    const QPen minorGrid(QColor(200, 200, 200), 1, Qt::DotLine);

    painter.setRenderHint(QPainter::Antialiasing, false);
    forTicks(view.x(), view.width(), timeStep, [&](double time, bool major)
        {
            const int x = xOf(time);
            if (x <= plotRect.left() || x >= plotRect.right())
                return;
            painter.setPen(major ? majorGrid : minorGrid);
            painter.drawLine(x, plotRect.top() + 1, x, plotRect.bottom() - 1);
        });
    forTicks(view.y(), view.height(), valueStep, [&](double value, bool major)
        {
            const int y = yOf(value);
            if (y <= plotRect.top() || y >= plotRect.bottom())
                return;
            painter.setPen(major ? majorGrid : minorGrid);
            painter.drawLine(plotRect.left() + 1, y, plotRect.right() - 1, y);
        });

    painter.setPen(Qt::black);
    painter.drawRect(plotRect);
    forTicks(view.x(), view.width(), timeStep, [&](double time, bool major)
        {
            const int x = xOf(time);
            painter.drawLine(x, plotRect.bottom(), x, plotRect.bottom() + (major ? 5 : 2));
            if (major)
                painter.drawText(QRect(x - 40, plotRect.bottom() + 6, 80, 20), Qt::AlignHCenter, TickLabel(time, timeStep));
        });
    forTicks(view.y(), view.height(), valueStep, [&](double value, bool major)
        {
            const int y = yOf(value);
            painter.drawLine(plotRect.left() - (major ? 5 : 2), y, plotRect.left(), y);
            if (major)
                painter.drawText(QRect(0, y - 8, kMarginLeft - 8, 16), Qt::AlignRight | Qt::AlignVCenter, TickLabel(value, valueStep));
        });
    painter.drawText(QRect(plotRect.left(), plotRect.bottom() + 20, plotRect.width(), 18), Qt::AlignHCenter, "Zeit [s]");
    painter.setRenderHint(QPainter::Antialiasing, true);

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
        QColor fill = ColorForIndex(s.colorIndex);
        fill.setAlpha(55);
        painter.setPen(Qt::NoPen);
        painter.setBrush(fill);
        painter.drawPolygon(band);
    }
    painter.setBrush(Qt::NoBrush);

    // --- 2) Einzellaeufe (duenn, halbtransparent) -------------------------
    for (int seriesIdx = 0; seriesIdx < static_cast<int>(m_series.size()); ++seriesIdx)
    {
        const SCurveSeries& s = m_series[seriesIdx];
        QColor runColor = ColorForIndex(s.colorIndex);
        runColor.setAlpha(70);
        painter.setPen(QPen(runColor, 0.8, StyleForIndex(s.styleIndex)));
        for (const std::vector<double>& run : m_series[seriesIdx].runs)
            painter.drawPolyline(toPolyline(run));
    }

    // --- 3) Hauptlinien obenauf -------------------------------------------
    for (int seriesIdx = 0; seriesIdx < static_cast<int>(m_series.size()); ++seriesIdx)
    {
        const SCurveSeries& s = m_series[seriesIdx];
        painter.setPen(QPen(ColorForIndex(s.colorIndex), 2, StyleForIndex(s.styleIndex)));
        painter.drawPolyline(toPolyline(m_series[seriesIdx].values));
    }

    // --- Rechteck beim Aufziehen mit der rechten Maustaste -----------------
    if (m_drag == EDrag::Zoom)
    {
        painter.setPen(QPen(QColor(60, 60, 60), 1, Qt::DashLine));
        painter.setBrush(QColor(31, 119, 180, 30));
        painter.drawRect(QRect(m_pressPos, m_currentPos).normalized());
        painter.setBrush(Qt::NoBrush);
    }
    painter.restore();

    // Beschriftung der Werteachse oben links im Diagramm.
    if (!m_valueLabel.isEmpty())
    {
        painter.setPen(Qt::darkGray);
        painter.drawText(plotRect.adjusted(6, 4, 0, 0), Qt::AlignLeft | Qt::AlignTop, m_valueLabel);
    }

    Legend(&painter, plotRect.bottom() + kMarginBottom);
}

void CurveWidget::mousePressEvent(QMouseEvent* event)
{
    if (m_series.empty() || m_numTimeSteps < 2 || !PlotRect().contains(event->position().toPoint()))
    {
        QWidget::mousePressEvent(event);
        return;
    }
    m_pressPos = event->position().toPoint();
    m_currentPos = m_pressPos;
    m_pressView = CurrentView();
    if (event->button() == Qt::LeftButton)
    {
        m_drag = EDrag::Pan;
        setCursor(Qt::ClosedHandCursor);
    }
    else if (event->button() == Qt::RightButton)
        m_drag = EDrag::Zoom;
}

void CurveWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_drag == EDrag::None)
        return;
    m_currentPos = event->position().toPoint();
    if (m_drag == EDrag::Pan)
    {
        const QPointF a = ToData(m_pressPos, m_pressView);
        const QPointF b = ToData(m_currentPos, m_pressView);
        SetView(m_pressView.translated(a - b));
    }
    else
        update();
}

void CurveWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_drag == EDrag::Zoom)
    {
        // Rectangle within the plot; small rectangles count as a plain click.
        const QRect box = QRect(m_pressPos, event->position().toPoint()).normalized().intersected(PlotRect());
        if (box.width() > 4 && box.height() > 4)
        {
            const QPointF a = ToData(box.bottomLeft(), m_pressView);
            const QPointF b = ToData(box.topRight(), m_pressView);
            SetView(QRectF(a, b).normalized());
        }
    }
    m_drag = EDrag::None;
    unsetCursor();
    update();
}

void CurveWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        ResetView();
}

void CurveWidget::wheelEvent(QWheelEvent* event)
{
    const QPoint pos = event->position().toPoint();
    if (m_series.empty() || m_numTimeSteps < 2 || !PlotRect().contains(pos) || event->angleDelta().y() == 0)
    {
        QWidget::wheelEvent(event);
        return;
    }
    // One notch zooms by 20 % around the point under the cursor.
    const double factor = std::pow(0.8, event->angleDelta().y() / 120.0);
    const QRectF view = CurrentView();
    const QPointF center = ToData(pos, view);
    SetView(QRectF(center.x() - (center.x() - view.x()) * factor, center.y() - (center.y() - view.y()) * factor,
        view.width() * factor, view.height() * factor));
    event->accept();
}
