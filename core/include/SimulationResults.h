#pragma once
#include <vector>
#include <cstddef>
#include <cmath>
#include "SizeGrid.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace stosis
{
    // Ergebnis-Speicher fuer eine Groesse, die von Zeit UND
    // Partikelgroesse abhaengt (z.B. Partikelfluss/-zahl pro
    // Groessenklasse). Feste Gitter in beiden Achsen - wie besprochen keine
    // adaptive Zeitschrittweite; die Groessenachse ist linear oder
    // logarithmisch (SSizeGrid). Die Matrix liegt
    // row-major (ein Zeitschritt = ein zusammenhaengender Block), das
    // ist die Speicherreihenfolge, die man beim Aufbauen (Zeitschritt
    // fuer Zeitschritt schreiben) und beim Rendern (Zeile fuer Zeile
    // lesen) gleichermassen will.
    class CSimulationResults
    {
    public:
        void Configure(size_t numTimeSteps, double dt, const SSizeGrid& sizes)
        {
            m_numTimeSteps = numTimeSteps;
            m_dt = dt;
            m_sizes = sizes;
            m_numSizeClasses = sizes.numClasses;
            m_weights = sizes.GetWeights();
            m_data.assign(numTimeSteps * m_numSizeClasses, 0.0);
        }

        // Lineares Raster (alte Vergleichsansicht der Transportmodelle).
        void Configure(size_t numTimeSteps, double dt,
                        size_t numSizeClasses, double sizeMin, double sizeMax)
        {
            Configure(numTimeSteps, dt, SSizeGrid::Linear(numSizeClasses, sizeMin, sizeMax));
        }

        void SetValue(size_t timeIdx, size_t sizeIdx, double value)
        {
            m_data[timeIdx * m_numSizeClasses + sizeIdx] = value;
        }

        double GetValue(size_t timeIdx, size_t sizeIdx) const
        {
            return m_data[timeIdx * m_numSizeClasses + sizeIdx];
        }

        size_t GetNumTimeSteps() const { return m_numTimeSteps; }
        size_t GetNumSizeClasses() const { return m_numSizeClasses; }
        double GetDt() const { return m_dt; }
        double GetSizeMin() const { return m_sizes.sizeMin; }
        double GetSizeMax() const { return m_sizes.sizeMax; }
        const SSizeGrid& GetSizeGrid() const { return m_sizes; }
        double GetTotalTime() const
        {
            return (m_numTimeSteps > 1) ? m_dt * static_cast<double>(m_numTimeSteps - 1) : 0.0;
        }

        // Abstand zweier benachbarter Gitterpunkte Delta d - nur beim
        // linearen Raster sinnvoll (alte Transportmodelle). Das Raster ist
        // knotenbasiert: Index 0 liegt genau auf sizeMin, der letzte Index
        // genau auf sizeMax.
        double GetSizeStep() const
        {
            return m_sizes.IsLogarithmic() ? 0.0 : m_sizes.GetStep();
        }

        double GetSizeAt(size_t sizeIdx) const
        {
            return m_sizes.GetSizeAt(sizeIdx);
        }

        bool IsEmpty() const { return m_data.empty(); }

        bool HasSameGrid(const CSimulationResults& other) const
        {
            return m_numTimeSteps == other.m_numTimeSteps && m_numSizeClasses == other.m_numSizeClasses
                && m_dt == other.m_dt && m_sizes == other.m_sizes;
        }

        // Alle Werte mit factor multiplizieren (z.B. Splitter-Anteil).
        void Scale(double factor)
        {
            for (double& v : m_data)
                v *= factor;
        }

        // Werte von other aufaddieren (z.B. Merger). Nur bei gleichem
        // Raster - sonst false und nichts passiert.
        bool Add(const CSimulationResults& other)
        {
            if (!HasSameGrid(other))
                return false;
            for (size_t i = 0; i < m_data.size(); ++i)
                m_data[i] += other.m_data[i];
            return true;
        }

        // Integral ueber die Groesse zu einem Zeitpunkt (Trapezregel mit den
        // Gewichten des Rasters, SSizeGrid::GetWeight: linear Delta d,
        // logarithmisch in ln d). Die gespeicherten Werte sind Dichten pro
        // Groesseneinheit (z.B. Massenstrom pro m) - erst die Gewichte machen
        // daraus den Gesamt-Massenstrom. Reines Aufsummieren der Klassenwerte
        // waere falsch.
        double IntegrateOverSize(size_t timeIdx) const
        {
            if (m_numSizeClasses == 0)
                return 0.0;
            if (m_numSizeClasses == 1)
                return GetValue(timeIdx, 0);
            if (!m_sizes.IsLogarithmic())
            {
                // Linear: wie bisher erst summieren, dann mit Delta d malnehmen
                // (gleiche Rundung wie vor der Umstellung).
                double sum = 0.5 * (GetValue(timeIdx, 0) + GetValue(timeIdx, m_numSizeClasses - 1));
                for (size_t s = 1; s + 1 < m_numSizeClasses; ++s)
                    sum += GetValue(timeIdx, s);
                return sum * m_sizes.GetStep();
            }
            double sum = 0.0;
            for (size_t s = 0; s < m_numSizeClasses; ++s)
                sum += m_weights[s] * GetValue(timeIdx, s);
            return sum;
        }

        // Integral ueber die Zeit fuer eine Groessenklasse (Trapezregel).
        // Bei einem Massenstrom-Ergebnis: die Masse pro Groesseneinheit,
        // die im gesamten Zeitraum bei dieser Groesse durchgegangen ist -
        // also die Groessenverteilung des gesamten Durchsatzes.
        double IntegrateOverTime(size_t sizeIdx) const
        {
            if (m_numTimeSteps < 2)
                return 0.0;

            double sum = 0.5 * (GetValue(0, sizeIdx) + GetValue(m_numTimeSteps - 1, sizeIdx));
            for (size_t t = 1; t + 1 < m_numTimeSteps; ++t)
                sum += GetValue(t, sizeIdx);
            return sum * m_dt;
        }

        // Doppelintegral ueber Groesse UND Zeit (Trapezregel in beiden
        // Richtungen). Bei einem Massenstrom-Ergebnis ist das die
        // gesamte Masse, die im Simulationszeitraum durch die Einheit
        // geflossen ist - die Grundlage fuer Massenbilanzen.
        double IntegrateTotal() const
        {
            if (m_numTimeSteps < 2)
                return 0.0;

            double sum = 0.5 * (IntegrateOverSize(0) + IntegrateOverSize(m_numTimeSteps - 1));
            for (size_t t = 1; t + 1 < m_numTimeSteps; ++t)
                sum += IntegrateOverSize(t);
            return sum * m_dt;
        }

        double GetMaxValue() const
        {
            double maxVal = 0.0;
            for (double v : m_data)
                if (v > maxVal)
                    maxVal = v;
            return maxVal;
        }

        double GetAbsMaxValue() const
        {
            double maxVal = 0.0;
            for (double v : m_data)
                if (std::abs(v) > maxVal)
                    maxVal = std::abs(v);
            return maxVal;
        }

        // Fuellt die Matrix mit einer Gauss-Verteilung, deren Zentrum
        // linear driftet (mean(t) = d0 + driftVelocity*t) und deren
        // Varianz linear mit der Zeit waechst (sigma^2 = sigma0^2 +
        // 2*D*t). Das ist exakt ein Wiener-Prozess mit Drift - also das
        // Modell, das du spaeter fuer deine WienerDrift-Unit brauchst
        // (driftVelocity = Drift, diffusionCoeff = Volatilitaet/2).
        // driftVelocity=0 ergibt reine Diffusion ohne Wanderung.
        // NUR zu Demozwecken fuer die Heatmap-Darstellung - hat noch
        // keinen Bezug zu einer echten Flowsheet-Simulation.
        void GenerateDiffusionDemo(double d0, double sigma0, double diffusionCoeff,
                                    double driftVelocity = 0.0)
        {
            for (size_t t = 0; t < m_numTimeSteps; ++t)
            {
                const double time = static_cast<double>(t) * m_dt;
                const double mean = d0 + driftVelocity * time;
                const double sigma2 = sigma0 * sigma0 + 2.0 * diffusionCoeff * time;
                const double norm = 1.0 / std::sqrt(2.0 * M_PI * sigma2);

                for (size_t s = 0; s < m_numSizeClasses; ++s)
                {
                    const double size = GetSizeAt(s);
                    const double diff = size - mean;
                    const double value = norm * std::exp(-(diff * diff) / (2.0 * sigma2));
                    SetValue(t, s, value);
                }
            }
        }

    private:
        size_t m_numTimeSteps = 0;
        size_t m_numSizeClasses = 0;
        double m_dt = 0.0;
        SSizeGrid m_sizes = SSizeGrid::Linear(0, 0.0, 0.0);
        std::vector<double> m_data;
        std::vector<double> m_weights; // Trapezgewichte des Rasters (SSizeGrid::GetWeight)
    };
}
