#pragma once
#include <vector>
#include <cstddef>
#include <algorithm>
#include "Materials.h"

namespace stosis
{
    // Ein Strom zu EINEM Zeitpunkt: Massenstromdichte je Stoff und
    // Groessenklasse (gleiches knotenbasiertes Groessenraster wie
    // CSimulationResults), dazu Temperatur und Druck des Stroms. Das ist das
    // "Paket", das die Units in jedem Makroschritt austauschen. Bewusst als
    // eigene Klasse statt nacktem Vektor: spaeter kann hier eine zweite
    // Darstellung dazukommen (Liste gewichteter MC-Partikel), ohne dass sich
    // die Schnittstelle der Units aendert.
    //
    // Stoffe sind ueber ihren Index in SSimulationGrid::compounds
    // angesprochen (die aktiven Stoffe des Flowsheets).
    class CStreamSlice
    {
    public:
        void Resize(size_t numSizeClasses, size_t numCompounds = 1)
        {
            m_numSizeClasses = numSizeClasses;
            m_numCompounds = std::max<size_t>(numCompounds, 1);
            m_values.assign(m_numCompounds * m_numSizeClasses, 0.0);
            ResetConditions();
        }

        // Kein Material mehr; Temperatur und Druck zurueck auf Raumbedingungen.
        void Clear()
        {
            std::fill(m_values.begin(), m_values.end(), 0.0);
            ResetConditions();
        }

        size_t GetNumSizeClasses() const { return m_numSizeClasses; }
        size_t GetNumCompounds() const { return m_numCompounds; }

        // Gesamte Massenstromdichte (alle Stoffe) in Groessenklasse sizeIdx.
        double GetValue(size_t sizeIdx) const
        {
            double sum = m_values[sizeIdx];
            for (size_t c = 1; c < m_numCompounds; ++c)
                sum += m_values[c * m_numSizeClasses + sizeIdx];
            return sum;
        }

        double GetValue(size_t compound, size_t sizeIdx) const { return m_values[compound * m_numSizeClasses + sizeIdx]; }
        void SetValue(size_t compound, size_t sizeIdx, double value) { m_values[compound * m_numSizeClasses + sizeIdx] = value; }

        double GetTemperature() const { return m_temperature; }
        double GetPressure() const { return m_pressure; }
        void SetTemperature(double temperature) { m_temperature = temperature; }
        void SetPressure(double pressure) { m_pressure = pressure; }

        // Massenstrom eines Stoffs: Trapezregel ueber die Groesse mit den
        // Gewichten des Rasters (SSizeGrid::GetWeights, wie CSimulationResults::
        // IntegrateOverSize). Fuer Anteile und Energiebilanzen.
        double GetCompoundWeight(size_t compound, const std::vector<double>& sizeWeights) const
        {
            if (m_numSizeClasses == 0)
                return 0.0;
            const double* row = &m_values[compound * m_numSizeClasses];
            const size_t numS = std::min(m_numSizeClasses, sizeWeights.size());
            double sum = 0.0;
            for (size_t s = 0; s < numS; ++s)
                sum += sizeWeights[s] * row[s];
            return sum;
        }

        double GetTotalWeight(const std::vector<double>& sizeWeights) const
        {
            double sum = 0.0;
            for (size_t c = 0; c < m_numCompounds; ++c)
                sum += GetCompoundWeight(c, sizeWeights);
            return sum;
        }

        // this = other * factor (z.B. Splitter-Anteil); T und p bleiben.
        void AssignScaled(const CStreamSlice& other, double factor)
        {
            m_numSizeClasses = other.m_numSizeClasses;
            m_numCompounds = other.m_numCompounds;
            m_values.resize(other.m_values.size());
            for (size_t i = 0; i < m_values.size(); ++i)
                m_values[i] = other.m_values[i] * factor;
            m_temperature = other.m_temperature;
            m_pressure = other.m_pressure;
        }

        // this += other je Stoff und Groessenklasse (nur die Mengen; T und p
        // bestimmt, wer mischt - siehe CMergerUnit). Bei unterschiedlicher
        // Groesse wird nur der gemeinsame Bereich addiert.
        void Add(const CStreamSlice& other)
        {
            const size_t numC = std::min(m_numCompounds, other.m_numCompounds);
            const size_t numS = std::min(m_numSizeClasses, other.m_numSizeClasses);
            for (size_t c = 0; c < numC; ++c)
                for (size_t s = 0; s < numS; ++s)
                    m_values[c * m_numSizeClasses + s] += other.m_values[c * other.m_numSizeClasses + s];
        }

        const std::vector<double>& GetValues() const { return m_values; }

    private:
        void ResetConditions()
        {
            m_temperature = kRoomTemperature;
            m_pressure = kRoomPressure;
        }

        size_t m_numSizeClasses = 0;
        size_t m_numCompounds = 1;
        // [Stoff * numSizeClasses + Groessenklasse]
        std::vector<double> m_values;
        double m_temperature = kRoomTemperature; // K
        double m_pressure = kRoomPressure;       // Pa
    };
}
