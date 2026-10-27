// Regressionstest: feste Flowsheets rechnen und Kennzahlen ALLER Ergebnisse
// (Einzellauf, Mittelwert, Standardabweichung je Port: Summe und
// Quadratsumme aller Matrixwerte) mit gespeicherten Referenzwerten
// vergleichen. Schlaegt an, sobald sich irgendein Ergebnis aendert.
//
// Aendert sich die Rechnung bewusst, werden die Referenzwerte neu erzeugt:
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

    // Einfaches Flowsheet Inlet -> Verzoegerung -> Outlet, Standardwerte.
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
        // Ausdruecklich lineares Raster.
        g.ConfigureGrid(150, 0.0, 200e-6, 200, 10.0, ESizeGridType::Linear);
        g.RunSimulations(1);
        results["Splitter/Merger RRSB"] = Fingerprint(g);
    }

    // Logarithmisches Raster (Standard): einfaches Flowsheet mit
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
    // Start-Flowsheet des Programms (MainWindow::BuildTestFlowsheet; dort mit
    // dt = 0,2 s, also 201 statt hier 200 Zeitpunkten):
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

// Referenzwerte (mit --update erzeugt; Windows/MSVC, Release, SI-Einheiten:
// Groessen in m, Dichten je m).
static const std::map<std::string, std::vector<double>> kReference = {
    { "Kette d0 p1", {
        1128207035.1758759, 656574461453255.75, 1128207035.1758759, 656574461453255.75,
        0, 0, 1128207035.1758759, 656574461453255.75,
        1128207035.1758759, 656574461453255.75, 0, 0,
        1128207035.1758759, 656574461453255.75, 1128207035.1758759, 656574461453255.75,
        0, 0 } },
    { "Kette d0.37 p-1", {
        4988982478.0977087, 3173567110664703.5, 4988982478.0977087, 3173567110664703.5,
        0, 0, 4933525969.0943995, 3142383951435540.5,
        4933525969.0943995, 3142383951435540.5, 0, 0,
        4933525969.0943995, 3142383951435540.5, 4933525969.0943995, 3142383951435540.5,
        0, 0 } },
    { "Kette d2 p1", {
        1128207035.1758759, 656574461453255.75, 1128207035.1758759, 656574461453255.75,
        0, 0, 951368104.86403215, 595175110068509.25,
        951368104.86403215, 595175110068509.25, 0, 0,
        951368104.86403215, 595175110068509.25, 951368104.86403215, 595175110068509.25,
        0, 0 } },
    { "Kette d5 p3", {
        1869332073.5785596, 1189042420085881.8, 1869332073.5785596, 1189042420085881.8,
        0, 0, 1584328154.7891941, 1030639078060406.6,
        1584328154.7891941, 1030639078060406.6, 0, 0,
        1584328154.7891941, 1030639078060406.6, 1584328154.7891941, 1030639078060406.6,
        0, 0 } },
    { "Kreis stochastisch", {
        1286510751.6059866, 170388050964522.03, 1286510751.6059866, 170388050964522.03,
        0, 0, 743749999.81187892, 104657851176691.45,
        743749999.81187892, 104657851176691.45, 0, 0,
        2799215895.7074432, 611697543303311, 2801956877.1366782, 610397360430945.75,
        126907454.32934226, 1392317504524.1838, 1393915972.0402641, 155840828423320.19,
        1407921862.5364785, 154032215012323.5, 236529162.27772313, 4390167696468.3848,
        837357871.90400469, 56001984321523.766, 843032622.59050989, 55292242492616,
        144617081.16304538, 1648639461830.9141, 567942051.76321495, 27271883975338.883,
        551002392.00967765, 23884095948171.25, 262859691.97857338, 5489731193412.0957,
        772918078.02169287, 52058361428558.922, 775742942.27847981, 51095975880126.352,
        127538068.04366305, 1399254937018.5381, 1393915972.0402641, 155840828423320.19,
        1407921862.5364785, 154032215012323.5, 236529162.27772313, 4390167696468.3848,
        567942051.76321495, 27271883975338.883, 551002392.00967765, 23884095948171.25,
        262859691.97857338, 5489731193412.0957 } },
    { "Log Kreis stochastisch", {
        969012199.76756859, 129988891715579.44, 969012199.76756859, 129988891715579.44,
        0, 0, 7457626067.6869698, 1810669648687914,
        7457626067.6869698, 1810669648687914, 0, 0,
        11472828892.889452, 3668236090616827.5, 11310264984.866493, 3538144146639317,
        585654346.70078719, 14141461983513.83, 6810477026.1400671, 1309512534635073.5,
        6828985445.3191729, 1293326757318501.2, 982101980.16776717, 28267413140754.57,
        4662351866.7497892, 614707085657897.25, 4481279539.5476828, 554662887734813.19,
        937761298.268718, 25154049291625.059, 3067159121.4846082, 383439134306258.38,
        2903720483.5774078, 331564928132730.94, 589958595.05767643, 14263517509824.842,
        6810477026.1400671, 1309512534635073.5, 6828985445.3191729, 1293326757318501.2,
        982101980.16776717, 28267413140754.57 } },
    { "Log Start-Flowsheet", {
        774324056.97751093, 103922299018820.56, 774324056.97751093, 103922299018820.56,
        0, 0, 731255796.98773336, 98872114243904.766,
        731255796.98773336, 98872114243904.766, 0, 0,
        731255796.98773336, 98872114243904.766, 731255796.98773336, 98872114243904.766,
        0, 0 } },
    { "Log Start-Flowsheet Rueckfuehrung", {
        774324056.97751093, 103922299018820.56, 774324056.97751093, 103922299018820.56,
        0, 0, 1076708502.0635459, 188353921816217.41,
        1076708502.0635459, 188353921816217.41, 0, 0,
        753695951.44448495, 92293421689946.25, 753695951.44448495, 92293421689946.25,
        0, 0, 323012550.61906177, 16951852963459.518,
        323012550.61906177, 16951852963459.518, 0, 0,
        753695951.44448495, 92293421689946.25, 753695951.44448495, 92293421689946.25,
        0, 0, 304331231.49735403, 16019490906107.133,
        304331231.49735403, 16019490906107.133, 0, 0 } },
    { "Splitter/Merger RRSB", {
        1263879396.9849191, 442916947904666.44, 1263879396.9849191, 442916947904666.44,
        0, 0, 252775879.39698869, 17716677916187.234,
        252775879.39698869, 17716677916187.234, 0, 0,
        379163819.09547758, 39862525311419.859, 379163819.09547758, 39862525311419.859,
        0, 0, 631939698.49245954, 110729236976166.61,
        631939698.49245954, 110729236976166.61, 0, 0,
        1011103517.5879548, 283466846658995.75, 1011103517.5879548, 283466846658995.75,
        0, 0, 252775879.39698869, 17716677916187.234,
        252775879.39698869, 17716677916187.234, 0, 0,
        1011103517.5879548, 283466846658995.75, 1011103517.5879548, 283466846658995.75,
        0, 0 } },
    { "Start-Flowsheet", {
        1288237065.5938733, 170731366597404.5, 1288237065.5938733, 170731366597404.5,
        0, 0, 1212184513.5877273, 162175691137608.44,
        1212184513.5877273, 162175691137608.44, 0, 0,
        1212184513.5877273, 162175691137608.44, 1212184513.5877273, 162175691137608.44,
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
