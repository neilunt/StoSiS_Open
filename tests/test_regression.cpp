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
#include "RegressionFlowsheets.h"

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

// The flowsheets are in RegressionFlowsheets.h (also used by
// test_particle_vs_continuum); each case gets a fresh graph.
static std::map<std::string, std::vector<double>> Compute()
{
    std::map<std::string, std::vector<double>> results;
    for (const SRegressionCase& c : RegressionCases())
    {
        CFlowsheetGraph graph;
        c.build(graph);
        graph.RunSimulations(c.runs);
        results[c.name] = Fingerprint(graph);
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
        2799898682.2124958, 612003827994630.75, 2802567553.1238189, 610671435531907.12,
        127005955.62695773, 1393286435311.4104, 1394275694.0361388, 155929255234963.22,
        1408221796.9161568, 154098790001409.69, 236560272.23891494, 4391107100039.5234,
        837557490.33075118, 56027983130560.523, 843224242.56410849, 55318989543912.086,
        144650456.83499146, 1649393576562.0447, 568065497.84562612, 27281726180363.371,
        551121513.64352548, 23894634863526.012, 262910010.10092345, 5491493622245.5693,
        773600864.52677333, 52098818322765.141, 776353618.26565027, 51133673649325.938,
        127636569.34128472, 1400223867805.9231, 1394275694.0361388, 155929255234963.22,
        1408221796.9161568, 154098790001409.69, 236560272.23891494, 4391107100039.5234,
        568065497.84562612, 27281726180363.371, 551121513.64352548, 23894634863526.012,
        262910010.10092345, 5491493622245.5693 } },
    { "Log Kreis stochastisch", {
        969012199.76756859, 129988891715579.44, 969012199.76756859, 129988891715579.44,
        0, 0, 7457626067.6869698, 1810669648687914,
        7457626067.6869698, 1810669648687914, 0, 0,
        11482719249.09845, 3674485335369753, 11319516196.101002, 3543898712007071,
        587412593.83238637, 14191027792388.801, 6816305421.2820587, 1311723363269019.5,
        6834562384.7144222, 1295425294454676.2, 983115208.83460236, 28326596219559.355,
        4666413827.8168802, 615769945685765.25, 4484953811.3869638, 555568381055550.88,
        938598876.90683496, 25200526890147.188, 3077075476.5405822, 384804534849071.69,
        2912990009.0105839, 332748029755457.38, 591721702.24150646, 14313330477554.311,
        6816305421.2820587, 1311723363269019.5, 6834562384.7144222, 1295425294454676.2,
        983115208.83460236, 28326596219559.355 } },
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
