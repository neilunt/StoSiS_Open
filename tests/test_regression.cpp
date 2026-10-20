// Regressionstest: feste Flowsheets rechnen und Kennzahlen ALLER Ergebnisse
// (Einzellauf, Mittelwert, Standardabweichung je Port: Summe und
// Quadratsumme aller Matrixwerte) mit gespeicherten Referenzwerten
// vergleichen. Schlaegt an, sobald sich irgendein Ergebnis aendert.
//
// Die Referenzwerte stammen aus einem Lauf mit dem Stand, in dem dieser Test
// angelegt wurde (14.10.2026, Windows/MSVC, Release), umgerechnet auf
// SI-Einheiten und ergaenzt um drei Faelle auf dem logarithmischen Raster
// (19.10.2026). Aendert sich die Rechnung bewusst, werden sie neu erzeugt:
//     test_regression --update
// gibt die Tabelle kReference zum Einfuegen aus. Vorher die Aenderung mit
// den fachlichen Tests begruenden - dieser Test sagt nur DASS sich etwas
// geaendert hat, nicht ob es richtig ist.
#include "FlowsheetGraph.h"
#include "TestSupport.h"

#include <algorithm>
#include <cstring>
#include <map>

using namespace stosis;
using namespace stosis_test;

static void AddFingerprint(std::vector<double>& values, const CSimulationResults& r)
{
    double sum = 0.0, sumSq = 0.0;
    for (size_t t = 0; t < r.GetNumTimeSteps(); ++t)
        for (size_t c = 0; c < r.GetNumSizeClasses(); ++c)
        {
            const double v = r.GetValue(t, c);
            sum += v;
            sumSq += v * v;
        }
    values.push_back(sum);
    values.push_back(sumSq);
}

static std::vector<double> Fingerprint(CFlowsheetGraph& graph)
{
    std::vector<double> values;
    for (const SResultsEntry& entry : graph.GetResultsEntries())
    {
        const CEnsembleStatistics* stats = graph.GetStatistics(entry.nodeId, entry.port);
        AddFingerprint(values, stats->GetFirstRun());
        AddFingerprint(values, stats->GetMean());
        AddFingerprint(values, stats->GetStdDev());
    }
    return values;
}

// Inlet -> groessenabhaengige Verzoegerung -> Outlet mit wechselndem Zulauf.
static std::vector<double> Chain(double delay, double exponent, size_t numTimeSteps, double totalTime)
{
    CFlowsheetGraph g;
    const std::string in = g.AddInletNode("In", 0, 0);
    const std::string pr = g.AddProcessNode("P", 0, 0);
    const std::string out = g.AddOutletNode("Out", 0, 0);
    g.AddConnection(in, pr);
    g.AddConnection(pr, out);
    g.GetInlet(in)->SetTimeProfile({ {0, 0}, {2, 10}, {4, 0}, {6, 20}, {7, 10}, {12, 0}, {18, 10}, {20, 0}, {30, 20}, {33, 10} });
    g.GetInlet(in)->SetSizeDistribution(60e-6, 5e-6);
    g.GetProcessor(pr)->SetParameter("delayRef", delay);
    g.GetProcessor(pr)->SetParameter("sizeExponent", exponent);
    g.GetProcessor(pr)->SetParameter("referenceSize", 60e-6);
    g.ConfigureGrid(150, 0.0, 200e-6, numTimeSteps, totalTime, ESizeGridType::Linear);
    g.RunSimulations(1);
    return Fingerprint(g);
}

static std::map<std::string, std::vector<double>> Compute()
{
    std::map<std::string, std::vector<double>> results;

    // Start-Flowsheet des Programms bis 19.10.2026 (Inlet -> Verzoegerung -> Outlet), Standardwerte.
    {
        CFlowsheetGraph graph;
        const std::string in = graph.AddInletNode("Inlet1", 0, 0);
        const std::string pr = graph.AddProcessNode("Prozess1", 1, 0);
        const std::string out = graph.AddOutletNode("Outlet1", 2, 0);
        graph.AddConnection(in, pr);
        graph.AddConnection(pr, out);
        graph.ConfigureGrid(150, 0.0, 200e-6, 200, 40.0, ESizeGridType::Linear);
        graph.RunSimulations(20);
        results["Start-Flowsheet"] = Fingerprint(graph);
    }

    // Zwei Inlets, Merger, stochastischer 3er-Splitter, Rueckfuehrung ueber Verzoegerung.
    {
        CFlowsheetGraph graph;
        const std::string in1 = graph.AddInletNode("Inlet1", 0, 0);
        const std::string in2 = graph.AddInletNode("Inlet2", 0, 1);
        const std::string mg = graph.AddMergerNode("Merger1", 3, 1, 0);
        const std::string sp = graph.AddSplitterNode("Splitter1", 3, 2, 0);
        const std::string pr = graph.AddProcessNode("Prozess1", 3, 1);
        const std::string o1 = graph.AddOutletNode("Outlet1", 4, 0);
        const std::string o2 = graph.AddOutletNode("Outlet2", 4, 1);
        graph.GetInlet(in2)->SetTimeProfile({ {0.0, 5.0}, {40.0, 5.0} });
        graph.GetInlet(in2)->SetSizeDistribution(60e-6, 10e-6);
        graph.GetProcessor(pr)->SetParameter("delayRef", 3.0);
        graph.GetProcessor(pr)->SetParameter("sizeExponent", 1.0);
        CSplitterUnit* splitter = graph.GetSplitter(sp);
        splitter->SetFractions({ 0.5, 0.3, 0.2 });
        splitter->SetStochastic(true);
        splitter->SetFluctuations({ {0.08, 1.5}, {0.05, 0.5} });
        graph.AddConnection(in1, 0, mg, 0);
        graph.AddConnection(in2, 0, mg, 1);
        graph.AddConnection(mg, 0, sp, 0);
        graph.AddConnection(sp, 0, o1, 0);
        graph.AddConnection(sp, 1, pr, 0);
        graph.AddConnection(pr, 0, mg, 2);
        graph.AddConnection(sp, 2, o2, 0);
        graph.ConfigureGrid(120, 0.0, 200e-6, 250, 40.0, ESizeGridType::Linear);
        graph.SetRandomSeed(7);
        graph.RunSimulations(20);
        results["Kreis stochastisch"] = Fingerprint(graph);
    }

    results["Kette d2 p1"] = Chain(2.0, 1.0, 200, 10.0);
    results["Kette d0.37 p-1"] = Chain(0.37, -1.0, 800, 40.0);
    results["Kette d0 p1"] = Chain(0.0, 1.0, 200, 10.0);
    results["Kette d5 p3"] = Chain(5.0, 3.0, 300, 40.0);

    // Splitter 20/30/50 und Merger, RRSB-Verteilung.
    {
        CFlowsheetGraph g;
        const std::string in = g.AddInletNode("In", 0, 0);
        const std::string sp = g.AddSplitterNode("Split", 3, 0, 0);
        const std::string mg = g.AddMergerNode("Merge", 2, 0, 0);
        const std::string oa = g.AddOutletNode("A", 0, 0);
        const std::string ob = g.AddOutletNode("B", 0, 0);
        g.GetInlet(in)->SetTimeProfile({ {0, 0}, {2, 10}, {4, 0}, {6, 20}, {7, 10} });
        g.GetInlet(in)->SetSizeDistribution(SSizeDistribution{ EDistributionType::RRSB, 65e-6, 8 });
        g.GetSplitter(sp)->SetFractions({ 0.2, 0.3, 0.5 });
        g.AddConnection(in, 0, sp, 0);
        g.AddConnection(sp, 0, oa, 0);
        g.AddConnection(sp, 1, mg, 0);
        g.AddConnection(sp, 2, mg, 1);
        g.AddConnection(mg, 0, ob, 0);
        // Bis 19.10.2026 das Standardraster; seitdem ausdruecklich linear.
        g.ConfigureGrid(150, 0.0, 200e-6, 200, 10.0, ESizeGridType::Linear);
        g.RunSimulations(1);
        results["Splitter/Merger RRSB"] = Fingerprint(g);
    }

    // Logarithmisches Raster (seit 19.10.2026 Standard): Start-Flowsheet mit
    // Standardwerten und Rueckfuehrung mit stochastischem Splitter.
    {
        CFlowsheetGraph graph;
        const std::string in = graph.AddInletNode("Inlet1", 0, 0);
        const std::string pr = graph.AddProcessNode("Prozess1", 1, 0);
        const std::string out = graph.AddOutletNode("Outlet1", 2, 0);
        graph.AddConnection(in, pr);
        graph.AddConnection(pr, out);
        graph.ConfigureGrid(300, 1e-6, 1e-3, 200, 40.0);
        graph.RunSimulations(20);
        results["Log Start-Flowsheet"] = Fingerprint(graph);
    }
    // Start-Flowsheet des Programms seit 19.10.2026 (MainWindow::BuildTestFlowsheet):
    // Inlet -> Merger -> Splitter 70/30 -> Outlet, 30 % ueber die
    // Verzoegerung zurueck zum Merger.
    {
        CFlowsheetGraph graph;
        const std::string in = graph.AddInletNode("Inlet1", 0, 55);
        const std::string mg = graph.AddMergerNode("Merger1", 2, 160, 60);
        const std::string sp = graph.AddSplitterNode("Splitter1", 2, 320, 60);
        const std::string out = graph.AddOutletNode("Outlet1", 500, 0);
        const std::string pr = graph.AddProcessNode("Prozess1", 500, 120);
        graph.GetSplitter(sp)->SetFractions({ 0.7, 0.3 });
        graph.AddConnection(in, 0, mg, 0);
        graph.AddConnection(mg, 0, sp, 0);
        graph.AddConnection(sp, 0, out, 0);
        graph.AddConnection(sp, 1, pr, 0);
        graph.AddConnection(pr, 0, mg, 1);
        graph.ConfigureGrid(300, 1e-6, 1e-3, 200, 40.0);
        graph.RunSimulations(20);
        results["Log Start-Flowsheet Rueckfuehrung"] = Fingerprint(graph);
    }
    {
        CFlowsheetGraph graph;
        const std::string in1 = graph.AddInletNode("Inlet1", 0, 0);
        const std::string in2 = graph.AddInletNode("Inlet2", 0, 1);
        const std::string mg = graph.AddMergerNode("Merger1", 3, 1, 0);
        const std::string sp = graph.AddSplitterNode("Splitter1", 2, 2, 0);
        const std::string pr = graph.AddProcessNode("Prozess1", 3, 1);
        const std::string o1 = graph.AddOutletNode("Outlet1", 4, 0);
        graph.GetInlet(in2)->SetTimeProfile({ {0.0, 5.0}, {40.0, 5.0} });
        graph.GetInlet(in2)->SetSizeDistribution(SSizeDistribution{ EDistributionType::LogNormal, 10e-6, 0.8 });
        graph.GetProcessor(pr)->SetParameter("delayRef", 3.0);
        graph.GetProcessor(pr)->SetParameter("sizeExponent", -0.5);
        CSplitterUnit* splitter = graph.GetSplitter(sp);
        splitter->SetFractions({ 0.6, 0.4 });
        splitter->SetStochastic(true);
        splitter->SetFluctuations({ {0.08, 1.5} });
        graph.AddConnection(in1, 0, mg, 0);
        graph.AddConnection(in2, 0, mg, 1);
        graph.AddConnection(mg, 0, sp, 0);
        graph.AddConnection(sp, 0, o1, 0);
        graph.AddConnection(sp, 1, pr, 0);
        graph.AddConnection(pr, 0, mg, 2);
        graph.ConfigureGrid(400, 0.1e-6, 1e-3, 250, 40.0);
        graph.SetRandomSeed(7);
        graph.RunSimulations(20);
        results["Log Kreis stochastisch"] = Fingerprint(graph);
    }
    return results;
}

// Referenzwerte (mit --update erzeugt am 19.10.2026 nach der Umstellung auf
// SI-Einheiten: Groessen in m, Dichten je m. Jede Summe ist genau das 1e6-,
// jede Quadratsumme das 1e12-Fache der Werte vom 14.10.2026 - groesste
// relative Abweichung 9e-14, die Rechnung selbst ist unveraendert. Neu: die
// drei Faelle "Log ..." auf dem logarithmischen Raster, darunter das neue
// Start-Flowsheet mit Rueckfuehrung).
static const std::map<std::string, std::vector<double>> kReference = {
    { "Kette d0 p1", {
        1128207035.1758766, 660509402647555.88, 1128207035.1758766, 660509402647555.88,
        0, 0, 1128207035.1758766, 660509402647555.88,
        1128207035.1758766, 660509402647555.88, 0, 0,
        1128207035.1758766, 660509402647555.88, 1128207035.1758766, 660509402647555.88,
        0, 0 } },
    { "Kette d0.37 p-1", {
        4988982478.0973873, 3192586735535583.5, 4988982478.0973873, 3192586735535583.5,
        0, 0, 4933530764.9659424, 3161217944302840,
        4933530764.9659424, 3161217944302840, 0, 0,
        4933530764.9659424, 3161217944302840, 4933530764.9659424, 3161217944302840,
        0, 0 } },
    { "Kette d2 p1", {
        1128207035.1758766, 660509402647555.88, 1128207035.1758766, 660509402647555.88,
        0, 0, 951373055.77802682, 598744563942247.88,
        951373055.77802682, 598744563942247.88, 0, 0,
        951373055.77802682, 598744563942247.88, 951373055.77802682, 598744563942247.88,
        0, 0 } },
    { "Kette d5 p3", {
        1869332073.5786037, 1196168515106745, 1869332073.5786037, 1196168515106745,
        0, 0, 1584421349.4351051, 1036839298957497.6,
        1584421349.4351051, 1036839298957497.6, 0, 0,
        1584421349.4351051, 1036839298957497.6, 1584421349.4351051, 1036839298957497.6,
        0, 0 } },
    { "Kreis stochastisch", {
        1289835216.4952712, 167823343751581.69, 1289835216.4952712, 167823343751581.69,
        0, 0, 743749999.58310592, 104904000690907.53,
        743749999.58310592, 104904000690907.53, 0, 0,
        2805915074.3956857, 623472567000517.88, 2808725666.8958564, 622235433114876.62,
        127544373.68902782, 1420497112599.6514, 1397369728.4647024, 158823593094959.72,
        1411304571.0870166, 157011517298810.31, 237058506.88845173, 4476619304698.5684,
        839331386.27744877, 57070116981899.977, 845071488.70579016, 56365503188917.961,
        145062125.27577141, 1681496811432.9629, 569213959.65351427, 27808054490763.02,
        552349607.1030333, 24349796537006.477, 263438887.98560888, 5594758784181.3848,
        776298434.08691955, 53054156984023.055, 779183768.89141715, 52095120215931.664,
        128175979.85628256, 1427539097813.3789, 1397369728.4647024, 158823593094959.72,
        1411304571.0870166, 157011517298810.31, 237058506.88845173, 4476619304698.5684,
        569213959.65351427, 27808054490763.02, 552349607.1030333, 24349796537006.477,
        263438887.98560888, 5594758784181.3848 } },
    { "Log Kreis stochastisch", {
        1017550540.6504942, 126439460899986.25, 1017550540.6504942, 126439460899986.25,
        0, 0, 7457294910.198967, 1810634463381902.5,
        7457294910.198967, 1810634463381902.5, 0, 0,
        11543296609.544096, 3669251190816737, 11379580979.577705, 3539014886806392.5,
        589913196.51584625, 14133257439597.635, 6852586785.3592548, 1309869946932567.8,
        6870813193.2370367, 1293662031982511.2, 988076542.78096747, 28274973735740.441,
        4690709824.184824, 614881493251815.12, 4508767786.3406382, 554787847704672.5,
        943512853.63612902, 25155741198399.855, 3089559690.6700826, 383384339143329,
        2924985106.8576565, 331462441890766.62, 594251393.82673979, 14254719458083.443,
        6852586785.3592548, 1309869946932567.8, 6870813193.2370367, 1293662031982511.2,
        988076542.78096747, 28274973735740.441 } },
    { "Log Start-Flowsheet", {
        812125675.54398692, 101084673539234.8, 812125675.54398692, 101084673539234.8,
        0, 0, 769059026.46683025, 96110377161050.641,
        769059026.46683025, 96110377161050.641, 0, 0,
        769059026.46683025, 96110377161050.641, 769059026.46683025, 96110377161050.641,
        0, 0 } },
    { "Log Start-Flowsheet Rueckfuehrung", {
        812125675.54398692, 101084673539234.8, 812125675.54398692, 101084673539234.8,
        0, 0, 1130608805.5130408, 183031713710217.91,
        1130608805.5130408, 183031713710217.91, 0, 0,
        791426163.85913348, 89685539718006.984, 791426163.85913348, 89685539718006.984,
        0, 0, 339182641.65391445, 16472854233919.732,
        339182641.65391445, 16472854233919.732, 0, 0,
        791426163.85913348, 89685539718006.984, 791426163.85913348, 89685539718006.984,
        0, 0, 320521723.11390656, 15553323980446.133,
        320521723.11390656, 15553323980446.133, 0, 0 } },
    { "Splitter/Merger RRSB", {
        1263879396.9849153, 443840020864333, 1263879396.9849153, 443840020864333,
        0, 0, 252775879.39698917, 17753600834573.41,
        252775879.39698917, 17753600834573.41, 0, 0,
        379163819.09548622, 39945601877791.391, 379163819.09548622, 39945601877791.391,
        0, 0, 631939698.49245763, 110960005216083.25,
        631939698.49245763, 110960005216083.25, 0, 0,
        1011103517.5879567, 284057613353174.56, 1011103517.5879567, 284057613353174.56,
        0, 0, 252775879.39698917, 17753600834573.41,
        252775879.39698917, 17753600834573.41, 0, 0,
        1011103517.5879567, 284057613353174.56, 1011103517.5879567, 284057613353174.56,
        0, 0 } },
    { "Start-Flowsheet", {
        1291603638.8897882, 168134796727700.94, 1291603638.8897882, 168134796727700.94,
        0, 0, 1217480320.7191072, 159579582331205.25,
        1217480320.7191072, 159579582331205.25, 0, 0,
        1217480320.7191072, 159579582331205.25, 1217480320.7191072, 159579582331205.25,
        0, 0 } },
};

int main(int argc, char** argv)
{
    const std::map<std::string, std::vector<double>> results = Compute();

    if (argc > 1 && std::strcmp(argv[1], "--update") == 0)
    {
        std::printf("static const std::map<std::string, std::vector<double>> kReference = {\n");
        for (const auto& [name, values] : results)
        {
            std::printf("    { \"%s\", {", name.c_str());
            for (size_t i = 0; i < values.size(); ++i)
                std::printf("%s%s%.17g", i == 0 ? "" : ",", i % 4 == 0 ? "\n        " : " ", values[i]);
            std::printf(" } },\n");
        }
        std::printf("};\n");
        return 0;
    }

    // Relative Toleranz 1e-9: unempfindlich gegen Rundung in der letzten
    // Stelle (anderer Compiler, andere mathematische Bibliothek), aber
    // jede echte Aenderung der Rechnung faellt auf. Referenz 0 (z. B.
    // Standardabweichung eines deterministischen Flowsheets) muss exakt 0 sein.
    for (const auto& [name, values] : results)
    {
        const auto ref = kReference.find(name);
        bool same = ref != kReference.end() && ref->second.size() == values.size();
        double worst = 0.0;
        for (size_t i = 0; same && i < values.size(); ++i)
        {
            const double expected = ref->second[i];
            const double deviation = std::abs(values[i] - expected);
            worst = std::max(worst, expected != 0.0 ? deviation / std::abs(expected) : deviation);
            same = same && deviation <= 1e-9 * std::abs(expected);
        }
        std::printf("      %-22s %zu Kennzahlen, groesste rel. Abweichung %.1e\n", name.c_str(), values.size(), worst);
        Check(same, name + ": alle Ergebnisse wie Referenz");
    }
    return Summary();
}
