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
#include "SimulationLog.h"
#include "CardHistory.h"
#include "CardLimits.h"
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

        // Nur das Zeitraster setzen: Dauer und gewuenschte Schrittweite dt; die
        // Zahl der Zeitpunkte folgt daraus (SSimulationGrid::TimePointsFor), das
        // Groessenraster bleibt. So aendert eine neue Dauer das dt nicht mehr.
        void ConfigureTime(double totalTime, double dt)
        {
            m_grid.totalTime = totalTime;
            m_grid.numTimeSteps = SSimulationGrid::TimePointsFor(totalTime, dt);
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

        // Kontinuum oder Partikel fuer alle Stroeme der naechsten Simulation.
        void SetRepresentation(ERepresentation representation) { m_grid.representation = representation; }
        ERepresentation GetRepresentation() const { return m_grid.representation; }

        // Kartenzahl je Stromscheibe, Mindestzahl, Groessenbereiche (Partikelmodus).
        void SetCardSettings(const SCardSettings& cards) { m_grid.cards = cards; }
        const SCardSettings& GetCardSettings() const { return m_grid.cards; }

        // Units, die im Partikelmodus noch nicht rechnen koennen, als
        // "Knotenname (Typ)" in der Reihenfolge der Knoten. Leer = alles
        // bereit (im Kontinuum immer leer); sonst startet keine Simulation.
        std::vector<std::string> GetUnitsWithoutParticleSupport() const
        {
            std::vector<std::string> names;
            if (m_grid.representation != ERepresentation::Particles)
                return names;
            for (const SFlowsheetNode& node : m_nodes)
            {
                auto it = m_units.find(node.id);
                if (it != m_units.end() && it->second && !it->second->SupportsParticles())
                    names.push_back(node.name + " (" + GetNodeTypeLabel(node.type) + ")");
            }
            return names;
        }

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

        // Haengt das, was ein Knoten liefert, vom Zufall ab? Ja, wenn seine
        // Unit selbst oder irgendeine davor (auch ueber Rueckfuehrungen)
        // stochastisch ist - heute ein Splitter im stochastischen Modus. Nur
        // dort hat das Fenster "Auswertung" Schwankungen zu zeigen.
        bool IsAffectedByRandomness(const std::string& nodeId) const
        {
            std::set<std::string> visited;
            std::vector<std::string> stack = { nodeId };
            while (!stack.empty())
            {
                const std::string current = stack.back();
                stack.pop_back();
                if (!visited.insert(current).second)
                    continue;
                auto it = m_units.find(current);
                if (it != m_units.end() && it->second && it->second->IsStochastic())
                    return true;
                for (const SFlowsheetConnection& conn : m_connections)
                    if (conn.targetNodeId == current)
                        stack.push_back(conn.sourceNodeId);
            }
            return false;
        }

        // Rueckmeldung waehrend mehrerer Laeufe: (fertige Laeufe, Gesamtzahl).
        // Liefert false, wenn abgebrochen werden soll.
        using ProgressCallback = std::function<bool(size_t, size_t)>;
        // Rueckmeldung je Zeitschritt (SSimulationProgress); false = abbrechen.
        using StepProgressCallback = std::function<bool(const SSimulationProgress&)>;

        // Hoechstens so oft (in s) meldet sich der Fortschritt innerhalb eines
        // Laufs (Standard 0,05 s; 0 = nach jedem Zeitschritt, fuer Tests).
        void SetProgressInterval(double seconds) { m_progressInterval = seconds; }

        // Rechnet das Flowsheet numRuns-mal mit verschiedenen Seeds (Lauf 1
        // mit genau dem eingestellten Seed) und sammelt fuer jedes Ergebnis
        // die Statistik. Ein deterministisches Flowsheet wird nur einmal
        // gerechnet. Rueckgabe: Anzahl tatsaechlich gerechneter Laeufe
        // (weniger, wenn abgebrochen wurde - die Statistik umfasst dann die
        // fertigen Laeufe). progress meldet sich am Ende jedes Laufs.
        size_t RunSimulations(size_t numRuns, const ProgressCallback& progress = ProgressCallback())
        {
            if (!progress)
                return RunSimulations(numRuns, StepProgressCallback());
            return RunSimulations(numRuns, StepProgressCallback([&progress](const SSimulationProgress& state)
                {
                    return !state.runFinished || progress(state.runsDone, state.runs);
                }));
        }

        // Wie oben, aber mit Rueckmeldung auch waehrend eines Laufs (im Abstand
        // von SetProgressInterval und am Ende jedes Laufs). Liefert sie
        // false, bricht die Rechnung sofort ab, auch mitten in einem Lauf; der
        // angefangene Lauf zaehlt dann nicht zur Statistik.
        size_t RunSimulations(size_t numRuns, const StepProgressCallback& progress)
        {
            if (m_grid.numTimeSteps < 2 || m_grid.numSizeClasses < 1)
                return 0;
            const size_t runs = IsStochastic() ? std::max<size_t>(numRuns, 1) : 1;

            PrepareMaterials();
            m_warnings.clear();
            BeginLog(runs);
            if (!CheckParticleSupportOrWarn())
            {
                for (const std::string& warning : m_warnings)
                    m_log.Add("Nicht gestartet: " + warning);
                return 0; // alte Ergebnisse bleiben stehen
            }
            m_inEnsemble = true;
            for (auto& entry : m_statistics)
                for (CStreamStatistics& stats : entry.second)
                    stats.Reset(m_grid);
            ResetCardHistories();

            SSimulationProgress state;
            state.runs = runs;
            state.steps = m_grid.numTimeSteps;
            const std::uint64_t baseSeed = m_grid.randomSeed;
            size_t done = 0;
            for (size_t r = 0; r < runs; ++r)
            {
                m_grid.randomSeed = MakeRunSeed(baseSeed, r);
                m_runIndex = r;
                state.runsDone = done;
                state.stepsDone = 0;
                state.runFinished = false;
                if (!RunOne(progress ? &progress : nullptr, state))
                {
                    m_timing.cancelled = true;
                    m_log.Add("Lauf " + std::to_string(r + 1) + " abgebrochen - er zaehlt nicht zur Statistik");
                    break;
                }
                CStopwatch statisticsClock;
                for (auto& entry : m_statistics)
                    for (size_t port = 0; port < entry.second.size(); ++port)
                        if (const CStreamRecord* record = GetDisplayRecord(entry.first, port))
                            entry.second[port].AddRun(*record);
                m_timing.statisticsSeconds += statisticsClock.GetSeconds();
                ++done;
                state.runsDone = done;
                state.stepsDone = 0;
                state.runFinished = true;
                state.elapsedSeconds = m_log.GetElapsedSeconds();
                if (progress && !progress(state))
                {
                    m_timing.cancelled = done < runs;
                    if (m_timing.cancelled)
                        m_log.Add("Abgebrochen nach Lauf " + std::to_string(done));
                    break;
                }
            }
            m_grid.randomSeed = baseSeed;
            m_inEnsemble = false;

            CStopwatch statisticsClock;
            for (auto& entry : m_statistics)
                for (CStreamStatistics& stats : entry.second)
                    stats.Finalize();
            m_timing.statisticsSeconds += statisticsClock.GetSeconds();
            EndLog(done, runs);
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
            if (m_grid.numTimeSteps < 2 || m_grid.numSizeClasses < 1)
                return;
            PrepareMaterials();
            m_warnings.clear();
            BeginLog(1);
            if (!CheckParticleSupportOrWarn())
            {
                for (const std::string& warning : m_warnings)
                    m_log.Add("Nicht gestartet: " + warning);
                return;
            }
            ResetCardHistories();
            m_runIndex = 0;
            SSimulationProgress state;
            state.runs = 1;
            state.steps = m_grid.numTimeSteps;
            RunOne(nullptr, state);
            EndLog(1, 1);
        }

        // Textprotokoll und Laufzeiten der letzten Simulation (Methoden Kap. 12).
        const CSimulationLog& GetSimulationLog() const { return m_log; }
        const SSimulationTiming& GetSimulationTiming() const { return m_timing; }
        // Eine Zeile von aussen anhaengen, z.B. die Zeit fuers Neuzeichnen.
        void AddLogLine(const std::string& text) { m_log.Add(text); }
        // Ausfuehrlich: in Lauf 1 jede Unit in jedem Zeitschritt (Start und Ende).
        void SetDetailedLog(bool detailed) { m_detailedLog = detailed; }
        bool IsDetailedLog() const { return m_detailedLog; }

        // Karten eines Eintrags aus GetResultsEntries() in Lauf 1 (nur im
        // Partikelmodus; beim Outlet die ankommenden Karten). nullptr, wenn
        // es keine gibt.
        const CCardHistory* GetCardHistory(const std::string& nodeId, size_t port) const
        {
            auto it = m_cardHistories.find(nodeId);
            if (it == m_cardHistories.end() || port >= it->second.size())
                return nullptr;
            return &it->second[port];
        }

    private:
        // Ein Lauf mit dem aktuellen Seed (m_runIndex = Nummer im Ensemble).
        // false, wenn progress abgebrochen hat.
        bool RunOne(const StepProgressCallback* progress, SSimulationProgress& state)
        {
            const size_t numT = m_grid.numTimeSteps;
            const size_t numS = m_grid.numSizeClasses;
            const double dt = m_grid.GetDt();
            const bool firstRun = m_runIndex == 0;
            const bool particles = m_grid.representation == ERepresentation::Particles;
            const bool limits = particles && m_grid.cards.sliceLimits;
            const bool detailed = m_detailedLog && firstRun;
            CStopwatch runClock;
            if (!firstRun)
                m_log.Add("Lauf " + std::to_string(m_runIndex + 1) + " von " + std::to_string(state.runs) + " gestartet");

            // 1) Aktive Stoffe bestimmen, alle Units zuruecksetzen,
            //    Ergebnis-Speicher vorbereiten.
            PrepareMaterials();
            const size_t numC = m_grid.GetNumCompounds();
            const ERepresentation representation = m_grid.representation;

            // 2) Reihenfolge pro Schritt + aufgeschnittene Verbindungen.
            std::vector<std::string> order;
            std::set<std::string> lagged;
            BuildExecutionPlan(order, lagged);
            if (firstRun)
            {
                m_timing.units.clear();
                m_timingIndex.clear();
                std::string plan;
                for (const std::string& id : order)
                {
                    const SFlowsheetNode* node = FindNode(id);
                    m_timingIndex[id] = m_timing.units.size();
                    SUnitTiming timing;
                    timing.nodeId = id;
                    timing.name = node ? node->name : id;
                    timing.type = node ? GetNodeTypeLabel(node->type) : "?";
                    m_timing.units.push_back(timing);
                    plan += (plan.empty() ? "" : " -> ") + timing.name;
                }
                m_log.Add("Reihenfolge je Zeitschritt: " + plan);
                for (const SFlowsheetConnection& conn : m_connections)
                    if (lagged.count(conn.id))
                    {
                        const SFlowsheetNode* source = FindNode(conn.sourceNodeId);
                        const SFlowsheetNode* target = FindNode(conn.targetNodeId);
                        m_log.Add("Rueckfuehrung aufgeschnitten: " + (source ? source->name : conn.sourceNodeId)
                            + " -> " + (target ? target->name : conn.targetNodeId) + " (Wert aus dem vorigen Schritt)");
                    }
                m_log.Add("Lauf 1 von " + std::to_string(state.runs) + " gestartet");
            }

            for (const std::string& id : order)
            {
                auto unitIt = m_units.find(id);
                if (unitIt == m_units.end() || !unitIt->second)
                    continue;
                SUnitTiming& timing = m_timing.units[m_timingIndex[id]];
                if (firstRun)
                    m_log.Add(timing.name + " (" + timing.type + "): Initialisierung gestartet");
                CStopwatch initClock;
                unitIt->second->Initialize(m_grid, GetUnitSeed(id));
                const double seconds = initClock.GetSeconds();
                timing.initSeconds += seconds;
                if (firstRun)
                {
                    const std::string info = unitIt->second->DescribeForLog();
                    m_log.Add(timing.name + ": initialisiert in " + FormatDuration(seconds) + (info.empty() ? "" : " - " + info));
                }
            }
            for (auto& entry : m_units)
                if (entry.second && !m_timingIndex.count(entry.first))
                    entry.second->Initialize(m_grid, GetUnitSeed(entry.first)); // nicht im Plan (sollte nicht vorkommen)
            for (auto& entry : m_outputRecords)
                for (CStreamRecord& record : entry.second)
                    record.Configure(m_grid);

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
                SUnitTiming* timing = nullptr;
                std::vector<CCardHistory>* cardHistories = nullptr; // nur Lauf 1 im Partikelmodus
            };

            std::vector<SRuntimeNode> runtime(order.size());
            std::map<std::string, size_t> indexOf;
            for (size_t i = 0; i < order.size(); ++i)
                indexOf[order[i]] = i;

            const bool recordCards = firstRun && particles;
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
                    rn.current[p].Resize(numS, numC, representation);
                    rn.previous[p].Resize(numS, numC, representation); // vor t=0 ist nichts unterwegs
                }
                rn.inputs.assign(node->numInputs, nullptr);
                rn.timing = &m_timing.units[m_timingIndex[order[i]]];
                auto historyIt = m_cardHistories.find(order[i]);
                if (recordCards && historyIt != m_cardHistories.end())
                    rn.cardHistories = &historyIt->second;
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

            // 4) Zeitschleife: Makroschritt fuer Makroschritt. Die Uhr misst je
            // Unit das Rechnen (Step) und das Aufzeichnen der Ausgaenge.
            CStopwatch clock;
            double lastReport = 0.0;
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
                    if (detailed)
                        m_log.Add("t = " + FormatNumber(context.time, 6) + " s: " + rn.timing->name + " startet");
                    const double t0 = clock.GetSeconds();
                    if (rn.unit)
                        rn.unit->Step(context, rn.inputs, rn.current);
                    const double t1 = clock.GetSeconds();
                    // Grenzen je Stromscheibe an jedem Ausgang (13.24): teilen
                    // bzw. zusammenlegen, bevor der naechste Knoten die Karten sieht.
                    if (limits && !(rn.unit && rn.unit->OutputsWithinCardLimits()))
                        for (CStreamSlice& out : rn.current)
                        {
                            const SLimitCounts changed = ApplySliceLimits(out, m_grid.cards);
                            rn.timing->cardsAdded += static_cast<double>(changed.added);
                            rn.timing->cardsRemoved += static_cast<double>(changed.removed);
                        }
                    const double tLimits = clock.GetSeconds();
                    for (size_t p = 0; p < rn.current.size() && p < rn.records->size(); ++p)
                        (*rn.records)[p].Record(k, rn.current[p]);
                    const double t2 = clock.GetSeconds();

                    rn.timing->stepSeconds += t1 - t0;
                    rn.timing->limitSeconds += tLimits - t1;
                    rn.timing->recordSeconds += t2 - tLimits;
                    ++rn.timing->steps;
                    size_t cards = 0;
                    if (particles)
                    {
                        if (!rn.current.empty())
                            for (const CStreamSlice& out : rn.current)
                                cards += out.GetNumCards();
                        else
                            for (const CStreamSlice* in : rn.inputs)
                                cards += in ? in->GetNumCards() : 0;
                        rn.timing->cards += static_cast<double>(cards);
                    }
                    if (rn.cardHistories)
                    {
                        // Karten fuer die Kartenansicht: Ausgaenge, beim Outlet der Eingang.
                        static const CStreamSlice kEmpty;
                        if (!rn.current.empty())
                        {
                            for (size_t p = 0; p < rn.current.size() && p < rn.cardHistories->size(); ++p)
                                (*rn.cardHistories)[p].Record(k, rn.current[p]);
                        }
                        else if (!rn.cardHistories->empty())
                            (*rn.cardHistories)[0].Record(k, !rn.inputs.empty() && rn.inputs[0] ? *rn.inputs[0] : kEmpty);
                        m_timing.historySeconds += clock.GetSeconds() - t2;
                    }
                    if (detailed)
                    {
                        // Alle Zeiten der Zeile in derselben Einheit (us oder ms).
                        const ETimeUnit unit = ChooseTimeUnit(std::max({ t1 - t0, tLimits - t1, t2 - tLimits }));
                        m_log.Add("t = " + FormatNumber(context.time, 6) + " s: " + rn.timing->name + " fertig nach "
                            + FormatDurationIn(t1 - t0, unit) + " Rechnen + "
                            + (limits ? FormatDurationIn(tLimits - t1, unit) + " Grenzen + " : std::string())
                            + FormatDurationIn(t2 - tLimits, unit) + " Aufzeichnen"
                            + (particles ? " (" + std::to_string(cards) + " Karten)" : ""));
                    }
                }

                // Ausgaenge dieses Schritts werden zum "vorherigen" Schritt
                // fuer die aufgeschnittenen Verbindungen.
                for (SRuntimeNode& rn : runtime)
                    rn.previous.swap(rn.current);

                if (progress && k + 1 < numT && clock.GetSeconds() - lastReport >= m_progressInterval)
                {
                    lastReport = clock.GetSeconds();
                    state.stepsDone = k + 1;
                    state.runFinished = false;
                    state.elapsedSeconds = m_log.GetElapsedSeconds();
                    if (!(*progress)(state))
                        return false;
                }
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

            if (firstRun)
                for (const SRuntimeNode& rn : runtime)
                {
                    const SUnitTiming& t = *rn.timing;
                    const ETimeUnit unit = ChooseTimeUnit(std::max({ t.stepSeconds, t.limitSeconds, t.recordSeconds }));
                    std::string line = t.name + ": " + std::to_string(numT) + " Zeitschritte fertig - Rechnen "
                        + FormatDurationIn(t.stepSeconds, unit)
                        + (limits ? ", Grenzen " + FormatDurationIn(t.limitSeconds, unit) : std::string())
                        + ", Aufzeichnen " + FormatDurationIn(t.recordSeconds, unit);
                    if (particles)
                        line += ", im Mittel " + FormatDecimal(t.GetCardsPerStep(), 0) + " Karten je Schritt";
                    if (t.cardsAdded > 0.0 || t.cardsRemoved > 0.0)
                        line += " (davon +" + FormatDecimal(t.GetAddedPerStep(), 0) + " durch Teilen, -"
                            + FormatDecimal(t.GetRemovedPerStep(), 0) + " durch Zusammenlegen)";
                    m_log.Add(line);
                }
            m_log.Add("Lauf " + std::to_string(m_runIndex + 1) + " fertig nach " + FormatDuration(runClock.GetSeconds()));
            return true;
        }

        // Protokoll und Zeiten fuer eine neue Simulation beginnen.
        void BeginLog(size_t runs)
        {
            m_log.Start();
            m_timing = SSimulationTiming();
            m_timing.steps = m_grid.numTimeSteps;
            m_timing.particles = m_grid.representation == ERepresentation::Particles;
            m_timing.sliceLimits = m_timing.particles && m_grid.cards.sliceLimits;
            std::string line = "Simulation gestartet: " + std::to_string(runs) + (runs == 1 ? " Lauf, " : " Laeufe, ")
                + std::to_string(m_grid.numTimeSteps) + " Zeitpunkte (0 bis " + FormatNumber(m_grid.totalTime, 6)
                + " s, dt = " + FormatNumber(m_grid.GetDt(), 6) + " s), Raster " + std::to_string(m_grid.numSizeClasses) + " Klassen, ";
            line += m_timing.particles
                ? "Partikelmodus mit " + std::to_string(m_grid.cards.cardsPerSlice) + " Karten je Zeitschritt und Inlet"
                    + (m_grid.cards.sliceLimits
                        ? ", Grenzen je Stromscheibe: N_min bis " + FormatDecimal(m_grid.cards.upperFactor, 1) + " x Sollzahl"
                        : std::string(", ohne Grenzen je Stromscheibe"))
                : std::string("Kontinuum");
            m_log.Add(line);
            if (IsDebugBuild())
                m_log.Add("Debug-Fassung: Rechenzeiten etwa sechsmal laenger als in der Release-Fassung");
        }

        void EndLog(size_t done, size_t runs)
        {
            m_timing.runs = done;
            for (const std::string& warning : m_warnings)
                m_log.Add("Hinweis: " + warning);
            if (runs > 1)
                m_log.Add("Statistik ueber " + std::to_string(done) + " Laeufe gesammelt in " + FormatDuration(m_timing.statisticsSeconds));
            if (m_timing.historySeconds > 0.0)
                m_log.Add("Karten fuer die Kartenansicht gespeichert in " + FormatDuration(m_timing.historySeconds)
                    + " (" + FormatDecimal(static_cast<double>(GetCardHistoryBytes()) / 1048576.0, 1) + " MB)");
            m_timing.totalSeconds = m_log.GetElapsedSeconds();
            if (m_timing.cancelled)
                m_log.Add("Simulation abgebrochen: " + std::to_string(done) + (done == 1 ? " fertiger Lauf in " : " fertige Laeufe in ")
                    + FormatDuration(m_timing.totalSeconds));
            else
                m_log.Add("Simulation fertig: " + std::to_string(done) + (done == 1 ? " Lauf in " : " Laeufe in ")
                    + FormatDuration(m_timing.totalSeconds));
        }

        // Kartenspeicher fuer alle Eintraege aus GetResultsEntries() leeren
        // (nur im Partikelmodus); die Speichergrenze teilen sie sich.
        void ResetCardHistories()
        {
            m_cardHistories.clear();
            if (m_grid.representation != ERepresentation::Particles)
                return;
            const std::vector<SResultsEntry> entries = GetResultsEntries();
            const size_t budget = m_grid.cards.historyBudgetBytes / std::max<size_t>(entries.size(), 1);
            for (const SResultsEntry& entry : entries)
            {
                std::vector<CCardHistory>& histories = m_cardHistories[entry.nodeId];
                if (histories.size() <= entry.port)
                    histories.resize(entry.port + 1);
                histories[entry.port].Reset(m_grid.numTimeSteps, m_grid.GetNumCompounds(), budget, m_grid.GetDt());
            }
        }

        size_t GetCardHistoryBytes() const
        {
            size_t bytes = 0;
            for (const auto& entry : m_cardHistories)
                for (const CCardHistory& history : entry.second)
                    bytes += history.GetStoredBytes();
            return bytes;
        }

    public:
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

        // false (und ein Hinweis in m_warnings), wenn im Partikelmodus
        // eine Unit noch keine Karten kann.
        bool CheckParticleSupportOrWarn()
        {
            const std::vector<std::string> names = GetUnitsWithoutParticleSupport();
            if (names.empty())
                return true;
            std::string list;
            for (const std::string& name : names)
                list += (list.empty() ? "" : ", ") + name;
            m_warnings.insert("Partikelmodus: Simulation nicht gestartet. Noch ohne Karten: " + list +
                ". Die Units lernen Karten schrittweise (Methoden-Dokument Kap. 15); "
                "bis dahin Darstellung \"Kontinuum\" waehlen.");
            return false;
        }

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

        // Laufzeitprotokoll und Rechenzeiten der letzten Simulation
        CSimulationLog m_log;
        SSimulationTiming m_timing;
        std::map<std::string, size_t> m_timingIndex; // Knoten-ID -> Eintrag in m_timing.units
        size_t m_runIndex = 0;      // laufender Lauf im Ensemble (0 = Lauf 1)
        bool m_detailedLog = false; // Lauf 1: jede Unit in jedem Zeitschritt protokollieren
        double m_progressInterval = 0.05; // s zwischen zwei Fortschrittsmeldungen
        // Karten von Lauf 1 je Eintrag aus GetResultsEntries() (Partikelmodus)
        std::map<std::string, std::vector<CCardHistory>> m_cardHistories;

        int m_nextNodeId = 0;
        int m_nextConnectionId = 0;

        SSimulationGrid m_grid;
    };
}
