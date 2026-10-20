#pragma once
#include <vector>
#include <map>
#include <set>
#include <memory>
#include <string>
#include <algorithm>
#include <functional>
#include "FlowsheetUnit.h"
#include "Random.h"
#include "EnsembleStatistics.h"
#include "SimulationResults.h"
#include "StreamRecord.h"
#include "Materials.h"
#include "InletUnit.h"
#include "OutletUnit.h"
#include "SplitterUnit.h"
#include "MergerUnit.h"
#include "StreamProcessorRegistry.h"
#include "BuiltinStreamProcessors.h"

namespace stosis
{
    // Knotentyp im Flowsheet. Erweiterbar - weitere Einheiten (Screen,
    // Crusher, ...) kommen als weiterer Eintrag dazu, das aendert an der
    // Grundstruktur nichts.
    enum class ENodeType
    {
        Inlet,
        Outlet,
        Process,
        Splitter,
        Merger
    };

    // Anzeigename eines Knotentyps - an einer Stelle, damit alle Listen
    // (Einstellungen, Kurven, Heatmap) dieselben Bezeichnungen zeigen.
    inline const char* GetNodeTypeLabel(ENodeType type)
    {
        switch (type)
        {
        case ENodeType::Inlet:    return "Inlet";
        case ENodeType::Outlet:   return "Outlet";
        case ENodeType::Process:  return "Prozess";
        case ENodeType::Splitter: return "Splitter";
        case ENodeType::Merger:   return "Merger";
        }
        return "?";
    }

    // Ein einzelner Knoten im Flowsheet: Name, Typ, Anzahl Ein-/Ausgaenge
    // und Position auf der Zeichenflaeche. Die eigentliche Recheneinheit
    // (CInletUnit, CSplitterUnit, ...) liegt NICHT hier drin, sondern
    // separat in CFlowsheetGraph, ueber die Knoten-ID referenziert.
    struct SFlowsheetNode
    {
        std::string id;
        std::string name;
        ENodeType type = ENodeType::Inlet;
        size_t numInputs = 0;
        size_t numOutputs = 0;
        double posX = 0.0;
        double posY = 0.0;
    };

    // Eine Verbindung von einem AUSGANG (sourcePort) eines Knotens zu
    // einem EINGANG (targetPort) eines anderen. Jeder Port traegt
    // hoechstens eine Verbindung - Aufteilen/Zusammenfuehren erledigen
    // Splitter und Merger. Eine Verbindung selbst ist verzoegerungsfrei,
    // ausser sie schneidet einen Kreis auf (siehe CFlowsheetGraph).
    struct SFlowsheetConnection
    {
        std::string id;
        std::string sourceNodeId;
        size_t sourcePort = 0;
        std::string targetNodeId;
        size_t targetPort = 0;
        // Vom Nutzer als Rueckfuehrung markiert: hier wird ein Kreis
        // aufgeschnitten (sonst waehlt der Graph die Stelle selbst).
        bool isRecycle = false;
    };

    // Ein anzeigbares Ergebnis fuer die Ergebnisfenster: ein Knoten plus
    // Port (ein Splitter liefert z.B. ein Ergebnis pro Ausgang).
    struct SResultsEntry
    {
        std::string nodeId;
        size_t port = 0;
        std::string label;
    };

    // Generisches Flowsheet: beliebig viele Knoten, beliebig viele
    // Verbindungen dazwischen, auch mit Rueckfuehrungen (Kreisen).
    //
    // Simuliert wird in Makroschritten (siehe IFlowsheetUnit): In jedem
    // Schritt rechnen die Units in Flussrichtung nacheinander, jede liest
    // die Ausgaenge ihrer Vorgaenger aus DEMSELBEN Schritt - Material
    // laeuft also ohne kuenstliche Verzoegerung durch beliebig lange
    // Ketten. In einem Kreis gibt es keinen "Ersten"; dort wird genau eine
    // Verbindung aufgeschnitten: ihr Ziel liest den Wert aus dem
    // VORHERIGEN Schritt. Das wirkt wie ein kurzes Rohr der Laenge dt.
    class CFlowsheetGraph
    {
    public:
        CFlowsheetGraph()
        {
            RegisterBuiltinStreamProcessors(m_processorRegistry);
        }

        std::string AddInletNode(const std::string& name, double posX, double posY)
        {
            return AddNode(name, ENodeType::Inlet, 0, 1, posX, posY, std::make_unique<CInletUnit>());
        }

        std::string AddOutletNode(const std::string& name, double posX, double posY)
        {
            return AddNode(name, ENodeType::Outlet, 1, 0, posX, posY, std::make_unique<COutletUnit>());
        }

        std::string AddProcessNode(const std::string& name, double posX, double posY)
        {
            const std::vector<std::string> names = m_processorRegistry.GetAvailableTypeNames();
            std::unique_ptr<IFlowsheetUnit> processor;
            if (!names.empty())
                processor = m_processorRegistry.CreateProcessor(names.front());
            return AddNode(name, ENodeType::Process, 1, 1, posX, posY, std::move(processor));
        }

        std::string AddSplitterNode(const std::string& name, size_t numOutputs, double posX, double posY)
        {
            return AddNode(name, ENodeType::Splitter, 1, numOutputs, posX, posY,
                std::make_unique<CSplitterUnit>(numOutputs));
        }

        std::string AddMergerNode(const std::string& name, size_t numInputs, double posX, double posY)
        {
            return AddNode(name, ENodeType::Merger, numInputs, 1, posX, posY,
                std::make_unique<CMergerUnit>(numInputs));
        }

        // Entfernt einen Knoten samt seiner Recheneinheit, seiner Ergebnisse
        // und aller Verbindungen, die an ihm haengen. Achtung: Zeiger auf
        // seine Ergebnisse sind danach ungueltig - Anzeigen neu laden.
        void RemoveNode(const std::string& nodeId)
        {
            m_connections.erase(
                std::remove_if(m_connections.begin(), m_connections.end(),
                    [&nodeId](const SFlowsheetConnection& c)
                    {
                        return c.sourceNodeId == nodeId || c.targetNodeId == nodeId;
                    }),
                m_connections.end());
            m_units.erase(nodeId);
            m_outputRecords.erase(nodeId);
            m_statistics.erase(nodeId);
            m_nodes.erase(
                std::remove_if(m_nodes.begin(), m_nodes.end(),
                    [&nodeId](const SFlowsheetNode& n) { return n.id == nodeId; }),
                m_nodes.end());
        }

        void RenameNode(const std::string& nodeId, const std::string& name)
        {
            for (SFlowsheetNode& node : m_nodes)
                if (node.id == nodeId)
                    node.name = name;
        }

        // Kurzform fuer Knoten mit je einem Ein-/Ausgang.
        std::string AddConnection(const std::string& sourceNodeId, const std::string& targetNodeId)
        {
            return AddConnection(sourceNodeId, 0, targetNodeId, 0);
        }

        // Liefert die ID der neuen Verbindung - oder einen LEEREN String,
        // wenn sie ungueltig waere: unbekannter Knoten/Port, Verbindung
        // auf sich selbst, oder einer der beiden Ports ist schon belegt.
        std::string AddConnection(const std::string& sourceNodeId, size_t sourcePort,
            const std::string& targetNodeId, size_t targetPort)
        {
            const SFlowsheetNode* source = FindNode(sourceNodeId);
            const SFlowsheetNode* target = FindNode(targetNodeId);
            if (!source || !target || sourceNodeId == targetNodeId)
                return std::string();
            if (sourcePort >= source->numOutputs || targetPort >= target->numInputs)
                return std::string();
            if (IsOutputPortConnected(sourceNodeId, sourcePort) || IsInputPortConnected(targetNodeId, targetPort))
                return std::string();

            SFlowsheetConnection conn;
            conn.id = MakeConnectionId();
            conn.sourceNodeId = sourceNodeId;
            conn.sourcePort = sourcePort;
            conn.targetNodeId = targetNodeId;
            conn.targetPort = targetPort;
            m_connections.push_back(conn);
            return conn.id;
        }

        void RemoveConnection(const std::string& connectionId)
        {
            m_connections.erase(
                std::remove_if(m_connections.begin(), m_connections.end(),
                    [&connectionId](const SFlowsheetConnection& c) { return c.id == connectionId; }),
                m_connections.end());
        }

        // Markiert eine Verbindung als Rueckfuehrung (oder hebt das auf).
        void SetConnectionRecycle(const std::string& connectionId, bool isRecycle)
        {
            for (SFlowsheetConnection& conn : m_connections)
                if (conn.id == connectionId)
                    conn.isRecycle = isRecycle;
        }

        // Alle Verbindungen, deren Ziel den Wert aus dem vorherigen Schritt
        // liest: vom Nutzer markierte plus die, die der Graph selbst zum
        // Aufschneiden von Kreisen gewaehlt hat. Leer, wenn es keine Kreise gibt.
        std::set<std::string> GetLaggedConnectionIds() const
        {
            std::vector<std::string> order;
            std::set<std::string> lagged;
            BuildExecutionPlan(order, lagged);
            return lagged;
        }

        bool IsOutputPortConnected(const std::string& nodeId, size_t port) const
        {
            for (const SFlowsheetConnection& conn : m_connections)
                if (conn.sourceNodeId == nodeId && conn.sourcePort == port)
                    return true;
            return false;
        }

        bool IsInputPortConnected(const std::string& nodeId, size_t port) const
        {
            for (const SFlowsheetConnection& conn : m_connections)
                if (conn.targetNodeId == nodeId && conn.targetPort == port)
                    return true;
            return false;
        }

        // Vorschlag fuer den Namen eines neuen Knotens: Typ-Bezeichnung plus
        // die kleinste noch freie Nummer DIESES Typs (Inlet1, Inlet2, ...,
        // Prozess1, ...). Die Nummer zaehlt also je Knotentyp, nicht global.
        std::string MakeDefaultNodeName(ENodeType type) const
        {
            const std::string prefix = GetNodeTypeLabel(type);
            for (size_t number = 1;; ++number)
            {
                const std::string candidate = prefix + std::to_string(number);
                const bool taken = std::any_of(m_nodes.begin(), m_nodes.end(),
                    [&candidate](const SFlowsheetNode& node) { return node.name == candidate; });
                if (!taken)
                    return candidate;
            }
        }

        const std::vector<SFlowsheetNode>& GetNodes() const { return m_nodes; }
        const std::vector<SFlowsheetConnection>& GetConnections() const { return m_connections; }

        const SFlowsheetNode* FindNode(const std::string& nodeId) const
        {
            for (const SFlowsheetNode& node : m_nodes)
                if (node.id == nodeId)
                    return &node;
            return nullptr;
        }

        CInletUnit* GetInlet(const std::string& nodeId) { return FindUnit<CInletUnit>(nodeId); }
        COutletUnit* GetOutlet(const std::string& nodeId) { return FindUnit<COutletUnit>(nodeId); }
        IStreamProcessor* GetProcessor(const std::string& nodeId) { return FindUnit<IStreamProcessor>(nodeId); }
        CSplitterUnit* GetSplitter(const std::string& nodeId) { return FindUnit<CSplitterUnit>(nodeId); }
        CMergerUnit* GetMerger(const std::string& nodeId) { return FindUnit<CMergerUnit>(nodeId); }

        const CStreamProcessorRegistry& GetProcessorRegistry() const { return m_processorRegistry; }

        // Tauscht den Prozessortyp eines Knotens aus. Die alte Instanz wird
        // zerstoert; die Ergebnisse liegen im Graphen und bleiben gueltig.
        bool SetProcessorType(const std::string& nodeId, const std::string& typeName)
        {
            if (!GetProcessor(nodeId))
                return false;
            std::unique_ptr<IStreamProcessor> processor = m_processorRegistry.CreateProcessor(typeName);
            if (!processor)
                return false;
            m_units[nodeId] = std::move(processor);
            return true;
        }

        // Was an einem AUSGANG eines Knotens herauskommt (und an den
        // naechsten Knoten weitergereicht wird), ueber die ganze Zeit.
        // nullptr, wenn der Knoten diesen Ausgang nicht hat (z.B. Outlet).
        const CSimulationResults* GetOutputResults(const std::string& nodeId, size_t port) const
        {
            auto it = m_outputRecords.find(nodeId);
            if (it == m_outputRecords.end() || port >= it->second.size())
                return nullptr;
            return &it->second[port].GetTotal();
        }

        // Vollstaendiger Verlauf eines Ausgangs: je Stoff, Temperatur, Druck.
        const CStreamRecord* GetOutputRecord(const std::string& nodeId, size_t port) const
        {
            auto it = m_outputRecords.find(nodeId);
            if (it == m_outputRecords.end() || port >= it->second.size())
                return nullptr;
            return &it->second[port];
        }

        // Was die Ergebnisfenster fuer einen Eintrag aus GetResultsEntries()
        // anzeigen: beim Outlet das, was ankommt, sonst der Ausgang.
        const CSimulationResults* GetDisplayResults(const std::string& nodeId, size_t port)
        {
            const CStreamRecord* record = GetDisplayRecord(nodeId, port);
            return record ? &record->GetTotal() : nullptr;
        }

        const CStreamRecord* GetDisplayRecord(const std::string& nodeId, size_t port)
        {
            if (COutletUnit* outlet = GetOutlet(nodeId))
                return &outlet->GetRecord();
            return GetOutputRecord(nodeId, port);
        }

        // Stoffdaten fuer alle Units (Energiebilanzen). nullptr ist erlaubt,
        // dann fehlen nur die Stoffwerte. Die Datenbank muss leben, solange
        // der Graph simuliert.
        void SetMaterialDatabase(const CMaterialDatabase* materials) { m_materials = materials; }

        // Aktive Stoffe der naechsten Simulation: die Stoffe aller Inlets in
        // der Reihenfolge der Materialdatei.
        std::vector<std::string> GetActiveCompounds()
        {
            PrepareMaterials();
            return m_grid.compounds;
        }

        // Hinweise der letzten Simulation (z.B. fehlende Waermekapazitaet),
        // je Hinweis "Knotenname: Text", ohne Doppelungen.
        std::vector<std::string> GetSimulationWarnings() const
        {
            return std::vector<std::string>(m_warnings.begin(), m_warnings.end());
        }

        // Alle anzeigbaren Ergebnisse - ein Eintrag pro Knoten, beim
        // Splitter einer pro Ausgang.
        std::vector<SResultsEntry> GetResultsEntries() const
        {
            std::vector<SResultsEntry> entries;
            for (const SFlowsheetNode& node : m_nodes)
            {
                const std::string base = node.name + " (" + GetNodeTypeLabel(node.type);
                if (node.type == ENodeType::Splitter)
                {
                    for (size_t port = 0; port < node.numOutputs; ++port)
                        entries.push_back({ node.id, port, base + ", Ausgang " + std::to_string(port + 1) + ")" });
                }
                else
                {
                    entries.push_back({ node.id, 0, base + ")" });
                }
            }
            return entries;
        }

        // Speichert das gemeinsame Zeit-/Groessenraster des Flowsheets. Es wird
        // erst in RunSimulation() an die Units verteilt - so bekommen auch
        // spaeter hinzugefuegte Units automatisch das richtige Raster.
        // Groessen in m; type = linear oder logarithmisch (SSizeGrid).
        void ConfigureGrid(size_t numClasses, double sizeMin, double sizeMax,
            size_t numTimeSteps, double totalTime, ESizeGridType type = ESizeGridType::Logarithmic)
        {
            m_grid.sizeGridType = type;
            m_grid.numSizeClasses = numClasses;
            m_grid.sizeMin = sizeMin;
            m_grid.sizeMax = sizeMax;
            m_grid.numTimeSteps = numTimeSteps;
            m_grid.totalTime = totalTime;
        }

        // Passt ein (noch nicht uebernommenes) Groessenraster zu den Units?
        // Je Hinweis "Knotenname: Text" (z.B. Inlet-Verteilung ragt hinaus).
        std::vector<std::string> CheckSizeGrid(const SSizeGrid& sizes) const
        {
            std::vector<std::string> notes;
            for (const SFlowsheetNode& node : m_nodes)
            {
                auto it = m_units.find(node.id);
                if (it == m_units.end() || !it->second)
                    continue;
                for (const std::string& text : it->second->CheckSizeGrid(sizes))
                    notes.push_back(node.name + ": " + text);
            }
            return notes;
        }

        const SSimulationGrid& GetGrid() const { return m_grid; }

        void SetRandomSeed(std::uint64_t seed) { m_grid.randomSeed = seed; }

        // Eigener Seed einer Unit: haengt nur vom globalen Seed und der
        // Knoten-ID ab - nicht von der Reihenfolge der Knoten. Fuegt man
        // irgendwo einen Block hinzu, aendern sich die Zufallszahlen der
        // anderen Units also nicht.
        std::uint64_t GetUnitSeed(const std::string& nodeId) const
        {
            return MakeUnitSeed(m_grid.randomSeed, nodeId);
        }

        // Haengt das Ergebnis irgendwo vom Zufall ab?
        bool IsStochastic() const
        {
            for (const auto& entry : m_units)
                if (entry.second && entry.second->IsStochastic())
                    return true;
            return false;
        }

        // Rueckmeldung waehrend mehrerer Laeufe: (fertige Laeufe, Gesamtzahl).
        // Liefert false, wenn abgebrochen werden soll.
        using ProgressCallback = std::function<bool(size_t, size_t)>;

        // Rechnet das Flowsheet numRuns-mal mit verschiedenen Seeds (Lauf 1
        // mit genau dem eingestellten Seed) und sammelt fuer jedes Ergebnis
        // die Statistik. Ein deterministisches Flowsheet wird nur einmal
        // gerechnet. Rueckgabe: Anzahl tatsaechlich gerechneter Laeufe
        // (weniger, wenn abgebrochen wurde - die Statistik umfasst dann die
        // fertigen Laeufe).
        size_t RunSimulations(size_t numRuns, const ProgressCallback& progress = ProgressCallback())
        {
            if (m_grid.numTimeSteps < 2 || m_grid.numSizeClasses < 1)
                return 0;
            const size_t runs = IsStochastic() ? std::max<size_t>(numRuns, 1) : 1;

            PrepareMaterials();
            m_warnings.clear();
            m_inEnsemble = true;
            for (auto& entry : m_statistics)
                for (CStreamStatistics& stats : entry.second)
                    stats.Reset(m_grid);

            const std::uint64_t baseSeed = m_grid.randomSeed;
            size_t done = 0;
            for (size_t r = 0; r < runs; ++r)
            {
                m_grid.randomSeed = MakeRunSeed(baseSeed, r);
                RunSimulation();
                for (auto& entry : m_statistics)
                    for (size_t port = 0; port < entry.second.size(); ++port)
                        if (const CStreamRecord* record = GetDisplayRecord(entry.first, port))
                            entry.second[port].AddRun(*record);
                ++done;
                if (progress && !progress(done, runs))
                    break;
            }
            m_grid.randomSeed = baseSeed;
            m_inEnsemble = false;

            for (auto& entry : m_statistics)
                for (CStreamStatistics& stats : entry.second)
                    stats.Finalize();
            return done;
        }

        // Statistik ueber alle Laeufe fuer einen Eintrag aus
        // GetResultsEntries(); nullptr, wenn es ihn nicht gibt.
        const CEnsembleStatistics* GetStatistics(const std::string& nodeId, size_t port) const
        {
            auto it = m_statistics.find(nodeId);
            if (it == m_statistics.end() || port >= it->second.size())
                return nullptr;
            return &it->second[port].GetTotal();
        }

        // Wie GetStatistics, aber mit Stoffen, Temperatur und Druck.
        const CStreamStatistics* GetStreamStatistics(const std::string& nodeId, size_t port) const
        {
            auto it = m_statistics.find(nodeId);
            if (it == m_statistics.end() || port >= it->second.size())
                return nullptr;
            return &it->second[port];
        }

        // EIN Lauf mit dem aktuellen Seed. Die Ergebnisse stehen danach in
        // GetOutputResults/GetDisplayResults (ohne Statistik).
        void RunSimulation()
        {
            const size_t numT = m_grid.numTimeSteps;
            const size_t numS = m_grid.numSizeClasses;
            if (numT < 2 || numS < 1)
                return;
            const double dt = m_grid.GetDt();

            // 1) Aktive Stoffe bestimmen, alle Units zuruecksetzen,
            //    Ergebnis-Speicher vorbereiten.
            PrepareMaterials();
            if (!m_inEnsemble)
                m_warnings.clear();
            const size_t numC = m_grid.GetNumCompounds();
            for (auto& entry : m_units)
                if (entry.second)
                    entry.second->Initialize(m_grid, GetUnitSeed(entry.first));
            for (auto& entry : m_outputRecords)
                for (CStreamRecord& record : entry.second)
                    record.Configure(m_grid);

            // 2) Reihenfolge pro Schritt + aufgeschnittene Verbindungen.
            std::vector<std::string> order;
            std::set<std::string> lagged;
            BuildExecutionPlan(order, lagged);

            // 3) Laufzeit-Daten in Ausfuehrungsreihenfolge vorbereiten, damit
            // in der Zeitschleife keine Suche mehr noetig ist.
            struct SInputLink
            {
                size_t targetPort = 0;
                size_t sourceIndex = 0; // Index in runtime
                size_t sourcePort = 0;
                bool lagged = false;
            };
            struct SRuntimeNode
            {
                IFlowsheetUnit* unit = nullptr;
                std::vector<CStreamRecord>* records = nullptr;
                std::vector<CStreamSlice> current;  // Ausgaenge in diesem Schritt
                std::vector<CStreamSlice> previous; // Ausgaenge im vorherigen Schritt
                std::vector<SInputLink> links;
                std::vector<const CStreamSlice*> inputs;
            };

            std::vector<SRuntimeNode> runtime(order.size());
            std::map<std::string, size_t> indexOf;
            for (size_t i = 0; i < order.size(); ++i)
                indexOf[order[i]] = i;

            for (size_t i = 0; i < order.size(); ++i)
            {
                const SFlowsheetNode* node = FindNode(order[i]);
                SRuntimeNode& rn = runtime[i];
                auto unitIt = m_units.find(order[i]);
                rn.unit = (unitIt != m_units.end()) ? unitIt->second.get() : nullptr;
                rn.records = &m_outputRecords[order[i]];
                rn.current.resize(node->numOutputs);
                rn.previous.resize(node->numOutputs);
                for (size_t p = 0; p < node->numOutputs; ++p)
                {
                    rn.current[p].Resize(numS, numC);
                    rn.previous[p].Resize(numS, numC); // vor t=0 ist nichts unterwegs
                }
                rn.inputs.assign(node->numInputs, nullptr);
            }
            for (const SFlowsheetConnection& conn : m_connections)
            {
                auto src = indexOf.find(conn.sourceNodeId);
                auto tgt = indexOf.find(conn.targetNodeId);
                if (src == indexOf.end() || tgt == indexOf.end())
                    continue;
                SInputLink link;
                link.targetPort = conn.targetPort;
                link.sourceIndex = src->second;
                link.sourcePort = conn.sourcePort;
                link.lagged = lagged.count(conn.id) > 0;
                runtime[tgt->second].links.push_back(link);
            }

            // 4) Zeitschleife: Makroschritt fuer Makroschritt.
            for (size_t k = 0; k < numT; ++k)
            {
                SStepContext context;
                context.stepIndex = k;
                context.time = static_cast<double>(k) * dt;
                context.dt = dt;

                for (SRuntimeNode& rn : runtime)
                {
                    std::fill(rn.inputs.begin(), rn.inputs.end(), nullptr);
                    for (const SInputLink& link : rn.links)
                    {
                        const SRuntimeNode& source = runtime[link.sourceIndex];
                        if (link.targetPort < rn.inputs.size() && link.sourcePort < source.current.size())
                            rn.inputs[link.targetPort] = link.lagged
                                ? &source.previous[link.sourcePort]
                                : &source.current[link.sourcePort];
                    }

                    for (CStreamSlice& out : rn.current)
                        out.Clear();
                    if (rn.unit)
                        rn.unit->Step(context, rn.inputs, rn.current);

                    for (size_t p = 0; p < rn.current.size() && p < rn.records->size(); ++p)
                        (*rn.records)[p].Record(k, rn.current[p]);
                }

                // Ausgaenge dieses Schritts werden zum "vorherigen" Schritt
                // fuer die aufgeschnittenen Verbindungen.
                for (SRuntimeNode& rn : runtime)
                    rn.previous.swap(rn.current);
            }

            // 5) Hinweise der Units mit Knotennamen einsammeln.
            for (const auto& entry : m_units)
            {
                if (!entry.second)
                    continue;
                const SFlowsheetNode* node = FindNode(entry.first);
                for (const std::string& warning : entry.second->GetWarnings())
                    m_warnings.insert((node ? node->name : entry.first) + ": " + warning);
            }
        }

        void SetNodePosition(const std::string& nodeId, double posX, double posY)
        {
            for (SFlowsheetNode& node : m_nodes)
            {
                if (node.id == nodeId)
                {
                    node.posX = posX;
                    node.posY = posY;
                    return;
                }
            }
        }

    private:
        std::string MakeNodeId() { return "node_" + std::to_string(m_nextNodeId++); }
        std::string MakeConnectionId() { return "conn_" + std::to_string(m_nextConnectionId++); }

        std::string AddNode(const std::string& name, ENodeType type, size_t numInputs, size_t numOutputs,
            double posX, double posY, std::unique_ptr<IFlowsheetUnit> unit)
        {
            SFlowsheetNode node;
            node.id = MakeNodeId();
            node.name = name;
            node.type = type;
            node.numInputs = numInputs;
            node.numOutputs = numOutputs;
            node.posX = posX;
            node.posY = posY;
            m_nodes.push_back(node);
            m_units[node.id] = std::move(unit);
            // Feste Groesse ab hier -> Zeiger auf die Ergebnisse (z.B. in der
            // Heatmap) bleiben ueber alle Simulationen hinweg gueltig.
            m_outputRecords[node.id] = std::vector<CStreamRecord>(numOutputs);
            // Statistik je angezeigtem Ergebnis: beim Outlet das, was ankommt,
            // sonst je Ausgang (wie GetDisplayResults).
            m_statistics[node.id] = std::vector<CStreamStatistics>(type == ENodeType::Outlet ? 1 : numOutputs);
            return node.id;
        }

        // Aktive Stoffe = Stoffe aller Inlets, in der Reihenfolge der
        // Materialdatei (dort fehlende Stoffe alphabetisch dahinter). Das
        // Ergebnis haengt also nicht von der Reihenfolge der Knoten ab.
        void PrepareMaterials()
        {
            std::vector<std::string> names;
            for (const SFlowsheetNode& node : m_nodes)
            {
                const CInletUnit* inlet = node.type == ENodeType::Inlet ? FindUnit<CInletUnit>(node.id) : nullptr;
                if (inlet && std::find(names.begin(), names.end(), inlet->GetMaterial()) == names.end())
                    names.push_back(inlet->GetMaterial());
            }
            const size_t notInDatabase = static_cast<size_t>(-1);
            auto rank = [this, notInDatabase](const std::string& name)
            {
                if (m_materials)
                {
                    const std::vector<SMaterial>& list = m_materials->GetMaterials();
                    for (size_t i = 0; i < list.size(); ++i)
                        if (list[i].name == name)
                            return i;
                }
                return notInDatabase;
            };
            std::sort(names.begin(), names.end(), [&rank](const std::string& a, const std::string& b)
                {
                    const size_t ra = rank(a);
                    const size_t rb = rank(b);
                    return ra != rb ? ra < rb : a < b;
                });
            m_grid.compounds = std::move(names);
            m_grid.materials = m_materials;
        }

        template <typename T>
        T* FindUnit(const std::string& nodeId)
        {
            auto it = m_units.find(nodeId);
            return (it != m_units.end()) ? dynamic_cast<T*>(it->second.get()) : nullptr;
        }

        // Bestimmt die Reihenfolge, in der die Knoten in JEDEM Schritt
        // rechnen (topologische Sortierung: ein Knoten kommt dran, sobald
        // alle seine Vorgaenger fertig sind), und welche Verbindungen
        // aufgeschnitten werden muessen, damit das trotz Kreisen geht.
        void BuildExecutionPlan(std::vector<std::string>& order, std::set<std::string>& lagged) const
        {
            order.clear();
            lagged.clear();

            // inDegree = Anzahl noch nicht erledigter, nicht aufgeschnittener Eingaenge.
            std::map<std::string, int> inDegree;
            for (const SFlowsheetNode& node : m_nodes)
                inDegree[node.id] = 0;
            for (const SFlowsheetConnection& conn : m_connections)
            {
                if (!inDegree.count(conn.sourceNodeId) || !inDegree.count(conn.targetNodeId))
                    continue;
                if (conn.isRecycle)
                    lagged.insert(conn.id);
                else
                    ++inDegree[conn.targetNodeId];
            }

            std::set<std::string> done;
            std::vector<std::string> ready;
            for (const SFlowsheetNode& node : m_nodes)
                if (inDegree[node.id] == 0)
                    ready.push_back(node.id);

            while (order.size() < m_nodes.size())
            {
                while (!ready.empty())
                {
                    const std::string nodeId = ready.front();
                    ready.erase(ready.begin());
                    order.push_back(nodeId);
                    done.insert(nodeId);

                    // Nachfolger freigeben, deren Vorgaenger jetzt alle fertig sind.
                    for (const SFlowsheetConnection& conn : m_connections)
                    {
                        if (conn.sourceNodeId != nodeId || lagged.count(conn.id) || done.count(conn.targetNodeId))
                            continue;
                        if (--inDegree[conn.targetNodeId] == 0)
                            ready.push_back(conn.targetNodeId);
                    }
                }
                if (order.size() >= m_nodes.size())
                    break;

                // Festgefahren: alle uebrigen Knoten warten (indirekt) auf sich
                // selbst - es gibt mindestens einen Kreis. Einen Knoten darin
                // waehlen und seine noch offenen Eingaenge aufschneiden.
                const std::string breakNode = ChooseCycleBreakNode(done, lagged);
                for (const SFlowsheetConnection& conn : m_connections)
                {
                    if (conn.targetNodeId == breakNode && !done.count(conn.sourceNodeId) && !lagged.count(conn.id))
                    {
                        lagged.insert(conn.id);
                        --inDegree[breakNode];
                    }
                }
                ready.push_back(breakNode);
            }
        }

        // Trennapparate: hier beginnt physikalisch ein Rueckfuehrstrom
        // (heute nur der Splitter, spaeter auch Sieb, Sichter, Zyklon, ...).
        static bool IsSeparatorType(ENodeType type)
        {
            return type == ENodeType::Splitter;
        }

        // Welcher noch offene Knoten soll seine Eingaenge aufschneiden?
        // Nur Knoten, die wirklich auf einem Kreis liegen (nicht z.B. ein
        // Outlet hinter dem Kreis). Rangfolge:
        //  1. ein Merger - dort muendet eine Rueckfuehrung ein,
        //  2. dessen offene Eingaenge von einem Trennapparat kommen - dann
        //     wird der eigentliche Rueckfuehrstrom geschnitten und frisches
        //     Material, das noch nie im Kreis war, bekommt keine Verzoegerung,
        //  3. der Knoten mit den meisten schon erledigten Eingaengen.
        // So haengt das Ergebnis nicht von der Reihenfolge ab, in der die
        // Knoten angelegt wurden.
        std::string ChooseCycleBreakNode(const std::set<std::string>& done,
            const std::set<std::string>& lagged) const
        {
            std::string best;
            int bestScore = -1;
            for (const SFlowsheetNode& node : m_nodes)
            {
                if (done.count(node.id) || !IsOnOpenCycle(node.id, done, lagged))
                    continue;
                int resolvedInputs = 0;
                bool openInputsFromSeparators = true;
                for (const SFlowsheetConnection& conn : m_connections)
                {
                    if (conn.targetNodeId != node.id || lagged.count(conn.id))
                        continue;
                    if (done.count(conn.sourceNodeId))
                    {
                        ++resolvedInputs;
                        continue;
                    }
                    const SFlowsheetNode* source = FindNode(conn.sourceNodeId);
                    if (!source || !IsSeparatorType(source->type))
                        openInputsFromSeparators = false;
                }
                const int score = (node.type == ENodeType::Merger ? 1000 : 0)
                    + (openInputsFromSeparators ? 100 : 0) + resolvedInputs;
                if (score > bestScore)
                {
                    bestScore = score;
                    best = node.id;
                }
            }
            if (!best.empty())
                return best;
            // Sollte nicht vorkommen - dann eben den ersten offenen Knoten.
            for (const SFlowsheetNode& node : m_nodes)
                if (!done.count(node.id))
                    return node.id;
            return std::string();
        }

        // Kommt man von startId ueber offene, nicht aufgeschnittene
        // Verbindungen wieder bei startId an?
        bool IsOnOpenCycle(const std::string& startId, const std::set<std::string>& done,
            const std::set<std::string>& lagged) const
        {
            std::set<std::string> visited;
            std::vector<std::string> stack = { startId };
            while (!stack.empty())
            {
                const std::string current = stack.back();
                stack.pop_back();
                for (const SFlowsheetConnection& conn : m_connections)
                {
                    if (conn.sourceNodeId != current || lagged.count(conn.id) || done.count(conn.targetNodeId))
                        continue;
                    if (conn.targetNodeId == startId)
                        return true;
                    if (visited.insert(conn.targetNodeId).second)
                        stack.push_back(conn.targetNodeId);
                }
            }
            return false;
        }

        std::vector<SFlowsheetNode> m_nodes;
        std::vector<SFlowsheetConnection> m_connections;

        CStreamProcessorRegistry m_processorRegistry;
        // Recheneinheit pro Knoten (Inlet, Outlet, Prozessor, Splitter, Merger).
        std::map<std::string, std::unique_ptr<IFlowsheetUnit>> m_units;
        // Verlauf jedes Ausgangs-Ports ueber die ganze Simulation.
        std::map<std::string, std::vector<CStreamRecord>> m_outputRecords;
        // Statistik ueber mehrere Laeufe, je Eintrag aus GetResultsEntries().
        std::map<std::string, std::vector<CStreamStatistics>> m_statistics;

        const CMaterialDatabase* m_materials = nullptr;
        std::set<std::string> m_warnings;
        bool m_inEnsemble = false; // RunSimulation laeuft innerhalb von RunSimulations

        int m_nextNodeId = 0;
        int m_nextConnectionId = 0;

        SSimulationGrid m_grid;
    };
}
