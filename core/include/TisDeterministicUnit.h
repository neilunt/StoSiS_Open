#pragma once
#pragma once
#include <cmath>
#include <vector>
#include <algorithm>
#include "TransportUnit.h"

namespace stosis
{
    // Deterministisches Tanks-in-Series Modell. Nutzt die
    // geschlossene Erlang-Verteilung fuer die Verweilzeitverteilung
    // (keine Zeitschritt-Iteration noetig, im Gegensatz zur
    // DiffusionDriftUnit). Die mittlere Verweilzeit skaliert
    // groessenklassen-abhaengig nach demselben Stokes-artigen Muster
    // wie in der echten Dyssol-Arbeit: tau_c = tau_ref * (d_c/d_ref)^p.
    class CTisDeterministicUnit : public ITransportUnit
    {
    public:
        std::string GetTypeName() const override
        {
            return "Verweilzeit: TIS (deterministisch)";
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
            };
        }

        void SetParameter(const std::string& id, double value) override
        {
            if (id == "numTanks") m_numTanks = value;
            else if (id == "meanResidenceTime") m_meanResidenceTime = value;
            else if (id == "referenceSize") m_referenceSize = value;
            else if (id == "sizeExponent") m_sizeExponent = value;
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
            const double dt = m_totalTime / static_cast<double>(m_numTimeSteps - 1);

            m_results.Configure(m_numTimeSteps, dt, m_numClasses, m_sizeMin, m_sizeMax);

            const std::vector<double>& amplitude = m_input->GetValues();
            const double logGammaN = std::lgamma(static_cast<double>(N)); // ln((N-1)!)

            for (size_t s = 0; s < m_numClasses; ++s)
            {
                const double frac = static_cast<double>(s) / static_cast<double>(m_numClasses - 1);
                const double size = m_sizeMin + frac * (m_sizeMax - m_sizeMin);
                const double safeSize = std::max(size, 1e-6);
                const double tau_c = m_meanResidenceTime * std::pow(safeSize / m_referenceSize, m_sizeExponent);
                const double amp = (s < amplitude.size()) ? amplitude[s] : 0.0;

                for (size_t t = 0; t < m_numTimeSteps; ++t)
                {
                    const double time = static_cast<double>(t) * dt;
                    const double safeTime = std::max(time, 1e-12);

                    // Erlang-Verteilung in log-Form berechnet, damit bei
                    // groesserem N kein Zahlenueberlauf entsteht.
                    const double logE = N * std::log(static_cast<double>(N) / tau_c)
                        + (N - 1) * std::log(safeTime)
                        - (N * time / tau_c)
                        - logGammaN;

                    m_results.SetValue(t, s, amp * std::exp(logE));
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

        CStream* m_input = nullptr;
        CStream m_output;
        CSimulationResults m_results;
    };
}