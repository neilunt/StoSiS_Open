// Stochastischer Splitter (Ornstein-Uhlenbeck-Prozess) und Zufallszahlen
// (Methoden-Dokument Kapitel 8 und 9.4). Lange Einzellaeufe mit festem
// Seed; Toleranzen etwa 3-5 Standardfehler bei T/(2 tau) unabhaengigen
// Abschnitten.
#include "FlowsheetGraph.h"
#include "TestSupport.h"

#include <algorithm>
#include <vector>

using namespace stosis;
using namespace stosis_test;

struct SSplitterRun
{
    std::vector<std::vector<double>> fractions; // je Ausgang die Anteile ueber die Zeit
    double maxMassError = 0.0;                  // groesste Abweichung der Anteilssumme von 1
};

// Laesst einen Splitter direkt laufen (ohne Graph), Zulauf 1 je Schritt.
static SSplitterRun RunSplitter(CSplitterUnit& splitter, double totalTime, size_t numSteps, std::uint64_t seed)
{
    SSimulationGrid grid;
    grid.numSizeClasses = 1;
    grid.numTimeSteps = numSteps;
    grid.totalTime = totalTime;
    splitter.Initialize(grid, seed);
    CStreamSlice in;
    in.Resize(1);
    in.SetValue(0, 0, 1.0);
    const std::vector<const CStreamSlice*> inputs = { &in };
    std::vector<CStreamSlice> outputs(splitter.GetNumOutputs());
    SSplitterRun run;
    run.fractions.resize(splitter.GetNumOutputs());
    for (size_t k = 0; k < numSteps; ++k)
    {
        for (CStreamSlice& o : outputs)
            o.Resize(1);
        SStepContext context;
        context.stepIndex = k;
        context.dt = grid.GetDt();
        context.time = k * context.dt;
        splitter.Step(context, inputs, outputs);
        double sum = 0.0;
        for (size_t i = 0; i < outputs.size(); ++i)
        {
            sum += outputs[i].GetValue(0);
            run.fractions[i].push_back(outputs[i].GetValue(0));
        }
        run.maxMassError = std::max(run.maxMassError, std::abs(sum - 1.0));
    }
    return run;
}

struct SStats
{
    double mean = 0.0, stdDev = 0.0, autocorrelation = 0.0;
};

static SStats Stats(const std::vector<double>& x, double dt, double lagTime)
{
    SStats s;
    for (double v : x)
        s.mean += v;
    s.mean /= x.size();
    double var = 0.0;
    for (double v : x)
        var += (v - s.mean) * (v - s.mean);
    var /= x.size();
    s.stdDev = std::sqrt(var);
    const size_t lag = static_cast<size_t>(std::llround(lagTime / dt));
    double cov = 0.0;
    for (size_t i = 0; i + lag < x.size(); ++i)
        cov += (x[i] - s.mean) * (x[i + lag] - s.mean);
    cov /= (x.size() - lag);
    s.autocorrelation = cov / var;
    return s;
}

static CSplitterUnit MakeSplitter(const std::vector<double>& fractions, const std::vector<SSplitterFluctuation>& fluctuations)
{
    CSplitterUnit splitter(fractions.size());
    splitter.SetFractions(fractions);
    splitter.SetStochastic(true);
    splitter.SetFluctuations(fluctuations);
    return splitter;
}

int main()
{
    // A: mu 0.3, sigma 0.05, tau 2 s, T = 20000 s -> 5000 unabhaengige Abschnitte:
    //    Standardfehler Mittel ~ 0.0007, Std ~ 1.4 %, Autokorrelation ~ 0.014.
    //    Erwartung unabhaengig von dt (exakte Fortschreibung).
    for (double dt : { 0.1, 0.5 })
    {
        CSplitterUnit splitter = MakeSplitter({ 0.3, 0.7 }, { {0.05, 2.0} });
        const SSplitterRun run = RunSplitter(splitter, 20000.0, static_cast<size_t>(20000.0 / dt) + 1, 42);
        const SStats s = Stats(run.fractions[0], dt, 2.0);
        const SStats rest = Stats(run.fractions[1], dt, 2.0);
        std::printf("      dt = %.1f s: Mittel %.4f  Std %.4f  Autokorr(2 s) %.3f | Rest Mittel %.4f\n",
            dt, s.mean, s.stdDev, s.autocorrelation, rest.mean);
        const std::string tag = "dt = " + std::to_string(dt).substr(0, 3) + " s: ";
        Check(NearAbs(s.mean, 0.3, 0.004) && NearRel(s.stdDev, 0.05, 0.07) && NearAbs(s.autocorrelation, std::exp(-1.0), 0.05),
            tag + "Mittel 0.3, Std 0.05, Autokorrelation(tau) = e^-1");
        Check(NearAbs(rest.mean, 0.7, 0.004), tag + "Rest-Ausgang Mittel 0.7");
        Check(run.maxMassError <= 1e-15, tag + "Masse in jedem Schritt erhalten");
    }

    // B: tau = 0 -> jeder Schritt unabhaengig, Autokorrelation ~ 0 (Std.-Fehler 0.007).
    {
        CSplitterUnit splitter = MakeSplitter({ 0.3, 0.7 }, { {0.05, 0.0} });
        const SSplitterRun run = RunSplitter(splitter, 2000.0, 20001, 7);
        const SStats s = Stats(run.fractions[0], 0.1, 0.1);
        std::printf("      tau = 0: Autokorr(1 Schritt) %.4f\n", s.autocorrelation);
        Check(NearAbs(s.autocorrelation, 0.0, 0.03), "tau = 0: aufeinanderfolgende Schritte unkorreliert");
    }

    // C: 3 Ausgaenge, sigma 0.03/0.06, tau 1/5 s: Rest-Std = sqrt(0.03^2 + 0.06^2) = 0.067.
    {
        CSplitterUnit splitter = MakeSplitter({ 0.2, 0.3, 0.5 }, { {0.03, 1.0}, {0.06, 5.0} });
        const SSplitterRun run = RunSplitter(splitter, 20000.0, 200001, 3);
        const SStats rest = Stats(run.fractions[2], 0.1, 1.0);
        const double expected = std::sqrt(0.03 * 0.03 + 0.06 * 0.06);
        std::printf("      3 Ausgaenge: Rest Mittel %.4f  Std %.4f (erwartet %.4f)\n", rest.mean, rest.stdDev, expected);
        Check(NearAbs(rest.mean, 0.5, 0.01) && NearRel(rest.stdDev, expected, 0.07), "3 Ausgaenge: Rest Mittel 0.5, Std 0.067");
        Check(run.maxMassError <= 1e-15, "3 Ausgaenge: Masse in jedem Schritt erhalten");
    }

    // D: Reproduzierbarkeit.
    {
        auto run = [](std::uint64_t seed)
        {
            CSplitterUnit splitter = MakeSplitter({ 0.3, 0.7 }, { {0.05, 2.0} });
            return RunSplitter(splitter, 100.0, 1001, seed).fractions[0];
        };
        const std::vector<double> a = run(5), b = run(5), c = run(6);
        Check(a == b, "Gleicher Seed: bitgleiche Anteile");
        Check(a != c, "Anderer Seed: andere Anteile");
    }

    // E: Hinweis auf Abschneiden bei [0, 1].
    Check(!CSplitterUnit::IsClippingLikely({ 0.3, 0.7 }, { {0.05, 1.0} }), "mu 0.3 / sigma 0.05: kein Hinweis");
    Check(CSplitterUnit::IsClippingLikely({ 0.1, 0.9 }, { {0.05, 1.0} }), "mu 0.1 / sigma 0.05: Hinweis (mu - 3 sigma < 0)");

    // F: Seed je Unit haengt nur vom globalen Seed und der Knoten-ID ab.
    {
        CFlowsheetGraph g;
        g.SetRandomSeed(1);
        const std::string a = g.AddSplitterNode("A", 2, 0, 0);
        Check(g.GetUnitSeed(a) == MakeUnitSeed(1, a), "Unit-Seed = MakeUnitSeed(Seed, Knoten-ID)");
        Check(MakeUnitSeed(1, "node_2") != MakeUnitSeed(2, "node_1"), "Hash statt Addition: Seed 1/Knoten 2 != Seed 2/Knoten 1");
    }

    return Summary();
}
