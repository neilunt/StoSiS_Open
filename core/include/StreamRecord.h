#pragma once
#include <deque>
#include <string>
#include <vector>
#include <algorithm>
#include "FlowsheetUnit.h"
#include "SimulationResults.h"
#include "EnsembleStatistics.h"

namespace stosis
{
    // Verlauf eines Stroms ueber eine ganze Simulation: Gesamtmenge und
    // Menge je Stoff als Zeit-mal-Groesse Matrix, dazu Temperatur und Druck
    // je Zeitschritt.
    //
    // Mit nur einem aktiven Stoff gibt es keine eigene Stoff-Matrix -
    // GetCompound(0) ist dann die Gesamtmenge (spart Speicher).
    // Die Stoff-Matrizen liegen in einer deque, die nie schrumpft: Zeiger
    // darauf (z.B. in der Heatmap) bleiben gueltig, auch wenn sich die Zahl
    // der Stoffe zwischen zwei Simulationen aendert.
    class CStreamRecord
    {
    public:
        void Configure(const SSimulationGrid& grid)
        {
            const double dt = grid.GetDt();
            m_total.Configure(grid.numTimeSteps, dt, grid.GetSizeGrid());
            m_numCompounds = grid.GetNumCompounds();
            const size_t separate = m_numCompounds > 1 ? m_numCompounds : 0;
            while (m_compounds.size() < separate)
                m_compounds.emplace_back();
            for (size_t c = 0; c < separate; ++c)
                m_compounds[c].Configure(grid.numTimeSteps, dt, grid.GetSizeGrid());
            m_compoundNames = grid.compounds;
            m_temperature.assign(grid.numTimeSteps, kRoomTemperature);
            m_pressure.assign(grid.numTimeSteps, kRoomPressure);
        }

        void Record(size_t timeIdx, const CStreamSlice& slice)
        {
            if (timeIdx >= m_total.GetNumTimeSteps())
                return;
            const size_t numS = std::min(m_total.GetNumSizeClasses(), slice.GetNumSizeClasses());
            for (size_t s = 0; s < numS; ++s)
                m_total.SetValue(timeIdx, s, slice.GetValue(s));
            if (m_numCompounds > 1)
                for (size_t c = 0; c < m_numCompounds && c < slice.GetNumCompounds(); ++c)
                    for (size_t s = 0; s < numS; ++s)
                        m_compounds[c].SetValue(timeIdx, s, slice.GetValue(c, s));
            m_temperature[timeIdx] = slice.GetTemperature();
            m_pressure[timeIdx] = slice.GetPressure();
        }

        const CSimulationResults& GetTotal() const { return m_total; }
        size_t GetNumCompounds() const { return m_numCompounds; }
        const CSimulationResults& GetCompound(size_t c) const { return m_numCompounds > 1 ? m_compounds[c] : m_total; }
        const std::vector<std::string>& GetCompoundNames() const { return m_compoundNames; }
        const std::vector<double>& GetTemperature() const { return m_temperature; } // K je Zeitschritt
        const std::vector<double>& GetPressure() const { return m_pressure; }       // Pa je Zeitschritt

    private:
        CSimulationResults m_total;
        size_t m_numCompounds = 1;
        std::deque<CSimulationResults> m_compounds;
        std::vector<std::string> m_compoundNames;
        std::vector<double> m_temperature;
        std::vector<double> m_pressure;
    };

    // Statistik eines Stroms ueber mehrere Laeufe: fuer die Gesamtmenge und
    // je Stoff eine CEnsembleStatistics, dazu Temperatur und Druck jedes
    // Laufs (daraus Mittelwert und Streuband wie beim Massenstrom).
    class CStreamStatistics
    {
    public:
        void Reset(const SSimulationGrid& grid)
        {
            m_total.Reset(grid);
            m_numCompounds = grid.GetNumCompounds();
            const size_t separate = m_numCompounds > 1 ? m_numCompounds : 0;
            while (m_compounds.size() < separate)
                m_compounds.emplace_back();
            for (CEnsembleStatistics& stats : m_compounds)
                stats.Reset(grid); // auch unbenutzte leeren
            m_compoundNames = grid.compounds;
            m_runTemperatures.clear();
            m_runPressures.clear();
        }

        void AddRun(const CStreamRecord& record)
        {
            m_total.AddRun(record.GetTotal());
            if (m_numCompounds > 1)
                for (size_t c = 0; c < m_numCompounds && c < record.GetNumCompounds(); ++c)
                    m_compounds[c].AddRun(record.GetCompound(c));
            m_runTemperatures.push_back(record.GetTemperature());
            m_runPressures.push_back(record.GetPressure());
        }

        void Finalize()
        {
            m_total.Finalize();
            for (size_t c = 0; c < m_numCompounds && c < m_compounds.size(); ++c)
                m_compounds[c].Finalize();
        }

        const CEnsembleStatistics& GetTotal() const { return m_total; }
        size_t GetNumCompounds() const { return m_numCompounds; }
        const CEnsembleStatistics& GetCompound(size_t c) const { return m_numCompounds > 1 ? m_compounds[c] : m_total; }
        const std::vector<std::string>& GetCompoundNames() const { return m_compoundNames; }
        // [Lauf][Zeitschritt]
        const std::vector<std::vector<double>>& GetRunTemperatures() const { return m_runTemperatures; }
        const std::vector<std::vector<double>>& GetRunPressures() const { return m_runPressures; }

    private:
        CEnsembleStatistics m_total;
        size_t m_numCompounds = 1;
        std::deque<CEnsembleStatistics> m_compounds;
        std::vector<std::string> m_compoundNames;
        std::vector<std::vector<double>> m_runTemperatures;
        std::vector<std::vector<double>> m_runPressures;
    };
}
