#include "FlowsheetPaletteWidget.h"
#include "NodeTemplates.h"
#include <QPainter>
#include <QMouseEvent>
#include <QDrag>
#include <QMimeData>
#include <QApplication>
#include <QScrollArea>
#include <QScrollBar>
#include <QFrame>
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

FlowsheetPaletteWidget::FlowsheetPaletteWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* content = new QWidget();
    auto* contentLayout = new QHBoxLayout(content);
    contentLayout->setContentsMargins(6, 4, 6, 4);
    contentLayout->setSpacing(10);

    // Kacheln gruppenweise: kleine Ueberschrift, darunter die Kacheln,
    // zwischen den Gruppen eine senkrechte Trennlinie.
    const std::vector<SNodeTemplate>& templates = GetNodeTemplates();
    QString currentGroup;
    QHBoxLayout* tileRow = nullptr;
    for (size_t i = 0; i < templates.size(); ++i)
    {
        if (templates[i].group != currentGroup || !tileRow)
        {
            if (tileRow)
            {
                auto* separator = new QFrame(content);
                separator->setFrameShape(QFrame::VLine);
                separator->setFrameShadow(QFrame::Sunken);
                contentLayout->addWidget(separator);
            }
            currentGroup = templates[i].group;

            auto* groupLayout = new QVBoxLayout();
            groupLayout->setSpacing(2);
            auto* caption = new QLabel(currentGroup, content);
            QFont font = caption->font();
            font.setPointSizeF(font.pointSizeF() * 0.85);
            caption->setFont(font);
            caption->setStyleSheet("color: gray;");
            groupLayout->addWidget(caption);
            tileRow = new QHBoxLayout();
            tileRow->setSpacing(4);
            groupLayout->addLayout(tileRow);
            contentLayout->addLayout(groupLayout);
        }
        tileRow->addWidget(new PaletteTile(i, content));
    }
    contentLayout->addStretch();

    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidget(content);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // Feste Hoehe: Inhalt plus Platz fuer eine evtl. noetige Scrollleiste.
    scrollArea->setFixedHeight(content->sizeHint().height()
        + scrollArea->horizontalScrollBar()->sizeHint().height());

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(scrollArea);
}
