// Groessenverteilungen am Inlet (Methoden-Dokument Kapitel 3).
#include "FlowsheetGraph.h"
#include "SizeDistribution.h"
#include "TestSupport.h"

using namespace stosis;
using namespace stosis_test;

int main()
{
    const SSizeDistribution normal{ EDistributionType::Normal, 60e-6, 5e-6 };
    const SSizeDistribution logNormal{ EDistributionType::LogNormal, 60e-6, 0.1 };
    const SSizeDistribution rrsb{ EDistributionType::RRSB, 65e-6, 8.0 };

    // Feines, weites Raster: jede Dichte muss auf 1 integrieren.
    const double fineN = normal.CapturedFraction(SSizeGrid::Linear(100001, 0.0, 1000e-6));
    const double fineL = logNormal.CapturedFraction(SSizeGrid::Linear(100001, 0.0, 1000e-6));
    const double fineR = rrsb.CapturedFraction(SSizeGrid::Linear(100001, 0.0, 1000e-6));
    std::printf("      fein:     Normal %.8f  Log-Normal %.8f  RRSB %.8f\n", fineN, fineL, fineR);
    Check(NearAbs(fineN, 1.0, 1e-6) && NearAbs(fineL, 1.0, 1e-6) && NearAbs(fineR, 1.0, 1e-6),
        "Feines Raster 0-1000 um (100001 Punkte): alle Verteilungen integrieren auf 1");

    // Standardraster 150 Punkte, 0-200: Trapezregel mit dd = 1,34 um bei sigma = 5 um.
    const double stdN = normal.CapturedFraction(SSizeGrid::Linear(150, 0.0, 200e-6));
    const double stdL = logNormal.CapturedFraction(SSizeGrid::Linear(150, 0.0, 200e-6));
    const double stdR = rrsb.CapturedFraction(SSizeGrid::Linear(150, 0.0, 200e-6));
    std::printf("      Standard: Normal %.8f  Log-Normal %.8f  RRSB %.8f\n", stdN, stdL, stdR);
    Check(NearAbs(stdN, 1.0, 1e-3) && NearAbs(stdL, 1.0, 1e-3) && NearAbs(stdR, 1.0, 1e-3),
        "Standardraster 0-200 um (150 Punkte): Erfassungsanteil auf 0.1 % genau");

    // Am Rand abgeschnitten: d0 = 195 um, sigma = 5 um -> P(d < 200 um) = Phi(1) = 0.8413.
    const SSizeDistribution cut{ EDistributionType::Normal, 195e-6, 5e-6 };
    const double cutFraction = cut.CapturedFraction(SSizeGrid::Linear(150, 0.0, 200e-6));
    std::printf("      abgeschnitten (d0 = 195 um): %.4f\n", cutFraction);
    Check(NearAbs(cutFraction, 0.8413, 0.01), "Abgeschnittene Verteilung: Erfassungsanteil etwa Phi(1) = 0.84");

    // Inlet mit Testprofil: Integral des Massenstroms ueber 10 s = 85 kg
    // (0-2 s: 10, 2-4 s: 10, 4-6 s: 20, 6-7 s: 15, 7-10 s: 30).
    for (const SSizeDistribution& distribution : { normal, rrsb })
    {
        CFlowsheetGraph graph;
        const std::string in = graph.AddInletNode("Inlet1", 0, 0);
        const std::string out = graph.AddOutletNode("Outlet1", 1, 0);
        graph.GetInlet(in)->SetTimeProfile({ {0, 0}, {2, 10}, {4, 0}, {6, 20}, {7, 10} });
        graph.GetInlet(in)->SetSizeDistribution(distribution);
        graph.AddConnection(in, out);
        graph.ConfigureGrid(150, 0.0, 200e-6, 200, 10.0, ESizeGridType::Linear);
        graph.RunSimulation();
        const double total = graph.GetDisplayResults(in, 0)->IntegrateTotal();
        std::printf("      Inlet %s: Gesamtmasse %.4f kg\n", distribution.type == EDistributionType::Normal ? "Normal" : "RRSB", total);
        Check(NearRel(total, 85.0, 0.01), "Inlet mit Testprofil: Gesamtmasse 85 kg (Zeitdiskretisierung, 1 %)");
    }

    return Summary();
}
