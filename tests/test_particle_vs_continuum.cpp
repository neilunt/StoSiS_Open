// Continuum against particle mode at all regression flowsheets (methods
// handbook 9.7): the same flowsheets as test_regression (RegressionFlowsheets.h),
// once on the size grid and with N = 500, 2000 and 8000 cards (MC particles)
// per slice. Compared for every results entry:
//  - mass flow per step (integral over the size): largest deviation,
//    relative to the mean mass flow of the entry,
//  - total mass (integral over time and size), relative,
//  - size distribution of the throughput: largest distance of the
//    cumulative distributions Q3,
//  - stochastic flowsheets: with the same seed the fluctuation is the same,
//    so run 1 is compared directly; at N = 2000 also the mean over all runs
//    and the mean spread of the mass flow per step.
// The deviation of the mass flow must fall with N at least like 1/sqrt(N).
// The validation flowsheets are compared in test_levenspiel.
//
// Tolerances: the deviation per step is noise of the cards. In a recycle
// branch only a part of the cards flows (30-40 %), so at N = 2000 it is
// still 2-4 % there and falls cleanly with N. Checked are therefore
//  - mass flow per step at N = 8000 to 2 % of the mean, falling with N,
//  - total mass at N = 8000 to 1e-4,
//  - Q3 of the throughput at N = 2000 (default) to 1 percentage point,
//  - stochastic flowsheets at N = 2000: mean over the runs to 1 %, spread
//    per step to 10 % as in the continuum.
//
// Two differences that are not errors of the cards:
//  - The continuum drops the part of an inlet distribution outside the size
//    grid, the cards keep it at the boundary node. The default inlet
//    (log-normal, d50 = 100 um, sigma_ln = 0.25) has 0.28 % above 200 um,
//    so linear grids reach to 300 um here (in test_regression to 200 um).
//  - At the end of the simulation about one card more or less is still on
//    its way (delay, recycle); the total mass is compared to 1e-4.
#include "FlowsheetGraph.h"
#include "TestSupport.h"
#include "RegressionFlowsheets.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace stosis;
using namespace stosis_test;

namespace
{
    struct SDeviation
    {
        double massFlow = 0.0; // largest deviation per step / mean mass flow
        double total = 0.0;    // total mass, relative
        double q3 = 0.0;       // largest distance of the Q3 curves
        void Max(const SDeviation& other)
        {
            massFlow = std::max(massFlow, other.massFlow);
            total = std::max(total, other.total);
            q3 = std::max(q3, other.q3);
        }
    };

    // Q3 of the throughput: cumulative mass over the size (trapezoid weights of the grid).
    std::vector<double> CumulativeThroughput(const CSimulationResults& results)
    {
        const std::vector<double> weights = results.GetSizeGrid().GetWeights();
        std::vector<double> q(results.GetNumSizeClasses(), 0.0);
        double sum = 0.0;
        for (size_t s = 0; s < q.size() && s < weights.size(); ++s)
        {
            sum += weights[s] * results.IntegrateOverTime(s);
            q[s] = sum;
        }
        if (sum > 0.0)
            for (double& value : q)
                value /= sum;
        return q;
    }

    SDeviation Compare(const CSimulationResults& continuum, const CSimulationResults& cards)
    {
        SDeviation d;
        const size_t numT = std::min(continuum.GetNumTimeSteps(), cards.GetNumTimeSteps());
        double mean = 0.0;
        for (size_t k = 0; k < numT; ++k)
            mean += continuum.IntegrateOverSize(k);
        mean /= static_cast<double>(std::max<size_t>(numT, 1));
        if (!(mean > 1e-12))
            return d; // nothing flows through this entry
        for (size_t k = 0; k < numT; ++k)
            d.massFlow = std::max(d.massFlow, std::abs(cards.IntegrateOverSize(k) - continuum.IntegrateOverSize(k)) / mean);
        const double total = continuum.IntegrateTotal();
        d.total = std::abs(cards.IntegrateTotal() - total) / total;
        const std::vector<double> a = CumulativeThroughput(continuum);
        const std::vector<double> b = CumulativeThroughput(cards);
        for (size_t s = 0; s < a.size() && s < b.size(); ++s)
            d.q3 = std::max(d.q3, std::abs(a[s] - b[s]));
        return d;
    }

    // Mean spread (standard deviation over the runs) of the mass flow per step.
    double MeanSpread(const CEnsembleStatistics& stats)
    {
        const std::vector<std::vector<double>>& runs = stats.GetRunTimeSeries();
        if (runs.size() < 2 || runs.front().empty())
            return 0.0;
        double sum = 0.0;
        for (size_t k = 0; k < runs.front().size(); ++k)
        {
            double mean = 0.0, spread = 0.0;
            CEnsembleStatistics::MeanAndStdDev(CEnsembleStatistics::Column(runs, k), mean, spread);
            sum += spread;
        }
        return sum / static_cast<double>(runs.front().size());
    }

    void Simulate(CFlowsheetGraph& graph, const SRegressionCase& c, size_t cards, size_t runs)
    {
        c.build(graph);
        // Linear grids up to 300 um, so that the default inlet lies inside;
        // with the same spacing of the nodes (more classes), so that the
        // continuum is not coarser than in test_regression.
        const SSimulationGrid grid = graph.GetGrid();
        if (grid.sizeGridType == ESizeGridType::Linear && grid.sizeMax < 300e-6 && grid.numSizeClasses > 1)
        {
            const double step = (grid.sizeMax - grid.sizeMin) / static_cast<double>(grid.numSizeClasses - 1);
            const size_t classes = static_cast<size_t>(std::lround((300e-6 - grid.sizeMin) / step)) + 1;
            graph.ConfigureGrid(classes, grid.sizeMin, grid.sizeMin + step * static_cast<double>(classes - 1),
                grid.numTimeSteps, grid.totalTime, ESizeGridType::Linear);
        }
        graph.SetRecordCardHistory(false);
        if (cards > 0)
        {
            graph.SetRepresentation(ERepresentation::Particles);
            SCardSettings settings = graph.GetCardSettings();
            settings.cardsPerSlice = cards;
            graph.SetCardSettings(settings);
        }
        graph.RunSimulations(runs);
    }

    // Rate of the deviation over N: exponent p in deviation ~ N^p.
    double Rate(double atLow, double atHigh, double ratio)
    {
        return (atLow > 0.0 && atHigh > 0.0) ? std::log(atHigh / atLow) / std::log(ratio) : 0.0;
    }
}

int main()
{
    const size_t counts[3] = { 500, 2000, 8000 };
    const size_t standard = 1; // N = 2000, default of the program
    const double floor = 1e-9; // below: equal up to rounding, no rate needed

    std::printf("      %-34s %6s %12s %12s %12s\n", "Flowsheet (Lauf 1)", "N", "Massenstrom", "Gesamtmasse", "Q3");
    for (const SRegressionCase& c : RegressionCases())
    {
        CFlowsheetGraph continuum;
        Simulate(continuum, c, 0, c.runs);
        const bool stochastic = continuum.IsStochastic();
        const std::vector<SResultsEntry> entries = continuum.GetResultsEntries();

        SDeviation worst[3];
        SDeviation meanWorst;
        std::vector<std::pair<std::string, SDeviation>> outside; // entries beyond the tolerances (N = 8000 / Q3 at 2000)
        double spreadLow = 1.0, spreadHigh = 1.0;
        bool complete = true;
        for (size_t i = 0; i < 3; ++i)
        {
            CFlowsheetGraph graph;
            const size_t runs = (i == standard && stochastic) ? c.runs : 1;
            Simulate(graph, c, counts[i], runs);
            for (const SResultsEntry& entry : entries)
            {
                const CEnsembleStatistics* a = continuum.GetStatistics(entry.nodeId, entry.port);
                const CEnsembleStatistics* b = graph.GetStatistics(entry.nodeId, entry.port);
                if (!a || !b || a->IsEmpty() || b->IsEmpty())
                {
                    complete = false;
                    continue;
                }
                const SDeviation d = Compare(a->GetFirstRun(), b->GetFirstRun());
                worst[i].Max(d);
                if ((i == 2 && (d.massFlow > 0.02 || d.total > 1e-4)) || (i == standard && d.q3 > 0.01))
                    outside.push_back({ entry.label + (i == 2 ? " (N = 8000)" : " (N = 2000)"), d });
                if (i == standard && stochastic && runs > 1)
                {
                    meanWorst.Max(Compare(a->GetMean(), b->GetMean()));
                    const double sa = MeanSpread(*a), sb = MeanSpread(*b);
                    // Spread from rounding only (e.g. an inlet, 1e-15): not a fluctuation.
                    const std::vector<std::vector<double>>& series = a->GetRunTimeSeries();
                    double level = 0.0;
                    for (const std::vector<double>& run : series)
                        for (double v : run)
                            level = std::max(level, std::abs(v));
                    if (sa > 1e-9 * level)
                    {
                        spreadLow = std::min(spreadLow, sb / sa);
                        spreadHigh = std::max(spreadHigh, sb / sa);
                        if (sb / sa < 0.9 || sb / sa > 1.1)
                            std::printf("        Streuung je Schritt: %-40s Kontinuum %.3e, Karten %.3e\n",
                                entry.label.c_str(), sa, sb);
                    }
                }
            }
            std::printf("      %-34s %6zu %12.2e %12.2e %12.2e\n", i == 0 ? c.name.c_str() : "", counts[i],
                worst[i].massFlow, worst[i].total, worst[i].q3);
        }
        const double rateFlow = Rate(worst[0].massFlow, worst[2].massFlow, 16.0);
        const double rateQ3 = Rate(worst[0].q3, worst[2].q3, 16.0);
        std::printf("      %-34s %6s Rate N^%.2f %12s Rate N^%.2f\n", "", "", rateFlow, "", rateQ3);
        if (stochastic)
            std::printf("      %-34s %6s Mittel ueber %zu Laeufe: Massenstrom %.2e, Q3 %.2e; Streuung je Schritt %.3f bis %.3f x Kontinuum\n",
                "", "", c.runs, meanWorst.massFlow, meanWorst.q3, spreadLow, spreadHigh);
        for (const auto& [label, d] : outside)
            std::printf("        ausserhalb: %-52s Massenstrom %.2e, Gesamtmasse %.2e, Q3 %.2e\n",
                label.c_str(), d.massFlow, d.total, d.q3);

        Check(complete, c.name + ": alle Eintraege in beiden Darstellungen");
        Check(worst[2].massFlow <= 0.02, c.name + ": Massenstrom je Schritt bei N = 8000 auf 2 % des Mittels");
        Check(worst[2].total <= 1e-4, c.name + ": Gesamtmasse bei N = 8000 auf 1e-4");
        Check(worst[standard].q3 <= 0.01, c.name + ": Groessenverteilung des Durchsatzes (Q3) bei N = 2000 auf 1 %-Punkt");
        Check(worst[2].massFlow <= floor || rateFlow <= -0.4,
            c.name + ": Abweichung des Massenstroms faellt mit N mindestens wie 1/sqrt(N)");
        if (stochastic)
            Check(meanWorst.massFlow <= 0.01 && meanWorst.q3 <= 0.01 && spreadLow >= 0.9 && spreadHigh <= 1.1,
                c.name + ": Mittel ueber die Laeufe auf 1 %, Streuung je Schritt auf 10 % wie im Kontinuum");
    }
    return Summary();
}
