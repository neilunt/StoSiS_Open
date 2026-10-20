#include "FractionPreviewWidget.h"
#include <QPainter>
#include <QPainterPath>
#include <algorithm>

namespace
{
    constexpr int kMarginLeft = 34;
    constexpr int kMarginRight = 8;
    constexpr int kMarginTop = 20;
    constexpr int kMarginBottom = 20;

    QColor SeriesColor(size_t index)
    {
        static const QColor colors[] = {
            QColor(31, 119, 180), QColor(230, 120, 20), QColor(44, 160, 44), QColor(150, 80, 170)
        };
        return colors[index % 4];
    }
}

FractionPreviewWidget::FractionPreviewWidget(QWidget* parent)
    : QWidget(parent)
{
}

void FractionPreviewWidget::SetSeries(std::vector<std::vector<double>> series, std::vector<double> means,
    std::vector<QString> names, double totalTime)
{
    m_series = std::move(series);
    m_means = std::move(means);
    m_names = std::move(names);
    m_totalTime = totalTime;
    update();
}

void FractionPreviewWidget::Clear()
{
    m_series.clear();
    m_means.clear();
    m_names.clear();
    update();
}

void FractionPreviewWidget::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), Qt::white);

    const QRectF plot(kMarginLeft, kMarginTop,
        width() - kMarginLeft - kMarginRight, height() - kMarginTop - kMarginBottom);
    if (plot.width() < 10 || plot.height() < 10)
        return;

    // Achsen: Anteil 0 .. 1 senkrecht, Zeit 0 .. T waagerecht.
    painter.setPen(QColor(200, 200, 200));
    for (double y : { 0.0, 0.5, 1.0 })
    {
        const qreal py = plot.bottom() - y * plot.height();
        painter.drawLine(QPointF(plot.left(), py), QPointF(plot.right(), py));
        painter.setPen(Qt::darkGray);
        painter.drawText(QRectF(0, py - 8, kMarginLeft - 4, 16), Qt::AlignRight | Qt::AlignVCenter,
            QString::number(y, 'g', 2));
        painter.setPen(QColor(200, 200, 200));
    }
    painter.setPen(Qt::darkGray);
    painter.drawRect(plot);
    painter.drawText(QRectF(plot.left(), plot.bottom() + 2, 60, 16), Qt::AlignLeft, "0");
    painter.drawText(QRectF(plot.right() - 80, plot.bottom() + 2, 80, 16), Qt::AlignRight,
        QString("%1 s").arg(m_totalTime, 0, 'g', 4));

    if (m_series.empty())
    {
        painter.drawText(plot, Qt::AlignCenter, "Keine gueltigen Anteile");
        return;
    }

    // Legende oben.
    qreal legendX = plot.left();
    for (size_t i = 0; i < m_series.size() && i < m_names.size(); ++i)
    {
        painter.setPen(QPen(SeriesColor(i), 2));
        painter.drawLine(QPointF(legendX, 9), QPointF(legendX + 14, 9));
        painter.setPen(Qt::black);
        const int textWidth = painter.fontMetrics().horizontalAdvance(m_names[i]);
        painter.drawText(QPointF(legendX + 18, 13), m_names[i]);
        legendX += 18 + textWidth + 14;
    }

    painter.setClipRect(plot);
    for (size_t i = 0; i < m_series.size(); ++i)
    {
        const std::vector<double>& values = m_series[i];
        if (values.empty())
            continue;

        // Mittelwert gestrichelt.
        if (i < m_means.size())
        {
            const qreal py = plot.bottom() - m_means[i] * plot.height();
            painter.setPen(QPen(SeriesColor(i).lighter(140), 1, Qt::DashLine));
            painter.drawLine(QPointF(plot.left(), py), QPointF(plot.right(), py));
        }

        QPainterPath path;
        const double n = static_cast<double>(std::max<size_t>(values.size() - 1, 1));
        for (size_t k = 0; k < values.size(); ++k)
        {
            const QPointF p(plot.left() + static_cast<double>(k) / n * plot.width(),
                plot.bottom() - values[k] * plot.height());
            if (k == 0)
                path.moveTo(p);
            else
                path.lineTo(p);
        }
        painter.setPen(QPen(SeriesColor(i), 1.5));
        painter.drawPath(path);
    }
}
