// Auswertung (kein automatischer Test): Vergleich einer Dosierwaage
// (Engisch & Muzzio 2012, Powder Technology 228, 395-403, Abb. 10a) mit
// StoSiS fuer Abbildung docs/abbildungen/Val_1_Dosierwaage_StoSiS.svg
// (Methoden-Dokument Kapitel 15). Gibt die Daten als CSV aus, wie in
// docs/abbildungen/Val_1_Daten.csv:
//     analysis_dosierwaage > Val_1_Daten.csv
// Mit Argument "zeitreihe" stattdessen den Massenstrom des gezeigten Laufs
// je 0,1-s-Schritt (Teil 2 und 3 der Abbildung):
//     analysis_dosierwaage zeitreihe > zeitreihe.csv
// Laufzeit in Release wenige Sekunden.
//
// Aufbau: Inlet 100 kg/h -> OU-Splitter (mu, sigma, tau) -> Outlet, dt = 0,1 s
// (Abtastung der Auffangwaage), 30 min. Auswertung wie im Paper: Massenstrom
// je 1-s-Intervall, Filter +-10 % um den Sollwert, dann iterativ +-3 sigma,
// bis sich nichts mehr aendert.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "FlowsheetGraph.h"
#include "SignalAnalysis.h"
using namespace stosis;

static const double kInletKgH = 100.0;
static const double kSetpoint = 70.0; // Sollwert im Paper [kg/h]
static const double kDt = 0.1;        // Abtastung der Auffangwaage [s]
static const int kStepsPerWindow = 10; // 1-s-Intervalle
static const double kTotal = 1800.0;  // 30 min
static const double kTauMain = 0.3;   // Korrelationszeit des gezeigten Laufs [s]
static const std::uint64_t kSeedMain = 2012;

// Abb. 10a, aus dem PDF-Bild digitalisiert (Achsen ueber die Teilstriche,
// Balkenhoehen per Pixelauswertung, Ablesefehler etwa 1 %): Dichte der
// 1-s-Werte [1/(kg/h)] je 0,1-kg/h-Klasse ab 67,9 kg/h.
static const double kBinStart = 67.9;
static const double kBinWidth = 0.1;
static const double kPaperDensity[] = {
    0.0060, 0.0209, 0.0149, 0.0716, 0.1075, 0.1373, 0.1910, 0.2687,
    0.4269, 0.4388, 0.5194, 0.4836, 0.5910, 0.6866, 0.7881, 0.7642,
    0.8388, 0.5493, 0.6358, 0.4179, 0.5612, 0.4060, 0.3104, 0.2388,
    0.1642, 0.1433, 0.1194, 0.1194, 0.0299, 0.0269, 0.0209, 0.0060 };
static const size_t kNumBins = sizeof(kPaperDensity) / sizeof(kPaperDensity[0]);

// Mittel und Streuung der 1-s-Werte aus dem digitalisierten Histogramm
// (Streuung mit Sheppard-Korrektur fuer die Klassenbreite).
static void PaperMoments(double& area, double& mean, double& stdDev)
{
    double m1 = 0.0, m2 = 0.0;
    area = 0.0;
    for (size_t i = 0; i < kNumBins; ++i)
    {
        const double c = kBinStart + (i + 0.5) * kBinWidth;
        const double w = kPaperDensity[i] * kBinWidth;
        area += w; m1 += w * c; m2 += w * c * c;
    }
    mean = m1 / area;
    stdDev = std::sqrt(m2 / area - mean * mean - kBinWidth * kBinWidth / 12.0);
}

// Streuung des Mittels aus n aufeinanderfolgenden Werten der exakten
// OU-Fortschreibung (a = exp(-dt/tau)) relativ zur Streuung der Einzelwerte.
static double WindowFactor(double tau, int n)
{
    const double a = tau > 0.0 ? std::exp(-kDt / tau) : 0.0;
    double s = n;
    for (int k = 1; k < n; ++k)
        s += 2.0 * (n - k) * std::pow(a, k);
    return std::sqrt(s) / n;
}

// Massenstrom am Outlet [kg/h] je 0,1-s-Schritt aus einem StoSiS-Lauf.
static std::vector<double> RunStoSiS(double mu, double sigma, double tau, std::uint64_t seed)
{
    CFlowsheetGraph g;
    g.SetRandomSeed(seed);
    auto in = g.AddInletNode("Dosierer", 0, 0);
    auto sp = g.AddSplitterNode("Schwankung", 2, 0, 0);
    auto out = g.AddOutletNode("Auffangwaage", 0, 0);
    auto rest = g.AddOutletNode("Rest", 0, 0);
    const double m = kInletKgH / 3600.0; // kg/s
    g.GetInlet(in)->SetTimeProfile({{0.0, m}, {kTotal, m}});
    g.GetSplitter(sp)->SetFractions({mu, 1.0 - mu}); g.GetSplitter(sp)->SetStochastic(true);
    g.GetSplitter(sp)->SetFluctuations({{sigma, tau}});
    g.AddConnection(in, 0, sp, 0); g.AddConnection(sp, 0, out, 0); g.AddConnection(sp, 1, rest, 0);
    const size_t steps = static_cast<size_t>(std::llround(kTotal / kDt)) + 1;
    g.ConfigureGrid(50, 1e-6, 1e-3, steps, kTotal);
    g.RunSimulations(1);
    const CSimulationResults* rOut = g.GetDisplayResults(out, 0);
    const CSimulationResults* rIn = g.GetOutputResults(in, 0);
    std::vector<double> flow(steps);
    for (size_t k = 0; k < steps; ++k) // Anteil am Zulauf, damit der Rasterfehler der Groessenintegration herausfaellt
        flow[k] = kInletKgH * rOut->IntegrateOverSize(k) / rIn->IntegrateOverSize(k);
    return flow;
}

struct SEval
{
    std::vector<double> values; // 1-s-Werte nach dem Filter
    double mean = 0, stdDev = 0, skew = 0, exKurt = 0, ac1 = 0;
    size_t removed = 0;
};

// Auswertung wie Engisch & Muzzio, Abschnitt 3.4 und 3.6 - derselbe Code wie
// im Fenster "Auswertung" der Oberflaeche (SignalAnalysis.h).
static SEval Evaluate(const std::vector<double>& flow)
{
    SEval e;
    e.values = FilterLikeEngischMuzzio(AverageOverWindows(flow, 0, static_cast<size_t>(kStepsPerWindow)),
        kSetpoint, 0.1, 3.0, e.removed);
    const SSignalStatistics s = ComputeSignalStatistics(e.values);
    e.mean = s.mean; e.stdDev = s.stdDev; e.skew = s.skewness; e.exKurt = s.excessKurtosis; e.ac1 = s.lag1Correlation;
    return e;
}

static std::vector<double> Histogram(const std::vector<double>& x)
{
    return MakeHistogram(x, kBinStart, kBinWidth, kNumBins).density;
}

int main(int argc, char** argv)
{
    double area, paperMean, paperStd;
    PaperMoments(area, paperMean, paperStd);
    // Kalibriert werden nur mu und sigma: Mittel und Streuung der 1-s-Werte wie im Paper.
    const double mu = 0.6944;
    const double sigma1s = 0.535 / kInletKgH;
    const double sigmaMain = sigma1s / WindowFactor(kTauMain, kStepsPerWindow);
    const std::vector<double> flow = RunStoSiS(mu, sigmaMain, kTauMain, kSeedMain);

    if (argc > 1 && std::strcmp(argv[1], "zeitreihe") == 0)
    {
        printf("t_s,massenstrom_kgh\n");
        for (size_t k = 0; k < flow.size(); ++k)
            printf("%.1f,%.5f\n", k * kDt, flow[k]);
        return 0;
    }

    const SEval shown = Evaluate(flow);
    const std::vector<double> hShown = Histogram(shown.values);
    std::vector<double> lo(kNumBins, 1e30), hi(kNumBins, 0.0);
    double minMean = 1e30, maxMean = 0, minStd = 1e30, maxStd = 0, sumStd = 0;
    const int runs = 20;
    for (int r = 0; r < runs; ++r)
    {
        const SEval e = Evaluate(RunStoSiS(mu, sigmaMain, kTauMain, 1000 + r));
        const std::vector<double> h = Histogram(e.values);
        for (size_t i = 0; i < kNumBins; ++i)
        {
            lo[i] = std::min(lo[i], h[i]);
            hi[i] = std::max(hi[i], h[i]);
        }
        minMean = std::min(minMean, e.mean); maxMean = std::max(maxMean, e.mean);
        minStd = std::min(minStd, e.stdDev); maxStd = std::max(maxStd, e.stdDev);
        sumStd += e.stdDev;
    }

    printf("# Vergleich Dosierwaage (Engisch & Muzzio 2012, Powder Technol. 228, Abb. 10a) mit StoSiS (tests/analysis_dosierwaage.cpp)\n");
    printf("# paper_dichte: aus dem PDF-Bild digitalisiert (Pixelauswertung, Ablesefehler etwa 1 %%); stosis_*: Dichte der 1-s-Werte je Klasse\n");
    printf("# StoSiS: Inlet 100 kg/h -> OU-Splitter (mu %.4f, sigma %.4f kg/h, tau %.1f s) -> Outlet, dt 0.1 s, 1800 s; Band = min/max aus %d Laeufen\n",
        mu, sigmaMain * kInletKgH, kTauMain, runs);
    printf("von_kgh,bis_kgh,paper_dichte,stosis_lauf,stosis_band_min,stosis_band_max\n");
    int inside = 0;
    for (size_t i = 0; i < kNumBins; ++i)
    {
        const double a = kBinStart + i * kBinWidth;
        printf("%.2f,%.2f,%.4f,%.4f,%.4f,%.4f\n", a, a + kBinWidth, kPaperDensity[i], hShown[i], lo[i], hi[i]);
        if (kPaperDensity[i] >= lo[i] - 1e-9 && kPaperDensity[i] <= hi[i] + 1e-9)
            ++inside;
    }
    printf("# Paper (digitalisiert): Flaeche %.3f, Mittel %.3f, Std %.4f (Sheppard-korrigiert), RSD %.3f %%\n",
        area, paperMean, paperStd, 100 * paperStd / paperMean);
    printf("# StoSiS gezeigter Lauf (Seed %llu): Mittel %.3f, Std %.4f, RSD %.3f %%, Schiefe %.3f, Exzess %.3f, n %zu, entfernt %zu\n",
        static_cast<unsigned long long>(kSeedMain), shown.mean, shown.stdDev, 100 * shown.stdDev / shown.mean, shown.skew, shown.exKurt,
        shown.values.size(), shown.removed);
    printf("# StoSiS %d Laeufe: Mittel %.3f-%.3f, Std %.4f-%.4f (im Mittel %.4f)\n", runs, minMean, maxMean, minStd, maxStd, sumStd / runs);
    printf("# Paper-Balken im Band: %d von %zu\n", inside, kNumBins);
    // Gleiches Histogramm fuer jedes tau (sigma jeweils kalibriert); unterscheiden
    // laesst sich tau nur ueber die Korrelation benachbarter 1-s-Werte.
    printf("# tau_s;sigma_je_schritt_kgh;faktor_1s;std_1s_kgh;korrelation_benachbarter_1s_werte\n");
    for (double tau : {0.0, 0.1, 0.3, 1.0, 3.0})
    {
        const double f = WindowFactor(tau, kStepsPerWindow);
        const SEval e = Evaluate(RunStoSiS(mu, sigma1s / f, tau, kSeedMain));
        printf("# %.1f;%.4f;%.4f;%.4f;%.3f\n", tau, sigma1s / f * kInletKgH, f, e.stdDev, e.ac1);
    }
    return 0;
}
