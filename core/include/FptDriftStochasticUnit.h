#pragma once
#include <cmath>
#include <vector>
#include <random>
#include <algorithm>
#include "TransportUnit.h"

namespace stosis
{
    // Stochastisches Gegenstueck zu CFptDriftDeterministicUnit: statt
    // der geschlossenen Inverse-Gauss-Formel wird die Position jedes
    // simulierten Partikels als echter Random Walk (Euler-Maruyama)
    // bis zum Erreichen der Rohrlaenge L verfolgt. Die Groesse des
    // Partikels selbst aendert sich dabei nicht, nur seine
    // Geschwindigkeit haengt (wie bei TIS) von der Groesse ab. Bei
    // vielen Partikeln naehert sich das Ergebnis der deterministischen
    // Inverse-Gauss-Kurve an.
    class CFptDriftStochasticUnit : public ITransportUnit
    {
    public:
        std::string GetTypeName() const override
        {
            return "Verweilzeit: Drift/Diffusion FPT (stochastisch)";
        }

        std::string GetResultLabel() const override
        {
            return "Austritts-Wahrscheinlichkeitsdichte (1/Zeit)";
        }

        std::vector<SParameterDescriptor> GetParameterDescriptors() const override
        {
            return {
                {"tubeLength", "Rohrlaenge L", 0.1, 1000.0, 100.0},
                {"driftVelocityRef", "Referenz-Driftgeschwindigkeit v_ref", -50.0, 50.0, 10.0},
                {"diffusionCoeff", "Diffusionskoeffizient D", 0.01, 500.0, 20.0},
                {"referenceSize", "Referenzgroesse d_ref", 0.1, 500.0, 60.0},
                {"sizeExponent", "Groessenexponent p (auf Geschwindigkeit)", -5.0, 5.0, 2.0},
                {"numParticles", "Anzahl Partikel pro Groessenklasse", 10.0, 200000.0, 2000.0},
                {"stepsPerOutput", "Sub-Schritte pro Ausgabe-Zeitschritt", 1.0, 200.0, 10.0},
                {"seed", "Zufalls-Seed", 0.0, 100000.0, 42.0},
            };
        }

        void SetParameter(const std::string& id, double value) override
        {
            if (id == "tubeLength") m_tubeLength = value;
            else if (id == "driftVelocityRef") m_driftVelocityRef = value;
            else if (id == "diffusionCoeff") m_diffusionCoeff = value;
            else if (id == "referenceSize") m_referenceSize = value;
            else if (id == "sizeExponent") m_sizeExponent = value;
            else if (id == "numParticles") m_numParticles = value;
            else if (id == "stepsPerOutput") m_stepsPerOutput = value;
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

            const double dtOutput = m_totalTime / static_cast<double>(m_numTimeSteps - 1);
            const int stepsPerOutput = std::max(1, static_cast<int>(std::round(m_stepsPerOutput)));
            const double microDt = dtOutput / stepsPerOutput;
            const int numParticles = std::max(1, static_cast<int>(std::round(m_numParticles)));
            const int totalMicroSteps = static_cast<int>(m_numTimeSteps) * stepsPerOutput;
            const double L = m_tubeLength;

            m_results.Configure(m_numTimeSteps, dtOutput, m_numClasses, m_sizeMin, m_sizeMax);

            const std::vector<double>& amplitude = m_input->GetValues();

            std::mt19937 rng(static_cast<unsigned int>(m_seed));
            std::normal_distribution<double> normalDist(0.0, 1.0);
            const double sigmaSub = std::sqrt(2.0 * m_diffusionCoeff * microDt);

            for (size_t s = 0; s < m_numClasses; ++s)
            {
                const double frac = static_cast<double>(s) / static_cast<double>(m_numClasses - 1);
                const double size = m_sizeMin + frac * (m_sizeMax - m_sizeMin);
                const double safeSize = std::max(size, 1e-6);
                const double v_c = m_driftVelocityRef * std::pow(safeSize / m_referenceSize, -m_sizeExponent);
                const double amp = (s < amplitude.size()) ? amplitude[s] : 0.0;

                std::vector<double> counts(m_numTimeSteps, 0.0);

                if (amp > 0.0)
                {
                    for (int p = 0; p < numParticles; ++p)
                    {
                        double position = 0.0;

                        for (int step = 1; step <= totalMicroSteps; ++step)
                        {
                            position += v_c * microDt + sigmaSub * normalDist(rng);
                            if (position >= L)
                            {
                                const double elapsed = static_cast<double>(step) * microDt;
                                const int bin = static_cast<int>(elapsed / dtOutput);
                                if (bin >= 0 && bin < static_cast<int>(m_numTimeSteps))
                                    counts[bin] += 1.0;
                                break;
                            }
                            // Erreicht das Partikel L bis totalTime nicht,
                            // zaehlt es einfach nicht mit (analog zur
                            // Wahrscheinlichkeitsmasse < 1 im
                            // deterministischen Modell).
                        }
                    }
                }

                for (size_t t = 0; t < m_numTimeSteps; ++t)
                {
                    const double density = counts[t] / (static_cast<double>(numParticles) * dtOutput);
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

        double m_tubeLength = 100.0;
        double m_driftVelocityRef = 10.0;
        double m_diffusionCoeff = 20.0;
        double m_referenceSize = 60.0;
        double m_sizeExponent = 2.0;
        double m_numParticles = 2000.0;
        double m_stepsPerOutput = 10.0;
        double m_seed = 42.0;

        CStream* m_input = nullptr;
        CStream m_output;
        CSimulationResults m_results;
    };
}