#include "SieveGridWidget.h"
#include <QLocale>
#include <QPainter>
#include <algorithm>
#include <cmath>

namespace
{
    constexpr int kLabelWidth = 120;
    constexpr int kMarginRight = 16;
    const QColor kGridColor(31, 119, 180);
    const QColor kBandColors[2] = { QColor(238, 237, 254), QColor(218, 215, 248) };
    const QColor kBandBorder(83, 74, 183);

    // Groesse in um, ohne Tausendertrennzeichen (1000 statt 1.000).
    QString Size(double meters)
    {
        QLocale locale;
        locale.setNumberOptions(QLocale::OmitGroupSeparator);
        return locale.toString(meters * stosis::kSizeDisplayFactor, 'g', 4);
    }
}

SieveGridWidget::SieveGridWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(118);
}

void SieveGridWidget::SetGrid(const stosis::SSizeGrid& sizes, const stosis::SCardSettings& cards)
{
    m_sizes = sizes;
    m_cards = cards;
    update();
}

void SieveGridWidget::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.fillRect(rect(), Qt::white);
    if (!m_sizes.IsValid() || !(m_cards.rangeFactor > 1.0) || !(m_cards.rangeAnchor > 0.0))
    {
        p.setPen(Qt::gray);
        p.drawText(rect(), Qt::AlignCenter, "Ungueltiges Raster");
        return;
    }
    const double left = kLabelWidth;
    const double width = std::max(10, this->width() - kLabelWidth - kMarginRight);
    const bool log = m_sizes.IsLogarithmic();
    const double dMin = m_sizes.sizeMin, dMax = m_sizes.sizeMax;
    const auto X = [&](double d)
    {
        const double f = log ? std::log(d / dMin) / std::log(dMax / dMin) : (d - dMin) / (dMax - dMin);
        return left + std::clamp(f, 0.0, 1.0) * width;
    };
    const int rowGrid = 8, rowSieve = 46, rowAxis = 86, rowHeight = 28;

    // --- Raster: ein Strich je Stuetzstelle ------------------------------
    p.setPen(Qt::black);
    QFont bold = p.font();
    bold.setBold(true);
    p.setFont(bold);
    p.drawText(QRect(4, rowGrid, kLabelWidth - 8, 15), Qt::AlignLeft, "Raster");
    p.drawText(QRect(4, rowSieve, kLabelWidth - 8, 15), Qt::AlignLeft, "Siebsatz");
    p.setFont(font());
    p.setPen(Qt::darkGray);
    p.drawText(QRect(4, rowGrid + 14, kLabelWidth - 8, 15), Qt::AlignLeft,
               QString("%1 Stuetzstellen").arg(m_sizes.numClasses));
    p.setPen(QPen(kGridColor, 1.0));
    for (size_t s = 0; s < m_sizes.numClasses; ++s)
    {
        const double x = X(m_sizes.GetSizeAt(s));
        p.drawLine(QPointF(x, rowGrid + 2), QPointF(x, rowGrid + rowHeight));
    }

    // --- Siebsatz: Bereiche [anchor * factor^k, anchor * factor^(k+1)) ---
    // Beim linearen Raster mit d_min = 0 erst ab 1e-4 d_max (kleinere
    // Bereiche waeren schmaler als ein Bildpunkt).
    const double from = (dMin > 0.0) ? dMin : dMax * 1e-4;
    const int kFirst = stosis::GetCardRangeIndex(from, m_cards);
    int kLast = stosis::GetCardRangeIndex(dMax, m_cards);
    if (stosis::GetCardRangeLower(kLast, m_cards) >= dMax)
        --kLast;
    int count = 0;
    p.setRenderHint(QPainter::Antialiasing, false);
    for (int k = kFirst; k <= kLast; ++k, ++count)
    {
        const double lower = stosis::GetCardRangeLower(k, m_cards);
        const double upper = stosis::GetCardRangeLower(k + 1, m_cards);
        const QRectF band(X(std::max(lower, dMin)), rowSieve + 2, X(std::min(upper, dMax)) - X(std::max(lower, dMin)), rowHeight - 2);
        p.setPen(QPen(kBandBorder, 1.0));
        p.setBrush(kBandColors[(k % 2 + 2) % 2]);
        p.drawRect(band);
        const QString label = Size(lower) + "-" + Size(upper);
        if (band.width() > p.fontMetrics().horizontalAdvance(label) + 6)
        {
            p.setPen(kBandBorder.darker(140));
            p.drawText(band, Qt::AlignCenter, label);
        }
    }
    p.setPen(Qt::darkGray);
    p.drawText(QRect(4, rowSieve + 14, kLabelWidth - 8, 15), Qt::AlignLeft, QString("%1 Bereiche").arg(count));

    // --- Achse: Dekaden (logarithmisch) bzw. d_min, Mitte, d_max ----------
    p.setPen(Qt::black);
    p.drawLine(QPointF(left, rowAxis), QPointF(left + width, rowAxis));
    std::vector<double> ticks;
    if (log)
    {
        for (int e = static_cast<int>(std::ceil(std::log10(dMin) - 1e-9)); e <= static_cast<int>(std::floor(std::log10(dMax) + 1e-9)); ++e)
            ticks.push_back(std::pow(10.0, e));
    }
    else
        ticks = { dMin, 0.5 * (dMin + dMax), dMax };
    for (double t : ticks)
    {
        const double x = X(t);
        p.drawLine(QPointF(x, rowAxis), QPointF(x, rowAxis + 4));
        p.drawText(QRectF(x - 40, rowAxis + 5, 80, 15), Qt::AlignHCenter, Size(t));
    }
    p.drawText(QRect(4, rowAxis - 2, kLabelWidth - 8, 15), Qt::AlignLeft,
               "d [" + QString::fromUtf8(stosis::kSizeDisplayUnit) + "]" + (log ? ", log." : ""));
}
