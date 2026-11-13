#include "FlowsheetPaletteWidget.h"
#include "NodeTemplates.h"
#include <QPainter>
#include <QMouseEvent>
#include <QDrag>
#include <QMimeData>
#include <QApplication>
#include <algorithm>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>

PaletteTile::PaletteTile(size_t templateIndex, QWidget* parent)
    : QWidget(parent)
    , m_templateIndex(templateIndex)
{
    const SNodeTemplate& tmpl = GetNodeTemplates()[templateIndex];
    m_text = tmpl.tileText;
    m_color = GetNodeColor(tmpl.type);
    setToolTip(tmpl.menuText + "\nIns Flowsheet ziehen");
    setCursor(Qt::OpenHandCursor);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

QSize PaletteTile::sizeHint() const
{
    return QSize(fontMetrics().horizontalAdvance(m_text) + 22, 28);
}

void PaletteTile::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(Qt::black));
    painter.setBrush(m_color);
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
    painter.setPen(Qt::white);
    painter.drawText(rect(), Qt::AlignCenter, m_text);
}

void PaletteTile::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        m_pressed = true;
        m_pressPos = event->position().toPoint();
    }
    QWidget::mousePressEvent(event);
}

void PaletteTile::mouseMoveEvent(QMouseEvent* event)
{
    // Ziehen startet erst nach ein paar Pixeln Bewegung (Systemvorgabe),
    // damit ein einfacher Klick nicht versehentlich einen Drag ausloest.
    if (!m_pressed || !(event->buttons() & Qt::LeftButton))
        return;
    if ((event->position().toPoint() - m_pressPos).manhattanLength() < QApplication::startDragDistance())
        return;
    m_pressed = false;

    auto* mimeData = new QMimeData();
    mimeData->setData(kNodeTemplateMimeType, QByteArray::number(static_cast<qulonglong>(m_templateIndex)));

    auto* drag = new QDrag(this);
    drag->setMimeData(mimeData);
    drag->setPixmap(grab());
    drag->setHotSpot(m_pressPos);
    drag->exec(Qt::CopyAction);
}

namespace
{
    constexpr int kMarginX = 6;   // left and right
    constexpr int kMarginY = 4;   // top and bottom
    constexpr int kGroupGap = 21; // between groups in a row, the line in the middle
    constexpr int kRowGap = 6;    // between rows
}

FlowsheetPaletteWidget::FlowsheetPaletteWidget(QWidget* parent)
    : QWidget(parent)
{
    // One widget per group: small caption, the tiles below.
    const std::vector<SNodeTemplate>& templates = GetNodeTemplates();
    QString currentGroup;
    QHBoxLayout* tileRow = nullptr;
    for (size_t i = 0; i < templates.size(); ++i)
    {
        if (templates[i].group != currentGroup || !tileRow)
        {
            currentGroup = templates[i].group;
            auto* group = new QWidget(this);
            auto* groupLayout = new QVBoxLayout(group);
            groupLayout->setContentsMargins(0, 0, 0, 0);
            groupLayout->setSpacing(2);
            auto* caption = new QLabel(currentGroup, group);
            QFont font = caption->font();
            font.setPointSizeF(font.pointSizeF() * 0.85);
            caption->setFont(font);
            caption->setStyleSheet("color: gray;");
            groupLayout->addWidget(caption);
            tileRow = new QHBoxLayout();
            tileRow->setSpacing(4);
            groupLayout->addLayout(tileRow);
            m_groups.push_back(group);
        }
        tileRow->addWidget(new PaletteTile(i, m_groups.back()));
    }
    for (QWidget* group : m_groups)
        group->resize(group->sizeHint());

    // Not Fixed: a fixed policy caps the height at sizeHint (one row) and the
    // layout would centre that row in the space reserved for all rows.
    QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
}

int FlowsheetPaletteWidget::ArrangeGroups(int width, bool apply) const
{
    int x = kMarginX, y = kMarginY, rowHeight = 0;
    for (QWidget* group : m_groups)
    {
        const QSize size = group->sizeHint();
        if (x > kMarginX && x + size.width() > width - kMarginX)
        {
            x = kMarginX;
            y += rowHeight + kRowGap;
            rowHeight = 0;
        }
        if (apply)
            group->setGeometry(x, y, size.width(), size.height());
        x += size.width() + kGroupGap;
        rowHeight = std::max(rowHeight, size.height());
    }
    return y + rowHeight + kMarginY;
}

QSize FlowsheetPaletteWidget::sizeHint() const
{
    int width = 2 * kMarginX;
    for (size_t i = 0; i < m_groups.size(); ++i)
        width += m_groups[i]->sizeHint().width() + (i > 0 ? kGroupGap : 0);
    return QSize(width, heightForWidth(width));
}

QSize FlowsheetPaletteWidget::minimumSizeHint() const
{
    // Narrow down to the widest group; the bar then has one group per row.
    int width = 0;
    for (QWidget* group : m_groups)
        width = std::max(width, group->sizeHint().width());
    return QSize(width + 2 * kMarginX, heightForWidth(sizeHint().width()));
}

int FlowsheetPaletteWidget::heightForWidth(int width) const
{
    return ArrangeGroups(width, false);
}

void FlowsheetPaletteWidget::resizeEvent(QResizeEvent* event)
{
    ArrangeGroups(width(), true);
    QWidget::resizeEvent(event);
}

void FlowsheetPaletteWidget::paintEvent(QPaintEvent* /*event*/)
{
    // Line between neighbouring groups of the same row.
    QPainter painter(this);
    painter.setPen(palette().color(QPalette::Mid));
    for (size_t i = 0; i + 1 < m_groups.size(); ++i)
    {
        const QRect a = m_groups[i]->geometry();
        const QRect b = m_groups[i + 1]->geometry();
        if (a.y() != b.y())
            continue;
        const int x = (a.right() + b.left()) / 2;
        painter.drawLine(x, a.top(), x, std::max(a.bottom(), b.bottom()));
    }
}
