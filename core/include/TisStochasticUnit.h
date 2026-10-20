#pragma once
#include <cmath>
#include <vector>
#include <random>
#include <algorithm>
#include "TransportUnit.h"

namespace stosis
{
    // Stochastisches Tanks-in-Series Modell MIT Rueckvermischung.
    // Simuliert pro Groessenklasse eine Anzahl einzelner Partikel als
    // Random Walk ueber den Tank-Index: exponentialverteilte Wartezeit
    // pro Schritt, danach vorwaerts mit hoher Wahrscheinlichkeit,
    // rueckwaerts mit kleiner Wahrscheinlichkeit (reflektierend am
    // ersten Tank). Die Verteilung der Austrittszeiten aller Partikel
    // ergibt das Histogramm - bei backflowProbability=0 und vielen
    // Partikeln naehert sich das Ergebnis der deterministischen
    // Erlang-Verteilung aus CTisDeterministicUnit an.
    class CTisStochasticUnit : public ITransportUnit
    {
    public:
        std::string GetTypeName() const override
        {
            return "Verweilzeit: TIS (stochastisch, mit Rueckvermischung)";
        }

        std::string GetResultLabel() const override
        {
            return "Austrittswahrscheinlichkeitsdichte (1/Zeit)";
        }


        std::vector<SParameterDescriptor> GetParameterDescriptors() const override
        {
            return {
                {"numTanks", "Anzahl Tanks N", 1.0, 50.0, 5.0},
                {"meanResidenceTime", "Mittlere Verweilzeit tau_ref", 0.01, 100.0, 5.0},
                {"referenceSize", "Referenzgroesse d_ref", 0.1, 500.0, 60.0},
                {"sizeExponent", "Groessenexponent p", 0.0, 5.0, 2.0},
                {"backflowProbability", "Rueckvermischungs-Wahrscheinlichkeit", 0.0, 0.49, 0.05},
                {"numParticles", "Anzahl Partikel pro Groessenklasse", 10.0, 200000.0, 2000.0},
                {"seed", "Zufalls-Seed", 0.0, 100000.0, 42.0},
            };
        }

        void SetParameter(const std::string& id, double value) override
        {
            if (id == "numTanks") m_numTanks = value;
            else if (id == "meanResidenceTime") m_meanResidenceTime = value;
            else if (id == "referenceSize") m_referenceSize = value;
            else if (id == "sizeExponent") m_sizeExponent = value;
            else if (id == "backflowProbability") m_backflowProbability = value;
            else if (id == "numParticles") m_numParticles = value;
            else if (id == "seed") m_seed = value;
        }

        void ConfigureGrid(size_t numClasses, double sizeMin, double sizeMax,
            size_t numTimeSteps, double totalTime) override
        {
            m_numClasses = numClasses;
            m_sizeMin = sizeMin;
            m_sizeMax = sizeMax;
            m_numTimeSteps = numTimeSteps;
            m_totalTime = totalTime;
        }

        void SetInputStream(CStream* input) override { m_input = input; }
        CStream* GetOutputStream() override { return &m_output; }
        const CSimulationResults& GetResults() const override { return m_results; }

        void Simulate() override
        {
            if (!m_input || m_numClasses < 1 || m_numTimeSteps < 2)
                return;

            const int N = std::max(1, static_cast<int>(std::round(m_numTanks)));
            const int numParticles = std::max(1, static_cast<int>(std::round(m_numParticles)));
            const double dt = m_totalTime / static_cast<double>(m_numTimeSteps - 1);
            const double pBackward = m_backflowProbability;

            m_results.Configure(m_numTimeSteps, dt, m_numClasses, m_sizeMin, m_sizeMax);

            const std::vector<double>& amplitude = m_input->GetValues();

            std::mt19937 rng(static_cast<unsigned int>(m_seed));
            std::uniform_real_distribution<double> uniDist(0.0, 1.0);

            // Sicherheitsgrenze gegen Endlosschleifen, falls durch einen
            // sehr hohen backflowProbability-Wert ein Partikel extrem
            // lange braucht, um alle N Tanks zu durchlaufen.
            const int maxSteps = 200 * N + 1000;

            for (size_t s = 0; s < m_numClasses; ++s)
            {
                const double frac = static_cast<double>(s) / static_cast<double>(m_numClasses - 1);
                const double size = m_sizeMin + frac * (m_sizeMax - m_sizeMin);
                const double safeSize = std::max(size, 1e-6);
                const double tau_c = m_meanResidenceTime * std::pow(size / m_referenceSize, m_sizeExponent);
                const double amp = (s < amplitude.size()) ? amplitude[s] : 0.0;

                const double k = static_cast<double>(N) / tau_c; // Rate pro Tank-Schritt
                std::exponential_distribution<double> waitDist(k);

                std::vector<double> counts(m_numTimeSteps, 0.0);

                for (int p = 0; p < numParticles; ++p)
                {
                    int tank = 1;
                    double time = 0.0;
                    int steps = 0;

                    while (steps < maxSteps)
                    {
                        time += waitDist(rng);
                        ++steps;

                        if (uniDist(rng) < pBackward)
                        {
                            if (tank > 1)
                                --tank; // Rueckvermischung um einen Tank
                            // bei tank == 1: reflektierend, bleibt stehen
                        }
                        else
                        {
                            ++tank;
                            if (tank > N)
                                break; // ausgetreten
                        }
                    }

                    const int bin = static_cast<int>(time / dt);
                    if (bin >= 0 && bin < static_cast<int>(m_numTimeSteps))
                        counts[bin] += 1.0;
                }

                for (size_t t = 0; t < m_numTimeSteps; ++t)
                {
                    const double density = counts[t] / (static_cast<double>(numParticles) * dt);
                    m_results.SetValue(t, s, amp * density);
                }
            }

            std::vector<double> finalValues(m_numClasses);
            for (size_t s = 0; s < m_numClasses; ++s)
                finalValues[s] = m_results.GetValue(m_numTimeSteps - 1, s);
            m_output.SetValues(finalValues);
        }

    private:
        size_t m_numClasses = 0;
        double m_sizeMin = 0.0;
        double m_sizeMax = 0.0;
        size_t m_numTimeSteps = 0;
        double m_totalTime = 0.0;

        double m_numTanks = 5.0;
        double m_meanResidenceTime = 5.0;
        double m_referenceSize = 60.0;
        double m_sizeExponent = 2.0;
        double m_backflowProbability = 0.05;
        double m_numParticles = 2000.0;
        double m_seed = 42.0;

        CStream* m_input = nullptr;
        CStream m_output;
        CSimulationResults m_results;
    };
}