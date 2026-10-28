#include "CardPlotWidget.h"
#include "SizeGrid.h"
#include <QFontMetrics>
#include <QLinearGradient>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace
{
    constexpr int kMarginLeft = 70;
    constexpr int kMarginRight = 14;
    const QColor kBarColor(31, 119, 180);
    const QColor kWarnColor(227, 160, 8);
    const QColor kGridColor(214, 214, 214);
    const QColor kStripeColor(244, 244, 244);
    const QColor kReferenceColor(204, 51, 17);
    const QColor kMinLimitColor(118, 42, 131); // lila wie die Mindestzahl in Abb. 13.19.1
    const QColor kMaxLimitColor(204, 51, 17);

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
    QString Percent(double share, int decimals = 1) { return QLocale().toString(100.0 * share, 'f', decimals) + " %"; }

    // Titel im oberen Rand; reserveRight Pixel rechts bleiben frei (Farbskala).
    void DrawTitle(QPainter& p, const QRect& plot, const QString& title, int reserveRight = 0)
    {
        QFont font = p.font();
        font.setBold(true);
        p.save();
        p.setFont(font);
        p.setPen(Qt::black);
        p.drawText(QRect(plot.left(), 2, plot.width() - reserveRight, plot.top() - 4), Qt::AlignLeft | Qt::AlignVCenter, title);
        p.restore();
    }

    void DrawYTitle(QPainter& p, const QRect& plot, const QString& title)
    {
        p.save();
        p.setPen(Qt::black);
        p.translate(14, plot.center().y());
        p.rotate(-90);
        p.drawText(QRect(-plot.height() / 2, -10, plot.height(), 20), Qt::AlignCenter, title);
        p.restore();
    }

    // Waagerechte Gitterlinien und Beschriftung der y-Achse von 0 bis yMax.
    void DrawYAxis(QPainter& p, const QRect& plot, double yMax, int wanted, double labelFactor = 1.0,
                   const QString& suffix = QString())
    {
        for (double t : NiceTicks(0.0, yMax, wanted))
        {
            const double y = plot.bottom() - t / yMax * plot.height();
            p.setPen(kGridColor);
            p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
            p.setPen(Qt::black);
            p.drawLine(QPointF(plot.left() - 4, y), QPointF(plot.left(), y));
            p.drawText(QRectF(0, y - 8, kMarginLeft - 8, 16), Qt::AlignRight | Qt::AlignVCenter, Num(t * labelFactor, 6) + suffix);
        }
    }

    void DrawFrame(QPainter& p, const QRect& plot)
    {
        p.setPen(Qt::black);
        p.setBrush(Qt::NoBrush);
        p.drawRect(plot);
    }

    struct SLegendItem
    {
        enum EStyle { Bar, Line, Dash } style;
        QColor color;
        QString text;
    };

    // Legende oben rechts (oder oben links) im Diagramm auf weissem Grund.
    void DrawLegend(QPainter& p, const QRect& plot, const std::vector<SLegendItem>& items, bool left = false)
    {
        if (items.empty())
            return;
        const QFontMetrics metrics = p.fontMetrics();
        int textWidth = 0;
        for (const SLegendItem& item : items)
            textWidth = std::max(textWidth, metrics.horizontalAdvance(item.text));
        const double rowHeight = 17.0;
        const double boxWidth = textWidth + 40.0;
        const QRectF box(left ? plot.left() + 6.0 : plot.right() - boxWidth - 6.0, plot.top() + 6.0, boxWidth,
                         rowHeight * static_cast<double>(items.size()) + 6.0);
        p.save();
        p.setPen(QColor(200, 200, 200));
        p.setBrush(QColor(255, 255, 255, 235));
        p.drawRect(box);
        double y = box.top() + 3.0 + 0.5 * rowHeight;
        for (const SLegendItem& item : items)
        {
            if (item.style == SLegendItem::Bar)
                p.fillRect(QRectF(box.left() + 6.0, y - 5.0, 20.0, 10.0), item.color);
            else
            {
                p.setPen(QPen(item.color, 2.0, item.style == SLegendItem::Dash ? Qt::DashLine : Qt::SolidLine));
                p.drawLine(QPointF(box.left() + 6.0, y), QPointF(box.left() + 26.0, y));
            }
            p.setPen(Qt::black);
            p.drawText(QRectF(box.left() + 32.0, y - 0.5 * rowHeight, textWidth + 4.0, rowHeight),
                       Qt::AlignLeft | Qt::AlignVCenter, item.text);
            y += rowHeight;
        }
        p.restore();
    }

    // Farbe der Zeitkarte: hellblau (kleine Werte) bis dunkelblau (1);
    // Zellen ohne Masse bleiben weiss.
    QColor RampColor(double t)
    {
        static const QColor stops[] = { QColor(222, 235, 247), QColor(107, 174, 214), QColor(33, 113, 181),
                                        QColor(8, 48, 107) };
        const double x = std::clamp(t, 0.0, 1.0) * 3.0;
        const int i = std::min(2, static_cast<int>(x));
        const double f = x - i;
        const QColor& a = stops[i];
        const QColor& b = stops[i + 1];
        return QColor::fromRgbF(static_cast<float>(a.redF() + f * (b.redF() - a.redF())),
                                static_cast<float>(a.greenF() + f * (b.greenF() - a.greenF())),
                                static_cast<float>(a.blueF() + f * (b.blueF() - a.blueF())));
    }

    // Fester Zufallswert in [0, 1) je Karte (Ganzzahl-Hash der Kartennummer):
    // die Punkte im Kartenteppich liegen bei jedem Neuzeichnen gleich, ohne
    // ein regelmaessiges Muster, das eine Struktur vortaeuschen wuerde.
    double Jitter(size_t k)
    {
        uint64_t z = static_cast<uint64_t>(k) + 0x9E3779B97F4A7C15ull;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z ^= z >> 31;
        return static_cast<double>(z >> 11) / 9007199254740992.0;
    }

    // Wert einer Kurve bei size, linear in ln d zwischen den Stuetzstellen.
    double ValueAtSize(const std::vector<double>& sizes, const std::vector<double>& values, double size)
    {
        if (sizes.empty() || values.size() < sizes.size())
            return 0.0;
        const size_t i = static_cast<size_t>(std::upper_bound(sizes.begin(), sizes.end(), size) - sizes.begin());
        if (i == 0)
            return values.front();
        if (i >= sizes.size())
            return values[sizes.size() - 1];
        const double f = std::log(size / sizes[i - 1]) / std::log(sizes[i] / sizes[i - 1]);
        return values[i - 1] + f * (values[i] - values[i - 1]);
    }
}

CardPlotWidget::CardPlotWidget(EKind kind, QWidget* parent)
    : QWidget(parent)
    , m_kind(kind)
{
    if (m_kind == EKind::Rug)
    {
        setMinimumSize(320, 72);
        setMaximumHeight(90);
    }
    else
        setMinimumSize(320, 200);
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

int CardPlotWidget::TopMargin() const { return m_kind == EKind::Rug ? 22 : 30; }
int CardPlotWidget::BottomMargin() const { return m_kind == EKind::Rug ? 8 : 46; }

void CardPlotWidget::ShowMessage(const QString& message)
{
    m_message = message;
    m_cards.clear();
    m_classes.clear();
    m_rangeLimits.clear();
    m_cumulative = {};
    m_refSizes.clear();
    m_refDensity.clear();
    m_refCumulative.clear();
    m_mapTimes.clear();
    m_mapValues.clear();
    m_times.clear();
    m_counts.clear();
    update();
}

void CardPlotWidget::SetSizeAxis(double sizeMin, double sizeMax, std::vector<double> rangeBounds)
{
    if (!(sizeMin > 0.0) || !(sizeMax > sizeMin))
    {
        sizeMin = 1e-6;
        sizeMax = 1e-3;
    }
    m_sizeMin = sizeMin;
    m_sizeMax = sizeMax;
    m_rangeBounds = std::move(rangeBounds);
    update();
}

void CardPlotWidget::SetDistribution(std::vector<stosis::SCardClass> classes, stosis::SCardCumulative cumulative,
                                     std::vector<double> refSizes, std::vector<double> refDensity, QString refLabel)
{
    m_classes = std::move(classes);
    m_cumulative = std::move(cumulative);
    m_refSizes = std::move(refSizes);
    m_refDensity = std::move(refDensity);
    m_refCumulative = stosis::CumulativeFromDensity(m_refSizes, m_refDensity);
    m_refLabel = std::move(refLabel);
    m_message = m_classes.empty() ? "Keine Karten zu diesem Zeitpunkt" : QString();
    update();
}

void CardPlotWidget::SetCumulativeMode(bool cumulative)
{
    m_cumulativeMode = cumulative;
    update();
}

void CardPlotWidget::SetCards(std::vector<SCard> cards, QStringList compounds)
{
    m_cards = std::move(cards);
    m_compounds = std::move(compounds);
    m_message = m_cards.empty() ? "Keine Karten zu diesem Zeitpunkt" : QString();
    update();
}

void CardPlotWidget::SetClasses(std::vector<stosis::SCardClass> classes, size_t warnBelow)
{
    m_classes = std::move(classes);
    m_warnBelow = warnBelow;
    m_message = m_classes.empty() ? "Keine Karten zu diesem Zeitpunkt" : QString();
    update();
}

void CardPlotWidget::SetRangeLimits(std::vector<stosis::SSliceRange> limits, int classesPerRange)
{
    m_rangeLimits = std::move(limits);
    m_classesPerRange = std::max(1, classesPerRange);
    update();
}

void CardPlotWidget::SetTimeMap(std::vector<double> columnTimes, double columnWidth, double endTime,
                                std::vector<double> bounds, std::vector<double> values)
{
    m_mapTimes = std::move(columnTimes);
    m_mapColumnWidth = columnWidth;
    m_timeEnd = endTime;
    m_mapBounds = std::move(bounds);
    m_mapValues = std::move(values);
    m_mapMax = 0.0;
    for (double v : m_mapValues)
        m_mapMax = std::max(m_mapMax, v);
    const size_t numClasses = m_mapBounds.size() > 1 ? m_mapBounds.size() - 1 : 0;
    const bool valid = numClasses > 0 && m_mapValues.size() == m_mapTimes.size() * numClasses && m_mapMax > 0.0;
    m_message = valid ? QString() : QString("Keine Karten gespeichert");
    update();
}

void CardPlotWidget::SetCounts(std::vector<double> times, std::vector<double> counts)
{
    m_times = std::move(times);
    m_counts = std::move(counts);
    m_message = m_times.empty() ? "Keine Karten gespeichert" : QString();
    update();
}

void CardPlotWidget::SetMarkTime(double time)
{
    m_markTime = time;
    update();
}

void CardPlotWidget::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.fillRect(rect(), Qt::white);
    m_hits.clear();
    const QRect plot(kMarginLeft, TopMargin(), width() - kMarginLeft - kMarginRight,
                     height() - TopMargin() - BottomMargin());
    m_plot = plot;
    if (!m_message.isEmpty() || plot.width() < 40 || plot.height() < 20)
    {
        painter.setPen(Qt::gray);
        painter.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, m_message);
        return;
    }
    switch (m_kind)
    {
    case EKind::Distribution:
        if (m_cumulativeMode)
            PaintCumulative(painter, plot);
        else
            PaintDensity(painter, plot);
        break;
    case EKind::Rug:     PaintRug(painter, plot); break;
    case EKind::Classes: PaintClasses(painter, plot); break;
    case EKind::TimeMap: PaintTimeMap(painter, plot); break;
    case EKind::Counts:  PaintCounts(painter, plot); break;
    case EKind::Cloud:   PaintCloud(painter, plot); break;
    }
}

double CardPlotWidget::SizeToX(double size, const QRect& plot) const
{
    const double x0 = std::log10(m_sizeMin), x1 = std::log10(m_sizeMax);
    return plot.left() + (std::log10(std::max(size, 1e-30)) - x0) / (x1 - x0) * plot.width();
}

double CardPlotWidget::XToSize(double x, const QRect& plot) const
{
    const double x0 = std::log10(m_sizeMin), x1 = std::log10(m_sizeMax);
    return std::pow(10.0, x0 + (x - plot.left()) / plot.width() * (x1 - x0));
}

// d-Achse: Groessenbereiche des Siebsatzes als abwechselnde Streifen, ihre
// Grenzen als Gitterlinien und (labels) als beschriftete Teilstriche.
void CardPlotWidget::PaintSizeAxis(QPainter& p, const QRect& plot, bool labels) const
{
    p.save();
    p.setClipRect(plot);
    for (size_t i = 0; i + 1 < m_rangeBounds.size(); i += 2)
    {
        const double left = SizeToX(m_rangeBounds[i], plot), right = SizeToX(m_rangeBounds[i + 1], plot);
        p.fillRect(QRectF(left, plot.top(), right - left, plot.height()), kStripeColor);
    }
    p.restore();
    for (double b : m_rangeBounds)
    {
        const double x = SizeToX(b, plot);
        if (x < plot.left() - 0.5 || x > plot.right() + 1.5)
            continue;
        p.setPen(kGridColor);
        p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        if (!labels)
            continue;
        p.setPen(Qt::black);
        p.drawLine(QPointF(x, plot.bottom()), QPointF(x, plot.bottom() + 4));
        p.drawText(QRectF(x - 30, plot.bottom() + 5, 60, 16), Qt::AlignHCenter, Micro(b));
    }
    if (labels)
    {
        p.setPen(Qt::black);
        p.drawText(QRect(plot.left(), plot.bottom() + 22, plot.width(), 18), Qt::AlignHCenter,
                   "Groesse d [" + Unit() + "] (logarithmisch; Streifen = Groessenbereiche des Siebsatzes)");
    }
}

QString CardPlotWidget::ClassTip(const stosis::SCardClass& c) const
{
    QString tip = QString("%1-%2 %3\n%4 Karten\n%5 der Masse\nq3*(ln d) = %6")
        .arg(Micro(c.lower), Micro(c.upper), Unit()).arg(c.cards)
        .arg(Percent(c.massShare, c.massShare < 0.001 ? 3 : 1)).arg(Num(c.GetDensity(), 3));
    if (c.cards > 0 && c.cards < m_warnBelow)
        tip += QString("\nunter %1 Karten: Unsicherheit etwa 1/Wurzel(n) = %2 %").arg(m_warnBelow)
            .arg(QLocale().toString(100.0 / std::sqrt(static_cast<double>(c.cards)), 'f', 0));
    for (const stosis::SSliceRange& r : m_rangeLimits)
        if (c.lower >= r.lower * (1.0 - 1e-9) && c.upper <= r.upper * (1.0 + 1e-9))
        {
            tip += QString("\nBereich %1-%2 %3: %4 Karten, Grenzen %5 bis %6 (Sollzahl %7)")
                .arg(Micro(r.lower), Micro(r.upper), Unit()).arg(r.cards).arg(r.minCards).arg(r.maxCards)
                .arg(Num(r.target, 4));
            break;
        }
    return tip;
}

void CardPlotWidget::PaintDensity(QPainter& p, const QRect& plot)
{
    DrawTitle(p, plot, QString("A: Dichte q3*(ln d) aus den Karten - %1 Klassen").arg(m_classes.size()));
    double maxValue = 0.0;
    for (const stosis::SCardClass& c : m_classes)
        maxValue = std::max(maxValue, c.GetDensity());
    for (size_t i = 0; i < m_refSizes.size() && i < m_refDensity.size(); ++i)
        if (m_refSizes[i] >= m_sizeMin && m_refSizes[i] <= m_sizeMax)
            maxValue = std::max(maxValue, m_refDensity[i]);
    // Oben Platz fuer die Legende lassen.
    const double yMax = maxValue > 0.0 ? 1.3 * maxValue : 1.0;
    const auto Y = [&](double value) { return plot.bottom() - value / yMax * plot.height(); };

    PaintSizeAxis(p, plot, true);
    DrawYAxis(p, plot, yMax, 5);
    for (const stosis::SCardClass& c : m_classes)
    {
        const double left = SizeToX(c.lower, plot), right = SizeToX(c.upper, plot);
        const double gap = right - left > 4.0 ? 1.0 : 0.0;
        if (c.cards > 0)
        {
            const double top = Y(c.GetDensity());
            p.fillRect(QRectF(left + gap, top, right - left - 2.0 * gap, plot.bottom() - top), kBarColor);
        }
        m_hits.push_back({ QRectF(left, plot.top(), right - left, plot.height()), ClassTip(c) });
    }
    const bool reference = m_refSizes.size() >= 2 && m_refDensity.size() == m_refSizes.size();
    if (reference)
    {
        QPainterPath path;
        for (size_t i = 0; i < m_refSizes.size(); ++i)
        {
            const QPointF point(SizeToX(m_refSizes[i], plot), Y(m_refDensity[i]));
            if (i == 0)
                path.moveTo(point);
            else
                path.lineTo(point);
        }
        p.save();
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setClipRect(plot);
        p.setPen(QPen(kReferenceColor, 2.0));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
        p.restore();
    }
    DrawFrame(p, plot);
    DrawYTitle(p, plot, "q3*(ln d)");
    std::vector<SLegendItem> legend = { { SLegendItem::Bar, kBarColor, "Karten (Lauf 1)" } };
    if (reference)
        legend.push_back({ SLegendItem::Line, kReferenceColor, m_refLabel });
    DrawLegend(p, plot, legend);
}

void CardPlotWidget::PaintCumulative(QPainter& p, const QRect& plot)
{
    DrawTitle(p, plot, "B: Summenkurve Q3 direkt aus den Karten (ohne Klassen)");
    const auto Y = [&](double q) { return plot.bottom() - q * plot.height(); };
    PaintSizeAxis(p, plot, true);
    DrawYAxis(p, plot, 1.0, 5, 100.0, " %");
    const bool reference = m_refSizes.size() >= 2 && m_refCumulative.size() == m_refSizes.size();

    // d10, d50, d90 der Karten: gestrichelt von der y-Achse zur Kurve und
    // hinunter zur d-Achse; die Werte stehen in der Legende.
    QString cardText = "Karten (Lauf 1):", refText = m_refLabel + ":";
    p.save();
    p.setPen(QPen(Qt::darkGray, 1.0, Qt::DashLine));
    for (int percent : { 10, 50, 90 })
    {
        const double f = 0.01 * percent;
        const double d = stosis::GetCardQuantile(m_cumulative, f);
        const double x = SizeToX(d, plot);
        p.drawLine(QPointF(plot.left(), Y(f)), QPointF(x, Y(f)));
        p.drawLine(QPointF(x, Y(f)), QPointF(x, plot.bottom()));
        p.drawText(QRectF(x + 3, plot.bottom() - 18, 40, 16), Qt::AlignLeft | Qt::AlignVCenter, QString("d%1").arg(percent));
        cardText += QString("  d%1 = %2").arg(percent).arg(Micro(d));
        if (reference)
            refText += QString("  d%1 = %2").arg(percent).arg(Micro(stosis::GetCurveQuantile(m_refSizes, m_refCumulative, f)));
    }
    p.restore();

    // Treppe: jede Karte hebt Q3 um ihren Massenanteil.
    QPainterPath cards;
    cards.moveTo(plot.left(), Y(0.0));
    double previous = 0.0;
    for (size_t k = 0; k < m_cumulative.sizes.size(); ++k)
    {
        const double x = SizeToX(m_cumulative.sizes[k], plot);
        cards.lineTo(x, Y(previous));
        cards.lineTo(x, Y(m_cumulative.q3[k]));
        previous = m_cumulative.q3[k];
    }
    cards.lineTo(plot.right(), Y(previous));
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setClipRect(plot);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(kBarColor, 2.0));
    p.drawPath(cards);
    if (reference)
    {
        QPainterPath path;
        for (size_t i = 0; i < m_refSizes.size(); ++i)
        {
            const QPointF point(SizeToX(m_refSizes[i], plot), Y(m_refCumulative[i]));
            if (i == 0)
                path.moveTo(point);
            else
                path.lineTo(point);
        }
        p.setPen(QPen(kReferenceColor, 1.5));
        p.drawPath(path);
    }
    p.restore();

    DrawFrame(p, plot);
    DrawYTitle(p, plot, "Q3");
    // Links oben ist die Kurve nahe 0, dort verdeckt die Legende nichts.
    std::vector<SLegendItem> legend = { { SLegendItem::Line, kBarColor, cardText + " " + Unit() } };
    if (reference)
        legend.push_back({ SLegendItem::Line, kReferenceColor, refText + " " + Unit() });
    DrawLegend(p, plot, legend, true);
}

void CardPlotWidget::PaintRug(QPainter& p, const QRect& plot)
{
    DrawTitle(p, plot, "D: Kartenteppich - jede Karte ein Punkt (Hoehe zufaellig, nur gegen Ueberdecken)");
    PaintSizeAxis(p, plot, false);
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setClipRect(plot);
    p.setPen(Qt::NoPen);
    for (size_t k = 0; k < m_cards.size(); ++k)
    {
        const SCard& card = m_cards[k];
        const QPointF center(SizeToX(card.size, plot), plot.top() + 4.0 + Jitter(k) * (plot.height() - 8.0));
        QColor color = CompoundColor(card.compound);
        color.setAlpha(150);
        p.setBrush(color);
        p.drawEllipse(center, 2.0, 2.0);
        const QString compound = card.compound < m_compounds.size() ? m_compounds[card.compound] : QString();
        m_hits.push_back({ QRectF(center.x() - 3, center.y() - 3, 6, 6),
            QString("Karte %1\nd = %2 %3\n%4 kg/s%5").arg(k + 1).arg(Micro(card.size), Unit(), Num(card.mass, 4),
                compound.isEmpty() ? QString() : "\n" + compound) });
    }
    p.restore();
    DrawFrame(p, plot);
}

void CardPlotWidget::PaintClasses(QPainter& p, const QRect& plot)
{
    DrawTitle(p, plot, QString("C: Karten je Klasse - %1 Klassen").arg(m_classes.size()));
    // Grenzen je Bereich, umgerechnet auf eine Klasse.
    const double perClass = 1.0 / static_cast<double>(m_classesPerRange);
    double top = static_cast<double>(m_warnBelow);
    for (const stosis::SCardClass& c : m_classes)
        top = std::max(top, static_cast<double>(c.cards));
    for (const stosis::SSliceRange& r : m_rangeLimits)
        top = std::max(top, static_cast<double>(r.minCards) * perClass);
    // Obergrenzen weit ueber den Balken staucht die Achse nicht; sie stehen
    // dann als Pfeil am oberen Rand.
    const double barsTop = top;
    for (const stosis::SSliceRange& r : m_rangeLimits)
        if (static_cast<double>(r.maxCards) * perClass <= 1.6 * barsTop)
            top = std::max(top, static_cast<double>(r.maxCards) * perClass);
    const double yMax = 1.35 * top;
    const auto Y = [&](double value) { return plot.bottom() - value / yMax * plot.height(); };

    PaintSizeAxis(p, plot, true);
    DrawYAxis(p, plot, yMax, 4);
    for (const stosis::SCardClass& c : m_classes)
    {
        const double left = SizeToX(c.lower, plot), right = SizeToX(c.upper, plot);
        const double gap = right - left > 4.0 ? 1.0 : 0.0;
        const bool warn = c.cards > 0 && c.cards < m_warnBelow;
        if (c.cards > 0)
        {
            const double top = Y(static_cast<double>(c.cards));
            p.fillRect(QRectF(left + gap, top, right - left - 2.0 * gap, plot.bottom() - top), warn ? kWarnColor : kBarColor);
            if (right - left >= 24.0)
            {
                p.setPen(Qt::black);
                p.drawText(QRectF(left, top - 17, right - left, 16), Qt::AlignHCenter | Qt::AlignBottom,
                           QString::number(c.cards));
            }
        }
        m_hits.push_back({ QRectF(left, plot.top(), right - left, plot.height()), ClassTip(c) });
    }

    const QColor warnLine = kWarnColor.darker(130);
    p.setPen(QPen(warnLine, 1.2, Qt::DashLine));
    const double yWarn = Y(static_cast<double>(m_warnBelow));
    p.drawLine(QPointF(plot.left(), yWarn), QPointF(plot.right(), yWarn));

    // Grenzen je Stromscheibe: je Bereich eine Linie fuer N_min (lila) und
    // fuer die Obergrenze (rot gestrichelt).
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    for (const stosis::SSliceRange& r : m_rangeLimits)
    {
        const double left = std::max(SizeToX(r.lower, plot), static_cast<double>(plot.left())) + 2.0;
        const double right = std::min(SizeToX(r.upper, plot), static_cast<double>(plot.right())) - 2.0;
        if (r.minCards > 0)
        {
            const double y = Y(static_cast<double>(r.minCards) * perClass);
            p.setPen(QPen(kMinLimitColor, 2.0));
            p.drawLine(QPointF(left, y), QPointF(right, y));
        }
        const double upper = static_cast<double>(r.maxCards) * perClass;
        p.setPen(QPen(kMaxLimitColor, 2.0, Qt::DashLine));
        if (upper <= yMax)
            p.drawLine(QPointF(left, Y(upper)), QPointF(right, Y(upper)));
        else
        {
            // Ueber dem oberen Rand: Pfeil nach oben mit dem Wert, links im
            // Bereich (rechts oben steht die Legende).
            const double x = left + 10.0;
            p.setPen(QPen(kMaxLimitColor, 1.5));
            p.drawLine(QPointF(x, plot.top() + 16.0), QPointF(x, plot.top() + 3.0));
            p.drawLine(QPointF(x - 4.0, plot.top() + 7.0), QPointF(x, plot.top() + 3.0));
            p.drawLine(QPointF(x + 4.0, plot.top() + 7.0), QPointF(x, plot.top() + 3.0));
            // Nur die Zahl (rot wie "Obergrenze" in der Legende), damit sie
            // auch in schmalen Fenstern neben die Legende passt.
            p.drawText(QRectF(x + 6.0, plot.top() + 2.0, 60.0, 16.0), Qt::AlignLeft | Qt::AlignVCenter, Num(upper, 4));
        }
    }
    p.restore();

    DrawFrame(p, plot);
    DrawYTitle(p, plot, "Karten");
    std::vector<SLegendItem> legend = { { SLegendItem::Bar, kBarColor, "Karten je Klasse" },
        { SLegendItem::Bar, kWarnColor, QString("unter %1 Karten (Schwelle gestrichelt)").arg(m_warnBelow) } };
    if (!m_rangeLimits.empty())
    {
        const QString perClassText = m_classesPerRange > 1 ? QString(" / %1 je Klasse").arg(m_classesPerRange) : QString();
        legend.push_back({ SLegendItem::Line, kMinLimitColor, "Untergrenze N_min" + perClassText });
        legend.push_back({ SLegendItem::Dash, kMaxLimitColor, "Obergrenze" + perClassText });
    }
    DrawLegend(p, plot, legend);
}

void CardPlotWidget::PaintTimeMap(QPainter& p, const QRect& plot)
{
    DrawTitle(p, plot, "E: Verteilung ueber der Zeit - Farbe = q3*(ln d) aus den Karten (Lauf 1)", 250);
    const double timeEnd = m_timeEnd > 0.0 ? m_timeEnd : 1.0;
    const auto X = [&](double t) { return plot.left() + t / timeEnd * plot.width(); };
    const double y0 = std::log10(m_sizeMin), y1 = std::log10(m_sizeMax);
    const auto Y = [&](double size) { return plot.bottom() - (std::log10(size) - y0) / (y1 - y0) * plot.height(); };
    const size_t numClasses = m_mapBounds.size() - 1;

    // Je gespeichertem Zeitpunkt eine Spalte, je Klasse eine Zelle; die
    // Zellen ueberlappen um einen Bruchteil eines Pixels, damit keine
    // hellen Fugen bleiben.
    p.save();
    p.setClipRect(plot);
    for (size_t j = 0; j < m_mapTimes.size(); ++j)
    {
        const double left = X(std::max(0.0, m_mapTimes[j] - 0.5 * m_mapColumnWidth));
        const double right = X(std::min(timeEnd, m_mapTimes[j] + 0.5 * m_mapColumnWidth));
        for (size_t i = 0; i < numClasses; ++i)
        {
            const double v = m_mapValues[j * numClasses + i];
            if (!(v > 0.0))
                continue;
            const double top = Y(m_mapBounds[i + 1]), bottom = Y(m_mapBounds[i]);
            p.fillRect(QRectF(left, top, right - left + 0.6, bottom - top + 0.6), RampColor(v / m_mapMax));
        }
    }
    p.restore();

    for (double b : m_rangeBounds)
    {
        const double y = Y(b);
        if (y < plot.top() - 0.5 || y > plot.bottom() + 0.5)
            continue;
        p.setPen(kGridColor);
        p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        p.setPen(Qt::black);
        p.drawLine(QPointF(plot.left() - 4, y), QPointF(plot.left(), y));
        p.drawText(QRectF(0, y - 8, kMarginLeft - 8, 16), Qt::AlignRight | Qt::AlignVCenter, Micro(b));
    }
    p.setPen(Qt::black);
    for (double t : NiceTicks(0.0, timeEnd, 8))
    {
        const double x = X(t);
        p.drawLine(QPointF(x, plot.bottom()), QPointF(x, plot.bottom() + 4));
        p.drawText(QRectF(x - 40, plot.bottom() + 5, 80, 16), Qt::AlignHCenter, Num(t, 6));
    }
    p.drawText(QRect(plot.left(), plot.bottom() + 22, plot.width(), 18), Qt::AlignHCenter, "Zeit [s]");

    // Gewaehlter Zeitpunkt.
    const double xMark = X(m_markTime);
    p.save();
    p.setClipRect(plot);
    p.setPen(QPen(QColor(204, 51, 17), 1.5, Qt::DashLine));
    p.drawLine(QPointF(xMark, plot.top()), QPointF(xMark, plot.bottom()));
    p.restore();
    DrawFrame(p, plot);
    DrawYTitle(p, plot, "Groesse d [" + Unit() + "]");

    // Farbskala im oberen Rand rechts.
    const QRectF bar(plot.right() - 160.0, 9.0, 110.0, 12.0);
    QLinearGradient gradient(bar.topLeft(), bar.topRight());
    for (int s = 0; s <= 10; ++s)
        gradient.setColorAt(0.1 * s, RampColor(0.1 * s));
    p.fillRect(bar, gradient);
    p.setPen(Qt::gray);
    p.setBrush(Qt::NoBrush);
    p.drawRect(bar);
    p.setPen(Qt::black);
    p.drawText(QRectF(bar.left() - 134.0, 3.0, 130.0, 24.0), Qt::AlignRight | Qt::AlignVCenter, "q3*(ln d):  0");
    p.drawText(QRectF(bar.right() + 4.0, 3.0, 60.0, 24.0), Qt::AlignLeft | Qt::AlignVCenter, Num(m_mapMax, 3));
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

    DrawYAxis(p, plot, yMax, 4);
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
    const size_t index = std::min<size_t>(static_cast<size_t>(nearest - m_times.begin()),
                                          std::min(m_times.size(), m_counts.size()) - 1);
    const double xMark = X(m_times[index]);
    p.setPen(QPen(Qt::darkGray, 1.0, Qt::DashLine));
    p.drawLine(QPointF(xMark, plot.top()), QPointF(xMark, plot.bottom()));
    p.setPen(QPen(Qt::white, 1.5));
    p.setBrush(kBarColor);
    p.drawEllipse(QPointF(xMark, Y(m_counts[index])), 4.5, 4.5);
    p.restore();

    DrawFrame(p, plot);
    DrawYTitle(p, plot, "Karten");
}

void CardPlotWidget::PaintCloud(QPainter& p, const QRect& plot)
{
    DrawTitle(p, plot, "F: Gewicht je Karte - Groesse gegen Massenstrom (Diagnose)");
    double mMin = 0.0, mMax = 0.0;
    for (const SCard& card : m_cards)
        if (card.mass > 0.0)
        {
            mMin = (mMin > 0.0) ? std::min(mMin, card.mass) : card.mass;
            mMax = std::max(mMax, card.mass);
        }
    double y0 = mMin > 0.0 ? std::floor(std::log10(mMin)) : -10.0;
    double y1 = mMax > 0.0 ? std::ceil(std::log10(mMax)) : -9.0;
    if (y1 - y0 < 1.0)
    {
        y0 -= 1.0;
        y1 += 1.0;
    }
    const auto Y = [&](double mass) { return plot.bottom() - (std::log10(mass) - y0) / (y1 - y0) * plot.height(); };

    PaintSizeAxis(p, plot, true);
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
    DrawFrame(p, plot);
    DrawYTitle(p, plot, "Massenstrom je Karte [kg/s]");

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
        const QPointF center(SizeToX(card.size, plot), Y(card.mass));
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

    std::vector<SLegendItem> legend;
    for (int c = 0; c < m_compounds.size(); ++c)
        legend.push_back({ SLegendItem::Bar, CompoundColor(c), m_compounds[c] });
    DrawLegend(p, plot, legend);
}

void CardPlotWidget::mouseMoveEvent(QMouseEvent* event)
{
    const QPointF pos = event->position();
    const QPoint global = event->globalPosition().toPoint();
    const bool inPlot = m_message.isEmpty() && m_plot.contains(pos.toPoint());
    // Kartenzahl: naechster Zeitschritt unter der Maus.
    if (m_kind == EKind::Counts && inPlot && !m_times.empty())
    {
        const double x0 = m_times.front();
        const double x1 = (m_times.back() > x0) ? m_times.back() : x0 + 1.0;
        const double t = x0 + (pos.x() - m_plot.left()) / m_plot.width() * (x1 - x0);
        size_t i = static_cast<size_t>(std::lower_bound(m_times.begin(), m_times.end(), t) - m_times.begin());
        if (i >= m_times.size() || (i > 0 && t - m_times[i - 1] < m_times[i] - t))
            i = (i > 0) ? i - 1 : 0;
        if (i < m_counts.size())
            QToolTip::showText(global, QString("t = %1 s\n%2 Karten").arg(Num(m_times[i], 6)).arg(m_counts[i], 0, 'f', 0), this);
        return;
    }
    // Zeitkarte: Zelle unter der Maus (naechster gespeicherter Zeitpunkt).
    if (m_kind == EKind::TimeMap && inPlot && !m_mapTimes.empty())
    {
        const double timeEnd = m_timeEnd > 0.0 ? m_timeEnd : 1.0;
        const double t = (pos.x() - m_plot.left()) / m_plot.width() * timeEnd;
        size_t j = static_cast<size_t>(std::lower_bound(m_mapTimes.begin(), m_mapTimes.end(), t) - m_mapTimes.begin());
        if (j >= m_mapTimes.size() || (j > 0 && t - m_mapTimes[j - 1] < m_mapTimes[j] - t))
            j = (j > 0) ? j - 1 : 0;
        const double y0 = std::log10(m_sizeMin), y1 = std::log10(m_sizeMax);
        const double d = std::pow(10.0, y0 + (m_plot.bottom() - pos.y()) / m_plot.height() * (y1 - y0));
        const size_t above = static_cast<size_t>(std::upper_bound(m_mapBounds.begin(), m_mapBounds.end(), d) - m_mapBounds.begin());
        const size_t numClasses = m_mapBounds.size() - 1;
        if (above == 0 || above > numClasses)
        {
            QToolTip::hideText();
            return;
        }
        const size_t i = above - 1;
        QToolTip::showText(global, QString("t = %1 s\n%2-%3 %4\nq3*(ln d) = %5").arg(Num(m_mapTimes[j], 6))
            .arg(Micro(m_mapBounds[i]), Micro(m_mapBounds[i + 1]), Unit(), Num(m_mapValues[j * numClasses + i], 3)), this);
        return;
    }
    // Summenkurve: Q3 der Karten und der Vergleichskurve bei d unter der Maus.
    if (m_kind == EKind::Distribution && m_cumulativeMode && inPlot && !m_cumulative.sizes.empty())
    {
        const double d = XToSize(pos.x(), m_plot);
        const size_t n = static_cast<size_t>(std::upper_bound(m_cumulative.sizes.begin(), m_cumulative.sizes.end(), d)
            - m_cumulative.sizes.begin());
        QString tip = QString("d = %1 %2\nQ3 Karten = %3").arg(Micro(d), Unit(), Percent(n > 0 ? m_cumulative.q3[n - 1] : 0.0));
        if (m_refCumulative.size() == m_refSizes.size() && !m_refSizes.empty())
            tip += QString("\nQ3 %1 = %2").arg(m_refLabel, Percent(ValueAtSize(m_refSizes, m_refCumulative, d)));
        QToolTip::showText(global, tip, this);
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
        QToolTip::showText(global, best->text, this);
    else
        QToolTip::hideText();
}
