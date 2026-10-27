// Logarithmisches Groessenraster und SI-Einheiten (Methoden-Dokument
// Kapitel 2.5 und 2.6).
#include "FlowsheetGraph.h"
#include "SizeDistribution.h"
#include "TestSupport.h"

#include <algorithm>
#include <cmath>

using namespace stosis;
using namespace stosis_test;

// Dichte an den Stuetzstellen, Trapezregel mit den Gewichten des Rasters
// (so integrieren Anzeige und Statistik eine Dichte).
static double TrapezoidOfDensity(const SSizeDistribution& distribution, const SSizeGrid& sizes)
{
    double sum = 0.0;
    for (size_t s = 0; s < sizes.numClasses; ++s)
        sum += sizes.GetWeight(s) * distribution.Density(sizes.GetSizeAt(s));
    return sum;
}

int main()
{
    // 1) Lage der Stuetzstellen im Standardraster (1 um bis 1 mm, 300 Stuetzstellen).
    {
        const SSizeGrid grid;
        const double r = grid.GetRatio();
        double worst = 0.0;
        for (size_t i = 0; i + 1 < grid.numClasses; ++i)
            worst = std::max(worst, std::abs(grid.GetSizeAt(i + 1) / grid.GetSizeAt(i) / r - 1.0));
        std::printf("      r = %.6f, %.2f Stuetzstellen je Dekade, groesste Abweichung vom Verhaeltnis r: %.1e\n",
            r, grid.GetClassesPerDecade(), worst);
        Check(grid.GetSizeAt(0) == 1e-6 && grid.GetSizeAt(grid.numClasses - 1) == 1e-3,
            "Erste und letzte Stuetzstelle genau auf d_min = 1 um und d_max = 1 mm");
        Check(worst < 1e-12, "Benachbarte Groessen ueberall im selben Verhaeltnis r");
        Check(NearRel(grid.GetClassesPerDecade(), 299.0 / 3.0, 1e-12), "299 Abstaende auf 3 Dekaden = 99,7 je Dekade");
        Check(SSimulationGrid().GetSizeGrid() == grid, "Flowsheet-Raster hat dasselbe Standardraster");
        Check(!SSizeGrid::Logarithmic(300, 0.0, 1e-3).IsValid() && SSizeGrid::Linear(150, 0.0, 200e-6).IsValid(),
            "Logarithmisches Raster braucht d_min > 0, lineares nicht");
        Check(std::string(kSizeDisplayUnit) == "\xC2\xB5m" && kSizeDisplayFactor == 1e6,
            "Anzeige in um: Faktor 1e6, Einheit als UTF-8");
    }

    // 2) Trapezregel in ln d gegen die exakte Verteilungssumme Q3 (Standardraster).
    {
        const SSizeGrid grid;
        struct SCase
        {
            const char* name;
            SSizeDistribution distribution;
        };
        const SCase cases[] = {
            { "Normal 100/25 um", { EDistributionType::Normal, 100e-6, 25e-6 } },
            { "Log-Normal 5 um/0.5", { EDistributionType::LogNormal, 5e-6, 0.5 } },
            { "Log-Normal 30 um/1", { EDistributionType::LogNormal, 30e-6, 1.0 } },
            { "Log-Normal 60 um/0.1", { EDistributionType::LogNormal, 60e-6, 0.1 } },
            { "RRSB 65 um/8", { EDistributionType::RRSB, 65e-6, 8.0 } },
        };
        double worst = 0.0;
        for (const SCase& c : cases)
        {
            const double exact = c.distribution.Cumulative(grid.sizeMax) - c.distribution.Cumulative(grid.sizeMin);
            const double captured = TrapezoidOfDensity(c.distribution, grid);
            std::printf("      %-22s erfasst %.8f | exakt %.8f | Abweichung %.1e\n", c.name, captured, exact, captured - exact);
            worst = std::max(worst, std::abs(captured - exact));
        }
        Check(worst < 1e-5, "Log. Raster: Erfassungsanteil (Trapez in ln d) trifft Q3 auf 1e-5 genau, fuenf Verteilungen");
    }

    // 3) Verteilungssumme Q3: bekannte Punkte und numerische Integration.
    {
        const SSizeDistribution normal{ EDistributionType::Normal, 100e-6, 25e-6 };
        const SSizeDistribution logNormal{ EDistributionType::LogNormal, 30e-6, 1.0 };
        const SSizeDistribution rrsb{ EDistributionType::RRSB, 65e-6, 8.0 };
        Check(NearAbs(normal.Cumulative(100e-6), 0.5, 1e-15) && NearAbs(logNormal.Cumulative(30e-6), 0.5, 1e-15)
            && NearAbs(rrsb.Cumulative(65e-6), 1.0 - std::exp(-1.0), 1e-15),
            "Q3 = 0,5 am Mittelwert bzw. Median, 63,2 % bei d' (RRSB)");
        const SSizeGrid fine = SSizeGrid::Linear(200001, 0.0, 150e-6);
        double worst = 0.0;
        for (const SSizeDistribution* d : { &normal, &logNormal, &rrsb })
            worst = std::max(worst, std::abs(TrapezoidOfDensity(*d, fine) - (d->Cumulative(150e-6) - d->Cumulative(0.0))));
        std::printf("      Q3 gegen Integral der Dichte bis 150 um (200001 Punkte): Abweichung hoechstens %.1e\n", worst);
        Check(worst < 1e-6, "Q3 stimmt mit dem Integral der Dichte ueberein");
    }

    // 4) Das Inlet meldet, wenn seine Verteilung aus dem Raster ragt.
    {
        CFlowsheetGraph graph;
        graph.AddInletNode("Inlet1", 0, 0); // Standard: Log-Normal d50 = 100 um, sigma_ln = 0,25
        const std::vector<std::string> inside = graph.CheckSizeGrid(SSizeGrid());
        const std::vector<std::string> below = graph.CheckSizeGrid(SSizeGrid::Logarithmic(300, 120e-6, 1e-3));
        const std::vector<std::string> above = graph.CheckSizeGrid(SSizeGrid::Logarithmic(300, 1e-6, 150e-6));
        for (const std::string& text : below)
            std::printf("      d_min = 120 um: %s\n", text.c_str());
        for (const std::string& text : above)
            std::printf("      d_max = 150 um: %s\n", text.c_str());
        Check(inside.empty(), "Standard-Inlet im Standardraster: keine Meldung (unter 1e-15 ausserhalb)");
        Check(below.size() == 1 && below[0].rfind("Inlet1: 77 %", 0) == 0 && below[0].find("unter d_min") != std::string::npos,
            "d_min = 120 um: Meldung '77 % unter d_min' (Q3(120 um) = Phi(ln 1,2 / 0,25))");
        Check(above.size() == 1 && above[0].rfind("Inlet1: 5.2 %", 0) == 0 && above[0].find("ueber d_max") != std::string::npos,
            "d_max = 150 um: Meldung '5,2 % ueber d_max' (1 - Phi(ln 1,5 / 0,25))");
    }

    // 5) Massenbilanz auf dem log. Raster: Inlet -> Verzoegerung (p = 1) ->
    //    Splitter 30/70 -> zwei Outlets. Zulauf steigt 0-1 s an, bleibt bis
    //    5 s und faellt bis 6 s auf 0: 50 kg mal Erfassungsanteil; alles ist
    //    nach 26 s durch. (Beginnt der Zulauf bei t = 0 nicht bei 0, zaehlt
    //    die Trapezregel den ersten Wert am Eingang nur halb.)
    {
        CFlowsheetGraph g;
        const std::string in = g.AddInletNode("Inlet1", 0, 0);
        const std::string pr = g.AddProcessNode("Prozess1", 0, 0);
        const std::string sp = g.AddSplitterNode("Splitter1", 2, 0, 0);
        const std::string oa = g.AddOutletNode("A", 0, 0);
        const std::string ob = g.AddOutletNode("B", 0, 0);
        g.GetInlet(in)->SetTimeProfile({ {0, 0}, {1, 10}, {5, 10}, {6, 0} });
        g.GetProcessor(pr)->SetParameter("delayRef", 2.0);
        g.GetProcessor(pr)->SetParameter("sizeExponent", 1.0);
        g.GetSplitter(sp)->SetFractions({ 0.3, 0.7 });
        g.AddConnection(in, 0, pr, 0);
        g.AddConnection(pr, 0, sp, 0);
        g.AddConnection(sp, 0, oa, 0);
        g.AddConnection(sp, 1, ob, 0);
        g.ConfigureGrid(300, 1e-6, 1e-3, 401, 40.0);
        g.RunSimulation();
        const double captured = g.GetInlet(in)->GetSizeDistribution().CapturedFraction(g.GetGrid().GetSizeGrid());
        const double mIn = g.GetDisplayResults(in, 0)->IntegrateTotal();
        const double mA = g.GetDisplayResults(oa, 0)->IntegrateTotal();
        const double mB = g.GetDisplayResults(ob, 0)->IntegrateTotal();
        std::printf("      rein %.9f kg (50 kg x %.9f) | A %.9f | B %.9f | A + B - rein = %.1e kg\n", mIn, captured, mA, mB, mA + mB - mIn);
        Check(NearRel(mIn, 50.0 * captured, 1e-12), "Inlet: 50 kg mal Erfassungsanteil des Rasters");
        Check(NearRel(mA + mB, mIn, 1e-12) && NearRel(mA, 0.3 * mIn, 1e-12), "Massenbilanz exakt: A + B = rein, A = 30 %");
    }

    // 6) Verzoegerung je Groesse auf dem log. Raster: Stuetzstelle d_i kommt
    //    um t_ref * d_i / d_ref spaeter an. Zulauf steigt in der ersten
    //    Sekunde linear auf 10 kg/s -> Ausgang = 10 * (t - Verzoegerung) * q(d_i).
    {
        CFlowsheetGraph g;
        const std::string in = g.AddInletNode("Inlet1", 0, 0);
        const std::string pr = g.AddProcessNode("Prozess1", 0, 0);
        const std::string out = g.AddOutletNode("Outlet1", 0, 0);
        g.GetInlet(in)->SetTimeProfile({ {0, 0}, {1, 10} });
        g.GetInlet(in)->SetSizeDistribution(SSizeDistribution{ EDistributionType::LogNormal, 30e-6, 1.0 });
        g.GetProcessor(pr)->SetParameter("delayRef", 2.0);
        g.GetProcessor(pr)->SetParameter("sizeExponent", 1.0);
        g.GetProcessor(pr)->SetParameter("referenceSize", 100e-6);
        g.AddConnection(in, pr);
        g.AddConnection(pr, out);
        g.ConfigureGrid(300, 1e-6, 1e-3, 401, 20.0); // dt = 0.05 s
        g.RunSimulation();
        const CSimulationResults& inlet = *g.GetDisplayResults(in, 0);
        const CSimulationResults& outlet = *g.GetDisplayResults(out, 0);
        const double dt = g.GetGrid().GetDt();
        double worst = 0.0;
        for (size_t s : { size_t(0), size_t(100), size_t(200), size_t(270) })
        {
            const double d = g.GetGrid().GetSizeAt(s);
            const double delay = 2.0 * d / 100e-6;
            const size_t k = static_cast<size_t>(std::ceil((delay + 0.5) / dt));
            const double q = inlet.GetValue(inlet.GetNumTimeSteps() - 1, s) / 10.0; // Dichte bei 1 kg/s
            const double expected = 10.0 * (static_cast<double>(k) * dt - delay) * q;
            const double deviation = std::abs(outlet.GetValue(k, s) / expected - 1.0);
            std::printf("      d = %8.3f um: Verzoegerung %6.3f s, t = %5.2f s, Ausgang/erwartet - 1 = %.1e\n",
                d * 1e6, delay, static_cast<double>(k) * dt, deviation);
            worst = std::max(worst, deviation);
        }
        Check(worst < 1e-9, "Verzoegerung je Stuetzstelle: t_ref * d / d_ref (1 um bis 500 um)");
    }

    return Summary();
}
