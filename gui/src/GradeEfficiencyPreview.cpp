#include "GradeEfficiencyPreview.h"
#include "SizeGrid.h"
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <cmath>
#include <algorithm>

QString GradeEfficiencyFormulaHtml(stosis::EScreenModel model, const QString& xFormula, const QString& xMeaning)
{
    const QString x = "<i>x</i>", a = "<i>&alpha;</i>", b = "<i>&beta;</i>", o = "<i>o</i>";
    QString formula;
    switch (model)
    {
    case stosis::EScreenModel::MolerusHoffmann:
        formula = "1 / [ 1 + " + x + "<sup>&minus;2</sup> &middot; exp( " + a + " (1 &minus; " + x + "<sup>2</sup>) ) ]";
        break;
    case stosis::EScreenModel::TeipelHennig:
        formula = "[ 1 &minus; (1 + 3 &middot; " + x + "<sup>(" + x + " + " + a + ") &middot; " + b + "</sup>)<sup>&minus;1/2</sup> ]"
                  " &middot; (1 &minus; " + o + ") + " + o;
        break;
    case stosis::EScreenModel::Plitt:
    default:
        formula = "1 &minus; exp( &minus;" + QLocale().toString(0.693, 'f', 3) + " &middot; " + x + "<sup>" + a + "</sup> )";
        break;
    }
    return QString("<div align=\"center\" style=\"font-family: 'Times New Roman'; font-size: 15pt;\"><i>T</i>(<i>x</i>) = %1</div>"
                   "<div align=\"center\" style=\"font-family: 'Times New Roman';\">"
                   "<span style=\"font-size: 14pt; color: #222222;\">%2</span>"
                   "<span style=\"font-size: 12pt; color: #444444;\">: %3</span></div>")
        .arg(formula, xFormula, xMeaning);
}

GradeEfficiencyPreview::GradeEfficiencyPreview(const QString& axisTitle, QWidget* parent)
    : QWidget(parent), m_axisTitle(axisTitle)
{
    setMinimumHeight(180);
}

void GradeEfficiencyPreview::SetCurves(std::vector<SCurve> curves, double sizeMin, double sizeMax, const QString& message)
{
    m_curves = std::move(curves);
    m_sizeMin = sizeMin;
    m_sizeMax = sizeMax;
    m_message = message;
    update();
}

QColor GradeEfficiencyPreview::CurveColor(size_t index) const
{
    if (m_curves.size() <= 1)
        return palette().color(QPalette::Highlight);
    static const QColor colors[] = { QColor(31, 119, 180), QColor(214, 39, 40), QColor(44, 160, 44),
        QColor(148, 103, 189), QColor(255, 127, 14), QColor(140, 86, 75) };
    return colors[index % (sizeof(colors) / sizeof(colors[0]))];
}

void GradeEfficiencyPreview::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QColor ink = palette().color(QPalette::WindowText);
    QColor faint = ink;
    faint.setAlpha(70);
    const QRect plot = rect().adjusted(50, 10, -15, -38);
    if (plot.width() < 40 || plot.height() < 40 || !(m_sizeMax > m_sizeMin))
        return;

    const double f = stosis::kSizeDisplayFactor;
    const bool logAxis = m_sizeMin > 0.0;
    const double x0 = logAxis ? std::log10(m_sizeMin * f) : m_sizeMin * f;
    const double x1 = logAxis ? std::log10(m_sizeMax * f) : m_sizeMax * f;
    const auto toPixelX = [&](double sizeDisplay)
    {
        const double x = logAxis ? std::log10(sizeDisplay) : sizeDisplay;
        return plot.left() + (x - x0) / (x1 - x0) * plot.width();
    };
    const auto toPixelY = [&](double t) { return plot.bottom() - t * plot.height(); };

    // Grid and ticks: T in steps of 0.25; size 1-2-5 per decade (log) or 5 steps (linear).
    QPen gridPen(faint, 1, Qt::DotLine);
    painter.setPen(ink);
    const QFontMetrics metrics(font());
    for (int i = 0; i <= 4; ++i)
    {
        const double t = 0.25 * i;
        const double y = toPixelY(t);
        painter.setPen(gridPen);
        painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        painter.setPen(ink);
        const QString label = QString::number(t, 'g', 2).replace('.', ',');
        painter.drawText(QRectF(0, y - 8, plot.left() - 6, 16), Qt::AlignRight | Qt::AlignVCenter, label);
    }
    std::vector<double> ticks;
    if (logAxis)
    {
        for (int k = static_cast<int>(std::floor(x0)); k <= static_cast<int>(std::ceil(x1)); ++k)
            for (double m : { 1.0, 2.0, 5.0 })
            {
                const double value = m * std::pow(10.0, k);
                const double x = std::log10(value);
                if (x >= x0 - 1e-9 && x <= x1 + 1e-9)
                    ticks.push_back(value);
            }
    }
    else
    {
        for (int i = 0; i <= 5; ++i)
            ticks.push_back(x0 + (x1 - x0) * i / 5.0);
    }
    double lastRight = -1e9;
    for (double value : ticks)
    {
        const double x = toPixelX(value);
        painter.setPen(gridPen);
        painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        painter.setPen(ink);
        const QString label = QString::number(value, 'g', 3).replace('.', ',');
        const int width = metrics.horizontalAdvance(label);
        if (x - width / 2.0 > lastRight + 4)
        {
            painter.drawText(QRectF(x - width / 2.0, plot.bottom() + 3, width, 16), Qt::AlignCenter, label);
            lastRight = x + width / 2.0;
        }
    }
    painter.setPen(ink);
    painter.drawRect(plot);
    painter.drawText(QRectF(plot.left(), plot.bottom() + 19, plot.width(), 16), Qt::AlignCenter,
        QString("Partikelgroesse d [%1]").arg(QString::fromUtf8(stosis::kSizeDisplayUnit)));
    painter.save();
    painter.translate(12, plot.center().y());
    painter.rotate(-90);
    painter.drawText(QRectF(-plot.height() / 2.0, -8, plot.height(), 16), Qt::AlignCenter, m_axisTitle);
    painter.restore();

    if (m_curves.empty())
    {
        if (!m_message.isEmpty())
            painter.drawText(plot.adjusted(10, 10, -10, -10), Qt::AlignCenter | Qt::TextWordWrap, m_message);
        return;
    }

    for (size_t k = 0; k < m_curves.size(); ++k)
    {
        const SCurve& curve = m_curves[k];
        const QColor color = CurveColor(k);
        const double marker = curve.markerSize * f;
        if (marker > 0.0 && marker >= m_sizeMin * f && marker <= m_sizeMax * f)
        {
            painter.setPen(QPen(m_curves.size() > 1 ? color : ink, 1, Qt::DashLine));
            const double x = toPixelX(marker);
            painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
            if (!curve.markerLabel.isEmpty())
                painter.drawText(QPointF(x + 4, plot.top() + 14), curve.markerLabel);
        }
        if (!curve.share)
            continue;
        QPainterPath path;
        const int numPoints = std::max(2, plot.width());
        for (int i = 0; i <= numPoints; ++i)
        {
            const double x = x0 + (x1 - x0) * i / numPoints;
            const double sizeDisplay = logAxis ? std::pow(10.0, x) : x;
            const QPointF point(toPixelX(sizeDisplay), toPixelY(std::clamp(curve.share(sizeDisplay / f), 0.0, 1.0)));
            if (i == 0)
                path.moveTo(point);
            else
                path.lineTo(point);
        }
        painter.setPen(QPen(color, 2));
        painter.drawPath(path);
    }

    // Legend top left inside the plot (only with several curves or labels).
    int y = plot.top() + 6;
    for (size_t k = 0; k < m_curves.size(); ++k)
    {
        if (m_curves[k].label.isEmpty())
            continue;
        painter.setPen(QPen(CurveColor(k), 2));
        painter.drawLine(QPointF(plot.left() + 8, y + 8), QPointF(plot.left() + 26, y + 8));
        painter.setPen(ink);
        painter.drawText(QRectF(plot.left() + 32, y, plot.width() - 40, 16), Qt::AlignLeft | Qt::AlignVCenter, m_curves[k].label);
        y += 17;
    }
}
