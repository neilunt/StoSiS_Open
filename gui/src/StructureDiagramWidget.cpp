#include "StructureDiagramWidget.h"
#include "NodeTemplates.h"
#include <QFontMetricsF>
#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QMouseEvent>
#include <QPainterPath>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

namespace
{
    using EKind = stosis::SStructureBranch::EKind;

    // Daten an den Eintraegen (Kasten bzw. "-> k").
    constexpr int kDataNode = 0;
    constexpr int kDataConnection = 1;
    constexpr int kDataKind = 2;

    constexpr double kBoxHeight = 46.0;
    constexpr double kRowHeight = 64.0;
    constexpr double kGapX = 56.0;    // vom Kasten bis zur naechsten Spalte
    constexpr double kRootGap = 0.5;  // Leerzeilen zwischen zwei Wurzeln
    constexpr double kMinScale = 0.2;
    constexpr double kMaxScale = 4.0;

    QColor CutColor() { return QColor(210, 120, 30); } // wie die Schnitte im Flowsheet
    QColor GrayText() { return QColor(130, 130, 130); }
    QColor LineColor() { return QColor(90, 90, 90); }

    // Blasse Fuellung aus der Farbe des Typs (Palette und Flowsheet).
    QColor Tint(const QColor& color, double share)
    {
        const auto mix = [share](int c) { return static_cast<int>(std::lround(255.0 - (255.0 - c) * share)); };
        return QColor(mix(color.red()), mix(color.green()), mix(color.blue()));
    }

    // Lage eines Eintrags: Spalte = Tiefe im Baum, Zeile = Mitte (in Zeilenhoehen).
    struct SLayout
    {
        const stosis::SStructureBranch* branch = nullptr;
        int column = 0;
        double row = 0.0;
        std::vector<size_t> children;
    };

    // Blaetter bekommen nacheinander eine Zeile, eine Unit sitzt mittig
    // zwischen ihrem ersten und letzten Kind (eine Unit mit einem Kind auf
    // dessen Zeile). Liefert die Zeile des Eintrags.
    double Place(const stosis::SStructureBranch& branch, int column, double& nextRow, std::vector<SLayout>& out)
    {
        const size_t self = out.size();
        out.push_back({ &branch, column, 0.0, {} });
        if (branch.children.empty())
        {
            out[self].row = nextRow;
            nextRow += 1.0;
            return out[self].row;
        }
        double first = 0.0;
        double last = 0.0;
        for (size_t i = 0; i < branch.children.size(); ++i)
        {
            out[self].children.push_back(out.size());
            const double row = Place(branch.children[i], column + 1, nextRow, out);
            if (i == 0)
                first = row;
            last = row;
        }
        out[self].row = 0.5 * (first + last);
        return out[self].row;
    }

    QString KeyOf(const QGraphicsItem* item)
    {
        return item->data(kDataNode).toString() + "|" + item->data(kDataConnection).toString() + "|"
            + item->data(kDataKind).toString();
    }
}

StructureDiagramWidget::StructureDiagramWidget(QWidget* parent)
    : QGraphicsView(parent)
{
    m_scene = new QGraphicsScene(this);
    setScene(m_scene);
    setRenderHint(QPainter::Antialiasing);
    setRenderHint(QPainter::TextAntialiasing);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setAlignment(Qt::AlignLeft | Qt::AlignTop);
    setBackgroundBrush(Qt::white);
}

void StructureDiagramWidget::SetStructure(const stosis::CFlowsheetGraph& graph, const stosis::SFlowsheetStructure& structure)
{
    m_scene->clear();
    m_highlight = nullptr;

    // Nummer = Platz in der Rechenreihenfolge ab 0, bei jedem Zeichnen neu.
    std::map<std::string, int> number;
    for (size_t i = 0; i < structure.order.size(); ++i)
        number[structure.order[i]] = static_cast<int>(i);
    const auto numberOf = [&number](const std::string& id)
    {
        const auto it = number.find(id);
        return it != number.end() ? it->second : -1;
    };
    const auto nameOf = [&graph](const std::string& id)
    {
        const stosis::SFlowsheetNode* node = graph.FindNode(id);
        return QString::fromStdString(node ? node->name : id);
    };

    std::vector<SLayout> layout;
    double nextRow = 0.0;
    for (const stosis::SStructureBranch& root : structure.roots)
    {
        if (!layout.empty())
            nextRow += kRootGap;
        Place(root, 0, nextRow, layout);
    }

    // Einheitliche Kastenbreite: Streifen fuer den Schritt plus laengster Name.
    QFont nameFont = font();
    QFont numberFont = font();
    numberFont.setPointSizeF(nameFont.pointSizeF() * 0.85);
    QFont stepLabelFont = font();
    stepLabelFont.setPointSizeF(nameFont.pointSizeF() * 0.75);
    QFont stepFont = font();
    stepFont.setBold(true);
    stepFont.setPointSizeF(nameFont.pointSizeF() * 1.25);
    QFont labelFont = font();
    labelFont.setBold(true);
    const QFontMetricsF nameMetrics(nameFont);
    const double stepWidth = std::max(30.0, QFontMetricsF(stepLabelFont).horizontalAdvance("Schritt") + 10.0);
    double boxWidth = 96.0;
    for (const SLayout& entry : layout)
        if (entry.branch->kind == EKind::Unit)
            boxWidth = std::max(boxWidth, stepWidth + nameMetrics.horizontalAdvance(nameOf(entry.branch->nodeId)) + 20.0);
    const double columnWidth = boxWidth + kGapX;
    const auto left = [columnWidth](int column) { return column * columnWidth; };
    const auto middle = [](double row) { return row * kRowHeight; };

    // Linien: vom Kasten nach rechts, eine senkrechte Sammelschiene, dann je
    // Kind ein waagrechtes Stueck; zu einem Schnitt orange gestrichelt.
    const QPen linePen(LineColor(), 1.5);
    QPen cutPen(CutColor(), 1.5, Qt::DashLine);
    const QPen referencePen(GrayText(), 1.2);
    for (const SLayout& entry : layout)
    {
        if (entry.children.empty())
            continue;
        const double xRight = left(entry.column) + boxWidth;
        const double xBus = xRight + 0.5 * kGapX;
        const double y = middle(entry.row);
        double top = y;
        double bottom = y;
        for (size_t child : entry.children)
        {
            top = std::min(top, middle(layout[child].row));
            bottom = std::max(bottom, middle(layout[child].row));
        }
        QPainterPath trunk(QPointF(xRight, y));
        trunk.lineTo(xBus, y);
        if (bottom > top)
        {
            trunk.moveTo(xBus, top);
            trunk.lineTo(xBus, bottom);
        }
        m_scene->addPath(trunk, linePen);
        for (size_t child : entry.children)
        {
            const EKind kind = layout[child].branch->kind;
            QPainterPath stub(QPointF(xBus, middle(layout[child].row)));
            stub.lineTo(left(layout[child].column) - (kind == EKind::Unit ? 0.0 : 4.0), middle(layout[child].row));
            m_scene->addPath(stub, kind == EKind::Cut ? cutPen : kind == EKind::Reference ? referencePen : linePen);
        }
    }

    // Kaesten und "-> k".
    for (const SLayout& entry : layout)
    {
        const stosis::SStructureBranch& branch = *entry.branch;
        const stosis::SFlowsheetNode* node = graph.FindNode(branch.nodeId);
        const stosis::SFlowsheetConnection* connection =
            branch.connectionId.empty() ? nullptr : graph.FindConnection(branch.connectionId);
        const double x = left(entry.column);
        const double y = middle(entry.row);
        const QString name = nameOf(branch.nodeId);
        const int k = numberOf(branch.nodeId);
        QString input;
        if (connection && node && node->numInputs > 1)
            input = " (Eingang " + QString::number(connection->targetPort + 1) + ")";
        QString via;
        if (connection)
        {
            const stosis::SFlowsheetNode* source = graph.FindNode(connection->sourceNodeId);
            via = nameOf(connection->sourceNodeId);
            if (source && source->numOutputs > 1)
                via += " Ausgang " + QString::number(connection->sourcePort + 1);
            via += " " + QString(QChar(0x2192)) + " " + name + input;
        }

        QGraphicsItem* item = nullptr;
        if (branch.kind == EKind::Unit)
        {
            const QColor color = node ? GetNodeColor(node->type) : QColor(150, 150, 150);
            QPainterPath box;
            box.addRoundedRect(QRectF(x, y - 0.5 * kBoxHeight, boxWidth, kBoxHeight), 6.0, 6.0);
            auto* boxItem = m_scene->addPath(box, QPen(color.darker(140), 1.5), QBrush(Tint(color, 0.35)));
            // Links ein dunkler Streifen mit dem Schritt (Rechenreihenfolge wie
            // in der Spalte "Schritt" des Reiters Text, Kapitel 3.3).
            const double top = y - 0.5 * kBoxHeight;
            QPainterPath stripArea;
            stripArea.addRect(QRectF(x, top, stepWidth, kBoxHeight));
            auto* strip = new QGraphicsPathItem(box.intersected(stripArea), boxItem);
            strip->setPen(Qt::NoPen);
            strip->setBrush(color.darker(170));
            auto* stepLabel = new QGraphicsSimpleTextItem("Schritt", strip);
            stepLabel->setFont(stepLabelFont);
            stepLabel->setBrush(Qt::white);
            stepLabel->setPos(x + 0.5 * (stepWidth - stepLabel->boundingRect().width()), top + 3.0);
            auto* stepItem = new QGraphicsSimpleTextItem(QString::number(k + 1), strip);
            stepItem->setFont(stepFont);
            stepItem->setBrush(Qt::white);
            stepItem->setPos(x + 0.5 * (stepWidth - stepItem->boundingRect().width()),
                top + kBoxHeight - stepItem->boundingRect().height() - 3.0);
            // Rechts der Name und darunter grau die ID aus der Datei.
            const double textLeft = x + stepWidth;
            const double textWidth = boxWidth - stepWidth;
            auto* nameItem = new QGraphicsSimpleTextItem(name, boxItem);
            nameItem->setFont(nameFont);
            nameItem->setPos(textLeft + 0.5 * (textWidth - nameItem->boundingRect().width()), top + 5.0);
            auto* idItem = new QGraphicsSimpleTextItem(QString::fromStdString(branch.nodeId), boxItem);
            idItem->setFont(numberFont);
            idItem->setBrush(GrayText());
            idItem->setPos(textLeft + 0.5 * (textWidth - idItem->boundingRect().width()),
                top + kBoxHeight - idItem->boundingRect().height() - 4.0);
            QString tip = name + " (" + (node ? stosis::GetNodeTypeLabel(node->type) : "?") + "), ID "
                + QString::fromStdString(branch.nodeId) + "\nRechnet als " + QString::number(k + 1) + ". Unit je Zeitschritt";
            const int cycle = structure.GetCycle(branch.nodeId);
            if (cycle >= 0)
                tip += "\nKreis " + QString::number(cycle + 1);
            if (connection)
                tip += "\nZulauf: " + via;
            boxItem->setToolTip(tip);
            item = boxItem;
        }
        else
        {
            const bool cut = branch.kind == EKind::Cut;
            auto* label = m_scene->addSimpleText(QString(QChar(0x2192)) + " " + name, labelFont);
            label->setBrush(cut ? CutColor() : GrayText());
            label->setPos(x, y - 0.5 * label->boundingRect().height());
            const bool byHand = connection && connection->isRecycle;
            label->setToolTip(cut
                ? via + "\nAufgeschnitten (" + (byHand ? "von Hand markiert, \"recycle\": \"manual\"" : "vom Graphen gewaehlt, \"recycle\": \"auto\"")
                    + "):\n" + name + " liest hier den Strom des vorigen Zeitschritts."
                : via + "\nLaeuft hier zusammen; " + name + " steht an anderer Stelle im Bild.");
            item = label;
        }
        item->setData(kDataNode, QString::fromStdString(branch.nodeId));
        item->setData(kDataConnection, QString::fromStdString(branch.connectionId));
        item->setData(kDataKind, static_cast<int>(branch.kind));
        item->setZValue(1.0);
    }

    QColor mark = palette().color(QPalette::Highlight); // durchscheinend, auch bei dunklem Farbschema lesbar
    mark.setAlpha(60);
    m_highlight = m_scene->addRect(QRectF(), Qt::NoPen, QBrush(mark));
    m_highlight->setZValue(-1.0);
    m_highlight->hide();
    setSceneRect(m_scene->itemsBoundingRect().adjusted(-24.0, -24.0, 24.0, 24.0));

    // Auswahl behalten, wenn es den Eintrag noch gibt.
    if (!m_selectedKey.isEmpty())
    {
        QGraphicsItem* found = nullptr;
        for (QGraphicsItem* candidate : m_scene->items())
            if (candidate->data(kDataKind).isValid() && KeyOf(candidate) == m_selectedKey)
                found = candidate;
        Select(found);
    }
}

QGraphicsItem* StructureDiagramWidget::FindEntry(QGraphicsItem* item) const
{
    for (QGraphicsItem* current = item; current; current = current->parentItem())
        if (current->data(kDataKind).isValid())
            return current;
    return nullptr;
}

void StructureDiagramWidget::Select(QGraphicsItem* item)
{
    m_selectedKey = item ? KeyOf(item) : QString();
    if (!m_highlight)
        return;
    if (!item)
    {
        m_highlight->hide();
        return;
    }
    m_highlight->setRect(item->sceneBoundingRect().adjusted(-5.0, -5.0, 5.0, 5.0));
    m_highlight->show();
}

void StructureDiagramWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        QGraphicsItem* entry = FindEntry(itemAt(event->position().toPoint()));
        Select(entry);
        if (entry)
            emit ItemClicked(entry->data(kDataNode).toString(), entry->data(kDataConnection).toString(),
                entry->data(kDataKind).toInt());
        else
            emit ItemClicked(QString(), QString(), -1);
    }
    QGraphicsView::mousePressEvent(event); // Ziehen verschiebt das Bild
}

void StructureDiagramWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    // Ganzes Bild einpassen, aber nicht ueber 100 % vergroessern.
    fitInView(sceneRect(), Qt::KeepAspectRatio);
    if (transform().m11() > 1.0)
        resetTransform();
    QGraphicsView::mouseDoubleClickEvent(event);
}

void StructureDiagramWidget::wheelEvent(QWheelEvent* event)
{
    const double steps = event->angleDelta().y() / 120.0;
    if (steps == 0.0)
        return;
    const double current = transform().m11();
    const double target = std::clamp(current * std::pow(1.15, steps), kMinScale, kMaxScale);
    scale(target / current, target / current);
    event->accept();
}
