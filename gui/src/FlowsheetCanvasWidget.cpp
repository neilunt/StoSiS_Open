#include "FlowsheetCanvasWidget.h"
#include "FlowsheetNodeItem.h"
#include "FlowsheetConnectionItem.h"
#include "FlowsheetPaletteWidget.h"
#include "FlowsheetScene.h"
#include "NodeTemplates.h"
#include <QGraphicsView>
#include <QGraphicsLineItem>
#include <QVBoxLayout>
#include <QPen>
#include <QPainter>
#include <QMenu>
#include <QInputDialog>
#include <QMessageBox>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QToolTip>
#include <QCursor>
#include <QScrollBar>
#include <QWheelEvent>
#include <cmath>
#include <set>
#include <algorithm>

FlowsheetCanvasWidget::FlowsheetCanvasWidget(QWidget* parent)
    : QWidget(parent)
{
    m_scene = new FlowsheetScene(this);
    // Grosse feste Zeichenflaeche, damit sich die Ansicht frei verschieben
    // laesst - auch wenn alle Bloecke schon zu sehen sind.
    m_scene->setSceneRect(-10000.0, -10000.0, 20000.0, 20000.0);
    m_view = new QGraphicsView(m_scene, this);
    m_view->setRenderHint(QPainter::Antialiasing);
    m_view->setMouseTracking(true); // noetig, damit MouseMove auch ohne gedrueckte Taste ankommt
    // Zoomen um die Mausposition (Mausrad, siehe eventFilter).
    m_view->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    m_view->setAcceptDrops(true);
    m_view->viewport()->setAcceptDrops(true);

    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_view, &QWidget::customContextMenuRequested, this, &FlowsheetCanvasWidget::ShowContextMenu);

    // Maus und Drag & Drop kommen am Viewport an, Tasten an der View selbst.
    m_view->viewport()->installEventFilter(this);
    m_view->installEventFilter(this);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(new FlowsheetPaletteWidget(this));
    layout->addWidget(m_view, 1);
}

void FlowsheetCanvasWidget::SetGraph(stosis::CFlowsheetGraph* graph)
{
    m_graph = graph;
    RebuildScene();
}

void FlowsheetCanvasWidget::RebuildScene()
{
    CancelConnecting();
    m_scene->clear();
    m_nodeItems.clear();
    m_connectionLines.clear();

    if (!m_graph)
        return;

    for (const stosis::SFlowsheetNode& node : m_graph->GetNodes())
        CreateNodeItem(node.id, node.name, node.type, QPointF(node.posX, node.posY));

    for (const stosis::SFlowsheetConnection& conn : m_graph->GetConnections())
        AddConnectionItem(conn.id, conn.sourceNodeId, conn.sourcePort, conn.targetNodeId, conn.targetPort);

    UpdateConnectionLines();
    UpdateConnectionStyles();
}

void FlowsheetCanvasWidget::AddConnectionItem(const std::string& connectionId, const std::string& sourceNodeId,
    size_t sourcePort, const std::string& targetNodeId, size_t targetPort)
{
    auto* line = new FlowsheetConnectionItem();
    m_scene->addItem(line);
    m_connectionLines.push_back({ line, connectionId, sourceNodeId, sourcePort, targetNodeId, targetPort });
}

void FlowsheetCanvasWidget::UpdateConnectionLines()
{
    for (auto& entry : m_connectionLines)
    {
        auto sourceIt = m_nodeItems.find(entry.sourceNodeId);
        auto targetIt = m_nodeItems.find(entry.targetNodeId);
        if (sourceIt == m_nodeItems.end() || targetIt == m_nodeItems.end())
            continue;

        FlowsheetPortItem* sourcePort = sourceIt->second->GetOutputPort(entry.sourcePort);
        FlowsheetPortItem* targetPort = targetIt->second->GetInputPort(entry.targetPort);
        if (!sourcePort || !targetPort)
            continue;
        entry.line->UpdateRoute(sourcePort->scenePos(), targetPort->scenePos(),
            sourceIt->second->sceneBoundingRect(), targetIt->second->sceneBoundingRect(),
            entry.sourcePort, entry.targetPort);
    }
}

bool FlowsheetCanvasWidget::IsMarkedRecycle(const std::string& connectionId) const
{
    for (const stosis::SFlowsheetConnection& conn : m_graph->GetConnections())
        if (conn.id == connectionId)
            return conn.isRecycle;
    return false;
}

void FlowsheetCanvasWidget::UpdateConnectionStyles()
{
    if (!m_graph)
        return;

    // Verbindungen, an denen ein Kreis aufgeschnitten wird, gestrichelt
    // und farbig - ihr Ziel liest den Strom aus dem vorherigen Zeitschritt.
    const std::set<std::string> lagged = m_graph->GetLaggedConnectionIds();
    for (const SConnectionLineEntry& entry : m_connectionLines)
    {
        if (lagged.count(entry.connectionId))
        {
            entry.line->setPen(QPen(QColor(210, 120, 30), 4, Qt::DashLine));
            entry.line->setToolTip(QString("Rueckfuehrung (%1): hier wird der Kreis aufgeschnitten.\n"
                "Das Ziel bekommt den Strom aus dem vorherigen Zeitschritt\n"
                "(wirkt wie ein kurzes Rohr der Laenge dt).")
                .arg(IsMarkedRecycle(entry.connectionId) ? "manuell markiert" : "automatisch gewaehlt"));
        }
        else
        {
            entry.line->setPen(QPen(Qt::darkGray, 4));
            entry.line->setToolTip(QString());
        }
    }
}

FlowsheetNodeItem* FlowsheetCanvasWidget::CreateNodeItem(const std::string& nodeId, const std::string& name,
    stosis::ENodeType type, QPointF pos)
{
    auto* item = new FlowsheetNodeItem(m_graph, nodeId, name, type);
    item->setPos(pos);
    item->SetOnMovedCallback([this]() { UpdateConnectionLines(); });
    item->SetOnDoubleClickedCallback([this](const std::string& id)
        {
            // Waehrend eine Verbindung gezogen wird, ist ein Doppelklick
            // eher ein Versehen - dann nichts oeffnen.
            if (!m_isConnecting)
                emit NodeDoubleClicked(QString::fromStdString(id));
        });
    m_scene->addItem(item);
    m_nodeItems[nodeId] = item;
    return item;
}

FlowsheetNodeItem* FlowsheetCanvasWidget::FindNodeItem(QGraphicsItem* item)
{
    for (QGraphicsItem* current = item; current; current = current->parentItem())
        if (auto* nodeItem = dynamic_cast<FlowsheetNodeItem*>(current))
            return nodeItem;
    return nullptr;
}

// ---------------------------------------------------------------------
// Kontextmenues
// ---------------------------------------------------------------------

void FlowsheetCanvasWidget::ShowContextMenu(const QPoint& viewPos)
{
    if (!m_graph)
        return;
    CancelConnecting();

    const QPointF scenePos = m_view->mapToScene(viewPos);
    QGraphicsItem* hitItem = m_scene->itemAt(scenePos, m_view->transform());

    for (size_t i = 0; i < m_connectionLines.size(); ++i)
    {
        if (m_connectionLines[i].line == hitItem)
        {
            ShowConnectionMenu(viewPos, i);
            return;
        }
    }
    if (FlowsheetNodeItem* nodeItem = FindNodeItem(hitItem))
    {
        ShowNodeMenu(viewPos, nodeItem);
        return;
    }
    if (hitItem == nullptr)
        ShowAddNodeMenu(viewPos, scenePos);
}

void FlowsheetCanvasWidget::ShowConnectionMenu(const QPoint& viewPos, size_t lineIndex)
{
    const std::string connectionId = m_connectionLines[lineIndex].connectionId;

    QMenu menu;
    QAction* recycleAction = menu.addAction("Als Rueckfuehrung markieren");
    recycleAction->setCheckable(true);
    recycleAction->setChecked(IsMarkedRecycle(connectionId));
    menu.addSeparator();
    QAction* deleteAction = menu.addAction("Verbindung loeschen");

    QAction* chosen = menu.exec(m_view->mapToGlobal(viewPos));
    if (chosen == recycleAction)
    {
        m_graph->SetConnectionRecycle(connectionId, recycleAction->isChecked());
    }
    else if (chosen == deleteAction)
    {
        m_graph->RemoveConnection(connectionId);
        m_scene->removeItem(m_connectionLines[lineIndex].line);
        delete m_connectionLines[lineIndex].line;
        m_connectionLines.erase(m_connectionLines.begin() + static_cast<std::ptrdiff_t>(lineIndex));
    }
    UpdateConnectionStyles();
}

void FlowsheetCanvasWidget::ShowNodeMenu(const QPoint& viewPos, FlowsheetNodeItem* nodeItem)
{
    const std::string nodeId = nodeItem->GetNodeId();

    QMenu menu;
    QAction* settingsAction = menu.addAction("Einstellungen oeffnen");
    QAction* renameAction = menu.addAction("Umbenennen...");
    menu.addSeparator();
    QAction* deleteAction = menu.addAction("Block loeschen");

    QAction* chosen = menu.exec(m_view->mapToGlobal(viewPos));
    if (chosen == settingsAction)
        emit NodeDoubleClicked(QString::fromStdString(nodeId));
    else if (chosen == renameAction)
        RenameNode(nodeId);
    else if (chosen == deleteAction)
        DeleteNodes({ nodeId });
}

void FlowsheetCanvasWidget::ShowAddNodeMenu(const QPoint& viewPos, QPointF scenePos)
{
    const std::vector<SNodeTemplate>& templates = GetNodeTemplates();

    QMenu menu;
    std::vector<QAction*> actions;
    for (size_t i = 0; i < templates.size(); ++i)
    {
        // Trennlinie zwischen den Gruppen (wie in der Palette).
        if (i > 0 && templates[i].group != templates[i - 1].group)
            menu.addSeparator();
        actions.push_back(menu.addAction(templates[i].menuText + " hinzufuegen"));
    }
    menu.addSeparator();
    QAction* fitAction = menu.addAction("Ganzes Flowsheet zeigen");
    QAction* resetZoomAction = menu.addAction("Zoom 100 %");

    QAction* chosen = menu.exec(m_view->mapToGlobal(viewPos));
    if (chosen == fitAction)
    {
        FitAll();
        m_userNavigated = true;
        return;
    }
    if (chosen == resetZoomAction)
    {
        m_view->setTransform(QTransform());
        m_userNavigated = true;
        return;
    }
    const auto chosenIt = std::find(actions.begin(), actions.end(), chosen);
    if (chosen == nullptr || chosenIt == actions.end())
        return;
    AddNodeFromTemplate(templates[static_cast<size_t>(chosenIt - actions.begin())], scenePos, false);
}

// ---------------------------------------------------------------------
// Bloecke anlegen, umbenennen, loeschen
// ---------------------------------------------------------------------

void FlowsheetCanvasWidget::AddNodeFromTemplate(const SNodeTemplate& tmpl, QPointF point, bool centerOnPoint)
{
    if (!m_graph)
        return;

    const std::string name = m_graph->MakeDefaultNodeName(tmpl.type);
    std::string nodeId;
    switch (tmpl.type)
    {
    case stosis::ENodeType::Inlet:
        nodeId = m_graph->AddInletNode(name, point.x(), point.y()); break;
    case stosis::ENodeType::Outlet:
        nodeId = m_graph->AddOutletNode(name, point.x(), point.y()); break;
    case stosis::ENodeType::Process:
        nodeId = m_graph->AddProcessNode(name, point.x(), point.y()); break;
    case stosis::ENodeType::Splitter:
        nodeId = m_graph->AddSplitterNode(name, tmpl.numPorts, point.x(), point.y()); break;
    case stosis::ENodeType::Merger:
        nodeId = m_graph->AddMergerNode(name, tmpl.numPorts, point.x(), point.y()); break;
    }

    FlowsheetNodeItem* item = CreateNodeItem(nodeId, name, tmpl.type, point);
    // Beim Ablegen aus der Palette soll die Box unter dem Mauszeiger
    // landen, nicht mit ihrer Ecke daran.
    if (centerOnPoint)
        item->setPos(point - item->rect().center());
    emit NodesChanged();
}

void FlowsheetCanvasWidget::RenameNode(const std::string& nodeId)
{
    const stosis::SFlowsheetNode* node = m_graph->FindNode(nodeId);
    auto itemIt = m_nodeItems.find(nodeId);
    if (!node || itemIt == m_nodeItems.end())
        return;

    bool ok = false;
    const QString name = QInputDialog::getText(this, "Block umbenennen", "Name:",
        QLineEdit::Normal, QString::fromStdString(node->name), &ok).trimmed();
    if (!ok || name.isEmpty())
        return;

    m_graph->RenameNode(nodeId, name.toStdString());
    itemIt->second->SetName(name.toStdString());
    emit NodesChanged();
}

void FlowsheetCanvasWidget::DeleteNodes(const std::vector<std::string>& nodeIds)
{
    if (!m_graph || nodeIds.empty())
        return;

    // Rueckfrage nur, wenn Inlets dabei sind - deren Zeitprofil und
    // Groessenverteilung waeren sonst ohne Warnung weg.
    QStringList inletNames;
    for (const std::string& id : nodeIds)
        if (const stosis::SFlowsheetNode* node = m_graph->FindNode(id); node && node->type == stosis::ENodeType::Inlet)
            inletNames << QString::fromStdString(node->name);
    if (!inletNames.isEmpty())
    {
        const QString text = QString("%1 loeschen?\n\nZeitprofil und Groessenverteilung gehen dabei verloren.")
            .arg(inletNames.join(", "));
        if (QMessageBox::question(this, "Block loeschen", text) != QMessageBox::Yes)
            return;
    }

    CancelConnecting();
    for (const std::string& id : nodeIds)
    {
        // Kabel, die an diesem Block haengen, zuerst entfernen.
        for (auto it = m_connectionLines.begin(); it != m_connectionLines.end();)
        {
            if (it->sourceNodeId == id || it->targetNodeId == id)
            {
                m_scene->removeItem(it->line);
                delete it->line;
                it = m_connectionLines.erase(it);
            }
            else
            {
                ++it;
            }
        }

        auto itemIt = m_nodeItems.find(id);
        if (itemIt != m_nodeItems.end())
        {
            m_scene->removeItem(itemIt->second);
            delete itemIt->second;
            m_nodeItems.erase(itemIt);
        }
        m_graph->RemoveNode(id);
    }

    UpdateConnectionStyles();
    // Ergebnisanzeigen und Einstellungen sofort neu laden - sie duerfen
    // nicht mehr auf die Ergebnisse geloeschter Bloecke zeigen.
    emit NodesChanged();
}

// ---------------------------------------------------------------------
// Verbinden per Klick-Klick
// ---------------------------------------------------------------------

void FlowsheetCanvasWidget::CancelConnecting()
{
    m_isConnecting = false;
    m_connectionSourceNodeId.clear();
    m_connectionSourcePort = 0;
    if (m_rubberBandLine)
    {
        m_scene->removeItem(m_rubberBandLine);
        delete m_rubberBandLine;
        m_rubberBandLine = nullptr;
    }
}

void FlowsheetCanvasWidget::HandleClick(QPointF scenePos)
{
    QGraphicsItem* hitItem = nullptr;
    {
        // itemAt() liefert normalerweise das oberste Item an dieser Position.
        // Waehrend eine Verbindung gezogen wird, liegt dort aber oft nur
        // das eigene Gummiband, das bei der Trefferpruefung nicht zaehlen
        // soll. Deshalb werden alle Items an dieser Position geprueft und
        // die Gummibandlinie gezielt uebersprungen.
        const QList<QGraphicsItem*> itemsHere = m_scene->items(scenePos, Qt::IntersectsItemShape,
            Qt::DescendingOrder, m_view->transform());
        for (QGraphicsItem* candidate : itemsHere)
        {
            if (candidate == m_rubberBandLine)
                continue;
            hitItem = candidate;
            break;
        }
    }
    auto* port = dynamic_cast<FlowsheetPortItem*>(hitItem);

    if (!m_isConnecting)
    {
        // Erster Klick: nur an einem freien Ausgangs-Port startet
        // ueberhaupt etwas.
        if (port && port->GetDirection() == EPortDirection::Output)
        {
            if (m_graph->IsOutputPortConnected(port->GetNodeId(), port->GetPortIndex()))
            {
                QToolTip::showText(QCursor::pos(),
                    "Dieser Ausgang ist schon verbunden.\n"
                    "Zum Aufteilen eines Stroms einen Splitter verwenden.", m_view);
                return;
            }

            m_isConnecting = true;
            m_connectionSourceNodeId = port->GetNodeId();
            m_connectionSourcePort = port->GetPortIndex();

            const QPointF startPos = port->scenePos();
            m_rubberBandLine = new QGraphicsLineItem(QLineF(startPos, startPos));
            m_rubberBandLine->setPen(QPen(Qt::darkGray, 1, Qt::DashLine));
            m_scene->addItem(m_rubberBandLine);
        }
        return;
    }

    // Zweiter Klick: verbindet nur, wenn ein passender, freier
    // Eingangs-Port getroffen wurde. In jedem anderen Fall wird verworfen.
    if (port && port->GetDirection() == EPortDirection::Input &&
        port->GetNodeId() != m_connectionSourceNodeId)
    {
        const std::string newConnId = m_graph->AddConnection(m_connectionSourceNodeId, m_connectionSourcePort,
            port->GetNodeId(), port->GetPortIndex());
        if (newConnId.empty())
        {
            QToolTip::showText(QCursor::pos(),
                "Dieser Eingang ist schon belegt.\n"
                "Zum Zusammenfuehren von Stroemen einen Merger verwenden.", m_view);
        }
        else
        {
            AddConnectionItem(newConnId, m_connectionSourceNodeId, m_connectionSourcePort,
                port->GetNodeId(), port->GetPortIndex());
            UpdateConnectionLines();
            UpdateConnectionStyles();
        }
    }

    CancelConnecting();
}

// ---------------------------------------------------------------------
// Ansicht: Zoom und ganzes Flowsheet
// ---------------------------------------------------------------------

void FlowsheetCanvasWidget::ZoomBy(double factor)
{
    const double current = m_view->transform().m11();
    const double target = std::clamp(current * factor, kMinZoom, kMaxZoom);
    if (current > 0.0 && target != current)
        m_view->scale(target / current, target / current);
}

// Zeigt alle Bloecke; vergroessert nie ueber 100 %, damit kleine
// Flowsheets nicht riesig erscheinen.
void FlowsheetCanvasWidget::FitAll()
{
    const QRectF items = m_scene->itemsBoundingRect().adjusted(-20.0, -20.0, 20.0, 20.0);
    const QRect viewport = m_view->viewport()->rect();
    if (items.isEmpty() || viewport.width() < 10 || viewport.height() < 10)
        return;
    const double fit = std::min(viewport.width() / items.width(), viewport.height() / items.height());
    const double zoom = std::clamp(fit, kMinZoom, 1.0);
    m_view->setTransform(QTransform::fromScale(zoom, zoom));
    m_view->centerOn(items.center());
}

// ---------------------------------------------------------------------
// Ereignisse: Maus, Tasten, Drag & Drop aus der Palette
// ---------------------------------------------------------------------

bool FlowsheetCanvasWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_view && event->type() == QEvent::KeyPress && m_graph)
    {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape && m_isConnecting)
        {
            CancelConnecting();
            return true;
        }
        if (keyEvent->key() == Qt::Key_Delete)
        {
            std::vector<std::string> selectedIds;
            for (QGraphicsItem* item : m_scene->selectedItems())
                if (auto* nodeItem = dynamic_cast<FlowsheetNodeItem*>(item))
                    selectedIds.push_back(nodeItem->GetNodeId());
            DeleteNodes(selectedIds);
            return true;
        }
    }

    if (watched == m_view->viewport())
    {
        switch (event->type())
        {
        case QEvent::DragEnter:
        case QEvent::DragMove:
        {
            auto* dragEvent = static_cast<QDragMoveEvent*>(event);
            if (dragEvent->mimeData()->hasFormat(kNodeTemplateMimeType))
            {
                dragEvent->acceptProposedAction();
                return true;
            }
            break;
        }
        case QEvent::Drop:
        {
            auto* dropEvent = static_cast<QDropEvent*>(event);
            if (!dropEvent->mimeData()->hasFormat(kNodeTemplateMimeType))
                break;
            bool ok = false;
            const size_t index = dropEvent->mimeData()->data(kNodeTemplateMimeType).toULongLong(&ok);
            if (ok && index < GetNodeTemplates().size())
            {
                const QPointF scenePos = m_view->mapToScene(dropEvent->position().toPoint());
                AddNodeFromTemplate(GetNodeTemplates()[index], scenePos, true);
            }
            dropEvent->acceptProposedAction();
            return true;
        }
        case QEvent::Wheel:
        {
            // Mausrad zoomt (ohne Strg); eine Raste = Faktor 1,15.
            auto* wheelEvent = static_cast<QWheelEvent*>(event);
            const int delta = wheelEvent->angleDelta().y();
            if (delta != 0)
            {
                ZoomBy(std::pow(1.15, delta / 120.0));
                m_userNavigated = true;
            }
            return true;
        }
        case QEvent::Resize:
            // Bis der Benutzer selbst zoomt oder verschiebt: ganzes
            // Flowsheet zeigen (z.B. nach dem Maximieren beim Start).
            if (!m_userNavigated)
                FitAll();
            break;
        case QEvent::MouseButtonRelease:
            if (m_isPanning)
            {
                m_isPanning = false;
                m_view->viewport()->unsetCursor();
                return true;
            }
            break;
        case QEvent::MouseMove:
            if (m_isPanning)
            {
                // Ansicht mit der Maus mitziehen.
                auto* mouseEvent = static_cast<QMouseEvent*>(event);
                const QPoint delta = mouseEvent->pos() - m_panLastPos;
                m_panLastPos = mouseEvent->pos();
                m_view->horizontalScrollBar()->setValue(m_view->horizontalScrollBar()->value() - delta.x());
                m_view->verticalScrollBar()->setValue(m_view->verticalScrollBar()->value() - delta.y());
                return true;
            }
            if (m_isConnecting && m_rubberBandLine)
            {
                auto* mouseEvent = static_cast<QMouseEvent*>(event);
                const QPointF scenePos = m_view->mapToScene(mouseEvent->pos());
                QLineF line = m_rubberBandLine->line();
                line.setP2(scenePos);
                m_rubberBandLine->setLine(line);
            }
            break;
        case QEvent::MouseButtonPress:
        {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            // Verschieben: mittlere Maustaste ueberall, linke auf freier
            // Flaeche (nicht waehrend eine Verbindung gezogen wird). Die
            // linke Taste geht trotzdem an die View weiter, damit ein Klick
            // ins Leere die Auswahl aufhebt.
            const bool onEmpty = m_scene->itemAt(m_view->mapToScene(mouseEvent->pos()), m_view->transform()) == nullptr;
            if (mouseEvent->button() == Qt::MiddleButton
                || (mouseEvent->button() == Qt::LeftButton && onEmpty && !m_isConnecting))
            {
                m_isPanning = true;
                m_panLastPos = mouseEvent->pos();
                m_userNavigated = true;
                m_view->viewport()->setCursor(Qt::ClosedHandCursor);
                if (mouseEvent->button() == Qt::MiddleButton)
                    return true;
            }
            if (mouseEvent->button() == Qt::LeftButton && m_graph)
            {
                const QPointF scenePos = m_view->mapToScene(mouseEvent->pos());
                HandleClick(scenePos);

                // Waehrend eines laufenden Verbindungs-Versuchs (oder
                // wenn der Klick einen Port getroffen hat) soll die
                // Box selbst NICHT gleichzeitig als Drag interpretiert
                // werden - das Event wird in diesem Fall verschluckt.
                QGraphicsItem* hitItem = m_scene->itemAt(scenePos, m_view->transform());
                if (dynamic_cast<FlowsheetPortItem*>(hitItem) != nullptr || m_isConnecting)
                    return true;
            }
            break;
        }
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}
