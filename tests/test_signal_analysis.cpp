// Auswertung schwankender Zeitreihen (SignalAnalysis.h, Fenster
// "Auswertung" der Oberflaeche; Methoden-Dokument Kapitel 12 und 15):
// Fenstermittel, Filter, Kennzahlen, Histogramm, Normalquantil und der
// Dosierwaagen-Vergleich ueber denselben Weg wie die Oberflaeche.
#include "FlowsheetGraph.h"
#include "SignalAnalysis.h"
#include "TestSupport.h"

#include <cmath>
#include <vector>

using namespace stosis;
using namespace stosis_test;

int main()
{
    // A: Fenstermittel. 21 Zeitpunkte (0 ... 2 s, dt 0,1 s), Fenster 1 s =
    //    10 Schritte -> 2 Fenster; der letzte Zeitpunkt zaehlt nicht.
    {
        std::vector<double> series;
        for (int k = 0; k <= 20; ++k)
            series.push_back(k);
        Check(StepsPerWindow(0.1, 1.0) == 10, "1 s bei dt 0,1 s = 10 Schritte");
        Check(StepsPerWindow(0.1, 0.05) == 1, "Fenster kuerzer als dt: jeder Schritt einzeln");
        const std::vector<double> means = AverageOverWindows(series, 0, 10);
        Check(means.size() == 2 && NearAbs(means[0], 4.5, 1e-12) && NearAbs(means[1], 14.5, 1e-12),
            "Fenstermittel 4,5 und 14,5");
        const std::vector<double> late = AverageOverWindows(series, 5, 10);
        Check(late.size() == 1 && NearAbs(late[0], 9.5, 1e-12), "Start bei Schritt 5: ein Fenster, Mittel 9,5");
    }

    // B: Kennzahlen bekannter Werte.
    {
        const SSignalStatistics s = ComputeSignalStatistics({ 1, 2, 3, 4, 5 });
        Check(NearAbs(s.mean, 3.0, 1e-12) && NearAbs(s.stdDev, std::sqrt(2.5), 1e-12)
            && NearAbs(s.rsd, std::sqrt(2.5) / 3.0, 1e-12) && NearAbs(s.skewness, 0.0, 1e-12),
            "1..5: Mittel 3, Std sqrt(2,5), Schiefe 0");
        Check(NearAbs(ComputeSignalStatistics({ 1, -1, 1, -1, 1, -1 }).lag1Correlation, -1.0, 1e-12),
            "Wechselfolge: Korrelation benachbarter Werte -1");
    }

    // C: Filter. 1000 normalverteilte Werte um 70 plus zwei grobe Ausreisser.
    {
        CRandom random(11);
        std::vector<double> v;
        for (int i = 0; i < 1000; ++i)
            v.push_back(70.0 + 0.5 * random.Normal());
        v.push_back(90.0); // ausserhalb +-10 %
        v.push_back(73.5); // innerhalb +-10 %, aber 7 sigma
        size_t removed = 0;
        const std::vector<double> kept = FilterLikeEngischMuzzio(v, 70.0, 0.1, 3.0, removed);
        Check(removed == v.size() - kept.size() && removed >= 2 && removed <= 10,
            "Filter: beide Ausreisser und nur wenige weitere Werte entfernt");
        bool outliersGone = true;
        for (double x : kept)
            outliersGone = outliersGone && x < 73.0;
        Check(outliersGone, "Filter: kein Wert ueber 73 bleibt");
    }

    // D: Histogramm hat Flaeche 1; Klassenbreite glatt.
    {
        const SHistogram h = MakeHistogram({ 0.05, 0.15, 0.15, 0.25 }, 0.0, 0.1, 3);
        Check(NearAbs(h.density[0], 2.5, 1e-12) && NearAbs(h.density[1], 5.0, 1e-12) && NearAbs(h.density[2], 2.5, 1e-12),
            "Histogramm: Dichte 2,5 / 5 / 2,5");
        double start = 0.0, width = 0.0;
        size_t numBins = 0;
        ChooseHistogramBins(67.94, 71.13, 1800, start, width, numBins);
        Check(NearAbs(width, 0.2, 1e-12) && NearAbs(start, 67.8, 1e-9) && start + numBins * width > 71.13,
            "Klassen fuer 1800 Werte in 67,94 ... 71,13: Breite 0,2 ab 67,8");
    }

    // E: Normalquantil.
    {
        Check(NearAbs(NormalQuantile(0.975), 1.959963985, 1e-8) && NearAbs(NormalQuantile(0.5), 0.0, 1e-12)
            && NearAbs(NormalQuantile(0.001), -3.090232306, 1e-8), "Normalquantil 0,975 / 0,5 / 0,001");
    }

    // F: Dosierwaage (Methoden Kap. 15, Abb. Val_1) ueber denselben Weg wie die
    //    Oberflaeche: Laufstatistik des Outlets, 1-s-Fenster, Filter. Lauf 1
    //    nutzt genau den Seed 2012 und ist damit der gezeigte Lauf aus
    //    tests/analysis_dosierwaage.cpp (69,423 / 0,5342 / 1798 Werte).
    {
        CFlowsheetGraph g;
        g.SetRandomSeed(2012);
        const std::string in = g.AddInletNode("Dosierer", 0, 0);
        const std::string sp = g.AddSplitterNode("Schwankung", 2, 0, 0);
        const std::string out = g.AddOutletNode("Auffangwaage", 0, 0);
        const std::string rest = g.AddOutletNode("Rest", 0, 0);
        const double m = 100.0 / 3600.0;
        g.GetInlet(in)->SetTimeProfile({ {0.0, m}, {1800.0, m} });
        g.GetSplitter(sp)->SetFractions({ 0.6944, 1.0 - 0.6944 });
        g.GetSplitter(sp)->SetStochastic(true);
        g.GetSplitter(sp)->SetFluctuations({ {0.535 / 100.0 / 0.658484, 0.3} });
        g.AddConnection(in, 0, sp, 0);
        g.AddConnection(sp, 0, out, 0);
        g.AddConnection(sp, 1, rest, 0);
        g.ConfigureGrid(50, 1e-6, 1e-3, 18001, 1800.0);
        g.RunSimulations(2);
        const CStreamStatistics* stats = g.GetStreamStatistics(out, 0);
        Check(stats && stats->GetTotal().GetRunTimeSeries().size() == 2, "Laufstatistik des Outlets mit 2 Laeufen");
        if (stats)
        {
            std::vector<double> kgh = stats->GetTotal().GetRunTimeSeries()[0];
            for (double& v : kgh)
                v *= 3600.0;
            const size_t n = StepsPerWindow(g.GetGrid().GetDt(), 1.0);
            size_t removed = 0;
            const std::vector<double> kept = FilterLikeEngischMuzzio(AverageOverWindows(kgh, 0, n), 70.0, 0.1, 3.0, removed);
            const SSignalStatistics s = ComputeSignalStatistics(kept);
            std::printf("      Oberflaechen-Weg: n %zu, entfernt %zu, Mittel %.4f, Std %.4f, RSD %.3f %%\n",
                kept.size(), removed, s.mean, s.stdDev, 100.0 * s.rsd);
            Check(n == 10 && kept.size() == 1798 && removed == 2, "Dosierwaage: 1800 Fenster, 2 gefiltert");
            Check(NearAbs(s.mean, 69.423, 0.01) && NearAbs(s.stdDev, 0.5342, 0.0005),
                "Dosierwaage: Mittel 69,42 und Std 0,534 wie Abb. Val_1");
        }
    }

    return Summary();
}
