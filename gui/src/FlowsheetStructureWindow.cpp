#include "FlowsheetStructureWindow.h"
#include "StructureDiagramWidget.h"
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSplitter>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTextBlock>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>
#include <map>

namespace
{
    enum EColumn { kColBranch, kColStep, kColType, kColPort, kColNote, kNumColumns };

    // Daten am Eintrag (Spalte 0): Knoten, Verbindung, Art (SStructureBranch::EKind).
    constexpr int kRoleNode = Qt::UserRole;
    constexpr int kRoleConnection = Qt::UserRole + 1;
    constexpr int kRoleKind = Qt::UserRole + 2;

    // Wie die aufgeschnittenen Verbindungen im Flowsheet.
    QColor CutColor() { return QColor(210, 120, 30); }

    const QString kScissors = QString(QChar(0x2702));
    const QString kArrow = QString(QChar(0x2192));

    // Schliessende Klammer zu text[open] ('{' oder '['), Texte in "..." uebersprungen.
    int MatchBrace(const QString& text, int open)
    {
        int depth = 0;
        bool inString = false;
        for (int i = open; i < text.size(); ++i)
        {
            const QChar ch = text[i];
            if (inString)
            {
                if (ch == '\\')
                    ++i;
                else if (ch == '"')
                    inString = false;
                continue;
            }
            if (ch == '"')
                inString = true;
            else if (ch == '{' || ch == '[')
                ++depth;
            else if ((ch == '}' || ch == ']') && --depth == 0)
                return i;
        }
        return static_cast<int>(text.size()) - 1;
    }
}

FlowsheetStructureWindow::FlowsheetStructureWindow(QWidget* parent)
    : QWidget(parent, Qt::Window)
{
    setWindowTitle("Flowsheet-Struktur");
    resize(1350, 780);

    auto* layout = new QVBoxLayout(this);
    auto* splitter = new QSplitter(Qt::Horizontal, this);

    // Links: Reihenfolge und Kreise, darunter die Reiter "Bild" und "Text".
    auto* left = new QWidget(splitter);
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    m_summary = new QLabel(left);
    m_summary->setWordWrap(true);
    m_summary->setTextFormat(Qt::RichText);
    m_summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    leftLayout->addWidget(m_summary);
    m_tabs = new QTabWidget(left);
    leftLayout->addWidget(m_tabs, 1);

    // Reiter "Bild": Aststruktur als Zeichnung.
    auto* picturePage = new QWidget(m_tabs);
    auto* pictureLayout = new QVBoxLayout(picturePage);
    m_diagram = new StructureDiagramWidget(picturePage);
    pictureLayout->addWidget(m_diagram, 1);
    auto* pictureLegend = new QLabel(picturePage);
    pictureLegend->setWordWrap(true);
    pictureLegend->setStyleSheet("color: gray;");
    pictureLegend->setText("Kasten = Unit, darunter ihre Nummer \"node k\" = Platz in der Rechenreihenfolge ab 0 (bei "
                           "jedem Anzeigen neu vergeben; die ID in der Datei bleibt). Jede Unit steht rechts neben dem "
                           "Vorgaenger an ihrem ersten Eingang. " + kArrow + " k orange: aufgeschnittene Rueckfuehrung, "
                           "Unit k liest dort den Strom des vorigen Zeitschritts. " + kArrow + " k grau: weiterer Zulauf "
                           "zu Unit k, die an anderer Stelle im Bild steht. Mausrad zoomt, Ziehen verschiebt, "
                           "Doppelklick passt ein.");
    pictureLayout->addWidget(pictureLegend);
    m_tabs->addTab(picturePage, "Bild");

    // Reiter "Text": derselbe Baum als Liste.
    auto* textPage = new QWidget(m_tabs);
    auto* textLayout = new QVBoxLayout(textPage);
    m_tree = new QTreeWidget(textPage);
    m_tree->setColumnCount(kNumColumns);
    m_tree->setHeaderLabels({ "Ast (in Stromrichtung)", "Nr.", "Typ", "Anschluss", "Hinweis" });
    m_tree->headerItem()->setToolTip(kColStep, "node k = Platz in der Rechenreihenfolge ab 0 (wie im Bild): In jedem\n"
                                               "Zeitschritt rechnen die Units in dieser Folge.");
    m_tree->headerItem()->setToolTip(kColPort, "Ausgang des Vorgaengers und Eingang dieser Unit, wenn es mehrere gibt.");
    m_tree->setUniformRowHeights(true);
    m_tree->header()->setStretchLastSection(true);
    textLayout->addWidget(m_tree, 1);
    auto* legend = new QLabel(textPage);
    legend->setWordWrap(true);
    legend->setStyleSheet("color: gray;");
    legend->setText("Jede Unit steht unter dem Vorgaenger an ihrem ersten Eingang. " + kScissors
        + " aufgeschnittene Verbindung: Das Ziel liest den Strom aus dem vorigen Zeitschritt (Totzeit dt, im Flowsheet "
          "orange gestrichelt). " + kArrow + " weitere Verbindung zu einer Unit, die an anderer Stelle im Baum steht "
          "(z.B. zweiter Eingang eines Mergers).");
    textLayout->addWidget(legend);
    m_tabs->addTab(textPage, "Text");

    // Rechts: das Flowsheet, wie es in der Datei steht.
    auto* right = new QWidget(splitter);
    auto* rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->addWidget(new QLabel("<b>So steht das Flowsheet in der Datei (JSON)</b>", right));
    auto* jsonNote = new QLabel(right);
    jsonNote->setWordWrap(true);
    jsonNote->setStyleSheet("color: gray;");
    jsonNote->setText("Nur Eingaben: Simulation, Ausfuehrung, Units mit allen Einstellungen, Verbindungen. Bei jeder "
                      "Verbindung \"recycle\": \"manual\" = von Hand als Rueckfuehrung markiert, \"auto\" = vom Graphen "
                      "aufgeschnitten (beim Laden neu bestimmt), \"no\" = nicht aufgeschnitten. Die Rechenreihenfolge "
                      "steht nicht darin. Stoffe nur mit Namen (Materialien.json). Ein Klick links markiert die Stelle hier.");
    rightLayout->addWidget(jsonNote);
    m_json = new QPlainTextEdit(right);
    m_json->setReadOnly(true);
    m_json->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_json->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    rightLayout->addWidget(m_json, 1);

    splitter->addWidget(left);
    splitter->addWidget(right);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    layout->addWidget(splitter, 1);

    auto* bottom = new QHBoxLayout();
    m_saveInfo = new QLabel(this);
    m_saveInfo->setStyleSheet("color: gray;");
    m_saveInfo->setTextInteractionFlags(Qt::TextSelectableByMouse);
    bottom->addWidget(m_saveInfo, 1);
    auto* saveButton = new QPushButton("Flowsheet speichern unter ...", this);
    connect(saveButton, &QPushButton::clicked, this, [this]()
        {
            if (!m_graph)
                return;
            const QString path = SaveFlowsheetAs(this, *m_graph,
                m_settingsProvider ? m_settingsProvider() : stosis::SFlowsheetFileSettings());
            if (!path.isEmpty())
                m_saveInfo->setText("Gespeichert: " + QDir::toNativeSeparators(path));
        });
    bottom->addWidget(saveButton);
    layout->addLayout(bottom);

    connect(m_tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* current)
        {
            if (!current)
                HighlightInJson(QString(), QString(), -1);
            else
                HighlightInJson(current->data(kColBranch, kRoleNode).toString(),
                    current->data(kColBranch, kRoleConnection).toString(), current->data(kColBranch, kRoleKind).toInt());
        });
    connect(m_diagram, &StructureDiagramWidget::ItemClicked, this, &FlowsheetStructureWindow::HighlightInJson);
}

void FlowsheetStructureWindow::SetGraph(stosis::CFlowsheetGraph* graph)
{
    m_graph = graph;
    m_lastJson.clear();
    Refresh();
}

void FlowsheetStructureWindow::SetFileSettingsProvider(std::function<stosis::SFlowsheetFileSettings()> provider)
{
    m_settingsProvider = std::move(provider);
    Refresh();
}

void FlowsheetStructureWindow::Refresh()
{
    if (isVisible())
        Rebuild();
}

void FlowsheetStructureWindow::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    Rebuild();
}

void FlowsheetStructureWindow::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    // Units verschoben oder Protokoll umgestellt: beim Zurueckkommen neu.
    if (event->type() == QEvent::ActivationChange && isActiveWindow())
        Refresh();
}

QString FlowsheetStructureWindow::NodeName(const std::string& nodeId) const
{
    const stosis::SFlowsheetNode* node = m_graph ? m_graph->FindNode(nodeId) : nullptr;
    return QString::fromStdString(node ? node->name : nodeId);
}

void FlowsheetStructureWindow::Rebuild()
{
    if (!m_graph)
        return;
    const stosis::SFlowsheetFileSettings settings = m_settingsProvider ? m_settingsProvider() : stosis::SFlowsheetFileSettings();
    const QString json = QString::fromStdString(stosis::WriteFlowsheetJson(*m_graph, settings));
    // Die Datei enthaelt alles, was die Struktur bestimmt: unveraendert = nichts zu tun.
    if (json == m_lastJson)
        return;
    m_lastJson = json;

    // Auswahl und Bildlauf behalten.
    QString selectedNode;
    QString selectedConnection;
    int selectedKind = -1;
    if (const QTreeWidgetItem* current = m_tree->currentItem())
    {
        selectedNode = current->data(kColBranch, kRoleNode).toString();
        selectedConnection = current->data(kColBranch, kRoleConnection).toString();
        selectedKind = current->data(kColBranch, kRoleKind).toInt();
    }
    const int jsonScroll = m_json->verticalScrollBar()->value();

    const stosis::SFlowsheetStructure structure = m_graph->GetStructure();
    // Unter welcher Unit jede Unit im Baum steht (leer = Wurzel), fuer die Verweise.
    std::map<std::string, std::string> placedUnder;
    std::function<void(const stosis::SStructureBranch&, const std::string&)> collect =
        [&](const stosis::SStructureBranch& branch, const std::string& parent)
        {
            if (branch.kind != stosis::SStructureBranch::EKind::Unit)
                return;
            placedUnder[branch.nodeId] = parent;
            for (const stosis::SStructureBranch& child : branch.children)
                collect(child, branch.nodeId);
        };
    for (const stosis::SStructureBranch& root : structure.roots)
        collect(root, std::string());

    m_summary->setText(DescribeSummary(structure));
    m_diagram->SetStructure(*m_graph, structure);
    m_tree->clear();
    for (const stosis::SStructureBranch& root : structure.roots)
        AddBranch(nullptr, root, structure, placedUnder);
    m_tree->expandAll();
    for (int column = 0; column < kNumColumns - 1; ++column)
        m_tree->resizeColumnToContents(column);

    m_json->setPlainText(json);
    m_json->verticalScrollBar()->setValue(jsonScroll);

    if (selectedKind >= 0)
        for (QTreeWidgetItemIterator it(m_tree); *it; ++it)
            if ((*it)->data(kColBranch, kRoleNode).toString() == selectedNode
                && (*it)->data(kColBranch, kRoleConnection).toString() == selectedConnection
                && (*it)->data(kColBranch, kRoleKind).toInt() == selectedKind)
            {
                m_tree->setCurrentItem(*it);
                break;
            }
}

void FlowsheetStructureWindow::AddBranch(QTreeWidgetItem* parent, const stosis::SStructureBranch& branch,
    const stosis::SFlowsheetStructure& structure, const std::map<std::string, std::string>& placedUnder)
{
    using EKind = stosis::SStructureBranch::EKind;
    const stosis::SFlowsheetNode* node = m_graph->FindNode(branch.nodeId);
    const stosis::SFlowsheetConnection* connection =
        branch.connectionId.empty() ? nullptr : m_graph->FindConnection(branch.connectionId);
    auto* item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
    item->setData(kColBranch, kRoleNode, QString::fromStdString(branch.nodeId));
    item->setData(kColBranch, kRoleConnection, QString::fromStdString(branch.connectionId));
    item->setData(kColBranch, kRoleKind, static_cast<int>(branch.kind));

    const QString name = NodeName(branch.nodeId);
    item->setText(kColType, node ? QString(stosis::GetNodeTypeLabel(node->type)) : QString());
    if (connection)
    {
        const stosis::SFlowsheetNode* source = m_graph->FindNode(connection->sourceNodeId);
        QStringList port;
        if (source && source->numOutputs > 1)
            port << "aus Ausgang " + QString::number(connection->sourcePort + 1);
        if (node && node->numInputs > 1)
            port << "in Eingang " + QString::number(connection->targetPort + 1);
        item->setText(kColPort, port.join(", "));
    }

    // Nummer wie im Bild: Platz in der Rechenreihenfolge ab 0.
    const QString number = QString::number(static_cast<int>(structure.GetStep(branch.nodeId)) - 1);
    if (branch.kind == EKind::Unit)
    {
        item->setText(kColBranch, name);
        item->setText(kColStep, number);
        const int cycle = structure.GetCycle(branch.nodeId);
        if (cycle >= 0)
            item->setText(kColNote, "Kreis " + QString::number(cycle + 1));
        QFont font = item->font(kColBranch);
        font.setBold(true);
        item->setFont(kColBranch, font);
        for (const stosis::SStructureBranch& child : branch.children)
            AddBranch(item, child, structure, placedUnder);
        return;
    }

    if (branch.kind == EKind::Cut)
    {
        const bool byHand = connection && connection->isRecycle;
        item->setText(kColBranch, kScissors + " " + name + " (node " + number + ")");
        item->setText(kColNote, QString("aufgeschnitten (") + (byHand ? "von Hand markiert" : "vom Graphen gewaehlt")
            + "): liest den Strom des vorigen Zeitschritts");
        const QString tip = byHand
            ? "Von Hand als Rueckfuehrung markiert (in der Datei \"recycle\": \"manual\")."
            : "Der Graph hat den Kreis hier aufgeschnitten (Regel im Methodenhandbuch 3.2);\n"
              "in der Datei \"recycle\": \"auto\", beim Laden wird es neu bestimmt.";
        for (int column = 0; column < kNumColumns; ++column)
        {
            item->setForeground(column, CutColor());
            item->setToolTip(column, tip);
        }
        return;
    }

    // Verweis: die Unit steht an anderer Stelle im Baum.
    item->setText(kColBranch, kArrow + " " + name + " (node " + number + ")");
    const auto it = placedUnder.find(branch.nodeId);
    item->setText(kColNote, it != placedUnder.end() && !it->second.empty()
        ? "laeuft hier zusammen; " + name + " steht unter " + NodeName(it->second)
        : "laeuft hier zusammen");
    QFont font = item->font(kColBranch);
    font.setItalic(true);
    for (int column = 0; column < kNumColumns; ++column)
    {
        item->setForeground(column, QColor(110, 110, 110));
        item->setFont(column, font);
    }
}

QString FlowsheetStructureWindow::DescribeSummary(const stosis::SFlowsheetStructure& structure) const
{
    QStringList steps;
    for (size_t i = 0; i < structure.order.size(); ++i)
        steps << QString::number(i) + "&nbsp;" + NodeName(structure.order[i]).toHtmlEscaped();
    QString text = "<b>Rechenreihenfolge je Zeitschritt:</b> " + (steps.isEmpty() ? QString("(leer)") : steps.join(" &rarr; "));

    // Schnitte je Kreis; von Hand markierte Verbindungen ausserhalb eines Kreises extra.
    std::vector<QStringList> cuts(structure.cycles.size());
    QStringList outside;
    for (const stosis::SFlowsheetConnection& c : m_graph->GetConnections())
    {
        if (!structure.lagged.count(c.id))
            continue;
        const stosis::SFlowsheetNode* target = m_graph->FindNode(c.targetNodeId);
        QString what = NodeName(c.sourceNodeId).toHtmlEscaped() + " &rarr; " + NodeName(c.targetNodeId).toHtmlEscaped();
        if (target && target->numInputs > 1)
            what += " (Eingang " + QString::number(c.targetPort + 1) + ")";
        what += c.isRecycle ? ", von Hand markiert" : ", vom Graphen gewaehlt";
        const int cycle = structure.GetCycle(c.sourceNodeId);
        if (cycle >= 0 && cycle == structure.GetCycle(c.targetNodeId))
            cuts[static_cast<size_t>(cycle)] << what;
        else
            outside << what;
    }
    if (structure.cycles.empty())
        text += "<br><b>Kreise:</b> keine";
    for (size_t i = 0; i < structure.cycles.size(); ++i)
    {
        QStringList members;
        for (const std::string& id : structure.cycles[i])
            members << NodeName(id).toHtmlEscaped();
        text += "<br><b>Kreis " + QString::number(i + 1) + ":</b> " + members.join(", ")
            + " &ndash; aufgeschnitten: " + cuts[i].join("; ");
    }
    if (!outside.isEmpty())
        text += "<br><b>Von Hand markiert, aber auf keinem Kreis:</b> " + outside.join("; ")
            + " (wirkt trotzdem wie eine Totzeit von einem Zeitschritt)";
    return text;
}

void FlowsheetStructureWindow::HighlightInJson(const QString& nodeId, const QString& connectionId, int kind)
{
    m_json->setExtraSelections({});
    if (kind < 0 || !m_graph)
        return;
    const QString text = m_json->toPlainText();
    qsizetype at = -1;
    if (kind == static_cast<int>(stosis::SStructureBranch::EKind::Unit))
        at = text.indexOf("\"id\": \"" + nodeId + "\"");
    else if (const stosis::SFlowsheetConnection* c = m_graph->FindConnection(connectionId.toStdString()))
        at = text.indexOf(QString("\"from\": \"%1\", \"output\": %2, \"to\": \"%3\", \"input\": %4")
            .arg(QString::fromStdString(c->sourceNodeId)).arg(c->sourcePort + 1)
            .arg(QString::fromStdString(c->targetNodeId)).arg(c->targetPort + 1));
    if (at < 0)
        return;
    const int start = static_cast<int>(text.lastIndexOf(QChar('{'), at));
    if (start < 0)
        return;
    const int end = MatchBrace(text, start);

    // Ganze Zeilen des Eintrags hinterlegen und den Anfang in die Mitte holen.
    QList<QTextEdit::ExtraSelection> selections;
    const QTextBlock first = m_json->document()->findBlock(start);
    const QTextBlock last = m_json->document()->findBlock(end);
    for (QTextBlock block = first; block.isValid(); block = block.next())
    {
        QTextEdit::ExtraSelection selection;
        selection.cursor = QTextCursor(block);
        selection.format.setBackground(QColor(255, 236, 179));
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        selections.append(selection);
        if (block == last)
            break;
    }
    m_json->setExtraSelections(selections);
    m_json->setTextCursor(QTextCursor(first));
    m_json->centerCursor();
}

QString FlowsheetStructureWindow::SaveFlowsheetAs(QWidget* parent, const stosis::CFlowsheetGraph& graph,
    const stosis::SFlowsheetFileSettings& settings)
{
    static QString lastPath;
    if (lastPath.isEmpty())
        lastPath = QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath("Flowsheet.json");
    const QString path = QFileDialog::getSaveFileName(parent, "Flowsheet speichern", lastPath, "StoSiS-Flowsheet (*.json)");
    if (path.isEmpty())
        return QString();
    lastPath = path;
    std::string error;
    if (!stosis::SaveFlowsheetFile(path.toStdString(), graph, settings, error))
    {
        QMessageBox::warning(parent, "Flowsheet speichern", QString::fromStdString(error));
        return QString();
    }
    return path;
}
