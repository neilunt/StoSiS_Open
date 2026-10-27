#include "CardPlotWidget.h"
#include "SizeGrid.h"
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>
#include <algorithm>
#include <cmath>

namespace
{
    constexpr int kMarginLeft = 70;
    constexpr int kMarginRight = 14;
    constexpr int kMarginTop = 30;
    constexpr int kMarginBottom = 46;
    const QColor kBarColor(31, 119, 180);
    const QColor kWarnColor(227, 160, 8);
    const QColor kGridColor(214, 214, 214);

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

    // Zahl im Gebietsschema, ohne Tausendertrennzeichen (1000 statt 1.000).
    QString Num(double v, int digits = 4)
    {
        QLocale locale;
        locale.setNumberOptions(QLocale::OmitGroupSeparator);
        return locale.toString(v, 'g', digits);
    }
    QString Micro(double meters) { return Num(meters * stosis::kSizeDisplayFactor, 4); }
    QString Unit() { return QString::fromUtf8(stosis::kSizeDisplayUnit); }

    void DrawTitle(QPainter& p, const QRect& plot, const QString& title)
    {
        QFont font = p.font();
        font.setBold(true);
        p.save();
        p.setFont(font);
        p.setPen(Qt::black);
        p.drawText(QRect(plot.left(), 4, plot.width(), kMarginTop - 8), Qt::AlignLeft | Qt::AlignVCenter, title);
        p.restore();
    }
}

CardPlotWidget::CardPlotWidget(EKind kind, QWidget* parent)
    : QWidget(parent)
    , m_kind(kind)
{
    setMinimumSize(320, 220);
    setMouseTracking(true);
}

QColor CardPlotWidget::CompoundColor(int index)
{
    static const QColor palette[] = {
        QColor(31, 119, 180), QColor(255, 127, 14), QColor(44, 160, 44),
        QColor(214, 39, 40), QColor(148, 103, 189), QColor(140, 86, 75),
        QColor(227, 119, 194), QColor(127, 127, 127),
    };
    return palette[static_cast<size_t>(std::max(index, 0)) % std::size(palette)];
}

void CardPlotWidget::ShowMessage(const QString& message)
{
    m_message = message;
    m_cards.clear();
    m_ranges.clear();
    m_times.clear();
    m_counts.clear();
    update();
}

void CardPlotWidget::SetCloud(std::vector<SCard> cards, std::vector<double> bounds, QStringList compounds)
{
    m_cards = std::move(cards);
    m_bounds = std::move(bounds);
    m_compounds = std::move(compounds);
    m_message = m_cards.empty() ? "Keine Karten in diesem Zeitschritt" : QString();
    update();
}

void CardPlotWidget::SetRanges(std::vector<SRange> ranges, size_t warnBelow)
{
    m_ranges = std::move(ranges);
    m_warnBelow = warnBelow;
    m_message = m_ranges.empty() ? "Keine Karten in diesem Zeitschritt" : QString();
    update();
}

void CardPlotWidget::SetCounts(std::vector<double> times, std::vector<double> counts, double markTime)
{
    m_times = std::move(times);
    m_counts = std::move(counts);
    m_markTime = markTime;
    m_message = m_times.empty() ? "Keine Karten gespeichert" : QString();
    update();
}

void CardPlotWidget::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.fillRect(rect(), Qt::white);
    m_hits.clear();
    const QRect plot(kMarginLeft, kMarginTop, width() - kMarginLeft - kMarginRight,
                     height() - kMarginTop - kMarginBottom);
    m_plot = plot;
    if (!m_message.isEmpty() || plot.width() < 40 || plot.height() < 40)
    {
        painter.setPen(Qt::gray);
        painter.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, m_message);
        return;
    }
    switch (m_kind)
    {
    case EKind::Cloud:  PaintCloud(painter, plot); break;
    case EKind::Ranges: PaintRanges(painter, plot); break;
    case EKind::Counts: PaintCounts(painter, plot); break;
    }
}

void CardPlotWidget::PaintCloud(QPainter& p, const QRect& plot)
{
    DrawTitle(p, plot, "Kartenwolke: Groesse und Massenstrom jeder Karte");

    // x: log10 der Groesse, auf ganze Groessenbereiche erweitert.
    double dMin = m_cards.front().size, dMax = dMin, mMin = 0.0, mMax = 0.0;
    for (const SCard& card : m_cards)
    {
        dMin = std::min(dMin, card.size);
        dMax = std::max(dMax, card.size);
        if (card.mass > 0.0)
        {
            mMin = (mMin > 0.0) ? std::min(mMin, card.mass) : card.mass;
            mMax = std::max(mMax, card.mass);
        }
    }
    double lower = dMin, upper = dMax;
    for (double b : m_bounds)
    {
        if (b <= dMin)
            lower = b;
        if (b >= dMax)
        {
            upper = b;
            break;
        }
    }
    double x0 = std::log10(lower), x1 = std::log10(upper);
    if (x1 - x0 < 0.3)
    {
        const double mid = 0.5 * (x0 + x1);
        x0 = mid - 0.15;
        x1 = mid + 0.15;
    }
    double y0 = mMin > 0.0 ? std::floor(std::log10(mMin)) : -10.0;
    double y1 = mMax > 0.0 ? std::ceil(std::log10(mMax)) : -9.0;
    if (y1 - y0 < 1.0)
    {
        y0 -= 1.0;
        y1 += 1.0;
    }
    const auto X = [&](double size) { return plot.left() + (std::log10(size) - x0) / (x1 - x0) * plot.width(); };
    const auto Y = [&](double mass) { return plot.bottom() - (std::log10(mass) - y0) / (y1 - y0) * plot.height(); };

    // Siebsatz als Streifen, Grenzen als x-Teilstriche.
    p.save();
    p.setClipRect(plot);
    for (size_t i = 0; i + 1 < m_bounds.size(); ++i)
    {
        const double left = X(m_bounds[i]), right = X(m_bounds[i + 1]);
        if (right < plot.left() || left > plot.right())
            continue;
        p.fillRect(QRectF(left, plot.top(), right - left, plot.height()), (i % 2 == 0) ? QColor(244, 244, 244) : Qt::white);
    }
    p.restore();
    p.setPen(Qt::black);
    for (double b : m_bounds)
    {
        const double x = X(b);
        if (x < plot.left() - 0.5 || x > plot.left() + plot.width() + 0.5)
            continue;
        p.setPen(kGridColor);
        p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        p.setPen(Qt::black);
        p.drawLine(QPointF(x, plot.bottom()), QPointF(x, plot.bottom() + 4));
        p.drawText(QRectF(x - 30, plot.bottom() + 5, 60, 16), Qt::AlignHCenter, Micro(b));
    }
    // Teilstriche je Dekade; bei hoechstens zwei Dekaden auch bei 2 und 5.
    const bool fine = y1 - y0 <= 2.0;
    for (int decade = static_cast<int>(y0); decade <= static_cast<int>(y1); ++decade)
        for (double factor : { 1.0, 2.0, 5.0 })
        {
            if (factor > 1.0 && !fine)
                continue;
            const double value = factor * std::pow(10.0, decade);
            const double y = Y(value);
            if (y < plot.top() - 0.5)
                continue;
            p.setPen(kGridColor);
            p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
            p.setPen(Qt::black);
            p.drawLine(QPointF(plot.left() - 4, y), QPointF(plot.left(), y));
            p.drawText(QRectF(0, y - 8, kMarginLeft - 8, 16), Qt::AlignRight | Qt::AlignVCenter, Num(value, 3));
        }
    p.setPen(Qt::black);
    p.setBrush(Qt::NoBrush);
    p.drawRect(plot);
    p.drawText(QRect(plot.left(), plot.bottom() + 22, plot.width(), 18), Qt::AlignHCenter,
               "Groesse d [" + Unit() + "] (logarithmisch; Streifen = Groessenbereiche des Siebsatzes)");
    p.save();
    p.translate(14, plot.center().y());
    p.rotate(-90);
    p.drawText(QRect(-plot.height() / 2, -10, plot.height(), 20), Qt::AlignCenter, "Massenstrom je Karte [kg/s]");
    p.restore();

    // Karten als Punkte. Ohne Rand: Karten eines Bereichs tragen dieselbe
    // Masse und liegen dicht an dicht auf einer Linie, ein heller Rand liesse
    // sie blass erscheinen.
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setClipRect(plot.adjusted(-4, -4, 4, 4));
    p.setPen(Qt::NoPen);
    for (const SCard& card : m_cards)
    {
        if (!(card.mass > 0.0))
            continue;
        const QPointF center(X(card.size), Y(card.mass));
        QColor color = CompoundColor(card.compound);
        color.setAlpha(200);
        p.setBrush(color);
        p.drawEllipse(center, 3.5, 3.5);
        const QString compound = card.compound < m_compounds.size() ? m_compounds[card.compound] : QString();
        m_hits.push_back({ QRectF(center.x() - 4, center.y() - 4, 8, 8),
            QString("d = %1 %2\n%3 kg/s%4").arg(Micro(card.size), Unit(), Num(card.mass, 4),
                compound.isEmpty() ? QString() : "\n" + compound) });
    }
    p.restore();

    // Legende der Stoffe oben rechts.
    int y = plot.top() + 6;
    for (int c = 0; c < m_compounds.size(); ++c)
    {
        const QRect textRect(plot.right() - 150, y, 140, 16);
        p.setPen(Qt::NoPen);
        p.setBrush(CompoundColor(c));
        p.drawEllipse(QPointF(textRect.left() - 8, textRect.center().y()), 4, 4);
        p.setPen(Qt::black);
        p.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, m_compounds[c]);
        y += 17;
    }
}

void CardPlotWidget::PaintRanges(QPainter& p, const QRect& plot)
{
    DrawTitle(p, plot, "Karten je Groessenbereich (Siebsatz)");
    size_t maxCards = m_warnBelow;
    for (const SRange& range : m_ranges)
        maxCards = std::max(maxCards, range.cards);
    const double yMax = 1.15 * static_cast<double>(maxCards);
    const auto Y = [&](double value) { return plot.bottom() - value / yMax * plot.height(); };

    p.setPen(Qt::black);
    for (double t : NiceTicks(0.0, yMax, 5))
    {
        const double y = Y(t);
        p.setPen(kGridColor);
        p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        p.setPen(Qt::black);
        p.drawLine(QPointF(plot.left() - 4, y), QPointF(plot.left(), y));
        p.drawText(QRectF(0, y - 8, kMarginLeft - 8, 16), Qt::AlignRight | Qt::AlignVCenter, Num(t, 6));
    }

    const double slot = static_cast<double>(plot.width()) / static_cast<double>(m_ranges.size());
    for (size_t i = 0; i < m_ranges.size(); ++i)
    {
        const SRange& range = m_ranges[i];
        const double left = plot.left() + static_cast<double>(i) * slot;
        const QRectF bar(left + 0.15 * slot, Y(static_cast<double>(range.cards)), 0.7 * slot,
                         plot.bottom() - Y(static_cast<double>(range.cards)));
        const bool warn = range.cards > 0 && range.cards < m_warnBelow;
        if (range.cards > 0)
        {
            p.fillRect(bar, warn ? kWarnColor : kBarColor);
            p.setPen(Qt::black);
            p.drawText(QRectF(left, bar.top() - 17, slot, 16), Qt::AlignHCenter | Qt::AlignBottom,
                       QString::number(range.cards));
        }
        const QString label = Micro(range.lower) + "-" + Micro(range.upper);
        const QString share = QLocale().toString(100.0 * range.massShare, 'f', range.massShare < 0.001 ? 3 : 1) + " %";
        p.setPen(Qt::black);
        p.drawText(QRectF(left, plot.bottom() + 4, slot, 16), Qt::AlignHCenter, slot > 60 ? label : QString());
        p.setPen(Qt::darkGray);
        p.drawText(QRectF(left, plot.bottom() + 19, slot, 16), Qt::AlignHCenter, slot > 45 ? share : QString());
        QString tip = QString("%1 %2\n%3 Karten\n%4 der Masse").arg(label, Unit()).arg(range.cards).arg(share);
        if (warn)
            tip += QString("\nunter %1 Karten: Unsicherheit etwa 1/Wurzel(n) = %2 %")
                .arg(m_warnBelow).arg(QLocale().toString(100.0 / std::sqrt(static_cast<double>(range.cards)), 'f', 0));
        m_hits.push_back({ QRectF(left, plot.top(), slot, plot.height() + 36), tip });
    }

    // Warnschwelle als gestrichelte Linie, erklaert in der Legende oben rechts
    // (ueber dem hoechsten Balken ist dort immer Platz).
    const QPen warnPen(kWarnColor.darker(130), 1.2, Qt::DashLine);
    const double yWarn = Y(static_cast<double>(m_warnBelow));
    p.setPen(warnPen);
    p.drawLine(QPointF(plot.left(), yWarn), QPointF(plot.right(), yWarn));
    const QString warnText = QString("Warnschwelle: %1 Karten je Bereich").arg(m_warnBelow);
    const double textWidth = p.fontMetrics().horizontalAdvance(warnText);
    const double legendX = plot.right() - textWidth - 36;
    const double legendY = plot.top() + 12;
    p.drawLine(QPointF(legendX, legendY), QPointF(legendX + 22, legendY));
    p.setPen(Qt::black);
    p.drawText(QRectF(legendX + 28, legendY - 8, textWidth + 4, 16), Qt::AlignLeft | Qt::AlignVCenter, warnText);

    p.setPen(Qt::black);
    p.setBrush(Qt::NoBrush);
    p.drawRect(plot);
    p.save();
    p.translate(14, plot.center().y());
    p.rotate(-90);
    p.drawText(QRect(-plot.height() / 2, -10, plot.height(), 20), Qt::AlignCenter, "Karten");
    p.restore();
}

void CardPlotWidget::PaintCounts(QPainter& p, const QRect& plot)
{
    DrawTitle(p, plot, "Kartenzahl des Stroms je Zeitschritt (Lauf 1)");
    const double x0 = m_times.front();
    const double x1 = (m_times.back() > x0) ? m_times.back() : x0 + 1.0;
    double maxCount = 1.0;
    for (double c : m_counts)
        maxCount = std::max(maxCount, c);
    const double yMax = 1.1 * maxCount;
    const auto X = [&](double t) { return plot.left() + (t - x0) / (x1 - x0) * plot.width(); };
    const auto Y = [&](double value) { return plot.bottom() - value / yMax * plot.height(); };

    for (double t : NiceTicks(0.0, yMax, 4))
    {
        const double y = Y(t);
        p.setPen(kGridColor);
        p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        p.setPen(Qt::black);
        p.drawLine(QPointF(plot.left() - 4, y), QPointF(plot.left(), y));
        p.drawText(QRectF(0, y - 8, kMarginLeft - 8, 16), Qt::AlignRight | Qt::AlignVCenter, Num(t, 6));
    }
    p.setPen(Qt::black);
    for (double t : NiceTicks(x0, x1, 8))
    {
        const double x = X(t);
        p.drawLine(QPointF(x, plot.bottom()), QPointF(x, plot.bottom() + 4));
        p.drawText(QRectF(x - 40, plot.bottom() + 5, 80, 16), Qt::AlignHCenter, Num(t, 6));
    }
    p.drawText(QRect(plot.left(), plot.bottom() + 22, plot.width(), 18), Qt::AlignHCenter, "Zeit [s]");

    QPainterPath path;
    for (size_t i = 0; i < m_times.size() && i < m_counts.size(); ++i)
    {
        const QPointF point(X(m_times[i]), Y(m_counts[i]));
        if (i == 0)
            path.moveTo(point);
        else
            path.lineTo(point);
    }
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setClipRect(plot);
    p.setPen(QPen(kBarColor, 2.0));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);

    // Markierung des gewaehlten Zeitpunkts.
    const auto nearest = std::lower_bound(m_times.begin(), m_times.end(), m_markTime);
    const size_t index = std::min<size_t>(static_cast<size_t>(nearest - m_times.begin()), m_times.size() - 1);
    const double xMark = X(m_times[index]);
    p.setPen(QPen(Qt::darkGray, 1.0, Qt::DashLine));
    p.drawLine(QPointF(xMark, plot.top()), QPointF(xMark, plot.bottom()));
    p.setPen(QPen(Qt::white, 1.5));
    p.setBrush(kBarColor);
    p.drawEllipse(QPointF(xMark, Y(m_counts[index])), 4.5, 4.5);
    p.restore();

    p.setPen(Qt::black);
    p.setBrush(Qt::NoBrush);
    p.drawRect(plot);
    p.save();
    p.translate(14, plot.center().y());
    p.rotate(-90);
    p.drawText(QRect(-plot.height() / 2, -10, plot.height(), 20), Qt::AlignCenter, "Karten");
    p.restore();
}

void CardPlotWidget::mouseMoveEvent(QMouseEvent* event)
{
    const QPointF pos = event->position();
    // Kartenzahl: naechster Zeitschritt unter der Maus.
    if (m_kind == EKind::Counts && !m_times.empty() && m_plot.contains(pos.toPoint()))
    {
        const double x0 = m_times.front();
        const double x1 = (m_times.back() > x0) ? m_times.back() : x0 + 1.0;
        const double t = x0 + (pos.x() - m_plot.left()) / m_plot.width() * (x1 - x0);
        size_t i = static_cast<size_t>(std::lower_bound(m_times.begin(), m_times.end(), t) - m_times.begin());
        if (i >= m_times.size() || (i > 0 && t - m_times[i - 1] < m_times[i] - t))
            i = (i > 0) ? i - 1 : 0;
        if (i < m_counts.size())
            QToolTip::showText(event->globalPosition().toPoint(),
                QString("t = %1 s\n%2 Karten").arg(Num(m_times[i], 6)).arg(m_counts[i], 0, 'f', 0), this);
        return;
    }
    const SHit* best = nullptr;
    double bestDistance = 1e300;
    for (const SHit& hit : m_hits)
    {
        if (!hit.rect.contains(pos))
            continue;
        const QPointF d = hit.rect.center() - pos;
        const double distance = d.x() * d.x() + d.y() * d.y();
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = &hit;
        }
    }
    if (best)
        QToolTip::showText(event->globalPosition().toPoint(), best->text, this);
    else
        QToolTip::hideText();
}
