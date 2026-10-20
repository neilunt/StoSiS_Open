// Mehrere Laeufe und Statistik, Validierung gegen die analytische Loesung
// (Methoden-Dokument Kapitel 10 und 11).
//
// Inlet (konstant m) -> Splitter (Ausgang 1: mu, sigma, tau) -> A, Rest -> B.
// Masse an A ueber [0, T]:
//   E[M]   = mu m T
//   Var[M] = 2 m^2 sigma^2 tau (T - tau (1 - e^(-T/tau)))
#include "FlowsheetGraph.h"
#include "TestSupport.h"

#include <algorithm>

using namespace stosis;
using namespace stosis_test;

static const double m = 10.0, mu = 0.3, sigma = 0.05, tau = 2.0, T = 40.0;

struct SSetup
{
    CFlowsheetGraph graph;
    std::string in, splitter, a, b;
    SSetup()
    {
        in = graph.AddInletNode("Inlet1", 0, 0);
        splitter = graph.AddSplitterNode("Splitter1", 2, 0, 0);
        a = graph.AddOutletNode("A", 0, 0);
        b = graph.AddOutletNode("B", 0, 0);
        graph.GetInlet(in)->SetTimeProfile({ {0, m} });
        graph.GetSplitter(splitter)->SetFractions({ mu, 1 - mu });
        graph.GetSplitter(splitter)->SetStochastic(true);
        graph.GetSplitter(splitter)->SetFluctuations({ {sigma, tau} });
        graph.AddConnection(in, 0, splitter, 0);
        graph.AddConnection(splitter, 0, a, 0);
        graph.AddConnection(splitter, 1, b, 0);
        graph.ConfigureGrid(20, 0.0, 200e-6, 401, T, ESizeGridType::Linear);
    }
};

int main()
{
    const double expMean = mu * m * T;
    const double expStd = std::sqrt(2 * m * m * sigma * sigma * tau * (T - tau * (1 - std::exp(-T / tau))));

    SSetup s;
    size_t callbacks = 0;
    const size_t N = 2000;
    const size_t runs = s.graph.RunSimulations(N, [&](size_t, size_t) { ++callbacks; return true; });
    Check(runs == N && callbacks == N, "2000 Laeufe, Fortschritt nach jedem Lauf gemeldet");

    const CEnsembleStatistics* statsA = s.graph.GetStatistics(s.a, 0);
    double meanA = 0.0, stdA = 0.0, meanB = 0.0, stdB = 0.0;
    CEnsembleStatistics::MeanAndStdDev(statsA->GetRunTotals(), meanA, stdA);
    CEnsembleStatistics::MeanAndStdDev(s.graph.GetStatistics(s.b, 0)->GetRunTotals(), meanB, stdB);
    std::printf("      Masse A: Mittel %.3f (erwartet %.3f) | Std %.3f (erwartet %.3f)\n", meanA, expMean, stdA, expStd);

    // Standardfehler: Mittel sigma_M/sqrt(N) = 0.14 kg; Std relativ 1/sqrt(2(N-1)) = 1.6 %.
    // Toleranz 5 Standardfehler. Das Raster 0-200 erfasst die Startverteilung
    // (Normal 100/25) nicht ganz: Bezug ist deshalb der tatsaechliche Zulauf.
    const double feedTotal = s.graph.GetDisplayResults(s.in, 0)->IntegrateTotal();
    const double scale = feedTotal / (m * T);
    std::printf("      erfasster Zulauf %.4f kg (Faktor %.6f)\n", feedTotal, scale);
    Check(NearAbs(meanA, expMean * scale, 5 * expStd / std::sqrt(double(N))), "Mittelwert der Masse = mu m T (5 Standardfehler)");
    Check(NearRel(stdA, expStd * scale, 5.0 / std::sqrt(2.0 * (N - 1))), "Standardabweichung = analytische Loesung (5 Standardfehler)");
    Check(NearRel(stdB, stdA, 1e-9), "Std an B = Std an A (B = Zulauf - A)");
    Check(NearAbs(meanA + meanB, feedTotal, 1e-9 * feedTotal), "Mittel A + Mittel B = Zulauf");

    // Std des Massenstroms an A zu einem Zeitpunkt (t = 20 s) = m sigma.
    double flowMean = 0.0, flowStd = 0.0;
    CEnsembleStatistics::MeanAndStdDev(CEnsembleStatistics::Column(statsA->GetRunTimeSeries(), 200), flowMean, flowStd);
    std::printf("      Std Massenstrom A bei t = 20 s: %.4f (erwartet %.4f)\n", flowStd, m * sigma * scale);
    Check(NearRel(flowStd, m * sigma * scale, 5.0 / std::sqrt(2.0 * (N - 1))), "Std des Massenstroms = m sigma");

    // 5-95-%-Perzentil gegen Normalverteilung mu +- 1.645 sigma_M.
    const double q05 = CEnsembleStatistics::Quantile(statsA->GetRunTotals(), 0.05);
    const double q95 = CEnsembleStatistics::Quantile(statsA->GetRunTotals(), 0.95);
    std::printf("      5-95 %%: %.2f .. %.2f (Normalverteilung %.2f .. %.2f)\n", q05, q95,
        (expMean - 1.645 * expStd) * scale, (expMean + 1.645 * expStd) * scale);
    Check(NearAbs(q05, (expMean - 1.645 * expStd) * scale, 1.0) && NearAbs(q95, (expMean + 1.645 * expStd) * scale, 1.0),
        "5-95-%-Perzentil wie Normalverteilung (+- 1 kg)");

    // Lauf 1 der Statistik = Einzellauf mit genau dem eingestellten Seed.
    {
        SSetup single;
        single.graph.RunSimulation();
        Check(single.graph.GetDisplayResults(single.a, 0)->IntegrateTotal() == statsA->GetFirstRun().IntegrateTotal(),
            "Lauf 1 der Statistik = Einzellauf mit dem Seed (bitgleich)");
    }

    // Deterministisch: nur 1 Lauf, Standardabweichung 0.
    s.graph.GetSplitter(s.splitter)->SetStochastic(false);
    const size_t detRuns = s.graph.RunSimulations(20);
    double maxStd = 0.0;
    const CSimulationResults& sd = s.graph.GetStatistics(s.a, 0)->GetStdDev();
    for (size_t t = 0; t < sd.GetNumTimeSteps(); ++t)
        for (size_t c = 0; c < sd.GetNumSizeClasses(); ++c)
            maxStd = std::max(maxStd, sd.GetValue(t, c));
    Check(detRuns == 1 && maxStd == 0.0, "Deterministisches Flowsheet: 1 Lauf, Std 0");

    // Abbruch ueber den Fortschritts-Callback.
    s.graph.GetSplitter(s.splitter)->SetStochastic(true);
    const size_t cancelled = s.graph.RunSimulations(100, [](size_t done, size_t) { return done < 7; });
    Check(cancelled == 7 && s.graph.GetStatistics(s.a, 0)->GetNumRuns() == 7, "Abbruch nach 7 Laeufen: Statistik aus 7 Laeufen");

    return Summary();
}
