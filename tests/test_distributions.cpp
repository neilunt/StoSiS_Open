// Groessenverteilungen am Inlet (Methoden-Dokument Kapitel 3).
#include "FlowsheetGraph.h"
#include "SizeDistribution.h"
#include "TestSupport.h"

using namespace stosis;
using namespace stosis_test;

// Dichte an den Stuetzstellen, Trapezregel mit den Gewichten des Rasters -
// prueft, dass Density und Cumulative zueinander passen.
static double TrapezoidOfDensity(const SSizeDistribution& distribution, const SSizeGrid& sizes)
{
    double sum = 0.0;
    for (size_t s = 0; s < sizes.numClasses; ++s)
        sum += sizes.GetWeight(s) * distribution.Density(sizes.GetSizeAt(s));
    return sum;
}

int main()
{
    const SSizeDistribution normal{ EDistributionType::Normal, 60e-6, 5e-6 };
    const SSizeDistribution logNormal{ EDistributionType::LogNormal, 60e-6, 0.1 };
    const SSizeDistribution rrsb{ EDistributionType::RRSB, 65e-6, 8.0 };

    // Feines, weites Raster: jede Dichte muss auf 1 integrieren (Dichte an
    // den Stuetzstellen, Trapezregel - prueft, dass Dichte und Q3 zusammenpassen).
    const double fineN = TrapezoidOfDensity(normal, SSizeGrid::Linear(100001, 0.0, 1000e-6));
    const double fineL = TrapezoidOfDensity(logNormal, SSizeGrid::Linear(100001, 0.0, 1000e-6));
    const double fineR = TrapezoidOfDensity(rrsb, SSizeGrid::Linear(100001, 0.0, 1000e-6));
    std::printf("      fein:     Normal %.8f  Log-Normal %.8f  RRSB %.8f\n", fineN, fineL, fineR);
    Check(NearAbs(fineN, 1.0, 1e-6) && NearAbs(fineL, 1.0, 1e-6) && NearAbs(fineR, 1.0, 1e-6),
        "Feines Raster 0-1000 um (100001 Punkte): alle Verteilungen integrieren auf 1");

    // Masse je Stuetzstelle exakt aus Q3 (so speist das Inlet ein): Summe =
    // Q3(d_max) - Q3(d_min) fuer alle Typen, linear und logarithmisch.
    SSizeDistribution tableForGrid{ EDistributionType::Table };
    tableForGrid.points = { { 40e-6, 0.0 }, { 63e-6, 0.03 }, { 100e-6, 0.5 }, { 160e-6, 0.97 }, { 250e-6, 1.0 } };
    const SSizeDistribution all[] = { normal, logNormal, rrsb, { EDistributionType::GGS, 200e-6, 2.0 }, tableForGrid,
        { EDistributionType::Normal, 195e-6, 5e-6 } };
    double worstSum = 0.0;
    for (const SSizeGrid& grid : { SSizeGrid::Linear(150, 0.0, 200e-6), SSizeGrid() })
        for (const SSizeDistribution& d : all)
        {
            double sum = 0.0;
            for (double m : d.MassesAtNodes(grid))
                sum += m;
            worstSum = std::max(worstSum, std::abs(sum - (d.Cumulative(grid.sizeMax) - d.Cumulative(grid.sizeMin))));
        }
    std::printf("      Masse je Stuetzstelle: Summe weicht hoechstens %.1e von Q3(d_max) - Q3(d_min) ab\n", worstSum);
    Check(worstSum < 1e-13, "Masse je Stuetzstelle: Summe = Q3(d_max) - Q3(d_min) exakt (6 Verteilungen, 2 Raster)");

    // Am Rand abgeschnitten: d0 = 195 um, sigma = 5 um -> P(0 < d < 200 um) = Phi(1).
    const SSizeDistribution cut{ EDistributionType::Normal, 195e-6, 5e-6 };
    const double cutFraction = cut.CapturedFraction(SSizeGrid::Linear(150, 0.0, 200e-6));
    std::printf("      abgeschnitten (d0 = 195 um): %.6f\n", cutFraction);
    Check(NearAbs(cutFraction, 0.841344746, 1e-9), "Abgeschnittene Verteilung: Erfassungsanteil Phi(1) = 0,841345");

    // Sprung der Dichte (GGS bei d_max = 200 um, Standardraster): Dichte an den
    // Stuetzstellen mit Trapezregel liegt daneben, die Masse je Stuetzstelle nicht.
    {
        const SSizeDistribution ggsJump{ EDistributionType::GGS, 200e-6, 2.0 };
        const SSizeGrid grid;
        double exact = 0.0;
        for (double m : ggsJump.MassesAtNodes(grid))
            exact += m;
        const double pointwise = TrapezoidOfDensity(ggsJump, grid);
        std::printf("      GGS 200 um / m = 2 im Standardraster: Dichtepunkte %.5f, Masse je Stuetzstelle %.12f\n", pointwise, exact);
        // Unter d_min = 1 um liegt (1 / 200)^2 = 2,5e-5 der Masse - ausserhalb des Rasters.
        Check(std::abs(pointwise - 1.0) > 1e-3 && NearAbs(exact, 1.0 - 2.5e-5, 1e-13),
            "GGS mit Sprung: Dichtepunkte verfehlen die Masse, Masse je Stuetzstelle trifft Q3(d_max) - Q3(d_min) exakt");
    }

    // Glatte Verteilung: Masse je Stuetzstelle / Gewicht entspricht der Dichte
    // (Standard-Inlet im Standardraster, Abweichung relativ zum Maximum).
    {
        const SSizeDistribution standard;
        const SSizeGrid grid;
        const std::vector<double> masses = standard.MassesAtNodes(grid);
        double worst = 0.0, peak = 0.0;
        for (size_t s = 0; s < grid.numClasses; ++s)
        {
            peak = std::max(peak, standard.Density(grid.GetSizeAt(s)));
            worst = std::max(worst, std::abs(masses[s] / grid.GetWeight(s) - standard.Density(grid.GetSizeAt(s))));
        }
        std::printf("      Standard-Inlet: Masse/Gewicht gegen Dichte, hoechstens %.1e des Maximums\n", worst / peak);
        Check(worst < 1e-3 * peak, "Glatte Verteilung: Masse je Stuetzstelle entspricht der Dichte (0,1 % des Maximums)");
    }

    // GGS (Gates-Gaudin-Schuhmann): Q3 = (d / d_max)^m. Mit d_max = 200 um,
    // m = 2 liegen unter 100 um genau 25 % der Masse.
    const SSizeDistribution ggs{ EDistributionType::GGS, 200e-6, 2.0 };
    const double fineG = TrapezoidOfDensity(ggs, SSizeGrid::Logarithmic(100001, 1e-9, 200e-6));
    std::printf("      GGS: Q3(100 um) = %.6f, Quantil(25 %%) = %.4f um, fein integriert %.8f\n",
        ggs.Cumulative(100e-6), ggs.Quantile(0.25) * 1e6, fineG);
    Check(NearAbs(ggs.Cumulative(100e-6), 0.25, 1e-15) && NearAbs(ggs.Quantile(0.25), 100e-6, 1e-18)
        && ggs.Cumulative(250e-6) == 1.0 && ggs.Density(250e-6) == 0.0 && NearAbs(fineG, 1.0, 1e-6),
        "GGS: Q3(100 um) = 25 %, Quantil zurueck, nichts ueber d_max, integriert auf 1");

    // Tabelle (benutzerdefiniert): Q3 zwischen den Punkten linear in ln d.
    SSizeDistribution table{ EDistributionType::Table };
    table.points = { { 40e-6, 0.0 }, { 63e-6, 0.03 }, { 100e-6, 0.5 }, { 160e-6, 0.97 }, { 250e-6, 1.0 } };
    const double mid = std::sqrt(63e-6 * 100e-6); // Mitte in ln d
    const double fineT = TrapezoidOfDensity(table, SSizeGrid::Logarithmic(100001, 30e-6, 300e-6));
    std::printf("      Tabelle: Q3(%.2f um) = %.6f, fein integriert %.8f\n", mid * 1e6, table.Cumulative(mid), fineT);
    Check(table.CheckTable().empty() && table.Cumulative(100e-6) == 0.5 && NearAbs(table.Cumulative(mid), 0.265, 1e-12)
        && NearRel(table.Quantile(0.265), mid, 1e-12) && table.Cumulative(30e-6) == 0.0 && table.Cumulative(300e-6) == 1.0,
        "Tabelle: Punkte getroffen, Mitte in ln d = Mittel der Q3-Werte, Quantil zurueck");
    Check(NearAbs(fineT, 1.0, 1e-4), "Tabelle: Dichte integriert auf 1 (Spruenge an den Punkten, Toleranz 1e-4)");
    SSizeDistribution broken = table;
    broken.points.front().fraction = 0.02;
    std::printf("      ungueltige Tabelle: %s\n", broken.CheckTable().c_str());
    Check(!broken.CheckTable().empty() && broken.Density(100e-6) == 0.0 && broken.Quantile(0.5) == 0.0,
        "Tabelle ohne Q3 = 0 % am Anfang: Meldung, Dichte und Quantil 0");

    // Standard ist Log-Normal 100 um / 0,25, die Normalverteilung hat eine Warnung.
    const SSizeDistribution standard;
    Check(standard.type == EDistributionType::LogNormal && standard.position == 100e-6 && standard.width == 0.25
        && std::string(GetDistributionWarning(EDistributionType::Normal)).size() > 0
        && std::string(GetDistributionWarning(EDistributionType::LogNormal)).empty(),
        "Standard Log-Normal d50 = 100 um, sigma_ln = 0,25; Warnung nur bei der Normalverteilung");

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
