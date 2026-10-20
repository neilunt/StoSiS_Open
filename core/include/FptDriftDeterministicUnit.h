#pragma once
#include <cmath>
#include <vector>
#include <algorithm>
#include "TransportUnit.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace stosis
{
    // Deterministisches First-Passage-Time Modell: ein Partikel bewegt
    // sich als Wiener-Prozess mit Drift entlang einer Position (Start
    // bei 0), bis es eine feste Rohrlaenge L erreicht. Die Zeit bis
    // dahin folgt der Inversen-Gauss-Verteilung (geschlossene Loesung
    // fuer die erste Durchgangszeit eines Drift-Diffusions-Prozesses):
    //
    //   f(t) = L / sqrt(4*pi*D*t^3) * exp( -(L - v*t)^2 / (4*D*t) )
    //
    // Die Groesse selbst bleibt dabei unveraendert (anders als bei
    // CDiffusionDriftUnit) - nur die Geschwindigkeit v haengt ueber
    // dasselbe Potenzgesetz wie bei TIS von der Groesse ab, damit
    // direkt vergleichbar mit den TIS-Modellen.
    class CFptDriftDeterministicUnit : public ITransportUnit
    {
    public:
        std::string GetTypeName() const override
        {
            return "Verweilzeit: Drift/Diffusion FPT (deterministisch)";
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
            };
        }

        void SetParameter(const std::string& id, double value) override
        {
            if (id == "tubeLength") m_tubeLength = value;
            else if (id == "driftVelocityRef") m_driftVelocityRef = value;
            else if (id == "diffusionCoeff") m_diffusionCoeff = value;
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

            const double dt = m_totalTime / static_cast<double>(m_numTimeSteps - 1);
            m_results.Configure(m_numTimeSteps, dt, m_numClasses, m_sizeMin, m_sizeMax);

            const std::vector<double>& amplitude = m_input->GetValues();
            const double L = m_tubeLength;

            for (size_t s = 0; s < m_numClasses; ++s)
            {
                const double frac = static_cast<double>(s) / static_cast<double>(m_numClasses - 1);
                const double size = m_sizeMin + frac * (m_sizeMax - m_sizeMin);
                const double safeSize = std::max(size, 1e-6);
                const double v_c = m_driftVelocityRef * std::pow(safeSize / m_referenceSize, -m_sizeExponent);
                const double amp = (s < amplitude.size()) ? amplitude[s] : 0.0;

                // t=0 immer exakt null - kein Partikel kann in null Zeit
                // eine Strecke L>0 zuruecklegen, und die Formel unten
                // waere dort ohnehin numerisch undefiniert (Division
                // durch t=0).
                m_results.SetValue(0, s, 0.0);

                for (size_t t = 1; t < m_numTimeSteps; ++t)
                {
                    const double time = static_cast<double>(t) * dt;

                    // Im Log-Raum berechnet: der Vorfaktor waechst bei
                    // kleinem t gegen unendlich, der Exponentialterm
                    // faellt gegen null, direkt multipliziert koennte
                    // das numerisch zu "unendlich mal null" werden.
                    // Im Log-Raum erst addieren, dann einmal exponieren
                    // ist stabil.
                    const double logPrefactor = std::log(L) - 0.5 * std::log(4.0 * M_PI * m_diffusionCoeff * time * time * time);
                    const double diff = L - v_c * time;
                    const double logExpTerm = -(diff * diff) / (4.0 * m_diffusionCoeff * time);
                    const double density = std::exp(logPrefactor + logExpTerm);

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

        CStream* m_input = nullptr;
        CStream m_output;
        CSimulationResults m_results;
    };
}