#pragma once
#include <vector>
#include <cmath>
#include <algorithm>
#include "SimulationResults.h"
#include "FlowsheetUnit.h"

namespace stosis
{
    // Statistik EINES Ausgangs (oder Outlets) ueber mehrere Simulations-
    // laeufe mit verschiedenen Seeds.
    //
    // Speichersparend: Die vollen Zeit-mal-Groesse Matrizen werden nicht
    // fuer jeden Lauf aufgehoben, sondern nur
    //  - Lauf 1 komplett (Einzellauf),
    //  - Mittelwert und Standardabweichung je Zelle, laufend mitgerechnet
    //    (Welford-Verfahren: numerisch stabil, ein Durchgang),
    // und fuer JEDEN Lauf nur die kleinen Kennwerte:
    //  - Massenstrom ueber die Zeit (Integral ueber die Groesse),
    //  - Groessenverteilung des Durchsatzes (Integral ueber die Zeit),
    //  - Gesamtmasse.
    // Daraus lassen sich Streubaender, Perzentile und Einzellauf-Kurven
    // exakt berechnen.
    class CEnsembleStatistics
    {
    public:
        void Reset(const SSimulationGrid& grid)
        {
            const double dt = grid.GetDt();
            m_firstRun.Configure(grid.numTimeSteps, dt, grid.GetSizeGrid());
            m_mean.Configure(grid.numTimeSteps, dt, grid.GetSizeGrid());
            m_stdDev.Configure(grid.numTimeSteps, dt, grid.GetSizeGrid());
            m_runTimeSeries.clear();
            m_runSizeDistributions.clear();
            m_runTotals.clear();
            m_numRuns = 0;
            m_finalized = false;
        }

        // Ergebnis eines weiteren Laufs einrechnen. Alle Laeufe muessen
        // dasselbe Raster haben (sonst wird der Lauf ignoriert).
        void AddRun(const CSimulationResults& run)
        {
            if (!run.HasSameGrid(m_mean))
                return;
            ++m_numRuns;
            if (m_numRuns == 1)
                m_firstRun = run;

            // Welford: mean += (x - mean)/n ;  M2 += (x - mean_alt)(x - mean_neu).
            // M2 steht bis Finalize() in m_stdDev.
            const double n = static_cast<double>(m_numRuns);
            for (size_t t = 0; t < run.GetNumTimeSteps(); ++t)
            {
                for (size_t s = 0; s < run.GetNumSizeClasses(); ++s)
                {
                    const double x = run.GetValue(t, s);
                    const double oldMean = m_mean.GetValue(t, s);
                    const double newMean = oldMean + (x - oldMean) / n;
                    m_mean.SetValue(t, s, newMean);
                    m_stdDev.SetValue(t, s, m_stdDev.GetValue(t, s) + (x - oldMean) * (x - newMean));
                }
            }

            std::vector<double> timeSeries(run.GetNumTimeSteps());
            for (size_t t = 0; t < timeSeries.size(); ++t)
                timeSeries[t] = run.IntegrateOverSize(t);
            m_runTimeSeries.push_back(std::move(timeSeries));

            std::vector<double> sizeDistribution(run.GetNumSizeClasses());
            for (size_t s = 0; s < sizeDistribution.size(); ++s)
                sizeDistribution[s] = run.IntegrateOverTime(s);
            m_runSizeDistributions.push_back(std::move(sizeDistribution));

            m_runTotals.push_back(run.IntegrateTotal());
        }

        // Nach dem letzten Lauf: M2 in die Standardabweichung umrechnen
        // (Stichproben-Standardabweichung, Teiler N-1; bei 1 Lauf = 0).
        void Finalize()
        {
            if (m_finalized)
                return;
            const double divisor = (m_numRuns > 1) ? static_cast<double>(m_numRuns - 1) : 1.0;
            for (size_t t = 0; t < m_stdDev.GetNumTimeSteps(); ++t)
                for (size_t s = 0; s < m_stdDev.GetNumSizeClasses(); ++s)
                    m_stdDev.SetValue(t, s, m_numRuns > 1 ? std::sqrt(std::max(0.0, m_stdDev.GetValue(t, s) / divisor)) : 0.0);
            m_finalized = true;
        }

        size_t GetNumRuns() const { return m_numRuns; }
        bool IsEmpty() const { return m_numRuns == 0; }

        const CSimulationResults& GetFirstRun() const { return m_firstRun; }
        const CSimulationResults& GetMean() const { return m_mean; }
        const CSimulationResults& GetStdDev() const { return m_stdDev; } // erst nach Finalize()

        // [Lauf][Zeitschritt] bzw. [Lauf][Groessenklasse] bzw. [Lauf]
        const std::vector<std::vector<double>>& GetRunTimeSeries() const { return m_runTimeSeries; }
        const std::vector<std::vector<double>>& GetRunSizeDistributions() const { return m_runSizeDistributions; }
        const std::vector<double>& GetRunTotals() const { return m_runTotals; }

        // ---- Hilfsfunktionen fuer Statistik ueber die Laeufe ----------------

        // Mittelwert und Standardabweichung (N-1) einer Stichprobe.
        static void MeanAndStdDev(const std::vector<double>& values, double& mean, double& stdDev)
        {
            mean = 0.0;
            stdDev = 0.0;
            if (values.empty())
                return;
            for (double v : values)
                mean += v;
            mean /= static_cast<double>(values.size());
            if (values.size() < 2)
                return;
            double sumSq = 0.0;
            for (double v : values)
                sumSq += (v - mean) * (v - mean);
            stdDev = std::sqrt(sumSq / static_cast<double>(values.size() - 1));
        }

        // p-Quantil (p in [0, 1]) mit linearer Interpolation zwischen den
        // sortierten Werten.
        static double Quantile(std::vector<double> values, double p)
        {
            if (values.empty())
                return 0.0;
            std::sort(values.begin(), values.end());
            const double pos = std::clamp(p, 0.0, 1.0) * static_cast<double>(values.size() - 1);
            const size_t i0 = static_cast<size_t>(pos);
            const size_t i1 = std::min(i0 + 1, values.size() - 1);
            const double w = pos - static_cast<double>(i0);
            return (1.0 - w) * values[i0] + w * values[i1];
        }

        // Werte aller Laeufe an Position index aus [Lauf][index] sammeln.
        static std::vector<double> Column(const std::vector<std::vector<double>>& perRun, size_t index)
        {
            std::vector<double> column;
            column.reserve(perRun.size());
            for (const std::vector<double>& run : perRun)
                if (index < run.size())
                    column.push_back(run[index]);
            return column;
        }

    private:
        size_t m_numRuns = 0;
        bool m_finalized = false;
        CSimulationResults m_firstRun;
        CSimulationResults m_mean;
        CSimulationResults m_stdDev; // bis Finalize(): Welford-M2
        std::vector<std::vector<double>> m_runTimeSeries;
        std::vector<std::vector<double>> m_runSizeDistributions;
        std::vector<double> m_runTotals;
    };
}
