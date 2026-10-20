// Auswertung (kein automatischer Test): Konvergenz der Monte-Carlo-
// Schaetzung fuer Abbildung docs/abbildungen/Konvergenz_MonteCarlo.svg
// (Methoden-Dokument Kapitel 11.1). Gibt die Daten als CSV aus, wie in
// docs/abbildungen/Konvergenz_MonteCarlo_Daten.csv:
//     analysis_convergence > Konvergenz_MonteCarlo_Daten.csv
// Laufzeit in Release etwa eine Minute.
#include <cstdio>
#include <cmath>
#include <vector>
#include "FlowsheetGraph.h"
using namespace stosis;
// Konvergenz der Monte-Carlo-Schaetzung: relativer RMS-Fehler von
// Mittelwert und Standardabweichung der Masse an Ausgang A ueber N,
// gemittelt ueber R unabhaengige Wiederholungen (verschiedene Seeds).
int main()
{
    const double m = 10, mu = 0.3, sigma = 0.05, tau = 2.0, T = 40.0;
    const double expMean = mu * m * T;
    const double expStd = std::sqrt(2 * m * m * sigma * sigma * tau * (T - tau * (1 - std::exp(-T / tau))));
    CFlowsheetGraph g;
    auto in = g.AddInletNode("Inlet1", 0, 0);
    auto sp = g.AddSplitterNode("Splitter1", 2, 0, 0);
    auto a = g.AddOutletNode("A", 0, 0);
    auto b = g.AddOutletNode("B", 0, 0);
    g.GetInlet(in)->SetTimeProfile({{0, m}});
    g.GetSplitter(sp)->SetFractions({mu, 1 - mu}); g.GetSplitter(sp)->SetStochastic(true);
    g.GetSplitter(sp)->SetFluctuations({{sigma, tau}});
    g.AddConnection(in, 0, sp, 0); g.AddConnection(sp, 0, a, 0); g.AddConnection(sp, 1, b, 0);
    g.ConfigureGrid(20, 0.0, 200e-6, 401, T, ESizeGridType::Linear);
    const int R = 40;
    printf("N;rmsRelErrMean;rmsRelErrStd;theoryMean;theoryStd;exampleMean;exampleStd\n");
    for (size_t N : {10, 20, 50, 100, 200, 500, 1000, 2000})
    {
        double sumSqMean = 0, sumSqStd = 0, exMean = 0, exStd = 0;
        for (int r = 0; r < R; ++r)
        {
            g.SetRandomSeed(1000003ull * (r + 1) + N);
            g.RunSimulations(N);
            double mean, sd;
            CEnsembleStatistics::MeanAndStdDev(g.GetStatistics(a, 0)->GetRunTotals(), mean, sd);
            const double em = (mean - expMean) / expMean, es = (sd - expStd) / expStd;
            sumSqMean += em * em; sumSqStd += es * es;
            if (r == 0) { exMean = mean; exStd = sd; }
        }
        printf("%zu;%.6f;%.6f;%.6f;%.6f;%.4f;%.4f\n", N, std::sqrt(sumSqMean / R), std::sqrt(sumSqStd / R),
            (expStd / expMean) / std::sqrt((double)N), 1.0 / std::sqrt(2.0 * (N - 1)), exMean, exStd);
        fflush(stdout);
    }
    printf("# erwartet: Mittel %.4f, Std %.4f\n", expMean, expStd);
}
