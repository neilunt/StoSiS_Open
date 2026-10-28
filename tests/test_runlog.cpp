// Laufzeitprotokoll, Fortschritt je Zeitschritt und Kartenspeicher fuer die
// Kartenansicht (Methoden-Dokument Kap. 12, 13.23).
#include "FlowsheetGraph.h"
#include "CardHistory.h"
#include <cmath>
#include <cstdio>
#include <string>

#include "TestSupport.h"

using namespace stosis;
using namespace stosis_test;

namespace
{
    struct SSetup
    {
        CFlowsheetGraph graph;
        std::string inlet, splitter, outletA, outletB;

        // Inlet (1 kg/s) -> Splitter (30 / 70) -> zwei Outlets, 41 Zeitpunkte ueber 8 s.
        explicit SSetup(bool stochastic)
        {
            inlet = graph.AddInletNode("Inlet1", 0, 0);
            splitter = graph.AddSplitterNode("Splitter1", 2, 1, 0);
            outletA = graph.AddOutletNode("Outlet1", 2, 0);
            outletB = graph.AddOutletNode("Outlet2", 2, 1);
            graph.GetInlet(inlet)->SetTimeProfile({ { 0.0, 1.0 }, { 100.0, 1.0 } }); // konstant 1 kg/s
            graph.GetSplitter(splitter)->SetFractions({ 0.3, 0.7 });
            graph.GetSplitter(splitter)->SetStochastic(stochastic);
            graph.AddConnection(inlet, 0, splitter, 0);
            graph.AddConnection(splitter, 0, outletA, 0);
            graph.AddConnection(splitter, 1, outletB, 0);
            graph.ConfigureGrid(50, 1e-6, 1e-3, 41, 8.0);
        }

        void UseCards(size_t cards, size_t budgetBytes = SCardSettings().historyBudgetBytes)
        {
            graph.SetRepresentation(ERepresentation::Particles);
            SCardSettings settings = graph.GetCardSettings();
            settings.cardsPerSlice = cards;
            settings.historyBudgetBytes = budgetBytes;
            // Ohne Grenzen je Stromscheibe: Hier zaehlt, dass die Karten
            // unveraendert ankommen (Grenzen: test_particle_units, Block 6).
            settings.sliceLimits = false;
            graph.SetCardSettings(settings);
        }
    };

    size_t CountLines(const CSimulationLog& log, const std::string& part)
    {
        size_t count = 0;
        for (const CSimulationLog::SLine& line : log.GetLines())
            if (line.text.find(part) != std::string::npos)
                ++count;
        return count;
    }

    size_t CountLinesStartingWith(const CSimulationLog& log, const std::string& start)
    {
        size_t count = 0;
        for (const CSimulationLog::SLine& line : log.GetLines())
            if (line.text.compare(0, start.size(), start) == 0)
                ++count;
        return count;
    }
}

int main()
{
    const size_t numT = 41;

    // --- 1) Protokoll und Rechenzeiten (Kontinuum, 20 Laeufe) -----------
    {
        SSetup s(true);
        const size_t runs = s.graph.RunSimulations(20);
        const CSimulationLog& log = s.graph.GetSimulationLog();
        const SSimulationTiming& timing = s.graph.GetSimulationTiming();
        std::printf("%s", log.ToText().substr(0, 900).c_str());

        Check(runs == 20 && timing.runs == 20 && !timing.cancelled && timing.steps == numT,
            "20 Laeufe, nicht abgebrochen, 41 Zeitpunkte je Lauf");
        Check(!log.GetLines().empty() && log.GetLines().front().text.rfind("Simulation gestartet: 20 Laeufe, 41 Zeitpunkte", 0) == 0,
            "erste Zeile: Simulation gestartet mit Laeufen und Zeitpunkten");
        Check(CountLines(log, "Reihenfolge je Zeitschritt: Inlet1 -> Splitter1 -> Outlet") == 1, "Reihenfolge der Units steht im Protokoll");
        Check(CountLines(log, "Initialisierung gestartet") == 4 && CountLines(log, ": initialisiert in ") == 4,
            "jede Unit meldet Start und Ende ihrer Initialisierung (einmal, in Lauf 1)");
        Check(CountLines(log, "Splitter1: initialisiert in") == 1 && CountLines(log, "Anteile 30 / 70 %, schwankend (OU-Prozess)") == 1,
            "der Splitter beschreibt sich (Anteile, OU-Prozess)");
        Check(CountLines(log, "Lauf 1 von 20 gestartet") == 1 && CountLines(log, "Lauf 20 von 20 gestartet") == 1
            && CountLinesStartingWith(log, "Lauf ") == 40, "jeder Lauf meldet Start und Ende (40 Zeilen)");
        Check(CountLines(log, "41 Zeitschritte fertig - Rechnen") == 4, "Zusammenfassung je Unit nach Lauf 1");
        Check(CountLines(log, "Statistik ueber 20 Laeufe gesammelt") == 1, "Statistik im Protokoll");
        Check(log.GetLines().back().text.rfind("Simulation fertig: 20 Laeufe in ", 0) == 0, "letzte Zeile: Simulation fertig");
        bool ordered = true;
        for (size_t i = 1; i < log.GetLines().size(); ++i)
            ordered = ordered && log.GetLines()[i].seconds >= log.GetLines()[i - 1].seconds;
        Check(ordered, "Zeitstempel steigen");

        bool steps = timing.units.size() == 4;
        for (const SUnitTiming& unit : timing.units)
            steps = steps && unit.steps == 20 * numT && unit.cards == 0.0;
        Check(steps && timing.units[0].name == "Inlet1" && timing.units[1].name == "Splitter1"
            && timing.units[0].type == "Inlet" && timing.units[1].type == "Splitter",
            "Zeiten je Unit in Ausfuehrungsreihenfolge, je 20 x 41 Schritte, keine Karten im Kontinuum");
        Check(timing.totalSeconds > 0.0 && timing.GetUnitsSeconds() + timing.statisticsSeconds <= timing.totalSeconds,
            "Units und Statistik zusammen hoechstens die Gesamtzeit");
    }

    // --- 2) Ausfuehrliches Protokoll: jede Unit in jedem Schritt (Lauf 1) --
    {
        SSetup s(true);
        s.graph.SetDetailedLog(true);
        s.graph.RunSimulations(3);
        const CSimulationLog& log = s.graph.GetSimulationLog();
        Check(CountLinesStartingWith(log, "t = ") == 2 * 4 * numT && CountLines(log, "s: Splitter1 startet") == numT
            && CountLines(log, "s: Splitter1 fertig nach") == numT,
            "ausfuehrlich: Start und Ende jeder Unit in jedem Zeitschritt, nur Lauf 1 (328 Zeilen)");
        Check(CountLines(log, " Rechnen + ") == 4 * numT, "ausfuehrlich: je Ende die Zeit fuer Rechnen und Aufzeichnen");
    }

    // --- 3) Fortschritt je Zeitschritt, Abbruch mitten im Lauf -----------
    {
        SSetup s(true);
        s.graph.SetProgressInterval(0.0);
        size_t calls = 0, finished = 0;
        double last = -1.0;
        bool monotone = true;
        const size_t runs = s.graph.RunSimulations(4, [&](const SSimulationProgress& p)
            {
                ++calls;
                finished += p.runFinished ? 1 : 0;
                monotone = monotone && p.GetFraction() >= last && p.runs == 4 && p.steps == numT;
                last = p.GetFraction();
                return true;
            });
        Check(runs == 4 && calls == 4 * (numT - 1) + 4 && finished == 4,
            "Fortschritt nach jedem Zeitschritt (Abstand 0) und am Ende jedes Laufs");
        Check(monotone && NearAbs(last, 1.0, 1e-15), "Anteil steigt bis 1");

        const size_t cancelled = s.graph.RunSimulations(10, [](const SSimulationProgress& p)
            {
                return !(p.runsDone == 2 && p.stepsDone == 10);
            });
        const CEnsembleStatistics* stats = s.graph.GetStatistics(s.outletA, 0);
        Check(cancelled == 2 && s.graph.GetSimulationTiming().cancelled && stats && stats->GetNumRuns() == 2,
            "Abbruch in Lauf 3 bei Schritt 10: 2 fertige Laeufe, der angefangene zaehlt nicht");
        Check(CountLines(s.graph.GetSimulationLog(), "Lauf 3 abgebrochen") == 1
            && s.graph.GetSimulationLog().GetLines().back().text.rfind("Simulation abgebrochen: 2 fertige Laeufe", 0) == 0,
            "Abbruch steht im Protokoll");

        SSetup two(true);
        two.graph.RunSimulations(2);
        const double cut = stats ? stats->GetMean().IntegrateTotal() : 0.0;
        const double full = two.graph.GetStatistics(two.outletA, 0)->GetMean().IntegrateTotal();
        Check(cut > 0.0 && NearRel(cut, full, 1e-12), "Statistik nach dem Abbruch gleich der von 2 vollen Laeufen");
    }

    // --- 4) Kartenspeicher fuer die Kartenansicht ----------------------
    {
        SSetup s(true);
        s.UseCards(200);
        s.graph.RunSimulations(3);
        const CCardHistory* in = s.graph.GetCardHistory(s.inlet, 0);
        const CCardHistory* a = s.graph.GetCardHistory(s.splitter, 0);
        const CCardHistory* b = s.graph.GetCardHistory(s.splitter, 1);
        const CCardHistory* outA = s.graph.GetCardHistory(s.outletA, 0);
        const CCardHistory* outB = s.graph.GetCardHistory(s.outletB, 0);
        Check(in && a && b && outA && outB, "Kartenspeicher fuer Inlet, beide Splitter-Ausgaenge und beide Outlets");
        if (in && a && b && outA && outB)
        {
            bool counts = in->GetNumSteps() == numT;
            for (size_t k = 0; k < numT; ++k)
                counts = counts && in->GetCardCounts()[k] == 200 && a->GetCardCounts()[k] + b->GetCardCounts()[k] == 200
                    && outA->GetCardCounts()[k] == a->GetCardCounts()[k] && outB->GetCardCounts()[k] == b->GetCardCounts()[k];
            Check(counts, "Kartenzahl je Schritt: Inlet 200, A + B = 200, Outlet = ankommende Karten");
            Check(in->GetStride() == 1 && in->GetSnapshots().size() == numT, "ohne Speichergrenze jeder Schritt gespeichert");

            const double dt = s.graph.GetGrid().GetDt();
            bool mass = true, sizes = true;
            const std::vector<double>& placed = s.graph.GetInlet(s.inlet)->GetCardPlacement().sizes;
            for (const CCardHistory::SSnapshot& snapshot : in->GetSnapshots())
            {
                double sum = 0.0;
                for (double m : snapshot.massFlows)
                    sum += m;
                mass = mass && NearRel(sum, s.graph.GetInlet(s.inlet)->GetMassFlowAtTime(static_cast<double>(snapshot.step) * dt), 1e-12);
                sizes = sizes && snapshot.sizes == placed;
            }
            Check(mass && sizes, "gespeicherte Inlet-Karten: Groessen wie platziert, Massenstrom je Schritt exakt");

            // Gespeichert ist Lauf 1: derselbe Verlauf wie ein Einzellauf mit dem Seed.
            SSetup single(true);
            single.UseCards(200);
            single.graph.RunSimulation();
            const CCardHistory* singleA = single.graph.GetCardHistory(single.splitter, 0);
            SSetup other(true);
            other.UseCards(200);
            other.graph.SetRandomSeed(MakeRunSeed(other.graph.GetGrid().randomSeed, 2));
            other.graph.RunSimulation();
            const CCardHistory* otherA = other.graph.GetCardHistory(other.splitter, 0);
            Check(singleA && singleA->GetCardCounts() == a->GetCardCounts(), "Kartenspeicher zeigt Lauf 1 (wie der Einzellauf)");
            Check(otherA && otherA->GetCardCounts() != a->GetCardCounts(), "... und nicht Lauf 3 (anderer Verlauf)");
        }
        const SSimulationTiming& timing = s.graph.GetSimulationTiming();
        Check(timing.particles && NearAbs(timing.units[0].GetCardsPerStep(), 200.0, 1e-12)
            && NearAbs(timing.units[1].GetCardsPerStep(), 200.0, 1e-12)
            && NearAbs(timing.units[2].GetCardsPerStep() + timing.units[3].GetCardsPerStep(), 200.0, 1e-9),
            "Karten je Schritt: Inlet 200, Splitter 200, Outlets zusammen 200");
        Check(CountLines(s.graph.GetSimulationLog(), "Karten fuer die Kartenansicht gespeichert") == 1
            && CountLines(s.graph.GetSimulationLog(), "200 Karten je Zeitschritt in") == 1,
            "Partikelmodus im Protokoll: Karten des Inlets und Kartenspeicher");

        SSetup continuum(true);
        continuum.graph.RunSimulations(2);
        Check(continuum.graph.GetCardHistory(continuum.inlet, 0) == nullptr, "Kontinuum: kein Kartenspeicher");
    }

    // --- 5) Speichergrenze: weniger Schritte, Kartenzahl vollstaendig ------
    {
        SSetup s(true);
        const size_t perSnapshot = 400 * sizeof(double); // 200 Karten: Groesse + Massenstrom
        const size_t budget = 5 * 8 * perSnapshot;       // 5 Stroeme, je etwa 8 Schritte
        s.UseCards(200, budget);
        s.graph.RunSimulations(2);
        const CCardHistory* in = s.graph.GetCardHistory(s.inlet, 0);
        Check(in != nullptr, "Kartenspeicher mit Grenze vorhanden");
        if (in)
        {
            const size_t stride = in->GetStride();
            bool aligned = true, complete = true;
            for (const CCardHistory::SSnapshot& snapshot : in->GetSnapshots())
                aligned = aligned && snapshot.step % stride == 0;
            for (size_t k = 0; k < numT; ++k)
                complete = complete && in->GetCardCounts()[k] == 200;
            std::printf("      Grenze %zu Byte je Strom: jeder %zu. Schritt, %zu gespeichert, %zu Byte\n",
                budget / 5, stride, in->GetSnapshots().size(), in->GetStoredBytes());
            Check(stride == 8 && in->GetSnapshots().size() == 6 && in->GetStoredBytes() <= budget / 5,
                "Grenze eingehalten: jeder 8. Schritt (0, 8, ... 40)");
            Check(aligned && complete, "gespeicherte Schritte Vielfache des Abstands, Kartenzahl aller Schritte vollstaendig");
            const CCardHistory::SSnapshot* nearest = in->FindNearest(13);
            Check(nearest && nearest->step == 16 && in->FindNearest(100)->step == 40,
                "naechster gespeicherter Schritt zu 13 ist 16, zu 100 der letzte (40)");
        }
    }

    // --- 6) Beschreibung der Units im Protokoll -------------------------
    {
        SSetup s(false);
        s.UseCards(2000);
        s.graph.RunSimulations(1);
        const CSimulationLog& log = s.graph.GetSimulationLog();
        Check(CountLines(log, "Anteile 30 / 70 %, fest, Karten nach dem Konto-Verfahren") == 1,
            "Splitter mit festem Anteil im Partikelmodus");
        Check(CountLines(log, "Log-Normal") == 1 && CountLines(log, "2000 Karten je Zeitschritt in 3 Groessenbereichen") == 1,
            "Inlet: Verteilung und Karten je Groessenbereich");
        Check(CountLines(log, "Simulation gestartet: 1 Lauf,") == 1, "deterministisch: ein Lauf");
    }

    // --- 7) Zeiteinheiten: Zeitstempel auf 1 us, eine Einheit je Tabellenzeile
    {
        SSetup s(false);
        s.graph.RunSimulations(2);
        const CSimulationLog& log = s.graph.GetSimulationLog();
        const std::string ms = log.ToText();
        const std::string us = log.ToText(6);
        const std::string stamp = us.substr(0, us.find(" s] ")); // "[    0,000012"
        const size_t comma = stamp.find(',');
        Check(ms.find(" s] ") == 10 && us.find(" s] ") == 13 && comma != std::string::npos && stamp.size() - comma - 1 == 6,
            "Zeitstempel auf 1 ms (Standard) oder auf 1 us genau");
        Check(ChooseTimeUnit(0.9e-3) == ETimeUnit::Microseconds && ChooseTimeUnit(1e-3) == ETimeUnit::Milliseconds
            && FormatDurationIn(12.34e-6, ETimeUnit::Microseconds) == "12,3 \xC2\xB5s"
            && FormatDurationIn(0.0123456, ETimeUnit::Milliseconds) == "12,346 ms"
            && FormatDurationIn(4e-6, ETimeUnit::Milliseconds) == "0,004 ms"
            && FormatDurationIn(1.4412, ETimeUnit::Milliseconds) == "1441,200 ms",
            "Tabelle: us unter 1 ms, sonst ms mit drei Nachkommastellen");
    }

    return Summary();
}
