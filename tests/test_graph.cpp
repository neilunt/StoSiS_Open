// Flowsheet-Graph: Ports, Splitter/Merger, Wahl der Schnittstelle in
// Kreisen (Methoden-Dokument Kapitel 4 und 6.2).
#include "FlowsheetGraph.h"
#include "TestSupport.h"

using namespace stosis;
using namespace stosis_test;

// Namen der aufgeschnittenen Verbindungen als "Quelle -> Ziel".
static std::vector<std::string> LaggedNames(const CFlowsheetGraph& graph)
{
    std::vector<std::string> names;
    for (const std::string& id : graph.GetLaggedConnectionIds())
        for (const SFlowsheetConnection& c : graph.GetConnections())
            if (c.id == id)
                names.push_back(graph.FindNode(c.sourceNodeId)->name + " -> " + graph.FindNode(c.targetNodeId)->name);
    return names;
}

// Skizze mit zwei Mergern im Kreis (Frischgut-Zugabe in die Rueckfuehrung):
// InA -> M1 -> S -> Out, S -> M2, InB -> M2, M2 -> M1.
static std::vector<std::string> TwoMergerCut(bool m2First)
{
    CFlowsheetGraph g;
    std::string m1, m2;
    if (m2First) { m2 = g.AddMergerNode("M2", 2, 0, 0); m1 = g.AddMergerNode("M1", 2, 0, 0); }
    else         { m1 = g.AddMergerNode("M1", 2, 0, 0); m2 = g.AddMergerNode("M2", 2, 0, 0); }
    const std::string inA = g.AddInletNode("InA", 0, 0), inB = g.AddInletNode("InB", 0, 0);
    const std::string sp = g.AddSplitterNode("S", 2, 0, 0), out = g.AddOutletNode("Out", 0, 0);
    g.AddConnection(inA, 0, m1, 0); g.AddConnection(m1, 0, sp, 0); g.AddConnection(sp, 0, out, 0);
    g.AddConnection(sp, 1, m2, 0); g.AddConnection(inB, 0, m2, 1); g.AddConnection(m2, 0, m1, 1);
    return LaggedNames(g);
}

int main()
{
    // Splitter (3 Ausgaenge) und Merger: Inlet -> S (20/30/50) -> A; S2, S3 -> M -> B
    {
        CFlowsheetGraph g;
        const std::string in = g.AddInletNode("In", 0, 0);
        const std::string sp = g.AddSplitterNode("Split", 3, 0, 0);
        const std::string mg = g.AddMergerNode("Merge", 2, 0, 0);
        const std::string oa = g.AddOutletNode("A", 0, 0);
        const std::string ob = g.AddOutletNode("B", 0, 0);
        g.GetInlet(in)->SetTimeProfile({ {0, 0}, {2, 10}, {4, 0}, {6, 20}, {7, 10} });
        g.GetInlet(in)->SetSizeDistribution(60e-6, 5e-6);
        Check(!g.GetSplitter(sp)->SetFractions({ 0.2, 0.3, 0.4 }), "Splitter: Anteile mit Summe 0.9 abgelehnt");
        Check(g.GetSplitter(sp)->SetFractions({ 0.2, 0.3, 0.5 }), "Splitter: Anteile mit Summe 1 uebernommen");
        g.AddConnection(in, 0, sp, 0);
        g.AddConnection(sp, 0, oa, 0);
        g.AddConnection(sp, 1, mg, 0);
        g.AddConnection(sp, 2, mg, 1);
        g.AddConnection(mg, 0, ob, 0);
        Check(g.AddConnection(sp, 0, ob, 0).empty(), "Bereits belegter Eingang abgelehnt");
        Check(g.AddConnection(sp, 5, ob, 0).empty(), "Ungueltiger Port abgelehnt");
        g.RunSimulation();
        const double i = g.GetDisplayResults(in, 0)->IntegrateTotal();
        const double a = g.GetDisplayResults(oa, 0)->IntegrateTotal();
        const double b = g.GetDisplayResults(ob, 0)->IntegrateTotal();
        std::printf("      Inlet %.6f | A %.6f | B %.6f\n", i, a, b);
        Check(NearRel(a, 0.2 * i, 1e-12) && NearRel(b, 0.8 * i, 1e-12), "A = 20 %, B = 30 % + 50 % des Zulaufs");
        Check(NearAbs(a + b, i, 1e-12 * i), "Massenbilanz A + B = Inlet");
    }

    // Standardnamen je Typ: kleinste freie Nummer.
    {
        CFlowsheetGraph g;
        g.AddInletNode(g.MakeDefaultNodeName(ENodeType::Inlet), 0, 0);
        g.AddInletNode(g.MakeDefaultNodeName(ENodeType::Inlet), 0, 0);
        Check(g.MakeDefaultNodeName(ENodeType::Inlet) == "Inlet3" && g.MakeDefaultNodeName(ENodeType::Splitter) == "Splitter1",
            "Standardnamen: Inlet3 nach zwei Inlets, Splitter1 als erster Splitter");
    }

    // Schnittregel bei zwei Mergern im Kreis: geschnitten wird S -> M2
    // (Rueckfuehrung vom Trennapparat), unabhaengig von der Anlegereihenfolge.
    {
        const std::vector<std::string> cutA = TwoMergerCut(false);
        const std::vector<std::string> cutB = TwoMergerCut(true);
        for (const std::string& name : cutA)
            std::printf("      geschnitten: %s\n", name.c_str());
        Check(cutA.size() == 1 && cutA[0] == "S -> M2", "Zwei Merger im Kreis: Schnitt S -> M2");
        Check(cutA == cutB, "Schnitt unabhaengig davon, welcher Merger zuerst angelegt wurde");
    }

    return Summary();
}
